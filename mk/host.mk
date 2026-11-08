# Native host build: unit tests and host-side tools.
HOST_CFLAGS := -std=gnu99 -O1 -g -Wall -Wextra -Werror -Iglide/include -Ihal/include -Iglide/src -Ibuild/gen \
               -DMGA_HOST=1 -fno-strict-aliasing
HOST_CFLAGS32 := $(HOST_CFLAGS) -m32 -mfpmath=387

build/host/%.o: %.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  CC      $<"
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -MMD -c -o $@ $<

build/host32/%.o: %.c | build/gen/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  CC32    $<"
	$(Q)$(HOST_CC) $(HOST_CFLAGS32) -MMD -c -o $@ $<
