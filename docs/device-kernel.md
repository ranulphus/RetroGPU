# What GLOS's kernel desktop needs from the device table

**Status:** input for a later plan; nothing is built yet. From the GLOS session, 2026-11-14, in answer to the
DOS-GL session's question. It applies to `rg_device` (`hal/include/rg/device.h`) as of RetroGPU db5d766. Nothing in
GLOS changes until a device is ready for it.

Today GLOS's kernel drives the desktop through the Matrox HAL directly (GLOS `kernel/gfx/mga.c`, about 580 lines;
worth reading beside this). In the table's terms, the kernel needs the following.

1. **Attach, never open.** The kernel sets the mode itself through the video BIOS (VBE), then hands the device
   the linear framebuffer's physical address, the pitch, the bits and the height. The device must:
   - say whether that framebuffer is its card's: today, whether the LFB lies inside `[fb_phys, fb_phys +
     aperture)`. If it doesn't ("other LFB"), the desktop runs on VBE alone.
   - take the screen where the BIOS put it (colour offset = LFB − `fb_phys`) and set the engine up on it,
     without setting a mode or clearing.

   So: `attach(lfb, pitch_bytes, bpp, w, h)` returns 0, "not mine" or −1. `detach()` drains, turns the cursor off,
   lets the engine go, and leaves the card so that the next BIOS mode set, or a DOS session's own driver, finds
   nothing odd.
2. **A find that touches nothing.** At GLOS's start the kernel only wants to know the card is there, for its log:
   PCI reads (name, ID, revision, framebuffer and MMIO addresses, aperture size). Mapping and the first register
   write happen at the first attach. `probe()` as it stands may do more than that on some devices, so a cheap
   "find" separate from "map" is wanted.
3. **VRAM sized once,** apart from open and before anything is drawn. The G200's VBE says 2 MB whatever is
   fitted, so the Matrox probe writes a word per megabyte. `caps.vram_bytes` valid after attach is enough,
   provided the sizing never runs again while apps hold blocks.
4. **The memory manager stays the kernel's.** The kernel owns the VRAM heap: the screen, the cursor's 1 KB, the
   desktop's window store, and apps' blocks at fixed offsets across mode changes. The table's 2D on surfaces by
   offset, and `cursor_load(off)`, are already right for that. In attach mode the device must not allocate or
   assume any VRAM beyond what it is handed. (The ViRGE device's own A/B and depth layout belongs to `open()`
   only.)
5. **Engine ownership from the kernel's side of the lock.** Apps in ring 3 have a HAL of their own, and the kernel
   arbitrates (GLOS `gpu.c`). As the lock's owner, not a client, the kernel needs three things `rg_lock` doesn't
   give it:
   - `take(timeout)`: wait for whatever another owner queued to finish; −1 if it had to reset.
   - `resync()`: forget everything the device believed about the registers.
   - `reset()`: the watchdog's, for when an app died or stuck holding the engine.

   Today these are `engine_take`, `engine_resync` and `engine_reset`; `sync()` with `stats.resets` covers the
   ordinary drain.
6. **The cursor must not belong to the engine.** The pointer moves with interrupts off, while another thread or an
   app may be in the middle of an engine operation. So `cursor_move`, `cursor_show` and `cursor_colours` must not
   wait, must not touch the FIFO or engine state, and must put back any index register they use. (On the ViRGE
   the cursor lives behind the CRTC index at 3D4h: save and restore the index.)
7. **Exactness and order.** Every 2D operation must give exactly the software's pixels. RGDEV's comparison with
   `sw2d.c` is the right test; the kernel's desktop goldens then hold on either path. The kernel calls `sync()`
   before its software touches pixels, so `idle()` should be cheap and `sync()` bounded (the kernel passes one
   second and logs a reset).
8. **Which formats.** The desktop runs at 8 (palette), 15/16 and 32 bits, never 24 packed. The caps should say
   which surface formats the device's 2D takes, so the kernel can pick a desktop mode the device accelerates and
   fall back to plain VBE otherwise.
9. **Build rules for whatever the kernel links:**
   - no FPU at all (the kernel has none: the HAL's FPU-free part, as now);
   - no libc beyond `string.h`, no `malloc`, no `printf` (`rg_set_log` is fine);
   - delays and mapping through the port layer (`hal/port/glos.c`, with `mm_dev_map` behind it);
   - no DPMI calls, and modest static buffers.
10. **For apps in windows** (later: ViRGE V4 and after): `info()` already carries what `gpu.c` hands to an app's
    own HAL (IDs, VRAM, addresses). The Matrox devices also need option and subsystem words, so a device-private
    blob beside `rg_info` would save a table change per card.

**GLOS's proposed first step:** a `kernel/gfx/rgdev.c` that drives the desktop through the table for the Matrox
device only, proving points 1 to 7 with no change in behaviour before a second card arrives. The GLOS session
plans it with the owner once the table has `attach`/`detach` and `take`/`resync`/`reset`.
