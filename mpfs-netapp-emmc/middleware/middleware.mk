# ==============================================================================
# Middleware Sources & Includes (lwIP + Bare-Metal Net Stack)
# ==============================================================================

LWIPDIR := middleware/lwip/src

MIDDLEWARE_INC_DIRS := middleware/net_stack \
                       $(LWIPDIR)/include \
                       $(LWIPDIR)/include/ipv4

# Use upstream lwIP Filelists.mk for core/IPv4 sources when present
ifneq ($(wildcard $(LWIPDIR)/Filelists.mk),)
    include $(LWIPDIR)/Filelists.mk
    LWIP_CORE_SRCS := $(COREFILES) $(CORE4FILES)
else
    LWIP_CORE_SRCS := $(wildcard $(LWIPDIR)/core/*.c) \
                      $(wildcard $(LWIPDIR)/core/ipv4/*.c)
endif

LWIP_SRCS_C := $(LWIP_CORE_SRCS) \
               $(LWIPDIR)/netif/ethernet.c \
               $(LWIPDIR)/apps/http/httpd.c \
               $(LWIPDIR)/apps/http/fs.c \
               $(wildcard $(LWIPDIR)/apps/tftp/*.c)

# Exclude fsdata_custom.c (included directly by lwIP's fs.c)
NET_STACK_SRCS_C := middleware/net_stack/net_port.c \
                    middleware/net_stack/web_server.c \
                    middleware/net_stack/tftp_server_app.c

MIDDLEWARE_SRCS_C := $(LWIP_SRCS_C) $(NET_STACK_SRCS_C)