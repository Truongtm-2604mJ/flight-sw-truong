# Lab 4.1 - host build of the Space Packet test suite.
#   make test
include ../rtems.mk

.PHONY: test clean

test: test_sp
	./test_sp

test_sp: test_sp.c sp.c sp.h test_util.h
	$(CC) $(TEST_CFLAGS) -o $@ test_sp.c sp.c

clean:
	rm -f test_sp
