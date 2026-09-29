content: include lib/symbols/libpng LICENSE libpng.pc

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/libpng)

include:
	mkdir $@
	cp -r $(PORT_DIR)/include/libpng/* $@/

lib/symbols/libpng:
	$(mirror_from_rep_dir)

LICENSE:
	cp $(PORT_DIR)/src/lib/libpng/LICENSE $@

VERSION := $(shell sed -n 's/VERSION.*:=[ ]*\(.*\)/\1/p' $(REP_DIR)/ports/libpng.port)

libpng.pc:
	echo "depot_api_dir=DEPOT_API_DIR" > $@
	echo 'includedir=$${depot_api_dir}/include' >> $@
	echo "" >> $@
	echo "Name: libpng" >> $@
	echo "Description: Loads and saves PNG files" >> $@
	echo "Version: $(VERSION)" >> $@
	echo "Libs: -l:libpng.lib.so" >> $@
	echo 'Cflags: -I$${includedir}' >> $@
