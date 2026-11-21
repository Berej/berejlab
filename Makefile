# berejlab — build system (SPEC §11.2). GNU make, no generators.

.DEFAULT_GOAL := all

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------

# CROSS=aarch64-linux-gnu- selects $(CROSS)gcc (cross toolchains ship gcc, not cc);
# an explicit CC always wins.
CROSS ?=
ifeq ($(origin CC),default)
ifneq ($(CROSS),)
CC := $(CROSS)gcc
endif
endif

BUILD ?= release
ifeq ($(filter $(BUILD),release debug),)
$(error BUILD must be 'release' or 'debug', got '$(BUILD)')
endif

# Ask the compiler itself, so cross and native builds both see the real target.
TARGET_MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
ifeq ($(TARGET_MACHINE),)
$(error cannot determine target machine: '$(CC) -dumpmachine' failed)
endif
ARCH_FLAGS :=
ifneq ($(filter aarch64%,$(TARGET_MACHINE)),)
ARCH_FLAGS += -mcpu=cortex-a53
endif

WARN_FLAGS := -Wall -Wextra -Werror
BASE_FLAGS := -std=c17 $(WARN_FLAGS) $(ARCH_FLAGS) -Isrc
DEP_FLAGS  := -MMD -MP

ifeq ($(BUILD),release)
MODE_FLAGS := -O3
MODE_LDFLAGS :=
else
MODE_FLAGS := -O0 -g -fno-omit-frame-pointer -fsanitize=address,undefined
MODE_LDFLAGS := -fsanitize=address,undefined
endif

STATIC_LDFLAGS :=
ifeq ($(STATIC),1)
STATIC_LDFLAGS := -static
endif

ALL_CFLAGS  := $(BASE_FLAGS) $(MODE_FLAGS) $(CFLAGS)
ALL_LDFLAGS := $(MODE_LDFLAGS) $(STATIC_LDFLAGS) $(LDFLAGS)
LDLIBS ?=

# ---------------------------------------------------------------------------
# Sources and outputs
# ---------------------------------------------------------------------------

OUT     := build/$(BUILD)
OBJDIR  := $(OUT)/obj

COMMON_SRCS := $(wildcard src/common/*.c)
DAEMON_SRCS := $(wildcard src/daemon/*.c)
CLIENT_SRCS := $(wildcard src/client/*.c)
# Every tests/test_*.c is picked up automatically; runner.c holds main().
TEST_SRCS   := tests/runner.c $(wildcard tests/test_*.c)

obj = $(patsubst %.c,$(OBJDIR)/%.o,$(1))

COMMON_OBJS := $(call obj,$(COMMON_SRCS))
DAEMON_OBJS := $(call obj,$(DAEMON_SRCS))
CLIENT_OBJS := $(call obj,$(CLIENT_SRCS))
TEST_OBJS   := $(call obj,$(TEST_SRCS))

DAEMON_BIN := $(OUT)/berejlabd
CLIENT_BIN := $(OUT)/berejlab
TEST_BIN   := $(OUT)/test_runner

ALL_OBJS := $(COMMON_OBJS) $(DAEMON_OBJS) $(CLIENT_OBJS) $(TEST_OBJS)

# Rebuild everything when the compiler or flags change (e.g. `make` then `make CC=clang`),
# otherwise stale objects from another compiler would be silently reused.
FLAGS_STAMP := $(OUT)/.buildflags
FLAGS_LINE  := $(CC) | $(ALL_CFLAGS) | $(ALL_LDFLAGS) | $(LDLIBS)

# Relink a binary when its object list changes, e.g. after a test_*.c file is deleted.
DAEMON_LIST := $(OUT)/.berejlabd.objs
CLIENT_LIST := $(OUT)/.berejlab.objs
TEST_LIST   := $(OUT)/.test_runner.objs

# $(call write_if_changed,file,content): touch the stamp only when its content differs.
define write_if_changed
	@mkdir -p $(dir $(1))
	@if [ "$$(cat $(1) 2>/dev/null)" != '$(2)' ]; then printf '%s\n' '$(2)' > $(1); fi
endef

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

.PHONY: all debug test bench run-dev install uninstall clean FORCE

all: $(DAEMON_BIN) $(CLIENT_BIN)

debug:
	@$(MAKE) --no-print-directory BUILD=debug all

test: $(TEST_BIN)
	@./$(TEST_BIN)

bench:
	@echo "make bench: not implemented yet (stage 2)"

run-dev:
	@echo "make run-dev: not implemented yet (stage 4)"

install:
	@echo "make install: not implemented yet (stage 4)"

uninstall:
	@echo "make uninstall: not implemented yet (stage 4)"

clean:
	rm -rf build

$(DAEMON_BIN): $(DAEMON_OBJS) $(COMMON_OBJS) $(DAEMON_LIST)
	$(CC) $(ALL_LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(CLIENT_BIN): $(CLIENT_OBJS) $(COMMON_OBJS) $(CLIENT_LIST)
	$(CC) $(ALL_LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(TEST_BIN): $(TEST_OBJS) $(COMMON_OBJS) $(TEST_LIST)
	$(CC) $(ALL_LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(DAEMON_LIST): FORCE
	$(call write_if_changed,$@,$(DAEMON_OBJS) $(COMMON_OBJS))

$(CLIENT_LIST): FORCE
	$(call write_if_changed,$@,$(CLIENT_OBJS) $(COMMON_OBJS))

$(TEST_LIST): FORCE
	$(call write_if_changed,$@,$(TEST_OBJS) $(COMMON_OBJS))

$(OBJDIR)/tests/%.o: tests/%.c $(FLAGS_STAMP)
	@mkdir -p $(@D)
	$(CC) $(ALL_CFLAGS) -Itests $(DEP_FLAGS) -c -o $@ $<

$(OBJDIR)/%.o: %.c $(FLAGS_STAMP)
	@mkdir -p $(@D)
	$(CC) $(ALL_CFLAGS) $(DEP_FLAGS) -c -o $@ $<

$(FLAGS_STAMP): FORCE
	$(call write_if_changed,$@,$(FLAGS_LINE))

FORCE:

-include $(ALL_OBJS:.o=.d)
