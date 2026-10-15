# Lab 5.1 - Parameter monitoring.   make test
include ../rtems.mk

.PHONY: test clean

test: test_monitor
	./test_monitor

test_monitor: tests/test_monitor.c monitor.c monitor.h
	$(CC) $(TEST_CFLAGS) -I. -I$(L41) -o $@ tests/test_monitor.c monitor.c

clean:
	rm -f test_monitor
