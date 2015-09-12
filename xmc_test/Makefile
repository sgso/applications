APPLICATION = xmc_test

BOARD = xmc-2go

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../..

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
CFLAGS += -DDEVELHELP

FEATURES_REQUIRED += periph_rtc periph_adc periph_gpio periph_random

# Why? periph_rtc doesn't work
CFLAGS += -DMODULE_RTC

#USEMODULE += schedstatistics
USEMODULE += xtimer
USEMODULE += shell
USEMODULE += shell_commands
USEMODULE += ps
USEMODULE += od

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include
