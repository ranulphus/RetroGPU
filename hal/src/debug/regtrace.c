/* regtrace.c - register write trace (PRD §9.2; DOS-GL FR-DBG-4/5).
 *
 * With MGA_TRACE_MMIO (or MG_DEBUG) defined, every MGA_WR32 records the
 * register offset, value and call site in a ring of the most recent
 * writes; mga_trace_dump() prints them oldest first, for example from a
 * fault hook, so the last commands before a hang or crash are known. The
 * ring costs a few stores per write, so release builds leave it out. */
#include "mga/mmio.h"

#define TRACE_LEN 256            /* power of two */

static struct { uint32_t seq, off, v; const char *file; int line; } ring[TRACE_LEN];
static uint32_t trace_seq;

/* A running CRC-32 of everything recorded (DOS-GL's REGCRC=1 build prints
 * it at each swap: B1's check that moving code changed no register write,
 * gl/docs/b1.md). The DMA lists' own pointers are left out: they hold
 * addresses in system memory, which move with the program's size. */
static uint32_t crc = 0xFFFFFFFFu, crc_n;

static void crc_bytes(const uint8_t *p, uint32_t n)
{
    int k;
    while (n--) {
        crc ^= *p++;
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
}

static void crc_word(uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)v, b[1] = (uint8_t)(v >> 8), b[2] = (uint8_t)(v >> 16), b[3] = (uint8_t)(v >> 24);
    crc_bytes(b, 4);
}

void mga_trace_data(const void *p, uint32_t bytes)
{
    crc_bytes((const uint8_t *)p, bytes);
    crc_n++;
}

uint32_t mga_trace_crc(void) { return ~crc; }
uint32_t mga_trace_crc_count(void) { return crc_n; }

void mga_trace_record_at(uint32_t off, uint32_t v, const char *file, int line)
{
    uint32_t i = trace_seq & (TRACE_LEN - 1);
    if (off != 0x1E58u && off != 0x1E5Cu && off != 0x2C40u && off != 0x2C44u) {   /* (PRIMADDRESS, PRIMEND, SECADDRESS, SECEND) */
        crc_word(off);
        crc_word(v);
        crc_n++;
    }
    ring[i].seq = ++trace_seq;
    ring[i].off = off;
    ring[i].v = v;
    ring[i].file = file;
    ring[i].line = line;
}

void mga_trace_record(uint32_t off, uint32_t v)
{
    mga_trace_record_at(off, v, "?", 0);
}

uint32_t mga_trace_count(void) { return trace_seq; }

void mga_trace_clear(void) { trace_seq = 0; }

/* No C library here (MGA-Glide's DLL links without one): tiny formatters. */
static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *put_hex(char *p, uint32_t v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--)
        *p++ = "0123456789abcdef"[(v >> (i * 4)) & 15];
    return p;
}

static char *put_dec(char *p, uint32_t v)
{
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n)
        *p++ = t[--n];
    return p;
}

void mga_trace_dump(void (*out)(const char *line), int max)
{
    uint32_t n = trace_seq < TRACE_LEN ? trace_seq : TRACE_LEN, i;
    char buf[160], *p;
    if (max > 0 && (uint32_t)max < n)
        n = (uint32_t)max;
    for (i = trace_seq - n; i != trace_seq; i++) {
        uint32_t k = i & (TRACE_LEN - 1);
        p = put_str(buf, "MGA-WR #");
        p = put_dec(p, ring[k].seq);
        *p++ = ' ';
        p = put_hex(p, ring[k].off, 4);
        *p++ = '=';
        p = put_hex(p, ring[k].v, 8);
        *p++ = ' ';
        p = put_str(p, ring[k].file ? ring[k].file : "?");
        *p++ = ':';
        p = put_dec(p, (uint32_t)(ring[k].line < 0 ? 0 : ring[k].line));
        *p = 0;
        out(buf);
    }
}
