/*
 * \brief  Replaces driver/acpi/property.c
 * \author Josef Soentgen
 * \date   2022-05-06
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <lx_emul.h>

#include <linux/acpi.h>


static const void * acpi_fwnode_device_get_match_data(const struct fwnode_handle *fwnode, const struct device *dev)
{
	const struct acpi_device *adev = to_acpi_device_node(fwnode);
	if (WARN_ON(!adev))
		return NULL;

	return acpi_device_get_match_data(dev);
}


static const char *acpi_fwnode_get_name(const struct fwnode_handle *fwnode)
{
	const struct acpi_device *adev = to_acpi_device_node(fwnode);
	if (WARN_ON(!adev))
		return NULL;

	return acpi_device_bid(adev);
}


static int acpi_fwnode_property_read_int_array(const struct fwnode_handle *fwnode,
                                               const char *propname,
                                               unsigned int elem_size, void *val,
                                               size_t nval)
{
	enum dev_prop_type type;
	const struct acpi_device *adev = to_acpi_device_node(fwnode);
	printk("%s:%d adev: %px propname: '%s'\n", __func__, __LINE__, adev, propname);
	if (adev && !strcmp(adev->pnp.bus_id, "CSC3556")
	         && !strcmp(propname, "cirrus,dev-index")) {
		printk("%s:%d adev: %px propname: '%s DING DING'\n", __func__, __LINE__, adev, propname);
		if (val && nval) {
			unsigned *p = val;
			p[0] = 0;
			p[1] = 1;

			return 0;
		}

		/* probe size */
		return 2;
	}

	// switch (elem_size) {
	// case sizeof(u8):
	// 	type = DEV_PROP_U8;
	// 	break;
	// case sizeof(u16):
	// 	type = DEV_PROP_U16;
	// 	break;
	// case sizeof(u32):
	// 	type = DEV_PROP_U32;
	// 	break;
	// case sizeof(u64):
	// 	type = DEV_PROP_U64;
	// 	break;
	// default:
	// 	return -ENXIO;
	// }

	return -ENXIO;
}

const struct fwnode_operations acpi_device_fwnode_ops = {
	.device_get_match_data   = acpi_fwnode_device_get_match_data,
	.get_name                = acpi_fwnode_get_name,
	.property_read_int_array =
		acpi_fwnode_property_read_int_array
};


bool is_acpi_device_node(const struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) && fwnode->ops == &acpi_device_fwnode_ops;
}


bool is_acpi_data_node(const struct fwnode_handle *fwnode)
{
	lx_emul_trace(__func__);
	return false;
}


int __acpi_node_get_property_reference(const struct fwnode_handle *fwnode,
                                       const char *propname,
                                       size_t index, size_t num_args,
                                       struct fwnode_reference_args *args)
{
	lx_emul_trace(__func__);
	return -ENOENT;
}
