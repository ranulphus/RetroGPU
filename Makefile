# RetroGPU top-level build: the HAL, the test platform and the Glide runtime
# (glide/glide.mk); DOS-GL builds in gl/. `make help` lists the targets.
include config.mk
-include config.local.mk
include tools/setup/versions.mk
include mk/ow.mk
include mk/host.mk
include mk/djgpp.mk
include mk/dos.mk

export WATCOM DJGPP_PREFIX MGA_CACHE OW_URL OW_SHA256 DJGPP_URL DJGPP_SHA256 CWSDPMI_URL CWSDPMI_SHA256

.DEFAULT_GOAL := runtime
.PHONY: help runtime header check-header check-exports check-clib tests-host \
        setup-ow setup-djgpp hal-ow hal-djgpp hal-host clean


# ---- HAL -----------------------------------------------------------------
HAL_COMMON := hal/src/debug/serial.c hal/src/debug/regtrace.c hal/src/pci.c hal/src/chip.c hal/src/vbe.c hal/src/fifo.c hal/src/engine.c hal/src/dac.c hal/src/texhw.c hal/src/present.c hal/src/setup/trap.c hal/src/vram.c hal/src/blit.c hal/src/cursor.c hal/src/warp.c
HAL_OW     := $(HAL_COMMON) hal/port/ow_dos4g.c
HAL_DJGPP  := $(HAL_COMMON) hal/port/djgpp.c
HAL_HOST   := $(HAL_COMMON) hal/port/host.c

build/ow/mgahal.lib: $(HAL_OW:%.c=build/ow/%.obj)
	@rm -f $@
	$(Q)echo "  WLIB    $@"
	$(Q)$(WLIB) -q -b -n $@ $(addprefix +,$^)
build/ow/mgahal_exe.lib: $(HAL_OW:%.c=build/ow/exe/%.obj)
	@rm -f $@
	$(Q)echo "  WLIB    $@"
	$(Q)$(WLIB) -q -b -n $@ $(addprefix +,$^)
hal-ow: build/ow/mgahal.lib build/ow/mgahal_exe.lib

build/djgpp/libmgahal.a: $(HAL_DJGPP:%.c=build/djgpp/%.o)
	@rm -f $@
	$(DJAR) rcs $@ $^
hal-djgpp: build/djgpp/libmgahal.a

build/host/libmgahal.a: $(HAL_HOST:%.c=build/host/%.o)
	@rm -f $@
	ar rcs $@ $^
hal-host: build/host/libmgahal.a

# ---- The Glide runtime (glide/glide.mk) ---------------------------------
include glide/glide.mk

# ---- Toolchains ----------------------------------------------------------
setup-ow:
	tools/setup/setup-ow.sh
setup-djgpp:
	tools/setup/setup-djgpp.sh

# The HAL's smoke test with DJGPP (as DOS-GL builds it) and with Open Watcom.
build/djgpp/SMOKE.EXE: tests/hal/smoke.c build/djgpp/libmgahal.a
	@mkdir -p $(dir $@)
	$(Q)echo "  DJLD    $@"
	$(Q)$(DJCC) $(DJ_CFLAGS) -o $@ $< build/djgpp/libmgahal.a
smoke-djgpp: build/djgpp/SMOKE.EXE

# The guest shim's programs built with DJGPP (as DOS-GL's are): Loop A and
# bench jobs run them with CWSDPMI.
DJ_SHIM := -Itests/shim -DHX_BUILD_ID='"$(BUILD_ID)"'
build/djgpp/%.EXE: build/djgpp/libmgahal.a tests/shim/hx.c tests/shim/hx.h
	@mkdir -p $(dir $@)
	$(Q)echo "  DJLD    $@"
	$(Q)$(DJCC) $(DJ_CFLAGS) $(DJ_SHIM) -o $@ $(DJ_SRC_$*) tests/shim/hx.c build/djgpp/libmgahal.a -lm
DJ_SRC_HELLO := tests/shim/hello.c
DJ_SRC_PROBE := tests/hal/probe.c
DJ_SRC_HOOKS := tests/hal/hooks.c
DJ_SRC_SCALE := tests/hal/scale.c
DJ_SRC_STACKPG := tests/shim/stackpg.c
DJ_SRC_MOUSETST := tests/shim/mousetst.c
DJ_SRC_JOYTEST := tests/shim/joytest.c
DJ_SRC_SBBEEP := tests/shim/sbbeep.c
DJ_SRC_FPCHECK := tests/hal/fpcheck.c
DJ_SRC_TWODTEST := tests/hal/twodtest.c tests/hal/sw2d.c
DJ_SRC_WARPTRI := tests/hal/warptri.c
DJ_SRC_CLOCKMON := tests/hal/clockmon.c
build/djgpp/HOOKS.EXE: tests/hal/hooks.c
build/djgpp/SCALE.EXE: tests/hal/scale.c
build/djgpp/HELLO.EXE: tests/shim/hello.c
build/djgpp/PROBE.EXE: tests/hal/probe.c
build/djgpp/TWODTEST.EXE: tests/hal/twodtest.c tests/hal/sw2d.c tests/hal/sw2d.h
build/djgpp/WARPTRI.EXE: tests/hal/warptri.c
build/djgpp/CLOCKMON.EXE: tests/hal/clockmon.c
build/djgpp/STACKPG.EXE: tests/shim/stackpg.c
build/djgpp/MOUSETST.EXE: tests/shim/mousetst.c
build/djgpp/JOYTEST.EXE: tests/shim/joytest.c
build/djgpp/SBBEEP.EXE: tests/shim/sbbeep.c
build/djgpp/FPCHECK.EXE: tests/hal/fpcheck.c tests/unit/fpcheck.inc hal/include/mga/fp.h hal/include/mga/setupconv.h
dostests-djgpp: build/djgpp/HELLO.EXE build/djgpp/PROBE.EXE build/djgpp/HOOKS.EXE build/djgpp/SCALE.EXE \
                build/djgpp/STACKPG.EXE build/djgpp/MOUSETST.EXE build/djgpp/JOYTEST.EXE build/djgpp/SBBEEP.EXE \
                build/djgpp/FPCHECK.EXE build/djgpp/TWODTEST.EXE
.PHONY: dostests-djgpp

# The FISTP float-to-integer helpers on both DOS compilers, in 86Box
# (tests/hal/fpcheck.c; the host builds run the same checks in tests-host).
loopa-fpcheck: dostests build/djgpp/FPCHECK.EXE
	@set -e; for b in ow djgpp; do \
	  exe=build/$$b/dos/FPCHECK.EXE; [ $$b = djgpp ] && exe=build/djgpp/FPCHECK.EXE; \
	  $(DEV) $(PYTHON) tools/loopa/run.py --name fpcheck-$$b --exe $$exe --idle 60 --timeout 300 >/dev/null || true; \
	  got=$$(cat out/fpcheck-$$b/status); grep -ah 'HX-TEST fpcheck' out/fpcheck-$$b/serial.log || true; \
	  if [ "$$got" != PASS ]; then echo "loopa-fpcheck: $$b: $$got"; exit 1; fi; done; \
	echo "loopa-fpcheck: Watcom and DJGPP pass"
.PHONY: loopa-fpcheck


# ---- Host tests ----------------------------------------------------------
include tests/unit/unit.mk

clean:
	rm -rf build out

help:
	@echo "runtime        build build/ow/GLIDE2X.OVL (default)"
	@echo "runtime-prof   build build/ow-prof/GLIDE2X.OVL with stage timers (MGL-PROF)"
	@echo "check-exports  verify module name and exported names against the games"
	@echo "check-clib     verify the DLL links only self-contained C-library code"
	@echo "tests-host     build and run host unit tests"
	@echo "hal-ow|hal-djgpp|hal-host   build the shared HAL for each toolchain"
	@echo "header         regenerate glide/include/glide/glide2.h from glide/abi/"
	@echo "setup-ow|setup-djgpp        install pinned toolchains"
	@echo "loopa TEST=..  run a DOS test in 86Box (see tools/loopa)"

-include $(shell find build -name '*.d' 2>/dev/null)

# ---- Loop A (86Box) ------------------------------------------------------
.PHONY: 86box loopa loopa-selftest loopa-cfgcheck loopa-selftest-ext
86box:
	$(DEV) tools/86box/build.sh

# Generated 86Box configs, RUN.BAT and golden keys unchanged (tools/loopa/ref/);
# no emulator needed. After a deliberate change: tools/loopa/cfgcheck.py --update.
loopa-cfgcheck:
	$(PYTHON) tools/loopa/cfgcheck.py

# The harness's machine options: 486 profiles, the VBE card, and more
# (tools/loopa/selftest_ext.py; CHECKS="486 vbe" runs some).
loopa-selftest-ext: dostests build/djgpp/STACKPG.EXE build/djgpp/PROBE.EXE build/djgpp/WARPTRI.EXE \
                    build/djgpp/CLOCKMON.EXE v86test
	$(DEV) $(PYTHON) tools/loopa/selftest_ext.py $(CHECKS)

# The HAL's 2D primitives, cursor and display start on every Matrox card
# (tests/hal/twodtest.c, docs/matrox-2d.md; CARDS="g200 g450pci" picks some).
loopa-twodtest: dostests build/djgpp/TWODTEST.EXE
	$(DEV) $(PYTHON) tools/loopa/twodtest.py $(CARDS)

# make loopa TEST=hello [ARGS="--frames 10"] [OVL=build/ow/GLIDE2X.OVL]
loopa: dostests
	$(DEV) $(PYTHON) tools/loopa/run.py --name $(TEST) --exe build/ow/dos/$(shell echo $(TEST) | tr a-z A-Z).EXE \
	  $(if $(ARGS),--args="$(ARGS)") $(if $(OVL),--ovl $(OVL))

loopa-selftest: dostests runtime build/djgpp/STACKPG.EXE build/djgpp/MOUSETST.EXE build/djgpp/JOYTEST.EXE \
                build/djgpp/SBBEEP.EXE
	@set -e; \
	check() { $(DEV) $(PYTHON) tools/loopa/run.py --name selftest-$$1 --exe build/ow/dos/HELLO.EXE \
	            --idle 25 --boot-grace 20 $${3:+--args=$$3} >/dev/null || true; \
	          got=$$(cat out/selftest-$$1/status); \
	          if [ "$$got" = "$$2" ]; then echo "  selftest $$1: $$got (ok)"; \
	          else echo "  selftest $$1: got $$got, want $$2"; exit 1; fi; }; \
	check pass PASS; check fail FAIL --fail; check hang HANG --hang; check crash GUEST-EXC --crash; \
	hook() { $(DEV) $(PYTHON) tools/loopa/run.py --name selftest-hook-$$1 --exe build/ow/dos/CONFORM.EXE \
	           --args=$$1 --ovl build/ow/GLIDE2X.OVL --idle 30 --timeout 90 >/dev/null || true; \
	         if grep -q "$$2" out/selftest-hook-$$1/serial.log && \
	            grep -q "HX-VMODE bios=03" out/selftest-hook-$$1/serial.log; then \
	           echo "  selftest hook-$$1: $$2, text mode restored (ok)"; \
	         else echo "  selftest hook-$$1: expected $$2 and text mode"; exit 1; fi; }; \
	hook x01 MGL-EXC; hook x02 MGL-EXIT-HOOK; \
	$(DEV) $(PYTHON) tools/loopa/run.py --name selftest-stackpg --exe build/djgpp/STACKPG.EXE \
	  --idle 30 --timeout 90 >/dev/null || true; \
	got=$$(cat out/selftest-stackpg/status); \
	if [ "$$got" = PASS ]; then echo "  selftest stackpg: PASS (ok)"; \
	else echo "  selftest stackpg: got $$got, want PASS (an 86Box without local patch 0103?)"; exit 1; fi; \
	$(DEV) $(PYTHON) tools/loopa/run.py --name selftest-mouse --exe build/djgpp/MOUSETST.EXE --mouse ps2 \
	  --keys '@HX-TEST driver,1:mouse:40:-20:1,2:mouse:40:-20:0' --idle 40 --timeout 120 >/dev/null || true; \
	got=$$(cat out/selftest-mouse/status); \
	if [ "$$got" = PASS ]; then echo "  selftest mouse: PASS (ok)"; \
	else echo "  selftest mouse: got $$got, want PASS (an 86Box without local patch 0104?)"; exit 1; fi; \
	$(DEV) $(PYTHON) tools/loopa/run.py --name selftest-joy --exe build/djgpp/JOYTEST.EXE \
	  --keys '@HX-TEST centre,1:joy:axis:0:-32767,2:joy:axis:0:32767,3:joy:axis:0:0,3:joy:axis:1:-32767,4:joy:axis:1:32767,5:joy:axis:1:0,5:joy:axis:2:-32767,6:joy:axis:2:32767,7:joy:axis:2:0,7:joy:axis:3:-32767,8:joy:axis:3:32767,9:joy:axis:3:0,10:joy:button:0:1,11:joy:button:1:1,12:joy:button:2:1,13:joy:button:3:1' --idle 40 --timeout 150 >/dev/null || true; \
	got=$$(cat out/selftest-joy/status); \
	if [ "$$got" = PASS ]; then echo "  selftest joy: PASS (ok)"; \
	else echo "  selftest joy: got $$got, want PASS (an 86Box without local patch 0105?)"; exit 1; fi; \
	$(DEV) $(PYTHON) tools/loopa/run.py --name selftest-wav --exe build/djgpp/SBBEEP.EXE --sound sb16 --wav \
	  --pre "SET BLASTER=A220 I5 D1 H5 T6" --idle 40 --timeout 120 >/dev/null || true; \
	got=$$(cat out/selftest-wav/status); \
	if [ "$$got" = PASS ] && $(PYTHON) tools/loopa/wavcheck.py out/selftest-wav/audio.wav --tone 440 >/dev/null; \
	then echo "  selftest wav: PASS, 440 Hz recorded (ok)"; \
	else echo "  selftest wav: got $$got; see tools/loopa/wavcheck.py out/selftest-wav/audio.wav --tone 440"; exit 1; fi; \
	$(DEV) $(PYTHON) tools/loopa/run.py --name selftest-keywait --cmd "SERSAY HX-START keywait" \
	  --cmd "VECCHK save" --cmd "VECCHK check" --cmd "KEYWAIT 20" --cmd "SERSAY HX-DONE 0" \
	  --keys '@HX-KEYWAIT ready,1:0x1c' --idle 40 --timeout 120 >/dev/null || true; \
	if grep -q "HX-VECCHK ok" out/selftest-keywait/serial.log && \
	   grep -q "HX-KEY scan=1c" out/selftest-keywait/serial.log; then \
	  echo "  selftest keywait: VECCHK ok, KEYWAIT read the key (ok)"; \
	else echo "  selftest keywait: expected HX-VECCHK ok and HX-KEY scan=1c"; exit 1; fi


# Loop C rig program (static x86-64 Linux, for cuda6; docs/loop-c-results.md).
build/mgarig: tests/rig/mgarig.c tests/rig/edgecal.h $(HAL_COMMON) hal/port/linux.c $(wildcard hal/include/mga/*.h)
	$(Q)echo "  CC      $@"
	$(Q)$(HOST_CC) -static -O2 -std=gnu99 -Wall -Wextra -Werror -Ihal/include -o $@ tests/rig/mgarig.c \
	  hal/src/engine.c hal/src/fifo.c hal/src/chip.c hal/src/pci.c hal/src/setup/trap.c hal/port/linux.c -lm
# mgarig edgecal's frames from a chip against refrast (docs/loop-c-results.md):
#   build/edgecmp g200e DIR
build/edgecmp: tests/rig/edgecmp.c tests/rig/edgecal.h tests/unit/refrast.c hal/src/setup/trap.c hal/src/engine.c \
               hal/src/texhw.c hal/src/present.c hal/src/chip.c $(wildcard hal/include/mga/*.h)
	@mkdir -p $(dir $@)
	$(Q)echo "  CC      $@"
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -O2 -Itests/shim -Itests/unit -Itests/rig -o $@ tests/rig/edgecmp.c tests/unit/refrast.c \
	  hal/src/setup/trap.c hal/src/engine.c hal/src/texhw.c hal/src/present.c hal/src/chip.c -lm
rig: build/mgarig build/edgecmp
