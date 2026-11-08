# DJGPP cross compiler: the HAL as DOS-GL (gl/) and the DJGPP test programs build it.
DJENV := env LD_LIBRARY_PATH=$(DJGPP_PREFIX)/hostlib$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))
DJCC := $(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-gcc
DJAR := $(DJENV) $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-ar
DJ_CFLAGS := -std=gnu99 -O2 -march=i586 -Wall -Wextra -Werror -Ihal/include -Iglide/include -DMGA_DJGPP=1

build/djgpp/%.o: %.c
	@mkdir -p $(dir $@)
	$(Q)echo "  DJCC    $<"
	$(Q)$(DJCC) $(DJ_CFLAGS) -MMD -c -o $@ $<
