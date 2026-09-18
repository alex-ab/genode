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

#ifndef _SRC__SERVER__USB__DEVICE_H_
#define _SRC__SERVER__USB__DEVICE_H_

/* Genode */
#include <base/allocator.h>
#include <base/id_space.h>
#include <usb_session/usb_session.h>
#include <util/dictionary.h>
#include <util/list_model.h>

namespace Usb {
	using namespace Genode;

	struct Device_id;

	class  Device;
	struct Device_model;
	class  Host_session_component;
}


struct Usb::Device_id : String<Usb::Session::Device_name::size()>
{
	using Name = String<32>;
	Name name {};

	using Host_id = Id_space<Host_session_component>::Id;
	Host_id host_id { ~0UL };

	Device_id() : String<size()>() {}

	Device_id(Host_session_component &, Name name);

	Device_id(Usb::Session::Device_name name);

	Device_id(Device_id const &did)
	:
		String<size()>(static_cast<String<size()> const&>(did)),
		name(did.name), host_id(did.host_id)
	{}
};


class Usb::Device
:
	public  Device_id,
	private Dictionary<Device, Device_id>::Element,
	private List_model<Device>::Element
{
	private:

		friend class Genode::Avl_tree<Device>;
		friend class Genode::Avl_node<Device>;
		friend class Genode::Dictionary<Device, Device_id>;
		friend class Genode::List_model<Device>;
		friend class Genode::List<Device>;

	public:

		Host_session_component &host_drv;

		struct Class { uint8_t value; } const dclass;

		struct Vendor_id { uint16_t value; } const vendor_id;

		struct Product_id { uint16_t value; } const product_id;;

		using Manufacturer = String<64>;
		Manufacturer const manufacturer;

		using Product = String<64>;
		Product const product;

		using Speed = String<16>;
		Speed const speed;

		struct Config;

		List_model<Config> config_model {};

		Device_id id() const {
			return Device_id(static_cast<Device_id const &>(*this)); }

		Device_id::Name name() const { return Device_id::name; }

		Device(Dictionary<Device, Device_id> &dict,
		       Host_session_component &host, Name name, Class dclass,
		       Vendor_id vid, Product_id pid, Manufacturer m, Product p,
		       Speed s);

		bool matches(Node const &node) const {
			return (node.attribute_value("name", Name())) == Device_id::name; }

		static bool type_matches(Node const &node) {
			return node.has_type("device"); }

		void update(Allocator&, Node const &node);

		void generate(Generator &, bool) const;

		void for_each_config(auto const &fn) const {
			config_model.for_each([&] (auto const &cfg) { fn(cfg); }); }
};


struct Usb::Device::Config : List_model<Config>::Element
{
	struct Id { uint8_t value; } const id;

	bool active;

	struct Interface;

	List_model<Interface> interface_model {};

	Config(Id id, bool active)
	: id(id), active(active) {}

	bool matches(Node const &node) const {
		return (node.attribute_value("value", 0xff)) == id.value; }

	static bool type_matches(Node const &node) {
		return node.has_type("config"); }

	void update(Allocator&, Node const &node);

	void for_each_interface(auto const &fn) const {
		interface_model.for_each([&] (auto const &iface) { fn(iface); }); }
};


struct Usb::Device::Config::Interface : List_model<Interface>::Element
{
	struct Number { uint8_t value; } const num;

	bool active;

	struct Alt_setting { uint8_t value; } const alt;

	struct Class { uint8_t value; } const iclass;

	struct Sub_class { uint8_t value; } const sub_class;

	struct Protocol { uint8_t value; } const protocol;

	using Info = String<64>;
	Info const info;

	struct Endpoint;

	List_model<Endpoint> ep_model {};

	Interface(Number num, bool active, Alt_setting alt, Class iclass,
	          Sub_class sub, Protocol prot, Info info)
	:
		num(num), active(active), alt(alt), iclass(iclass), sub_class(sub),
		protocol(prot), info(info) {}

	bool matches(Node const &node) const
	{
		return (node.attribute_value("number", 0xff) == num.value) &&
		       (node.attribute_value("alt_setting", 0xff) == alt.value);
	}

	static bool type_matches(Node const &node) {
		return node.has_type("interface"); }

	void update(Allocator&, Node const &node);
};


struct Usb::Device::Config::Interface::Endpoint : List_model<Endpoint>::Element
{
	struct Address { uint8_t value; } const address;

	struct Attributes { uint8_t value; } const attributes;

	size_t const max_packet_size;

	Endpoint(Address addr, Attributes attr, size_t max)
	: address(addr), attributes(attr), max_packet_size(max) {}

	bool matches(Node const &node) const
	{
		return (node.attribute_value("address", 0xff)) == address.value;
	}

	static bool type_matches(Node const &node) {
		return node.has_type("endpoint"); }
};


namespace Usb {
	using Device_dictionary = Dictionary<Device, Device_id>;
}


struct Usb::Device_model : List_model<Device>
{
	struct Update_handler : Genode::Interface
	{
		virtual void model_updated() = 0;
	};

	Device_dictionary &device_dict;

	Host_session_component &host_drv;

	Update_handler &update_handler;

	Device_model(Device_dictionary &dict, Host_session_component &host,
	             Update_handler &handler)
	:
		device_dict(dict), host_drv(host), update_handler(handler) {}

	void update(Allocator &alloc, Node const &node);
};

#endif /* _SRC__SERVER__USB__DEVICE_H_ */
