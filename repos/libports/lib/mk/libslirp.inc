REQUIRES += linux
LIBSLIRP_DIR     := $(call select_from_ports,libslirp)
LIBSLIRP_SRC_DIR := $(call select_from_ports,libslirp)/src/lib/libslirp/src

INC_DIR += $(LIBSLIRP_SRC_DIR)
INC_DIR += $(REP_DIR)/src/lib/libslirp

# for libslirp-version.h in depot
INC_DIR += $(LIBSLIRP_DIR)/include/slirp

SRC_CC = arp_table.c \
         bootp.c \
         cksum.c \
         dhcpv6.c \
         dnssearch.c \
         if.c \
         ip6_icmp.c \
         ip6_input.c \
         ip6_output.c \
         ip_icmp.c \
         ip_input.c \
         ip_output.c \
         mbuf.c \
         misc.c \
         ncsi.c \
         ndp_table.c \
         sbuf.c \
         slirp.c \
         socket.c \
         state.c \
         stream.c \
         tcp_input.c \
         tcp_output.c \
         tcp_subr.c \
         tcp_timer.c \
         tftp.c \
         udp.c \
         udp6.c \
         util.c \
         version.c \
         vmstate.c

vpath %.c $(LIBSLIRP_SRC_DIR)

CC_OPT += -DG_LOG_DOMAIN=\"Slirp\" -DBUILDING_LIBSLIRP

LIBS    += lx_hybrid
LX_LIBS += glib-2.0
