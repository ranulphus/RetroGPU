# DOS test programs (32-bit, DOS/4GW) and 16-bit batch helpers.
BUILD_ID := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
OW_EXEFLAGS := $(OW_CFLAGS) -i=tests/shim -dHX_BUILD_ID="\"$(BUILD_ID)\""

SHIM_SRCS := tests/shim/hx.c tests/shim/leload.c tests/shim/glbind.c build/gen/glapi_names.c
SHIM_OBJS := $(SHIM_SRCS:%.c=build/ow/exe/%.obj)

build/ow/exe/build/gen/%.obj: build/gen/%.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_EXEFLAGS) -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<

build/ow/exe/%.obj: %.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_EXEFLAGS) -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<

# build/ow/dos/<NAME>.EXE from a list of objects.
define dos_exe
build/ow/dos/$(1).EXE: $(2) $(SHIM_OBJS) build/ow/mgahal_exe.lib
	@mkdir -p build/ow/dos
	$$(Q)echo "  WLINK   $$@"
	$$(Q)$$(WLINK) system dos4g option quiet option stack=64k name $$@ \
	  $$(addprefix file ,$(2) $$(SHIM_OBJS)) library build/ow/mgahal_exe.lib option map=$$(@:.EXE=.map)
DOS_EXES += build/ow/dos/$(1).EXE
endef

$(eval $(call dos_exe,HELLO,build/ow/exe/tests/shim/hello.obj))
$(eval $(call dos_exe,LESPIKE,build/ow/exe/glide/tests/spike/lespike.obj))
$(eval $(call dos_exe,PROBE,build/ow/exe/tests/hal/probe.obj))
$(eval $(call dos_exe,ROMDUMP,build/ow/exe/tests/hal/romdump.obj))
$(eval $(call dos_exe,SMOKE,build/ow/exe/tests/hal/smoke.obj))
$(eval $(call dos_exe,SCALE,build/ow/exe/tests/hal/scale.obj))
$(eval $(call dos_exe,FPCHECK,build/ow/exe/tests/hal/fpcheck.obj))
$(eval $(call dos_exe,GLRES,build/ow/exe/glide/tests/glres/glres.obj))
$(eval $(call dos_exe,TEXPROBE,build/ow/exe/glide/tests/spike/texprobe.obj))
CONFORM_OBJS := $(patsubst %.c,build/ow/exe/%.obj,$(wildcard glide/tests/conform/*.c))
GLPLAY_OBJS := build/ow/exe/glide/tests/replay/glplay.obj build/ow/exe/build/gen/replay_gen.obj build/ow/exe/glide/src/trace/trfmt.obj
$(eval $(call dos_exe,GLPLAY,$(GLPLAY_OBJS)))
$(eval $(call dos_exe,CONFORM,$(CONFORM_OBJS)))

# 16-bit .COM helpers.
DOS_TOOLS := UTEXIT SERSAY WAITSEC REBOOT VMODE KEYWAIT VECCHK SBCHK VBEINFO COM2ECHO XMSINFO RTCSET UMBINFO
define dos_com
build/ow/dos/$(1).COM: tools/dos/$(2).c
	@mkdir -p build/ow/dos/obj16
	$$(Q)echo "  WCC16   $$<"
	$$(Q)$$(OWENV) $$(OWBIN)/wcc -bt=dos -ms -0 -os -zq -we -fo=build/ow/dos/obj16/$(2).obj $$<
	$$(Q)$$(WLINK) system com option quiet name $$@ file build/ow/dos/obj16/$(2).obj
endef
$(eval $(call dos_com,UTEXIT,utexit))
$(eval $(call dos_com,SERSAY,sersay))
$(eval $(call dos_com,WAITSEC,waitsec))
$(eval $(call dos_com,REBOOT,reboot))
$(eval $(call dos_com,VMODE,vmode))
$(eval $(call dos_com,KEYWAIT,keywait))
$(eval $(call dos_com,VECCHK,vecchk))
$(eval $(call dos_com,SBCHK,sbchk))
$(eval $(call dos_com,VBEINFO,vbeinfo))
$(eval $(call dos_com,COM2ECHO,com2echo))
$(eval $(call dos_com,XMSINFO,xmsinfo))
$(eval $(call dos_com,RTCSET,rtcset))
$(eval $(call dos_com,UMBINFO,umbinfo))

dostools: $(DOS_TOOLS:%=build/ow/dos/%.COM)
dostests: $(DOS_EXES) dostools
.PHONY: dostools dostests

# V86TEST, the CPU self-test (tests/cpu): a 16-bit loader that enters
# protected mode with paging, and a 32-bit monitor (host gcc) linked at 4 MB.
V86PM_CFLAGS := -m32 -march=i486 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
                -fno-asynchronous-unwind-tables -fno-delete-null-pointer-checks -mgeneral-regs-only \
                -O2 -Wall -Wextra -Werror -nostdinc -Itests/cpu
V86PM_SRCS := tests/cpu/pm_entry.S tests/cpu/v86snip.S tests/cpu/pm_main.c
build/cpu/V86PM.BIN: $(V86PM_SRCS) tests/cpu/pm.ld tests/cpu/v86pm.h
	@mkdir -p build/cpu
	$(Q)echo "  CC32    $@"
	$(Q)$(HOST_CC) $(V86PM_CFLAGS) -nostdlib -no-pie -Wl,-T,tests/cpu/pm.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments \
	  -o build/cpu/v86pm.elf $(V86PM_SRCS)
	$(Q)objcopy -O binary build/cpu/v86pm.elf $@
build/ow/dos/V86TEST.EXE: tests/cpu/v86test.c tests/cpu/v86sw.asm tests/cpu/v86pm.h
	@mkdir -p build/ow/dos/obj16
	$(Q)echo "  WCC16   $<"
	$(Q)$(OWENV) $(OWBIN)/wcc -bt=dos -ms -0 -os -zq -we -itests/cpu -dHX_BUILD_ID="\"$(BUILD_ID)\"" \
	  -fo=build/ow/dos/obj16/v86test.obj tests/cpu/v86test.c
	$(Q)$(OWENV) $(OWBIN)/wasm -q -fo=build/ow/dos/obj16/v86sw.obj tests/cpu/v86sw.asm
	$(Q)$(WLINK) system dos option quiet name $@ file build/ow/dos/obj16/v86test.obj,build/ow/dos/obj16/v86sw.obj
v86test: build/ow/dos/V86TEST.EXE build/cpu/V86PM.BIN
.PHONY: v86test
