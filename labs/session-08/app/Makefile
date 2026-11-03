# Session 8 application: the Session 6 flight software with TC authentication
# and key management, attached by linker wraps (sec_app.h). No Session 5 or 6
# source file is edited.
#
#   make host           lab8-test.host    (test build: fault injection service 200)
#   make host-flight    lab8-flight.host
#   make rtems          lab8-test.exe / lab8-flight.exe / lab8-timing.exe / sec-timing.exe
#                       (RTEMS 6, LEON3); run on SIS with ./run-sis-timing.sh
#   make timing-host    sec-timing.host   (crypto cost on the host, for comparison)
include ../rtems.mk

A5   = $(S5)/app
L51  = $(S5)/lab-5.1-monitoring
L52  = $(S5)/lab-5.2-liveness
L53  = $(S5)/lab-5.3-safe-mode
L54  = $(S5)/lab-5.4-fault-injection
SIM  = $(S5)/host-sim

INC  = -I. -I$(GEN) -I$(A5) -I$(S6)/app -I$(L41) -I$(L42) -I$(L51) -I$(L52) \
       -I$(L53) -I$(L54) $(SEC_INC)
# The Session 6 application, file for file (session-06/app/Makefile) ...
S6_SRC = $(A5)/lab5.c $(A5)/services.c $(A5)/tmtc5.c $(A5)/sensors5.c \
         $(S6)/app/fdir6.c $(GEN)/mode_sm.c \
         $(L51)/monitor.c $(L52)/supervisor.c $(L52)/watchdog.c $(L52)/nvstate.c \
         $(L53)/schedule.c $(L41)/sp.c $(L42)/crc16.c $(L42)/pus.c $(L42)/tc_handler.c
# ... plus the security layer.
SRC  = $(S6_SRC) $(SEC_SRC) $(TINYCRYPT_SRC) sec_app.c
TEST_SRC = $(SRC) $(L54)/finj.c
SEC_WRAP = -Wl,--wrap=services_register -Wl,--wrap=fdir_hk_cycle \
           -Wl,--wrap=tmtc_dispatch_locked

.PHONY: host host-flight rtems timing-host check-wraps clean

host: lab8-test.host
host-flight: lab8-flight.host

lab8-test.host: $(TEST_SRC) $(SIM)/rtems_shim.c
	@echo "CC $@ (host simulation, test build)"
	@$(CC) $(HOST_CFLAGS) -DFDIR_TEST_BUILD -I$(SIM) $(INC) -o $@ $(TEST_SRC) \
	    $(SIM)/rtems_shim.c $(SEC_WRAP) -lpthread

lab8-flight.host: $(SRC) $(SIM)/rtems_shim.c
	@echo "CC $@ (host simulation, flight build)"
	@$(CC) $(HOST_CFLAGS) -I$(SIM) $(INC) -o $@ $(SRC) $(SIM)/rtems_shim.c \
	    $(SEC_WRAP) -lpthread

timing-host: sec-timing.host
sec-timing.host: sec_timing.c $(SEC_SRC) $(TINYCRYPT_SRC) $(SIM)/rtems_shim.c
	$(CC) $(HOST_CFLAGS) -O2 -I$(SIM) $(INC) -o $@ sec_timing.c $(SEC_SRC) \
	    $(TINYCRYPT_SRC) $(L42)/crc16.c $(L42)/pus.c $(L42)/tc_handler.c $(L41)/sp.c \
	    $(SIM)/rtems_shim.c -lpthread

# The wraps really are in place (a missing one would silently disable a hook).
check-wraps: lab8-test.host lab8-flight.host
	@for f in lab8-test.host lab8-flight.host; do \
	    for s in services_register fdir_hk_cycle tmtc_dispatch_locked; do \
	        nm $$f | grep -q " T __wrap_$$s$$" || { echo "FAIL: $$f has no __wrap_$$s"; exit 1; }; \
	    done; echo "OK: $$f: 3 security hooks linked"; done

rtems: lab8-test.exe lab8-flight.exe lab8-timing.exe sec-timing.exe

lab8-test.exe: $(TEST_SRC) sec_app.c
	$(RTEMS_CC) $(RTEMS_CFLAGS) -DFDIR_TEST_BUILD $(INC) -o $@ $(TEST_SRC) \
	    $(SEC_WRAP) $(RTEMS_LDFLAGS)

lab8-flight.exe: $(SRC)
	$(RTEMS_CC) $(RTEMS_CFLAGS) $(INC) -o $@ $(SRC) $(SEC_WRAP) $(RTEMS_LDFLAGS)

# Flight build, 60 s, then the rate monotonic statistics (as Session 5).
lab8-timing.exe: $(SRC)
	$(RTEMS_CC) $(RTEMS_CFLAGS) -DRUN_SECONDS=60 $(INC) -o $@ $(SRC) $(SEC_WRAP) \
	    $(RTEMS_LDFLAGS)

sec-timing.exe: sec_timing.c $(SEC_SRC) $(TINYCRYPT_SRC)
	$(RTEMS_CC) $(RTEMS_CFLAGS) $(INC) -o $@ sec_timing.c $(SEC_SRC) $(TINYCRYPT_SRC) \
	    $(L42)/crc16.c $(L42)/pus.c $(L42)/tc_handler.c $(L41)/sp.c $(RTEMS_LDFLAGS)

clean:
	rm -f lab8-test.host lab8-flight.host sec-timing.host lab8-test.exe \
	      lab8-flight.exe lab8-timing.exe sec-timing.exe fdir_nv.bin sec_nv.bin
