# Shared RTEMS cross build settings for the Session 4 labs.
#
# NOT VERIFIED: these labs were developed without access to the RTEMS
# toolchain. The flags below are the standard RTEMS 6 form; if Lab 3 was
# built differently on your machine, use the flags that worked there.
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

# Host build settings.
CC          ?= gcc
TEST_CFLAGS  = -std=c99 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Werror \
               -fsanitize=address,undefined -fno-sanitize-recover=all
HOST_CFLAGS  = -std=gnu99 -O1 -g -Wall -Wextra -Werror
