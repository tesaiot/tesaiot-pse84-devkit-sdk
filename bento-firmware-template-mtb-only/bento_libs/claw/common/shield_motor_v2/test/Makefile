################################################################################
# Host unit tests for the Motor Shield v2 driver.
#
# These build and run on a development machine, not on the target. The driver
# layers under test take an injected bus vtable and call no PSoC, ModusToolbox
# or FreeRTOS API, so the exact I2C byte stream can be asserted long before any
# hardware exists.
#
#   make            build and run everything
#   make build      build only
#   make clean
################################################################################

CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O1 -g
# NOT "build": that name is also a .PHONY target below, and a directory sharing
# a phony target's name makes make treat it as circular and skip the mkdir.
BUILD   := bin

# One binary per test translation unit; each is self-contained with its own
# main(), so a new test file needs no edit here.
SRCS    := $(wildcard test_*.c)
BINS    := $(patsubst test_%.c,$(BUILD)/test_%,$(SRCS))

# Units under test live one directory up. Linking all of them into every test
# binary is harmless -- each test file carries its own main() -- and it means a
# new driver source needs no edit here.
UUT     := $(wildcard ../*.c)

.PHONY: all build clean run
all: run

build: $(BINS)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/test_%: test_%.c $(UUT) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(UUT)

run: $(BINS)
	@fail=0; \
	for b in $(BINS); do \
	  echo "== $$b"; \
	  ./$$b || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; fi; \
	echo "ALL TESTS PASSED"

clean:
	rm -rf $(BUILD)
