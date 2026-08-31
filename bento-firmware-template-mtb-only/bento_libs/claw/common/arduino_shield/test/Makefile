################################################################################
# Host unit tests for the Arduino header abstraction.
#
# Builds the core together with the REAL QWA309 board descriptor, which is the
# point: the capability table is the piece most likely to be wrong and most
# expensive to debug on a bench, and it is portable C precisely so it can be
# checked here. The board's PDL half (arduino_shield_qwa309_hal.c) is target
# only and is replaced by stubs inside the test.
#
#   make            build and run
#   make clean
################################################################################

CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O1 -g -I..
BUILD   := bin

OVERLAY := ../../../kit-tesaiot-pse84-ai

SRCS    := $(wildcard test_*.c)
BINS    := $(patsubst test_%.c,$(BUILD)/test_%,$(SRCS))
UUT     := ../arduino_shield.c $(OVERLAY)/arduino_shield_qwa309.c

.PHONY: all clean run
all: run

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
