# DOS-GL: OpenGL 1.1 subset for Matrox G400/G450/G200 under DJGPP (PRD.md).
#
#   make                  libGL.a and the examples (DJGPP)
#   make tests-host       host unit tests (Linux gcc)
#   make loopa TEST=hello [CARD=g450] [ARGS=--fail]    run an example in 86Box
#   make quake / loopa-quake GAME=quake2   the Quake ports (tools/quake)
#   make halflife / loopa-halflife MODE=play   Half-Life on Xash3D FWGS (tools/halflife)
#   make doom / loopa-doom MODE=timedemo   PrBoom-plus on SDL3 and DOS-GL (tools/doom)
#   make sdl / sdl-examples / loopa-sdl   SDL3 on DOS with DOS-GL (tools/sdl, docs/sdl.md)
#   make conform-bench PC=bench-g400 [TESTS=...]   the conformance suite on a bench PC
# The Matrox HAL, harness and 86Box patches come from MGA-Glide's checkout beside
# this one (MGA_GLIDE, default ~/MGA-Glide; PRD D15), built from its main as it is.
include config.mk
-include config.local.mk
.SECONDEXPANSION:

MGAHAL   := $(MGA_GLIDE)
export MGA_GLIDE
ifeq ($(wildcard $(MGAHAL)/hal/include/mga/hal.h),)
ifneq ($(filter-out clean help,$(or $(MAKECMDGOALS),all)),)
$(error No MGA-Glide checkout at MGA_GLIDE=$(MGA_GLIDE): clone github.com/ranulphus/MGA-Glide there, or set MGA_GLIDE)
endif
endif
# GLOS's API header (src/dgl/glos.c) comes from its checkout beside this one too (GLOS, default ~/GLOS), on main.
# Dependency files from before 2026-11-02 name the vendored copies that went (third_party/mgahal, third_party/glos):
# they and their objects are dropped once, so the next build makes them afresh against the checkouts.
$(foreach d,$(shell grep -rlE 'third_party/(mgahal|glos)/' build --include='*.d' 2>/dev/null),$(shell rm -f $(d) $(d:.d=.o)))
BUILD_ID := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
Q ?= @

DJENV := env LD_LIBRARY_PATH=$(DJGPP_PREFIX)/hostlib
DJCC  := $(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-gcc
DJAR  := $(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-ar
DJ_CFLAGS := -std=gnu99 -O2 -march=i586 -mtune=pentium2 -Wall -Wextra -Werror -Iinclude -I$(MGAHAL)/hal/include -I$(GLOS)/include \
             -DMGA_DJGPP=1 -DDGL_BUILD_ID='"$(BUILD_ID)"'
DJ_TESTFLAGS := -I$(MGAHAL)/tests/shim -DHX_BUILD_ID='"$(BUILD_ID)"'
# PROF=1: the whole tree with stage timers (MGA_PROF, hal/include/mga/prof.h):
# DGL_STATS=2 adds a DGL-PROF line a second, 3 also DGL-MICRO at start
# (tools/perf/profsum.py in the HAL sums them). Needs a Pentium. Building
# without PROF=1 again returns to the normal library (build/prof_mode).
ifeq ($(PROF),1)
DJ_CFLAGS += -DMGA_PROF=1
endif

# The shared HAL (PRD D15) goes into libGL.a so consumers link only -lGL. Its
# objects are built here, under build/*/mgahal/ (MGA-Glide's tree is only read).
HAL_SRCS := $(addprefix $(MGAHAL)/hal/,src/debug/serial.c src/pci.c src/chip.c src/vbe.c src/fifo.c \
              src/engine.c src/dac.c src/texhw.c src/present.c src/debug/regtrace.c src/setup/trap.c src/warp.c port/djgpp.c)
hal_objs = $(patsubst $(MGAHAL)/%.c,build/$(1)/mgahal/%.o,$(2))
DGL_SRCS := $(wildcard src/dgl/*.c) $(wildcard src/gl/*.c)
LIB := build/lib/libGL.a

.PHONY: all lib examples sdl sdl-host sdl-examples loopa-sdl tests-host loopa conform conform-dos conform-host classicube loopa-classicube quake loopa-quake halflife loopa-halflife doom loopa-doom setup-djgpp setup-ow dostools 86box clean help
all: lib examples

build/djgpp/%.o: %.c build/prof_mode
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) $(DJ_CFLAGS) -MMD -MP -c -o $@ $<
build/djgpp/mgahal/%.o: $(MGAHAL)/%.c build/prof_mode
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) $(DJ_CFLAGS) -MMD -MP -c -o $@ $<
-include $(shell find build/djgpp -name '*.d' 2>/dev/null)

# The version string carries the build ID: rebuild it whenever the ID changes
# (a new commit, or the tree turning dirty or clean), not only when the source does.
build/build_id: FORCE
	@mkdir -p $(dir $@)
	@echo '$(BUILD_ID)' | cmp -s - $@ || echo '$(BUILD_ID)' > $@
build/djgpp/src/dgl/version.o: build/build_id
.PHONY: FORCE
build/prof_mode: FORCE
	@mkdir -p $(dir $@)
	@echo '$(PROF)' | cmp -s - $@ || echo '$(PROF)' > $@

# Generated: stubs for the GL 1.1 functions not implemented, and the
# dglGetProcAddress table (tools/gen_stubs.py; files rewritten only on change).
GEN_SRCS := build/gen/stubs.c build/gen/procs.c
$(GEN_SRCS) &: tools/gen_stubs.py src/gl/gl11.api $(DGL_SRCS)
	$(Q)python3 tools/gen_stubs.py src/gl src/dgl build/gen

$(LIB): $(DGL_SRCS:%.c=build/djgpp/%.o) $(GEN_SRCS:%.c=build/djgpp/%.o) $(call hal_objs,djgpp,$(HAL_SRCS))
	@mkdir -p $(dir $@)
	$(Q)echo "  AR      $@"
	$(Q)rm -f $@ && $(DJAR) rcs $@ $^
lib: $(LIB)

# Examples: one directory each, linked with the guest test shim (HX- lines).
EXAMPLES := hello probe clear tri cube texcube g4exp texexp resgl
build/exe/%.EXE: $(LIB) $(MGAHAL)/tests/shim/hx.c
	@mkdir -p $(dir $@)
	$(Q)echo "  DJLD    $@"
	$(Q)$(DJCC) $(DJ_CFLAGS) $(DJ_TESTFLAGS) -o $@ $(wildcard examples/$(shell echo $* | tr A-Z a-z)/*.c) \
	  $(MGAHAL)/tests/shim/hx.c $(LIB) -lm
	@if [ -e "$(@:.EXE=.exe)" ] && ! [ "$(@:.EXE=.exe)" -ef "$@" ]; then rm -f "$(@:.EXE=.exe)"; fi
build/exe/HELLO.EXE: $(wildcard examples/hello/*.c)
build/exe/PROBE.EXE: $(wildcard examples/probe/*.c) src/dgl/dgl.h
build/exe/CLEAR.EXE: $(wildcard examples/clear/*.c) src/dgl/dgl.h
build/exe/TRI.EXE: $(wildcard examples/tri/*.c) src/dgl/dgl.h
build/exe/CUBE.EXE: $(wildcard examples/cube/*.c)
build/exe/TEXCUBE.EXE: $(wildcard examples/texcube/*.c) examples/common/texscene.h
build/exe/G4EXP.EXE: $(wildcard examples/g4exp/*.c) src/dgl/dgl.h src/gl/gl_tex.h
build/exe/TEXEXP.EXE: $(wildcard examples/texexp/*.c)
build/exe/RESGL.EXE: $(wildcard examples/resgl/*.c)
examples: $(foreach e,$(EXAMPLES),build/exe/$(shell echo $(e) | tr a-z A-Z).EXE)

# SDL3 (third_party/sdl, pinned upstream, with tools/sdl/patches) as a static
# library for DOS, and for Linux from the same source (docs/sdl.md). SDL
# examples link it before libGL.a.
SDL_LIB := build/sdl/dos/lib/libSDL3.a
SDL_EXAMPLES := sdlinfo sdlkeys sdlbeep sdlgl sdlcrash
$(SDL_LIB): third_party/sdl/CMakeLists.txt tools/sdl/build.sh $(wildcard tools/sdl/patches/*.patch) $(LIB)
	$(Q)tools/sdl/build.sh
sdl: $(SDL_LIB)
# In the dev container: its libsdl2-dev brings every SDL build dependency
# (X11, Wayland, PulseAudio, ALSA...); the static library loads them at run time.
sdl-host:
	$(Q)$(MGAHAL)/tools/dev tools/sdl/build.sh --host
define sdl_example
build/exe/$(1).EXE: $$(wildcard examples/$(2)/*.c) $(SDL_LIB) $(LIB) $(MGAHAL)/tests/shim/hx.c
	@mkdir -p $$(dir $$@)
	$$(Q)echo "  DJLD    $$@"
	$$(Q)$$(DJCC) $$(DJ_CFLAGS) $$(DJ_TESTFLAGS) -Ibuild/sdl/dos/include -o $$@ $$(wildcard examples/$(2)/*.c) \
	  $(MGAHAL)/tests/shim/hx.c $(SDL_LIB) $(LIB) -lm
endef
$(foreach e,$(SDL_EXAMPLES),$(eval $(call sdl_example,$(shell echo $(e) | tr a-z A-Z),$(e))))
sdl-examples: $(foreach e,$(SDL_EXAMPLES),build/exe/$(shell echo $(e) | tr a-z A-Z).EXE)
# The SDL examples in 86Box: keys typed, SB16 audio recorded (tools/sdl/loopa.sh).
loopa-sdl: sdl-examples dostools
	$(Q)tools/sdl/loopa.sh $(CARD)

# Conformance tests (tests/conform, D16): DOS builds against libGL.a and
# host builds against Mesa OSMesa (the references; built in the dev container).
CONFORM := $(sort $(foreach f,$(wildcard tests/conform/t[0-9]*_*.c),$(firstword $(subst _, ,$(notdir $(f))))))
build/exe/conform/%.EXE: $(LIB) tests/conform/ct_dos.c tests/conform/ct.h tests/conform/ct_tex.h \
                         $(MGAHAL)/tests/shim/hx.c $$(wildcard tests/conform/$$(shell echo $$* | tr A-Z a-z)_*.c)
	@mkdir -p $(dir $@)
	$(Q)echo "  DJLD    $@"
	$(Q)$(DJCC) $(DJ_CFLAGS) $(DJ_TESTFLAGS) -DCT_NAME='"$*"' -o $@ $(wildcard tests/conform/$(shell echo $* | tr A-Z a-z)_*.c) \
	  tests/conform/ct_dos.c $(MGAHAL)/tests/shim/hx.c $(LIB) -lm
	@if [ -e "$(@:.EXE=.exe)" ] && ! [ "$(@:.EXE=.exe)" -ef "$@" ]; then rm -f "$(@:.EXE=.exe)"; fi
build/host/conform/%: $$(wildcard tests/conform/$$*_*.c) tests/conform/ct_host.c tests/conform/ct.h tests/conform/ct_tex.h
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) -std=gnu99 -O1 -Wall -Wextra -Werror -o $@ $(wildcard tests/conform/$*_*.c) tests/conform/ct_host.c \
	  -lOSMesa -lm
conform-dos: $(foreach t,$(CONFORM),build/exe/conform/$(shell echo $(t) | tr a-z A-Z).EXE)
conform-host: $(foreach t,$(CONFORM),build/host/conform/$(t))
conform: conform-dos dostools
	$(MGAHAL)/tools/dev sh -c '$(MAKE) conform-host && python3 tools/conform/run.py --card $(CARD) \
	  $(if $(PRE),--pre "$(PRE)") $(TESTS)'
# The same on a bench PC (MGA-Glide's tools/bench/run.py, on the host: the PC must reach it).
conform-bench: conform-dos
	$(MGAHAL)/tools/dev sh -c '$(MAKE) conform-host && python3 tools/conform/run.py --refs $(TESTS)'
	python3 tools/conform/run.py --bench $(PC) $(if $(PRE),--pre "$(PRE)") $(TESTS)

# ClassiCube (M5): build against libGL.a, and run it in 86Box with the
# procedural test texture pack (tools/classicube/mkpack.py): singleplayer,
# DGL_EXIT_AFTER frames, DGL_STATS lines, console screenshots.
CC_FRAMES ?= 300
classicube: lib
	tools/classicube/build.sh
build/cc/default.zip: tools/classicube/mkpack.py
	@mkdir -p $(dir $@)
	python3 tools/classicube/mkpack.py $@
loopa-classicube: classicube build/cc/default.zip dostools
	$(MGAHAL)/tools/dev python3 $(MGAHAL)/tools/loopa/run.py --name classicube --exe build/cc/CCDOS.EXE \
	  --card $(CARD) --out $(CURDIR)/out/classicube-$(CARD) --args="--singleplayer" \
	  --file "build/cc/default.zip=/TEST/TEXPACKS/DEFAULT.ZIP" \
	  --pre "SET DGL_EXIT_AFTER=$(CC_FRAMES)" --pre "SET DGL_STATS=1" \
	  --timeout 1500 --idle 300 --shots 60,120,180

# The Quake ports (tools/quake): the pinned forks built against libGL.a, run
# in 86Box on the owner's game data (tools/quake/fixtures.py; never committed).
#   make loopa-quake GAME=quake|lq|quake2 [CARD=g450]
quake: lib
	tools/quake/build.sh
loopa-quake: quake dostools
	tools/quake/run.sh $(GAME) $(CARD)

# Half-Life (tools/halflife): the pinned Xash3D FWGS and hlsdk-portable forks
# built into one executable against libGL.a, run in 86Box on the owner's WON
# data (tools/halflife/fixtures.py; never committed).
#   make loopa-halflife MODE=boot|server|maps|map|keys|play [CARD=g450]
halflife: lib
	tools/halflife/build.sh
loopa-halflife: halflife dostools
	tools/halflife/run.sh $(MODE) $(CARD)

# PrBoom-plus (tools/doom): the pinned fork (a local repository, ~/prboom-plus-dos)
# built on SDL3 (build/sdl/dos) and libGL.a, run in 86Box on the owner's IWADs
# (tools/doom/fixtures.py; never committed).
#   make loopa-doom MODE=nodraw|timedemo|play|keys|cmd [CARD=g450]
doom: lib sdl
	tools/doom/build.sh
loopa-doom: doom dostools
	tools/doom/run.sh $(MODE) $(CARD)

# Host unit tests.
HOST_CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Werror -Iinclude -I$(MGAHAL)/hal/include
build/host/test_gl_h_abi: tests/unit/test_gl_h_abi.c include/GL/gl.h include/GL/glext.h
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -Ithird_party/classicube -o $@ $<
# Unit tests: tests/unit/test_<name>.c + UNIT_<name> sources, host gcc.
GL_SRCS := $(wildcard src/gl/*.c)
UNIT_TESTS := matrix clip assembly texconv vram lists buffer
UNIT_lists := src/gl/lists.c src/gl/vertex.c src/gl/buffer.c src/gl/matrix.c src/gl/state.c src/gl/raster.c
UNIT_buffer := src/gl/buffer.c src/gl/vertex.c src/gl/matrix.c src/gl/state.c src/gl/raster.c src/gl/combine.c
UNIT_texconv := src/gl/texconv.c
UNIT_vram := src/gl/vram.c
UNIT_matrix := src/gl/matrix.c src/gl/state.c
UNIT_clip := src/gl/clip.c
UNIT_assembly := src/gl/vertex.c src/gl/buffer.c src/gl/matrix.c src/gl/state.c src/gl/raster.c
build/host/test_%: tests/unit/test_%.c tests/unit/unit.c tests/unit/unit.h $$(UNIT_$$*) $(wildcard src/gl/*.h)
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -o $@ $< tests/unit/unit.c $(UNIT_$*) -lm
tests-host: build/host/test_gl_h_abi $(UNIT_TESTS:%=build/host/test_%)
	@set -e; for t in $^; do echo "== $$t"; $$t; done

# Loop C: DOS-GL built for 32-bit Linux (x87 maths, as DJGPP's code) on a
# real card through the HAL's Linux port: the conformance tests, static, for
# a machine with a Matrox chip (tools/rig/run.py; docs/rig.md). Built in the
# dev container (gcc-multilib).
RIG_CFLAGS := -std=gnu99 -O2 -m32 -march=i586 -mfpmath=387 -Wall -Wextra -Werror -Iinclude -I$(MGAHAL)/hal/include -I$(GLOS)/include \
              -DDGL_RIG=1 -DDGL_BUILD_ID='"$(BUILD_ID)-rig"'
RIG_HAL := $(filter-out %/port/djgpp.c,$(HAL_SRCS)) $(MGAHAL)/hal/port/linux.c
build/rig/%.o: %.c
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) $(RIG_CFLAGS) -MMD -MP -c -o $@ $<
build/rig/mgahal/%.o: $(MGAHAL)/%.c
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) $(RIG_CFLAGS) -MMD -MP -c -o $@ $<
build/rig/libGL.a: $(DGL_SRCS:%.c=build/rig/%.o) $(GEN_SRCS:%.c=build/rig/%.o) $(call hal_objs,rig,$(RIG_HAL))
	$(Q)rm -f $@ && ar rcs $@ $^
build/rig/conform/%: build/rig/libGL.a tests/conform/ct_rig.c
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) $(RIG_CFLAGS) -static -DCT_NAME='"$*"' -o $@ $(wildcard tests/conform/$*_*.c) \
	  tests/conform/ct_rig.c build/rig/libGL.a -lm
build/rig/rigbench: build/rig/libGL.a tests/rig/rigbench.c
	$(Q)$(HOST_CC) $(RIG_CFLAGS) -static -o $@ tests/rig/rigbench.c build/rig/libGL.a -lm
rig-build: $(foreach t,$(CONFORM),build/rig/conform/$(t)) build/rig/rigbench
rig:
	MGA_EXTRA_DIR=$(GLOS) $(MGAHAL)/tools/dev $(MAKE) -s rig-build
.PHONY: rig rig-build

# Loop A: the shared harness (86Box with the local patches), run in the
# dev container. Outputs in out/<TEST>/.
TEST ?= hello
CARD ?= g450
loopa: examples dostools
	$(MGAHAL)/tools/dev python3 $(MGAHAL)/tools/loopa/run.py --name $(TEST) \
	  --exe build/exe/$(shell echo $(TEST) | tr a-z A-Z).EXE --card $(CARD) --out $(CURDIR)/out/$(TEST) \
	  $(if $(ARGS),--args="$(ARGS)")

# Toolchain and emulator come from MGA-Glide's harness (its tools/setup/versions.mk;
# cached in ~/.cache/mga-glide). dostools and 86box build in MGA-Glide's checkout.
setup-djgpp:
	$(MAKE) -C $(MGAHAL) setup-djgpp DJGPP_PREFIX=$(DJGPP_PREFIX)
setup-ow:
	$(MAKE) -C $(MGAHAL) setup-ow
# The 16-bit DOS helpers Loop A and the bench put in C:\HX (built with Open Watcom).
dostools:
	$(MAKE) -C $(MGAHAL) dostools
86box:
	$(MAKE) -C $(MGAHAL) 86box

clean:
	rm -rf build out

help:
	@sed -n '3,12p' Makefile
