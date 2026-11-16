# The G450's memory after a soft reset (bench F, 2026-11-16)

## What happened

The first DOS-GL runs on a real G450 (bench F: P4 HT 3.06, G450 PCI rev 85h behind a HiNT bridge, 32 MB DDR, GLOS
on FreeDOS) drew garbage, and GLOS's text screen filled with random characters.

MODETEST (`tests/hal/modetest.c`) narrowed it down:
- **After a fresh boot the card is perfect.** CPU writes read back, and nothing in VRAM changes over 20 s.
- **After one drawing-engine soft reset** (`MGAREG_RST` bit 0, the HAL's `engine_reset`, done before every WARP
  microcode load), the memory stops holding data until the next boot:
  - a write to an 8-byte-aligned word reads back 0 at once;
  - 39% of a frame has changed by the time it is read, 78% after 1 s, 94% after 2 s and 99% after 3 s.
  - The owner watched the colour bars being "slowly eaten up by coloured static".
- The readable memory registers (OPTION, OPTION2, OPTION3, MEMMISC, MEMRDBK) are unchanged by the reset. The DDR
  chips' own set-up is what is lost.
- The G400's SGRAM survives the same reset (bench-g400).

## Why the reset can't simply be dropped

The G450 shares the G400's WARP microcode-reload fault. Without a reset before each load, 2 of 6 reloads failed
(WARPTRI `starts=6 reload noreset`). DOS-GL reloads whenever it switches between the single- and dual-texture
pipes: GLQuake `-mtex` made 3,687 switches in one demo. The HAL's hang recovery, and GLOS's GPU watchdog, also
reset the engine.

## The fix

At `mga_find`, for a G450 (device 0525h, revision 80h or later) with a PInS v5 block, `pci.c` reads the block's
memory values and points `mga_memory_reinit` at Linux matroxfb's `g450_memory_init` sequence. `engine_reset`
runs it right after releasing the reset. The sequence, with OPTION2 left as the card has it:

1. refresh off;
2. the memory interface bits of OPTION;
3. MCTLWTST;
4. memory clocks off (MEMMISC bit 31);
5. MEMRDBK, then MACCESS (EMRS enable);
6. memory clocks on, then 200 µs;
7. MACCESS bit 15 (the chips' mode registers), then 200 µs;
8. refresh on;
9. PLNWT 0, then all ones.

With no PInS v5 block there is no hook, and a reset stays unsafe there.

Bench F's PInS v5 values:

| Value | |
|---|---|
| opt | `404A1560` |
| opt2 | `0000AC00` |
| mctlwtst | `0A81462B` |
| memmisc | `80000004` |
| memrdbk | `01001522` |
| Memory | DDR, DLL, EMRS writes enabled |

## Results on bench F

- RSTTEST (one reset), then MODETEST: 0 pixels changed over 5 s.
- WARPTRI `starts=6 reload` (a reset before each load): 6 of 6 started, and the memory was intact afterwards.
- GLQuake demo1: 56.5 fps at 640x480, 77.2 at 320x200, 60.6 with `-mtex`. No WARP failures; the `-mtex` run made
  3,687 resets with the set-up each time. The snapshots look the same as the G400's, differing by rounding.
- DOS-GL conformance on the card: 33 of 33 tests' frames pass (t33 and t34 are skipped on benches).

The G450's clocks are already the BIOS table's (OPTION3 `0090A409` = PInS), so the G400's clock step (`clocks.c`)
doesn't apply to it.
