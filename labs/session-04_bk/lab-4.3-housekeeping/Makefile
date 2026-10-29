# Lab 4.3 - Periodic housekeeping and event reporting.
#
#   make test          host unit tests (limit monitor, HK report, service 3)
#   make host          lab4-3.host: the application on the POSIX RTEMS shim
#   make demo          run the ground station against lab4-3.host (30 s)
#   make rtems         lab4-3.exe for RTEMS 6 / LEON3, runs until stopped
#   make rtems-timing  lab4-3-timing.exe: runs 30 s, prints the rate
#                      monotonic statistics, shuts down (timing analysis)
include ../rtems.mk

L1   = ../lab-4.1-space-packet
L2   = ../lab-4.2-pus-service17
SIM  = ../host-sim
INC  = -I. -I$(L1) -I$(L2)
CORE = hk.c limit_mon.c sensors_sim.c $(L2)/crc16.c $(L2)/pus.c \
       $(L2)/tc_handler.c $(L1)/sp.c
APP  = lab4-3.c $(L2)/tmtc_tasks.c $(CORE)
HDRS = $(wildcard *.h) $(wildcard $(L2)/*.h) $(L1)/sp.h

.PHONY: test host demo rtems rtems-timing clean

test: test_hk
	./test_hk

test_hk: tests/test_hk.c $(CORE) $(HDRS)
	$(CC) $(TEST_CFLAGS) $(INC) -o $@ tests/test_hk.c $(CORE)

host: lab4-3.host

lab4-3.host: $(APP) $(HDRS) $(SIM)/rtems_shim.c $(SIM)/rtems.h
	$(CC) $(HOST_CFLAGS) -I$(SIM) $(INC) -o $@ $(APP) $(SIM)/rtems_shim.c -lpthread

demo: lab4-3.host
	python3 ground/gs_lab43.py --spawn ./lab4-3.host --log logs/host-sim-exchange.log

rtems: lab4-3.exe

lab4-3.exe: $(APP) $(HDRS)
	$(RTEMS_CC) $(RTEMS_CFLAGS) $(INC) -o $@ $(APP) $(RTEMS_LDFLAGS)

rtems-timing: lab4-3-timing.exe

lab4-3-timing.exe: $(APP) $(HDRS)
	$(RTEMS_CC) $(RTEMS_CFLAGS) -DRUN_SECONDS=30 $(INC) -o $@ $(APP) $(RTEMS_LDFLAGS)

clean:
	rm -f test_hk lab4-3.host lab4-3.exe lab4-3-timing.exe
