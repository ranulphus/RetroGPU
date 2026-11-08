# glide/glide.mk - the Glide 2.x runtime: GLIDE2X.OVL, its recording proxy
# GLTRACE.OVL, the host trace replay and the conformance suite. Included by
# the top-level Makefile and run from the repository root, so paths here are
# relative to it; outputs stay in build/ (build/ow/GLIDE2X.OVL). The Glide
# test programs (CONFORM, GLPLAY, GLRES, LESPIKE, TEXPROBE) are with the
# other DOS EXEs in mk/dos.mk.

# ---- Generated glue ------------------------------------------------------
API_SRCS := glide/abi/glide2x.api glide/tools/abi/api.py glide/tools/abi/gen_exports.py glide/tools/abi/gen_trace.py
build/gen/stamp: $(API_SRCS)
	@mkdir -p build/gen
	$(PYTHON) glide/tools/abi/gen_exports.py glide build/gen
	$(PYTHON) glide/tools/abi/gen_trace.py build/gen
	@touch $@

header: glide/include/glide/glide2.h
glide/include/glide/glide2.h: glide/abi/constants.tsv glide/abi/types.h.frag glide/abi/glide2x.api glide/tools/abi/gen_header.py
	$(PYTHON) glide/tools/abi/gen_header.py glide $@

check-header:
	@$(PYTHON) glide/tools/abi/gen_header.py glide build/glide2.h.check && cmp -s build/glide2.h.check glide/include/glide/glide2.h \
	  || { echo "glide/include/glide/glide2.h is stale: run make header"; exit 1; }

build/gen/stubs.c build/gen/api_names.c build/gen/api_ids.h build/gen/api_names.h build/gen/thunks.c build/gen/replay_gen.c build/gen/proxy_thunks.c build/gen/glapi_names.c build/gen/glapi_static.c: build/gen/stamp

# ---- Runtime DLL ---------------------------------------------------------
RT_SRCS := $(wildcard $(addprefix glide/src/,rt/*.c dll/*.c glide/*.c lfb/*.c tex/*.c combine/*.c trace/*.c))
RT_OBJS := $(RT_SRCS:%.c=build/ow/%.obj) build/ow/gen/stubs.obj build/ow/gen/api_names.obj build/ow/gen/thunks.obj

build/ow/glide2x.lnk: glide/glide2x.lnk.in glide/glide.mk
	@mkdir -p $(dir $@)
	$(Q)sed -e 's|@WATCOM@|$(WATCOM)|g' -e 's|@VER@|$(shell git describe --always --dirty 2>/dev/null)|' $< > $@

build/ow/GLIDE2X.OVL: $(RT_OBJS) build/ow/mgahal.lib build/ow/glide2x.lnk
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) @build/ow/glide2x.lnk name $@ option map=build/ow/GLIDE2X.map \
	  $(addprefix file ,$(RT_OBJS)) library build/ow/mgahal.lib
	$(Q)$(PYTHON) glide/tools/abi/lefix.py $@ >/dev/null
runtime: build/ow/GLIDE2X.OVL

# Profiling flavour (hal/include/mga/prof.h): the same runtime with stage
# timers; MGAGLIDE stats=N adds an MGL-PROF line to each MGL-STAT. Pentium only.
OWP := build/ow-prof
RT_OBJS_PROF := $(RT_SRCS:%.c=$(OWP)/%.obj) $(OWP)/gen/stubs.obj $(OWP)/gen/api_names.obj $(OWP)/gen/thunks.obj
$(OWP)/%.obj: %.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $< (prof)"
	$(Q)$(WCC) $(OW_CFLAGS) -dMGA_PROF=1 $(if $(filter glide/src/% hal/%,$<),$(OW_DLLFLAGS)) -ad=$(@:.obj=.d) -adt=$@ \
	  -add=$< -adfs -fo=$@ $<
$(OWP)/gen/%.obj: build/gen/%.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $< (prof)"
	$(Q)$(WCC) $(OW_CFLAGS) -dMGA_PROF=1 $(OW_DLLFLAGS) -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<
$(OWP)/mgahal.lib: $(HAL_OW:%.c=$(OWP)/%.obj)
	@rm -f $@
	$(Q)echo "  WLIB    $@"
	$(Q)$(WLIB) -q -b -n $@ $(addprefix +,$^)
$(OWP)/GLIDE2X.OVL: $(RT_OBJS_PROF) $(OWP)/mgahal.lib build/ow/glide2x.lnk
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) @build/ow/glide2x.lnk name $@ option map=$(OWP)/GLIDE2X.map \
	  $(addprefix file ,$(RT_OBJS_PROF)) library $(OWP)/mgahal.lib
	$(Q)$(PYTHON) glide/tools/abi/lefix.py $@ >/dev/null
runtime-prof: $(OWP)/GLIDE2X.OVL
.PHONY: runtime-prof

# GLTRACE.OVL: the recording proxy in front of a retail runtime (glide/docs/trace.md).
PX_OBJS := build/ow/gen/proxy_thunks.obj build/ow/proxy/proxy.obj build/ow/proxy/leload.obj \
           build/ow/glide/src/trace/trace.obj build/ow/glide/src/trace/trfmt.obj build/ow/glide/src/tex/texfmt.obj \
           build/ow/glide/src/dll/config.obj build/ow/glide/src/dll/dllmain.obj \
           $(patsubst %.c,build/ow/%.obj,$(wildcard glide/src/rt/*.c)) build/ow/gen/api_names.obj build/ow/gen/glapi_names.obj
build/ow/proxy/%.obj: glide/src/proxy/%.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_CFLAGS) $(OW_DLLFLAGS) -i=tests/shim -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<
build/ow/proxy/leload.obj: tests/shim/leload.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  WCC     $<"
	$(Q)$(WCC) $(OW_CFLAGS) $(OW_DLLFLAGS) -i=tests/shim -ad=$(@:.obj=.d) -adt=$@ -add=$< -adfs -fo=$@ $<
build/ow/GLTRACE.OVL: $(PX_OBJS) build/ow/mgahal.lib build/ow/glide2x.lnk
	$(Q)echo "  WLINK   $@"
	$(Q)$(WLINK) @build/ow/glide2x.lnk name $@ option map=build/ow/GLTRACE.map \
	  $(addprefix file ,$(PX_OBJS)) library build/ow/mgahal.lib
	$(Q)$(PYTHON) glide/tools/abi/lefix.py $@ >/dev/null
gltrace: build/ow/GLTRACE.OVL

check-exports: build/ow/GLIDE2X.OVL
	$(PYTHON) glide/tools/abi/check_exports.py $< glide/abi/glide2x.api glide/abi/games/*.names
check-clib: build/ow/GLIDE2X.OVL
	$(PYTHON) glide/tools/abi/check_clib.py build/ow/GLIDE2X.map

# ---- Host trace replay (glide/tools/hreplay) --------------------------------------
HR_SRCS := $(RT_SRCS) $(HAL_HOST) tests/unit/refrast.c glide/tools/hreplay/hreplay.c
HR_GEN  := build/gen/thunks.c build/gen/stubs.c build/gen/api_names.c build/gen/replay_gen.c build/gen/glapi_static.c
build/host32/hreplay: $(HR_SRCS) $(HR_GEN) $(wildcard glide/src/*/*.h hal/include/mga/*.h tests/unit/*.h tests/shim/*.h)
	@mkdir -p $(dir $@)
	$(Q)echo "  CC32    $@"
	$(Q)$(HOST_CC) $(HOST_CFLAGS32) -Wno-unused-parameter -Wno-array-parameter -Itests/shim -Itests/unit -o $@ $(HR_SRCS) $(HR_GEN) -lm
hreplay: build/host32/hreplay
hreplay-check: build/host32/hreplay dostests runtime
	$(DEV) $(PYTHON) glide/tools/hreplay/check.py

# Conformance suite (glide/tools/conform/run.py). References come from a retail
# OVL on the emulated Voodoo and are committed; checks run MGA-Glide.
.PHONY: conform conform-ref conform-ci rig hreplay hreplay-check gltrace smoke-djgpp
conform: runtime dostests
	$(DEV) $(PYTHON) glide/tools/conform/run.py check $(TESTS)
conform-ref: dostests
	$(DEV) $(PYTHON) glide/tools/conform/run.py ref $(TESTS)
conform-ci: runtime dostests
	$(PYTHON) glide/tools/conform/run.py check
	MGA_CARD=g200 $(PYTHON) glide/tools/conform/run.py check
	MGA_CARD=g400 $(PYTHON) glide/tools/conform/run.py check
	MGA_CARD=g450 $(PYTHON) glide/tools/conform/run.py check
