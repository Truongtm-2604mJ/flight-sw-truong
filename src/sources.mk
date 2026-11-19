# LOTUS-1 source list: the single definition of what is flight code, used by
# the top level Makefile and by tests/Makefile. Paths are relative to the
# including Makefile's directory through SRC_ROOT.
SRC_ROOT := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

# Space Packet, PUS, telecommand handler (Session 4)
CORE_SRC  := $(SRC_ROOT)/core/sp.c $(SRC_ROOT)/core/crc16.c $(SRC_ROOT)/core/pus.c \
             $(SRC_ROOT)/core/tc_handler.c
# FDIR: monitoring, liveness, watchdog, NV, schedule (Session 5), generated
# mode machine (Session 6), thermal recovery (capstone), FDIR glue
MON_SRC   := $(SRC_ROOT)/fdir/monitor.c
LIVE_SRC  := $(SRC_ROOT)/fdir/supervisor.c $(SRC_ROOT)/fdir/watchdog.c \
             $(SRC_ROOT)/fdir/nvstate.c
SCHED_SRC := $(SRC_ROOT)/fdir/schedule.c
MODE_SRC  := $(SRC_ROOT)/fdir/mode_sm.c
THERM_SRC := $(SRC_ROOT)/fdir/thermal.c
FDIR_SRC  := $(MON_SRC) $(LIVE_SRC) $(SCHED_SRC) $(MODE_SRC) $(THERM_SRC) \
             $(SRC_ROOT)/fdir/fdir.c
# Telecommand security (Session 8) and its crypto library
TINYCRYPT_SRC := $(addprefix $(SRC_ROOT)/third_party/tinycrypt/source/, \
                   sha256.c hmac.c aes_encrypt.c ccm_mode.c utils.c)
SEC_CORE_SRC  := $(SRC_ROOT)/security/tc_auth.c $(SRC_ROOT)/security/sec_report.c \
                 $(SRC_ROOT)/security/keystore.c $(SRC_ROOT)/security/sec_service.c
SEC_LIB_SRC   := $(SEC_CORE_SRC) $(TINYCRYPT_SRC)
SEC_SRC       := $(SEC_LIB_SRC) $(SRC_ROOT)/security/security.c
# Application: tasks, TM/TC chain, services, simulated sensors
APPL_SRC  := $(SRC_ROOT)/app/services.c $(SRC_ROOT)/app/tmtc.c $(SRC_ROOT)/app/sensors.c
MAIN_SRC  := $(SRC_ROOT)/app/main.c

# Everything but main.c (the integration test links its own Init)
LIB_SRC   := $(CORE_SRC) $(FDIR_SRC) $(SEC_SRC) $(APPL_SRC)
APP_SRC   := $(LIB_SRC) $(MAIN_SRC)
# Test builds only: fault injection (service 200)
TEST_SRC  := $(SRC_ROOT)/test/finj.c
# Host simulation of the RTEMS API
SIM_SRC   := $(SRC_ROOT)/sim/rtems_shim.c
SIM_INC   := -I$(SRC_ROOT)/sim

# Flight code written in this project (third party code excluded): the set
# held to the maximum warning set and analysed statically.
OWN_FLIGHT_SRC := $(CORE_SRC) $(FDIR_SRC) $(SEC_CORE_SRC) $(SRC_ROOT)/security/security.c \
                  $(APPL_SRC) $(MAIN_SRC) $(TEST_SRC)

SRC_INC := -I$(SRC_ROOT)/core -I$(SRC_ROOT)/fdir -I$(SRC_ROOT)/security -I$(SRC_ROOT)/app \
           -I$(SRC_ROOT)/test -I$(SRC_ROOT)/third_party/tinycrypt/include
