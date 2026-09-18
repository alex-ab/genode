/*
 * \brief  Usb multiplexing component - device abstractions
 * \author Stefan Kalkowski
 * \date   2026-08-10
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <device.h>
#include <host_session_component.h>

using namespace Usb;


Device_id::Device_id(Host_session_component &host, Name name)
:
	String<size()>(host.label().last_element(), "-", name),
	name(name), host_id(host.id())
{}


Device_id::Device_id(Usb::Session::Device_name name)
:
	String<size()>(name)
{}


void Device::update(Allocator &alloc, Node const &node)
{
	config_model.update_from_node(node,
		/* create */
		[&] (Node const &node) -> Config &
		{
			return *new (alloc) Config(
				Config::Id{ node.attribute_value<uint8_t>("value", 0xff) },
				node.attribute_value("active", false));
		},

		/* destroy */
		[&] (Config &config)
		{
			config.update(alloc, Node());
			destroy(alloc, &config);
		},

		/* update */
		[&] (Config &config, Node const &node) { config.update(alloc, node); }
	);
}


void Device::generate(Generator &g, bool acquired) const
{
	auto to_string = [] (auto value) {
		return String<64>(Hex(value)); };

	auto per_ep = [&] (auto const &ep) {
		g.node("endpoint", [&] {
			g.attribute("address",         to_string(ep.address.value));
			g.attribute("attributes",      to_string(ep.attributes.value));
			g.attribute("max_packet_size", to_string(ep.max_packet_size));
		});
	};

	auto per_iface = [&] (auto const &iface)
	{
		g.node("interface", [&] {
			g.attribute("active",      iface.active);
			g.attribute("number",      to_string(iface.num.value));
			g.attribute("alt_setting", to_string(iface.alt.value));
			g.attribute("class",       to_string(iface.iclass.value));
			g.attribute("subclass",    to_string(iface.sub_class.value));
			g.attribute("protocol",    to_string(iface.protocol.value));
			if (iface.info.valid()) g.attribute("info", iface.info);
			iface.ep_model.for_each(per_ep);
		});
	};

	auto per_config = [&] (auto const &cfg)
	{
		g.node("config", [&] {
			g.attribute("active", cfg.active);
			g.attribute("value",  to_string(cfg.id.value));
			cfg.interface_model.for_each(per_iface);
		});
	};

	g.node("device", [&] {
		g.attribute("name", id());
		g.attribute("class",      to_string(dclass.value));
		g.attribute("vendor_id",  to_string(vendor_id.value));
		g.attribute("product_id", to_string(product_id.value));
		g.attribute("speed", speed);
		if (manufacturer.valid()) g.attribute("manufacturer", manufacturer);
		if (product.valid()) g.attribute("product", product);
		if (acquired) g.attribute("acquired", acquired);

		config_model.for_each(per_config);
	});
}


Device::Device(Dictionary<Device, Device_id> &dict,
               Host_session_component &host, Name name, Class dclass,
               Vendor_id vid, Product_id pid, Manufacturer m, Product p,
               Speed s)
:
	Device_id(host, name),
	Dictionary<Device, Device_id>::Element(dict, *this),
	host_drv(host), dclass(dclass),
	vendor_id(vid), product_id(pid),
	manufacturer(m), product(p), speed(s) {}


void Device::Config::update(Allocator &alloc, Node const &node)
{
	active = node.attribute_value("active", false);

	interface_model.update_from_node(node,
		/* create */
		[&] (Node const &node) -> Interface &
		{
			return *new (alloc) Interface(
				{ node.attribute_value<uint8_t>("number", 0xff) },
				node.attribute_value("active", false),
				{ node.attribute_value<uint8_t>("alt_setting", 0xff) },
				{ node.attribute_value<uint8_t>("class", 0xff) },
				{ node.attribute_value<uint8_t>("subclass", 0xff) },
				{ node.attribute_value<uint8_t>("protocol", 0xff) },
				node.attribute_value("info", Interface::Info()));
		},

		/* destroy */
		[&] (Interface &iface)
		{
			iface.update(alloc, Node());
			destroy(alloc, &iface);
		},

		/* update */
		[&] (Interface &iface, Node const &node) { iface.update(alloc, node); }
	);
}


void Device::Config::Interface::update(Allocator &alloc, Node const &node)
{
	active = node.attribute_value("active", false);

	ep_model.update_from_node(node,

		/* create */
		[&] (Node const &node) -> Endpoint &
		{
			return *new (alloc) Endpoint(
				{ node.attribute_value<uint8_t>("address", 0xff) },
				{ node.attribute_value<uint8_t>("attributes", 0xff) },
				node.attribute_value<size_t>("max_packet_size", 0));
		},

		/* destroy */
		[&] (Endpoint &ep) { destroy(alloc, &ep); },

		/* update */
		[&] (Endpoint &, Node const &) { }
	);
}


void Device_model::update(Allocator &alloc, Node const &node)
{
	update_from_node(node,

		/* create */
		[&] (Node const &node) -> Device &
		{
			return *(new (alloc) Device(
				device_dict, host_drv,
				node.attribute_value("name", Device::Name()),
				{ node.attribute_value<uint8_t>("class", 0xff) },
				{ node.attribute_value<uint16_t>("vendor_id", 0xffff) },
				{ node.attribute_value<uint16_t>("product_id", 0xffff) },
				node.attribute_value("manufacturer", Device::Manufacturer()),
				node.attribute_value("product", Device::Product()),
				node.attribute_value("speed", Device::Speed())));
		},

		/* destroy */
		[&] (Device &device)
		{
			device.update(alloc, Node());
			destroy(alloc, &device);
		},

		/* update */
		[&] (Device &device, Node const &node)
		{
			device.update(alloc, node);
		}
	);

	update_handler.model_updated();
}
