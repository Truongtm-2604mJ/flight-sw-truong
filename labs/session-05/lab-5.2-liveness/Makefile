# Lab 5.2 - Liveness supervision, watchdog, NV record.   make test
include ../rtems.mk

SRC = supervisor.c watchdog.c nvstate.c $(L42)/crc16.c

.PHONY: test clean

test: test_liveness
	./test_liveness

test_liveness: tests/test_liveness.c $(SRC) $(wildcard *.h)
	$(CC) $(TEST_CFLAGS) -I. -I$(L41) -I$(L42) -o $@ tests/test_liveness.c $(SRC)

clean:
	rm -f test_liveness
