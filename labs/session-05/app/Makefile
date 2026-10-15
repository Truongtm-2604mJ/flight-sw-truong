# Session 5 application (Labs 5.1 to 5.4 integrated).
#
#   make host           lab5-test.host   test build on the POSIX RTEMS shim
#   make host-flight    lab5-flight.host flight build on the shim
#   make check-flight   prove the injection code is absent from flight builds
#   make rtems          lab5-test.exe and lab5-flight.exe for RTEMS 6 / LEON3
#   make rtems-timing   lab5-timing.exe: flight build, 60 s, rate monotonic stats
include ../rtems.mk

SIM  = ../host-sim
L51  = ../lab-5.1-monitoring
L52  = ../lab-5.2-liveness
L53  = ../lab-5.3-safe-mode
L54  = ../lab-5.4-fault-injection
INC  = -I. -I$(L41) -I$(L42) -I$(L51) -I$(L52) -I$(L53) -I$(L54)

SRC  = lab5.c fdir.c services.c tmtc5.c sensors5.c \
       $(L51)/monitor.c $(L52)/supervisor.c $(L52)/watchdog.c \
       $(L52)/nvstate.c $(L53)/modes.c $(L53)/schedule.c \
       $(L41)/sp.c $(L42)/crc16.c $(L42)/pus.c $(L42)/tc_handler.c
TEST_SRC = $(SRC) $(L54)/finj.c
HDRS = $(wildcard *.h $(L51)/*.h $(L52)/*.h $(L53)/*.h $(L54)/*.h)

.PHONY: host host-flight check-flight rtems rtems-timing clean

host: lab5-test.host
host-flight: lab5-flight.host

lab5-test.host: $(TEST_SRC) $(HDRS) $(SIM)/rtems_shim.c $(SIM)/rtems.h
	$(CC) $(HOST_CFLAGS) -DFDIR_TEST_BUILD -I$(SIM) $(INC) -o $@ $(TEST_SRC) $(SIM)/rtems_shim.c -lpthread

lab5-flight.host: $(SRC) $(HDRS) $(SIM)/rtems_shim.c $(SIM)/rtems.h
	$(CC) $(HOST_CFLAGS) -I$(SIM) $(INC) -o $@ $(SRC) $(SIM)/rtems_shim.c -lpthread

# A flight build must contain no fault injection symbol at all, and must
# reject service 200 like any unknown service.
check-flight: lab5-flight.host
	@if nm lab5-flight.host | grep -i finj; then \
	    echo "FAIL: fault injection symbols in the flight build"; exit 1; \
	else echo "OK: no finj_ symbols in lab5-flight.host"; fi
	@if [ -f lab5-flight.exe ]; then \
	    if $(RTEMS_PREFIX)/bin/$(RTEMS_ARCH)-nm lab5-flight.exe | grep -i finj; then \
	        echo "FAIL: fault injection symbols in lab5-flight.exe"; exit 1; \
	    else echo "OK: no finj_ symbols in lab5-flight.exe"; fi; fi

rtems: lab5-test.exe lab5-flight.exe

lab5-test.exe: $(TEST_SRC) $(HDRS)
	$(RTEMS_CC) $(RTEMS_CFLAGS) -DFDIR_TEST_BUILD $(INC) -o $@ $(TEST_SRC) $(RTEMS_LDFLAGS)

lab5-flight.exe: $(SRC) $(HDRS)
	$(RTEMS_CC) $(RTEMS_CFLAGS) $(INC) -o $@ $(SRC) $(RTEMS_LDFLAGS)

rtems-timing: lab5-timing.exe

lab5-timing.exe: $(SRC) $(HDRS)
	$(RTEMS_CC) $(RTEMS_CFLAGS) -DRUN_SECONDS=60 $(INC) -o $@ $(SRC) $(RTEMS_LDFLAGS)

clean:
	rm -f lab5-test.host lab5-flight.host lab5-test.exe lab5-flight.exe \
	      lab5-timing.exe fdir_nv.bin
