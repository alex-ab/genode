QEMU_CONTRIB_DIR = $(call select_from_ports,qemu)/src/qemu-3.1.0

LIBS   += libc glib zlib libpixman
CC_OPT += -Wno-unused-function
CC_OPT += -DNEED_CPU_H

INC_DIR += $(QEMU_CONTRIB_DIR)/i386-softmmu
INC_DIR += $(QEMU_CONTRIB_DIR)/target/i386
INC_DIR += $(QEMU_CONTRIB_DIR)/tcg
INC_DIR += $(QEMU_CONTRIB_DIR)/tcg/i386

INC_DIR += $(QEMU_CONTRIB_DIR)
INC_DIR += $(QEMU_CONTRIB_DIR)/include

#INTC_FILTER :=
#SRC_C += $(foreach FILE,$(filter-out $(INTC_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/intc/*.c))),hw/intc/$(FILE))
SRC_C += hw/intc/apic.c
SRC_C += hw/intc/apic_common.c
SRC_C += hw/intc/ioapic.c
SRC_C += hw/intc/ioapic_common.c
SRC_C += hw/intc/trace.c
SRC_C += hw/intc/i8259.c
SRC_C += hw/intc/i8259_common.c
SRC_C += hw/timer/mc146818rtc.c

SRC_C += hw/i386/pc.c hw/i386/multiboot.c
SRC_C += hw/i386/pc_piix.c hw/i386/pc_q35.c
SRC_C += hw/i386/pc_sysfw.c hw/i386/acpi-build.c
SRC_C += hw/i386/kvmvapic.c hw/i386/x86-iommu.c

SRC_C += hw/display/edid-generate.c

SRC_C += hw/smbios/smbios.c
SRC_C += hw/smbios/smbios_type_38.c

SRC_C += hw/char/serial.c
SRC_C += hw/char/serial-isa.c
SRC_C += hw/char/trace.c
SRC_C += hw/char/parallel-isa.c

SRC_C += hw/net/rocker/qmp-norocker.c

SRC_C += hw/watchdog/watchdog.c

SRC_C += hw/misc/macio/mac_dbdma.c

SRC_C += hw/pci-host/piix.c hw/pci-host/pam.c

SRC_C += hw/audio/soundhw.c

CORE_FILTER := loader-fit.c
SRC_C += $(foreach FILE,$(filter-out $(CORE_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/core/*.c))),hw/core/$(FILE))

CORE_FILTER := loader-fit.c
SRC_C += $(foreach FILE,$(filter-out $(CORE_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/core/*.c))),hw/core/$(FILE))

PCI_FILTER := pci-stub.c
SRC_C += $(foreach FILE,$(filter-out $(PCI_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/pci/*.c))),hw/pci/$(FILE))

MEM_FILTER :=
SRC_C += $(foreach FILE,$(filter-out $(MEM_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/mem/*.c))),hw/mem/$(FILE))

NVRAM_FILTER := spapr_nvram.c
SRC_C += $(foreach FILE,$(filter-out $(NVRAM_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/nvram/*.c))),hw/nvram/$(FILE))

USB_FILTER := ccid-card-passthru.c ccid-card-emulated.c host-libusb.c redirect.c xen-usb.c
SRC_C += $(foreach FILE,$(filter-out $(USB_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/usb/*.c))),hw/usb/$(FILE))

NET_FILTER := spapr_llan.c xen_nic.c
SRC_C += $(foreach FILE,$(filter-out $(NET_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/net/*.c))),hw/net/$(FILE))

SCSI_FILTER := spapr_vscsi.c vhost-scsi.c vhost-scsi-common.c vhost-user-scsi.c
SRC_C += $(foreach FILE,$(filter-out $(SCSI_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/scsi/*.c))),hw/scsi/$(FILE))

VIRTIO_FILTER := vhost-backend.c vhost-user.c vhost.c vhost-vsock.c virtio-crypto.c
SRC_C += $(foreach FILE,$(filter-out $(VIRTIO_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/virtio/*.c))),hw/virtio/$(FILE))

BLOCK_FILTER := xen_disk.c vhost-user-blk.c virtio-blk.c m25p80.c
SRC_C += $(foreach FILE,$(filter-out $(BLOCK_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/block/*.c))),hw/block/$(FILE))

INPUT_FILTER := virtio-input-host.c
SRC_C += $(foreach FILE,$(filter-out $(INPUT_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/input/*.c))),hw/input/$(FILE))

BT_FILTER :=
SRC_C += $(foreach FILE,$(filter-out $(BT_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/bt/*.c))),hw/bt/$(FILE))

ISA_FILTER :=
SRC_C += $(foreach FILE,$(filter-out $(ISA_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/isa/*.c))),hw/isa/$(FILE))

ACPI_FILTER := acpi-stub.c ipmi-stub.c
SRC_C += $(foreach FILE,$(filter-out $(ACPI_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/acpi/*.c))),hw/acpi/$(FILE))

DMA_FILTER :=
SRC_C += $(foreach FILE,$(filter-out $(DMA_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/dma/*.c))),hw/dma/$(FILE))

I2C_FILTER := omap_i2c.c
SRC_C += $(foreach FILE,$(filter-out $(I2C_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/i2c/*.c))),hw/i2c/$(FILE))

IDE_FILTER := microdrive.c
SRC_C += $(foreach FILE,$(filter-out $(IDE_FILTER),$(notdir $(wildcard $(QEMU_CONTRIB_DIR)/hw/ide/*.c))),hw/ide/$(FILE))

vpath %.c $(QEMU_CONTRIB_DIR)
