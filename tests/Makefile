# Host-side (x86) tests for the target-independent firmware modules.
#
# Only modules free of CMSIS/target dependencies can be built here:
#   foc, pid, traptraj, util, controller, encoder_pll, crc, led_pattern
# Everything else is covered by cross-compiling firmware/.
#
# Usage:  mingw32-make        -> build every test
#         mingw32-make run    -> build and run every test (stops on first failure)

CC      ?= gcc
CFLAGS  := -I../firmware/Inc -Wall -Wextra -O2
LDLIBS  := -lm

FW      := ../firmware/Src

TESTS   := test_foc.exe test_pid.exe test_traptraj.exe \
           test_controller.exe test_encoder_pll.exe test_crc.exe \
           test_led_pattern.exe

.PHONY: all run clean

all: $(TESTS)

run: $(TESTS)
	./test_foc.exe
	./test_pid.exe
	./test_traptraj.exe
	./test_controller.exe
	./test_encoder_pll.exe
	./test_crc.exe
	./test_led_pattern.exe

test_foc.exe: test_foc.c $(FW)/foc.c $(FW)/util.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test_pid.exe: test_pid.c $(FW)/pid.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test_traptraj.exe: test_traptraj.c $(FW)/traptraj.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test_controller.exe: test_controller.c $(FW)/controller.c $(FW)/pid.c $(FW)/traptraj.c $(FW)/util.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test_encoder_pll.exe: test_encoder_pll.c $(FW)/encoder_pll.c $(FW)/foc.c $(FW)/util.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test_crc.exe: test_crc.c $(FW)/crc.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test_led_pattern.exe: test_led_pattern.c $(FW)/led_pattern.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -f $(TESTS)
