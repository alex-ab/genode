content: include lib/symbols/jpeg LICENSE libjpeg.pc

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/jpeg)

include:
	mkdir -p $@
	cp -r $(PORT_DIR)/include/jpeg/* $@/
	cp -r $(REP_DIR)/include/jpeg/* $@/

lib/symbols/jpeg:
	$(mirror_from_rep_dir)

LICENSE:
	grep '^LEGAL ISSUES$$' -A57 $(PORT_DIR)/src/lib/jpeg/README > $@

libjpeg.pc:
	echo "depot_api_dir=DEPOT_API_DIR" > $@
	echo 'includedir=$${depot_api_dir}/include' >> $@
	echo "" >> $@
	echo "Name: libjpeg" >> $@
	echo "Description: Reads and writes JPEG files" >> $@
	echo "Version: 9.4.0" >> $@
	echo "Libs: -l:jpeg.lib.so" >> $@
	echo 'Cflags: -I$${includedir}' >> $@

