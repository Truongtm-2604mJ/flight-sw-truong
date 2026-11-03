# Shared build settings for the Session 8 labs (same tools as Sessions 4 to 7).
#
#   make rtems RTEMS_PREFIX=/path/to/rtems/6
RTEMS_PREFIX ?= $(HOME)/quick-start/rtems/6
RTEMS_ARCH   ?= sparc-rtems6
RTEMS_BSP    ?= leon3

RTEMS_CC     = $(RTEMS_PREFIX)/bin/$(RTEMS_ARCH)-gcc
RTEMS_CFLAGS = -B$(RTEMS_PREFIX)/$(RTEMS_ARCH)/$(RTEMS_BSP)/lib -qrtems \
               -mcpu=leon3 -O2 -g -Wall -Wextra \
               -ffunction-sections -fdata-sections
RTEMS_LDFLAGS = -Wl,--gc-sections

CC          ?= gcc
PYTHON      ?= python3
HOST_CFLAGS  = -std=gnu99 -O1 -g -Wall -Wextra -Werror
# Flight style warning set for the Session 8 modules (as Session 7, Lab 7.2).
SEC_WARN     = -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
               -Wshadow -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes \
               -Wswitch-enum -Wundef -Wstack-usage=2048 -Werror

ROOT = $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
S4   = $(ROOT)/labs/session-04
S5   = $(ROOT)/labs/session-05
S6   = $(ROOT)/labs/session-06
S8   = $(ROOT)/labs/session-08
L41  = $(S4)/lab-4.1-space-packet
L42  = $(S4)/lab-4.2-pus-service17
L81  = $(S8)/lab-8.1-authentication
L82  = $(S8)/lab-8.2-key-management
TC   = $(S8)/third_party/tinycrypt
GEN  = $(S6)/lab-6.2-generation/generated
UNITY = $(ROOT)/tests/support/unity

TINYCRYPT_SRC = $(TC)/source/sha256.c $(TC)/source/hmac.c $(TC)/source/aes_encrypt.c \
                $(TC)/source/ccm_mode.c $(TC)/source/utils.c
SEC_SRC  = $(L81)/tc_auth.c $(L81)/sec_report.c $(L82)/keystore.c $(L82)/sec_service.c
SEC_INC  = -I$(L81) -I$(L82) -I$(TC)/include -I$(L41) -I$(L42)
