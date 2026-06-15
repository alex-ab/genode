include $(REP_DIR)/lib/mk/libslirp.inc

# acquire include paths for glib-2.0 via pkg-config
INC_DIR += $(patsubst -I%,%,$(filter -I%,$(shell pkg-config --cflags glib-2.0)))
