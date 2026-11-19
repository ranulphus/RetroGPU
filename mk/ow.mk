# V=1 shows full command lines.
Q := $(if $(filter 1,$(V)),,@)
# Open Watcom v2 toolchain (runtime DLL, DOS/4GW test programs, HAL).
OWBIN   := $(WATCOM)/binl64
OWENV   := env WATCOM=$(WATCOM) INCLUDE=$(WATCOM)/h PATH=$(OWBIN):$(PATH)
WCC     := $(OWENV) $(OWBIN)/wcc386
WLINK   := $(OWENV) $(OWBIN)/wlink
WLIB    := $(OWENV) $(OWBIN)/wlib
WDUMP   := $(OWENV) $(OWBIN)/wdump

MGA_VERSION := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
OW_CFLAGS := -bt=dos -mf -3s -fp5 -fpi87 -zri -ei -j -zastd=c99 -zq -we -wx \
             -i=glide/include -i=hal/include -i=glide/src -i=build/gen -dMGA_OW=1 -dMGA_GLIDE_VERSION="\"$(MGA_VERSION)\""
ifeq ($(DEBUG),1)
OW_CFLAGS += -od -d1 -dMG_DEBUG=1
else
OW_CFLAGS += -oxt
endif
# REGCRC=1: the HAL's register trace with a running CRC of every register write and WARP vertex, printed at each
# buffer swap and at close (MGL-REGCRC lines): the check that moving Glide's Matrox code behind its back-end table
# changed no write (glide/tools/conform/regcrc.py). build/ow/regcrc_mode rebuilds everything when it changes.
ifeq ($(REGCRC),1)
OW_CFLAGS += -dMGA_TRACE_MMIO=1 -dMGA_REGCRC=1
endif
build/ow/regcrc_mode: FORCE
	@mkdir -p $(dir $@)
	@echo '$(REGCRC)' | cmp -s - $@ || echo '$(REGCRC)' > $@
.PHONY: FORCE
FORCE:
# DLL and HAL code: no stack probes, no default-library references.
OW_DLLFLAGS := -bd -s -zl

build/ow/%.obj: %.c build/ow/regcrc_mode | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_CFLAGS) $(if $(filter glide/src/% hal/%,$<),$(OW_DLLFLAGS)) -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<

build/ow/gen/%.obj: build/gen/%.c build/ow/regcrc_mode | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_CFLAGS) $(OW_DLLFLAGS) -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<
