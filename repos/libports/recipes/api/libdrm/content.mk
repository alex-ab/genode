MIRROR_FROM_REP_DIR := lib/import/import-libdrm.mk \
                       lib/symbols/libdrm

content: $(MIRROR_FROM_REP_DIR)

$(MIRROR_FROM_REP_DIR):
	$(mirror_from_rep_dir)

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/libdrm)

content: include

include:
	mkdir -p $@
	cp -r $(PORT_DIR)/src/lib/libdrm/*.h $@
	mkdir -p $@/intel
	cp -r $(PORT_DIR)/src/lib/libdrm/intel/*.h $@/intel
	mkdir -p $@/etnaviv
	cp -r $(PORT_DIR)/src/lib/libdrm/etnaviv/*.h $@/etnaviv
	mkdir -p $@/libdrm
	cp $(REP_DIR)/include/libdrm/ioctl_dispatch.h $@/libdrm
	cp -r $(PORT_DIR)/src/lib/libdrm/include/drm/*.h $@/libdrm

content: LICENSE libdrm.pc

LICENSE:
	echo "MIT, see header files" > $@

VERSION := $(shell sed -n 's/VERSION.*:=[ ]*\(.*\)/\1/p' $(REP_DIR)/ports/libdrm.port)

libdrm.pc:
	echo "depot_api_dir=DEPOT_API_DIR" > $@
	echo 'includedir=$${depot_api_dir}/include' >> $@
	echo "" >> $@
	echo "Name: libdrm" >> $@
	echo "Description: Userspace interface to kernel DRM services" >> $@
	echo "Version: $(VERSION)" >> $@
	echo "Libs: -l:libdrm.lib.so" >> $@
	echo 'Cflags: -I$${includedir} -I$${includedir}/libdrm' >> $@

