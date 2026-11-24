/* types.h - fixed-width types shared by the HAL and its clients.
 * Open Watcom, DJGPP and host gcc all provide <stdint.h>. */
#ifndef MGA_TYPES_H
#define MGA_TYPES_H
#include <stdint.h>
#include <stddef.h>

#if defined(__WATCOMC__) || defined(__GNUC__)
#  define MGA_INLINE static inline
#else
#  define MGA_INLINE static
#endif

/* MGA_HOT: a function on the drawing paths a game runs for every vertex, draw or bind. Under DJGPP they go to one
 * section, .text.hot, which the linker places together after all other code (its script: every .text, then every
 * .text.*): the Pentium III's 16 KB instruction cache then holds them without their colliding with each other
 * wherever they are defined (GLQuake on the P3's 7500 lost 6% when X1/X2's code moved them about: the vertex
 * stage's instruction-cache misses doubled; gl/docs/dx7.md). */
#if defined(__GNUC__) && defined(MGA_DJGPP)
#  define MGA_HOT __attribute__((section(".text.hot")))
#else
#  define MGA_HOT
#endif
/* MGA_NOINLINE: a rare case kept out of a hot function's body (GCC; Open Watcom decides for itself). */
#if defined(__GNUC__)
#  define MGA_NOINLINE __attribute__((noinline))
#  define MGA_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define MGA_NOINLINE
#  define MGA_UNLIKELY(x) (x)
#endif

#define MGA_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MGA_UNUSED(x) ((void)(x))

#endif
