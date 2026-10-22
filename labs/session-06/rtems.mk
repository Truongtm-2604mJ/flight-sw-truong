# Shared build settings for the Session 6 labs (same tools as Sessions 4/5).
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
# Flight style warning set the generated module must pass with no warning.
GEN_CFLAGS   = -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
               -Wswitch-enum -Wshadow -Wcast-qual -Wstrict-prototypes \
               -Wmissing-prototypes -Werror
HOST_CFLAGS  = -std=gnu99 -O1 -g -Wall -Wextra -Werror

S5   = ../../session-05
MODEL = ../lab-6.1-model/mode_machine.yaml
GEN  = ../lab-6.2-generation/generated
