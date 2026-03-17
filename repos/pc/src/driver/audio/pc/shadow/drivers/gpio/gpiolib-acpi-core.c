/*
 * \brief  Linux emulation environment: GPIO ACPI shortcuts
 * \author Christian Helmuth
 * \date   2022-05-04
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <lx_emul.h>

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <gpiolib.h>
#include <gpiolib-acpi.h>


void acpi_gpiochip_request_interrupts(struct gpio_chip *chip)
{
	/* XXX only used for _AEI */
	lx_emul_trace(__func__);
}


void acpi_gpiochip_free_interrupts(struct gpio_chip *chip)
{
	lx_emul_trace(__func__);
}


void acpi_gpiochip_remove(struct gpio_chip *chip)
{
	lx_emul_trace_and_stop(__func__);
}


void acpi_gpiochip_add(struct gpio_chip *chip)
{
	struct acpi_device *adev;

	if (!chip || !chip->parent)
		return;

	adev = ACPI_COMPANION(chip->parent);
	if (!adev)
		return;
}


void acpi_gpio_dev_init(struct gpio_chip *gc, struct gpio_device *gdev)
{
	lx_emul_trace(__func__);

	/* Set default fwnode to parent's one if present */
	if (gc->parent)
		ACPI_COMPANION_SET(&gdev->dev, ACPI_COMPANION(gc->parent));
}


static int find_match_name(struct gpio_chip *gc, const void *data)
{
	char const *name = data;

	return !strncmp(gc->label, name, strlen(name));
}


int acpi_dev_gpio_irq_wake_get_by(struct acpi_device *adev, const char *name, int index, bool *wake_capable)
{
	int irq, ret;
	struct gpio_desc   *desc;
	struct gpio_device *gdev;
	char label[32];
	unsigned long lflags = GPIO_ACTIVE_LOW | GPIO_PERSISTENT;
	enum gpiod_flags dflags = GPIOD_IN;

	if (index != 0)
		return -ENOENT;

	/* most interesting part happens in gpiod_to_irq(desc) */
	// READ FROM CONFIG
	if (!(gdev = gpio_device_find("INTC1083", find_match_name))) {
		printk("GPIO chip '%s' not available\n", "INTC1083");
		return -ENOENT;
	}
	gpio_device_put(gdev);

	desc = gpiochip_get_desc(gdev->chip , 209);
	irq  = gpiod_to_irq(desc);

	printk("%s:%d irq: %d\n", __func__, __LINE__, irq);

	snprintf(label, sizeof(label), "GpioInt() %d", index);
	ret = gpiod_configure_flags(desc, label, lflags, dflags);
	if (ret < 0)
		return ret;

	ret = gpio_set_debounce_timeout(desc, 0);
	if (ret < 0)
		return ret;

	irq_set_irq_type(irq, IRQ_TYPE_LEVEL_LOW);

	return irq;
}


struct gpio_desc * acpi_find_gpio(struct fwnode_handle *fwnode, const char *con_id,
                                  unsigned int idx, enum gpiod_flags *dflags,
                                  unsigned long *lookupflags)
{
	printk("%s:%d\n", __func__, __LINE__);

	/* called from designware i2c - trace always resulted in -ENOENT */
	return ERR_PTR(-ENOENT);
}


int acpi_gpio_count(const struct fwnode_handle *fwnode, const char *con_id)
{
	printk("%s:%d fwnode: %px con_id: '%s'\n", __func__, __LINE__, fwnode, con_id);

	return -1;
}
