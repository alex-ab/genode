content: include lib/symbols/libiconv LICENSE libiconv.pc

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/libiconv)

include:
	mkdir $@
	cp -r $(PORT_DIR)/include/iconv/* $@/

lib/symbols/libiconv:
	$(mirror_from_rep_dir)

LICENSE:
	cp $(PORT_DIR)/src/lib/libiconv/COPYING.LIB $@

VERSION := $(shell sed -n 's/VERSION.*:=[ ]*\(.*\)/\1/p' $(REP_DIR)/ports/libiconv.port)

libiconv.pc:
	echo "depot_api_dir=DEPOT_API_DIR" > $@
	echo 'includedir=$${depot_api_dir}/include' >> $@
	echo "" >> $@
	echo "Name: libiconv" >> $@
	echo "Description: Character set conversion library." >> $@
	echo "Version: $(VERSION)" >> $@
	echo "Libs: -l:libiconv.lib.so" >> $@
	echo 'Cflags: -I$${includedir}' >> $@

