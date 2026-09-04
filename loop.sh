#/bin/bash

while [ $? -eq 0 ]; do
	make -C build/x86_64 KERNEL=sel4 run/nic_dump
done
