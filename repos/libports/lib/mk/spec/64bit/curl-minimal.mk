CURL_DIR := $(call select_from_ports,curl-minimal)
INC_DIR += $(CURL_DIR)/include

INC_DIR += $(REP_DIR)/src/lib/curl/spec/64bit
INC_DIR += $(REP_DIR)/src/lib/curl/spec/64bit/curl

include $(REP_DIR)/lib/mk/curl-minimal.inc

CC_CXX_WARN_STRICT =
