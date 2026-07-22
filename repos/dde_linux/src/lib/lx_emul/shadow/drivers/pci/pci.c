/*
 * \brief  Replaces drivers/pci/pci.c
 * \author Stefan Kalkowski
 * \author Christian Helmuth
 * \date   2021-03-16
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pci.h>

#include <lx_emul/pci.h>

int pci_enable_device(struct pci_dev * dev)
{
	lx_emul_pci_enable(dev_name(&dev->dev));
	return 0;
}


int pcim_enable_device(struct pci_dev *pdev)
{
	/* for now ignore devres */
	return pci_enable_device(pdev);
}


void pci_set_master(struct pci_dev * dev) { }


int pci_set_mwi(struct pci_dev * dev)
{
	return 1;
}


int pci_try_set_mwi(struct pci_dev *dev)
{
	return pci_set_mwi(dev);
}


bool pci_dev_run_wake(struct pci_dev * dev)
{
	return false;
}


u8 pci_find_capability(struct pci_dev * dev,int cap)
{
	return 0;
}


void pci_release_regions(struct pci_dev *pdev) { }


int pci_request_regions(struct pci_dev *pdev, const char *res_name)
{
	return 0;
}


extern struct irq_chip dde_irqchip_data_chip;


int pci_enable_msi(struct pci_dev *dev)
{
	unsigned const num_vec = lx_emul_pci_msi_num_vec(pci_name(dev), false);
	if (num_vec != 1)
		return -ENOSYS;

	unsigned const msi = lx_emul_pci_msi_alloc(pci_name(dev), false);
	if (!msi)
		return -ENOSYS;

	int const irq = devm_irq_alloc_descs(&dev->dev, msi, 0, 1, 0);
	if (irq != msi) {
		devm_free_irq(&dev->dev, msi, NULL);
		lx_emul_pci_msi_free(pci_name(dev), msi);
		return -ENOSYS;
	}

	struct irq_data *irq_data = irq_get_irq_data(msi);
	irq_data->hwirq = msi;
	irq_set_chip_and_handler(msi, &dde_irqchip_data_chip,
	                         handle_edge_irq);

	/*
	 * Override the GSI with the MSI and for the time being we do
	 * not set it back as switching to GSI again is not anticipated.
	 */
	dev->irq = msi;

	dev->msi_enabled = 1;

	return 0;
}


void pci_disable_msi(struct pci_dev *dev)
{
	int const msi = dev->irq;
	irq_free_desc(msi);
	lx_emul_pci_msi_free(pci_name(dev), msi);
	dev->msi_enabled = 0;
}


int pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries,
                          int minvec, int maxvec)
{
	/*
	 * Happens with devices <= IWL_DEVICE_FAMILY_9000 that
	 * apparently only have 1 RX queue (see iwlwifi/pcie/gen1_2/trans.c).
	 */
	if (maxvec < minvec)
		return -EINVAL;

	unsigned const num_vec = lx_emul_pci_msi_num_vec(pci_name(dev), true);
	if (num_vec < minvec)
		return -ENOSPC;

	if (maxvec < 1)
		return -ENOSYS;

	unsigned const vec = min(num_vec, (unsigned)maxvec);

	for (unsigned i = 0; i < vec; i++) {

		/*
		 * Returning here will leak resources but that in this case the
		 * driver might be already non-functional and a restart is
		 * required.
		 */
		unsigned const msix = lx_emul_pci_msi_alloc(pci_name(dev), true);
		if (!msix)
			return -ENOSYS;

		int const irq = devm_irq_alloc_descs(&dev->dev, msix, 0, 1, 0);
		if (irq != msix)
			return -ENOSYS;

		entries[i].vector = msix;

		struct irq_data *irq_data = irq_get_irq_data(entries[i].vector);
		irq_data->hwirq = entries[i].vector;
		irq_set_chip_and_handler(entries[i].vector, &dde_irqchip_data_chip,
		                         handle_edge_irq);
	}

	dev->msix_enabled = 1;

	return vec;
}


struct pci_dev_msix_table_entry
{
	struct list_head node;

	struct pci_dev    *dev;
	struct msix_entry *table;
	unsigned           nvecs;
};


static LIST_HEAD(pci_dev_msix_table_entry_list);


static struct pci_dev_msix_table_entry *
lookup_entry(struct pci_dev *dev, struct list_head *head)
{
	struct pci_dev_msix_table_entry *entry = NULL;
	struct pci_dev_msix_table_entry *pos;

	list_for_each_entry(pos, &pci_dev_msix_table_entry_list, node) {
		if (pos->dev == dev)
			entry = pos;
	}
	return entry;
}


int pci_alloc_irq_vectors(struct pci_dev * dev, unsigned int min_vecs,
                          unsigned int max_vecs,unsigned int flags)
{
	int nvecs = -ENOSPC;

	if (flags & PCI_IRQ_MSIX) {

		/* create table for pci_dev */
		struct pci_dev_msix_table_entry *entry =
			lookup_entry(dev, &pci_dev_msix_table_entry_list);
		if (!entry) {
			entry = kzalloc(sizeof(*entry), GFP_KERNEL);
			if (!entry)
				return -ENOMEM;

			entry->dev = dev;
			list_add(&entry->node, &pci_dev_msix_table_entry_list);
		}

		/* was already used before and that's currently not supported */
		if (entry->table)
			return -ENOSPC;

		entry->table = kmalloc_array(max_vecs, sizeof(struct msix_entry),
		                             GFP_KERNEL);
		if (!entry->table)
			return -ENOMEM;

		entry->nvecs = max_vecs;

		nvecs = pci_enable_msix_range(dev, entry->table, min_vecs, max_vecs);
	}
	if (nvecs > 0)
		return nvecs;

	if (flags & PCI_IRQ_MSI)
		nvecs = pci_enable_msi(dev);
	/* 0 is good and 1 vec supported */
	if (nvecs == 0)
		return 1;

	if ((flags & PCI_IRQ_INTX) && min_vecs == 1 && dev->irq)
		return 1;

	return -ENOSPC;
}


int pci_alloc_irq_vectors_affinity(struct pci_dev *dev, unsigned int min_vecs,
                                   unsigned int max_vecs, unsigned int flags,
                                   struct irq_affinity *aff_desc)
{
	return pci_alloc_irq_vectors(dev, min_vecs, max_vecs, flags);
}


int pci_irq_vector(struct pci_dev *dev, unsigned int nr)
{
	if (dev->msix_enabled) {
		struct pci_dev_msix_table_entry *entry =
			lookup_entry(dev, &pci_dev_msix_table_entry_list);
		return entry ? entry->table[nr].vector : -EINVAL;
	}

	if (WARN_ON_ONCE(nr > 0))
		return -EINVAL;
	return dev->irq;
}


void pci_free_irq_vectors(struct pci_dev *dev) { }
