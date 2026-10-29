# Lab 4.2 - PUS service 17 round trip with verification.
#
#   make test        host unit tests (CRC, PUS layer, TC handler)
#   make host        lab4-2.host: the application on the POSIX RTEMS shim
#   make demo        run the ground station against lab4-2.host
#   make rtems       lab4-2.exe for RTEMS 6 / LEON3 (see ../rtems.mk)
include ../rtems.mk

L1   = ../lab-4.1-space-packet
SIM  = ../host-sim
CORE = crc16.c pus.c tc_handler.c $(L1)/sp.c
APP  = lab4-2.c tmtc_tasks.c $(CORE)
HDRS = $(wildcard *.h) $(L1)/sp.h

.PHONY: test host demo rtems clean

test: test_pus
	./test_pus

test_pus: tests/test_pus.c $(CORE) $(HDRS)
	$(CC) $(TEST_CFLAGS) -I$(L1) -o $@ tests/test_pus.c $(CORE)

host: lab4-2.host

lab4-2.host: $(APP) $(HDRS) $(SIM)/rtems_shim.c $(SIM)/rtems.h
	$(CC) $(HOST_CFLAGS) -I$(SIM) -I$(L1) -o $@ $(APP) $(SIM)/rtems_shim.c -lpthread

demo: lab4-2.host
	python3 ground/gs_lab42.py --spawn ./lab4-2.host --log logs/host-sim-exchange.log

rtems: lab4-2.exe

lab4-2.exe: $(APP) $(HDRS)
	$(RTEMS_CC) $(RTEMS_CFLAGS) -I$(L1) -o $@ $(APP) $(RTEMS_LDFLAGS)

clean:
	rm -f test_pus lab4-2.host lab4-2.exe
