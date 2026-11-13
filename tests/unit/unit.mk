# Host unit tests. Each tests/unit/test_*.c is a standalone program built
# with the files listed in its UNIT_<name> variable.
UNIT_TESTS := $(patsubst tests/unit/%.c,%,$(wildcard tests/unit/test_*.c))
UNIT_COMMON := tests/unit/unit.c
UNIT_test_fmt := glide/src/rt/fmt.c
UNIT_test_leload := tests/shim/leload.c build/gen/glapi_names.c
UNIT_test_trap := hal/src/setup/trap.c tests/unit/refrast.c
UNIT_test_texfmt := glide/src/tex/texfmt.c
UNIT_test_regtrace := hal/src/debug/regtrace.c
UNIT_test_plan := hal/src/vbe.c tests/unit/refrast.c
UNIT_test_present := hal/src/present.c hal/src/engine.c hal/src/texhw.c hal/src/setup/trap.c tests/unit/refrast.c
UNIT_test_setupgold := hal/src/setup/trap.c hal/src/engine.c hal/src/texhw.c hal/src/present.c hal/src/chip.c \
                       tests/unit/refrast.c
UNIT_test_edges := $(UNIT_test_setupgold)
# Also built 32-bit with x87 maths (HOST_CFLAGS32), the arithmetic the DOS
# builds use: needs gcc-multilib (the dev container: tools/dev make tests-host).
UNIT_test_fp :=
UNIT_test_vram := hal/src/vram.c
UNIT_test_rgdev := hal/src/rg/device.c
UNIT32_TESTS := test_trap test_setupgold test_fp test_edges

tests-host: $(UNIT_TESTS:%=build/host/tests/%) $(UNIT32_TESTS:%=build/host32/tests/%) build/ow/GLIDE2X.OVL
	@set -e; for t in $(UNIT_TESTS:%=build/host/tests/%) $(UNIT32_TESTS:%=build/host32/tests/%); do \
	  echo "== $$t"; FIXTURES_DIR=$(FIXTURES_DIR) $$t; done; \
	  $(PYTHON) tools/ci/ow_loops.py; echo "tests-host: all passed"

build/gen/glapi_names.c build/gen/glapi.h: build/gen/stamp

.SECONDEXPANSION:
build/host/tests/%: tests/unit/%.c $(UNIT_COMMON) $$(UNIT_$$*) build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  CC      $@"
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -Itests/shim -o $@ $< $(UNIT_COMMON) $(UNIT_$*) -lm

build/host32/tests/%: tests/unit/%.c $(UNIT_COMMON) $$(UNIT_$$*) build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  CC32    $@"
	$(Q)$(HOST_CC) $(HOST_CFLAGS32) -Itests/shim -o $@ $< $(UNIT_COMMON) $(UNIT_$*) -lm
