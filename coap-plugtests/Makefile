APPLICATION = ng_coap_plugtests

BOARD ?= native

RIOTBASE ?= $(CURDIR)/../../RIOT

BOARD_WHITELIST := native

# Uncomment this to enable scheduler statistics for ps:
CFLAGS += -DSCHEDSTATISTIC

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
#CFLAGS += -DDEVELHELP

# The global ip adress to set on startup
CFLAGS += -DHOST_IP=\"fddf:dead:beef::1\"

# This will be filled into the neighbour cache on startup
CFLAGS += -DREMOTE_IP=\"fddf:dead:beef::2\" -DREMOTE_MAC=\"02:3d:6e:53:82:31\"

# Supersized stack
CFLAGS += -DCOAP_STACK_SIZE=65000

# Uncomment for dynamic pktbuf
# CFLAGS += -DNG_PKTBUF_SIZE=0

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

# Modules to include:
USEMODULE += ng_nativenet
USEMODULE += ng_netdev_eth
USEMODULE += ng_nomac
# USEMODULE += ng_icmpv6
# USEMODULE += ng_icmpv6_echo
# USEMODULE += ng_ndp
USEMODULE += ng_ipv6
USEMODULE += ng_udp

USEMODULE += shell
USEMODULE += shell_commands
USEMODULE += ps
USEMODULE += uart0

USEMODULE += vtimer

# Packages to include:
USEPKG    += libcoap

include $(RIOTBASE)/Makefile.include
