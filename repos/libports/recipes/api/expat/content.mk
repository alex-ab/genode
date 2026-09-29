MIRROR_FROM_REP_DIR := lib/import/import-expat.mk \
                       lib/symbols/expat

content: $(MIRROR_FROM_REP_DIR)

$(MIRROR_FROM_REP_DIR):
	$(mirror_from_rep_dir)

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/expat)

content: include

include:
	mkdir -p $@
	cp -r $(PORT_DIR)/include/* $@

content: LICENSE expat.pc

LICENSE:
	cp $(PORT_DIR)/src/lib/expat/contrib/COPYING $@

VERSION := $(shell sed -n 's/VERSION.*:=[ ]*\(.*\)/\1/p' $(REP_DIR)/ports/expat.port)

expat.pc:
	echo "depot_api_dir=DEPOT_API_DIR" > $@
	echo 'includedir=$${depot_api_dir}/include' >> $@
	echo "Name: expat" >> $@
	echo "Description: expat XML parser" >> $@
	echo "Version: $(VERSION)" >> $@
	echo "Libs: -l:expat.lib.so" >> $@
	echo 'Cflags: -I$${includedir}' >> $@
