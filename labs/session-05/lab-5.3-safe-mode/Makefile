# Lab 5.3 - Mode management, FDIR inhibit, schedule.   make test
include ../rtems.mk

SRC = modes.c schedule.c

.PHONY: test clean

test: test_modes
	./test_modes

test_modes: tests/test_modes.c $(SRC) $(wildcard *.h)
	$(CC) $(TEST_CFLAGS) -I. -I$(L41) -o $@ tests/test_modes.c $(SRC)

clean:
	rm -f test_modes
