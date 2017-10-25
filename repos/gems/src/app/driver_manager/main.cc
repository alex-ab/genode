/*
 * \brief  Driver manager
 * \author Norman Feske
 * \date   2017-06-13
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/component.h>
#include <base/registry.h>
#include <base/attached_rom_dataspace.h>
#include <os/reporter.h>
#include <block_session/block_session.h>
#include <rm_session/rm_session.h>
#include <framebuffer_session/framebuffer_session.h>
#include <io_mem_session/io_mem_session.h>
#include <io_port_session/io_port_session.h>
#include <timer_session/timer_session.h>
#include <log_session/log_session.h>
#include <usb_session/usb_session.h>
#include <platform_session/platform_session.h>

namespace Driver_manager {
	using namespace Genode;
	struct Main;
	struct Block_devices_generator;
	struct Device_driver;
	struct Intel_fb_driver;
	struct Vesa_fb_driver;
	struct Boot_fb_driver;
	struct Ahci_driver;

	struct Priority { int value; };
}


struct Driver_manager::Block_devices_generator
{
	virtual void generate_block_devices() = 0;
};


class Driver_manager::Device_driver : Noncopyable
{
	public:

		typedef String<64>  Name;
		typedef String<100> Binary;
		typedef String<32>  Service;

	protected:

		static void _gen_common_start_node_content(Xml_generator &xml,
		                                           Name    const &name,
		                                           Binary  const &binary,
		                                           Ram_quota      ram,
		                                           Cap_quota      caps,
		                                           Priority       priority)
		{
			xml.attribute("name", name);
			xml.attribute("caps", String<64>(caps));
			xml.attribute("priority", priority.value);
			xml.node("binary", [&] () { xml.attribute("name", binary); });
			xml.node("resource", [&] () {
				xml.attribute("name", "RAM");
				xml.attribute("quantum", String<64>(ram));
			});
		}

		template <typename SESSION>
		static void _gen_provides_node(Xml_generator &xml)
		{
			xml.node("provides", [&] () {
				xml.node("service", [&] () {
					xml.attribute("name", SESSION::service_name()); }); });
		}

		static void _gen_config_route(Xml_generator &xml, char const *config_name)
		{
			xml.node("service", [&] () {
				xml.attribute("name", Rom_session::service_name());
				xml.attribute("label", "config");
				xml.node("parent", [&] () {
					xml.attribute("label", config_name); });
			});
		}

		static void _gen_default_parent_route(Xml_generator &xml)
		{
			xml.node("any-service", [&] () {
				xml.node("parent", [&] () { }); });
		}

		template <typename SESSION>
		static void _gen_forwarded_service(Xml_generator &xml,
		                                   Device_driver::Name const &name)
		{
			xml.node("service", [&] () {
				xml.attribute("name", SESSION::service_name());
				xml.node("default-policy", [&] () {
					xml.node("child", [&] () {
						xml.attribute("name", name);
					});
				});
			});
		};

		virtual ~Device_driver() { }

	public:

		virtual void generate_start_node(Xml_generator &xml) const = 0;
};


struct Driver_manager::Intel_fb_driver : Device_driver
{
	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "intel_fb_drv", "intel_fb_drv",
			                               Ram_quota{20*1024*1024}, Cap_quota{200},
			                               Priority{0});
			_gen_provides_node<Framebuffer::Session>(xml);
			xml.node("route", [&] () {
				_gen_config_route(xml, "fb_drv.config");
				_gen_default_parent_route(xml);
			});
		});
		_gen_forwarded_service<Framebuffer::Session>(xml, "intel_fb_drv");
	}
};


struct Driver_manager::Vesa_fb_driver : Device_driver
{
	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "vesa_fb_drv", "fb_drv",
			                               Ram_quota{8*1024*1024}, Cap_quota{100},
			                               Priority{-1});
			_gen_provides_node<Framebuffer::Session>(xml);
			xml.node("route", [&] () {
				_gen_config_route(xml, "fb_drv.config");
				_gen_default_parent_route(xml);
			});
		});
		_gen_forwarded_service<Framebuffer::Session>(xml, "vesa_fb_drv");
	}
};


struct Driver_manager::Boot_fb_driver : Device_driver
{
	Ram_quota const _ram_quota;

	struct Mode
	{
		unsigned _width = 0, _height = 0, _bpp = 0;

		Mode() { }

		Mode(Xml_node node)
		:
			_width (node.attribute_value("width",  0U)),
			_height(node.attribute_value("height", 0U)),
			_bpp   (node.attribute_value("bpp",    0U))
		{ }

		size_t num_bytes() const { return _width * _height * _bpp/8 + 512*1024; }

		bool valid() const { return _width*_height*_bpp != 0; }
	};

	Boot_fb_driver(Mode const mode) : _ram_quota(Ram_quota{mode.num_bytes()}) { }

	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "fb_boot_drv", "fb_boot_drv",
			                               _ram_quota, Cap_quota{100},
			                               Priority{-1});
			_gen_provides_node<Framebuffer::Session>(xml);
			xml.node("route", [&] () {
				_gen_config_route(xml, "fb_drv.config");
				_gen_default_parent_route(xml);
			});
		});
		_gen_forwarded_service<Framebuffer::Session>(xml, "fb_boot_drv");
	}
};


struct Driver_manager::Ahci_driver : Device_driver
{
	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "ahci_drv", "ahci_drv",
			                               Ram_quota{10*1024*1024}, Cap_quota{100},
			                               Priority{-1});
			_gen_provides_node<Block::Session>(xml);
			xml.node("config", [&] () {
				xml.node("report", [&] () { xml.attribute("ports", "yes"); });
				for (unsigned i = 0; i < 6; i++) {
					xml.node("policy", [&] () {
						xml.attribute("label_suffix", String<64>(" ahci-", i));
						xml.attribute("device", i);
						xml.attribute("writeable", "yes");
					});
				}
			});
			xml.node("route", [&] () {
				xml.node("service", [&] () {
					xml.attribute("name", "Report");
					xml.node("parent", [&] () { xml.attribute("label", "ahci_ports"); });
				});
				_gen_default_parent_route(xml);
			});
		});
	}

	void generate_block_service_forwarding_policy(Xml_generator &xml) const
	{
		for (unsigned i = 0; i < 6; i++) {
			xml.node("policy", [&] () {
				xml.attribute("label_suffix", String<64>(" ahci-", i));
				xml.node("child", [&] () {
					xml.attribute("name", "ahci_drv");
				});
			});
		}
	}
};


struct Driver_manager::Main : Block_devices_generator
{
	Env &_env;

	Attached_rom_dataspace _platform    { _env, "platform_info" };
	Attached_rom_dataspace _init_state  { _env, "init_state"  };
	Attached_rom_dataspace _usb_devices { _env, "usb_devices" };
	Attached_rom_dataspace _pci_devices { _env, "pci_devices" };
	Attached_rom_dataspace _ahci_ports  { _env, "ahci_ports"  };

	Reporter _init_config    { _env, "config", "init.config" };
	Reporter _usb_drv_config { _env, "config", "usb_drv.config" };
	Reporter _block_devices  { _env, "block_devices" };

	Constructible<Intel_fb_driver> _intel_fb_driver;
	Constructible<Vesa_fb_driver>  _vesa_fb_driver;
	Constructible<Boot_fb_driver>  _boot_fb_driver;
	Constructible<Ahci_driver>     _ahci_driver;

	Boot_fb_driver::Mode _boot_fb_mode() const
	{
		try {
			Xml_node fb = _platform.xml().sub_node("boot").sub_node("framebuffer");
			return Boot_fb_driver::Mode(fb);
		} catch (...) { }
		return Boot_fb_driver::Mode();
	}

	void _handle_pci_devices_update();

	Signal_handler<Main> _pci_devices_update_handler {
		_env.ep(), *this, &Main::_handle_pci_devices_update };

	void _handle_usb_devices_update();

	Signal_handler<Main> _usb_devices_update_handler {
		_env.ep(), *this, &Main::_handle_usb_devices_update };

	void _handle_ahci_ports_update();

	Signal_handler<Main> _ahci_ports_update_handler {
		_env.ep(), *this, &Main::_handle_ahci_ports_update };

	static void _gen_parent_service_xml(Xml_generator &xml, char const *name)
	{
		xml.node("service", [&] () { xml.attribute("name", name); });
	};

	void _generate_init_config    (Reporter &) const;
	void _generate_usb_drv_config (Reporter &, Xml_node) const;
	void _generate_block_devices  (Reporter &) const;

	/**
	 * Block_devices_generator interface
	 */
	void generate_block_devices() override { _generate_block_devices(_block_devices); }

	Main(Env &env) : _env(env)
	{
		_init_config.enabled(true);
		_usb_drv_config.enabled(true);
		_block_devices.enabled(true);

		_pci_devices.sigh(_pci_devices_update_handler);
		_usb_devices.sigh(_usb_devices_update_handler);
		_ahci_ports .sigh(_ahci_ports_update_handler);

		_generate_init_config(_init_config);
		_generate_usb_drv_config(_usb_drv_config, Xml_node("<devices/>"));

		_handle_pci_devices_update();
		_handle_usb_devices_update();
		_handle_ahci_ports_update();
	}
};


void Driver_manager::Main::_handle_pci_devices_update()
{
	_pci_devices.update();

	/* decide about fb not before the first valid pci report is available */
	if (!_pci_devices.valid())
		return;

	bool has_vga            = false;
	bool has_intel_graphics = false;
	bool has_ahci           = false;

	Boot_fb_driver::Mode const boot_fb_mode = _boot_fb_mode();

	_pci_devices.xml().for_each_sub_node([&] (Xml_node device) {

		uint16_t const vendor_id  = device.attribute_value("vendor_id",  0UL);
		uint16_t const class_code = device.attribute_value("class_code", 0UL) >> 8;

		enum {
			VENDOR_INTEL = 0x8086U,
			CLASS_VGA    = 0x300U,
			CLASS_AHCI   = 0x106U,
		};

		if (class_code == CLASS_VGA)
			has_vga = true;

		if (vendor_id == VENDOR_INTEL && class_code == CLASS_VGA)
			has_intel_graphics = true;

		if (vendor_id == VENDOR_INTEL && class_code == CLASS_AHCI)
			has_ahci = true;
	});

	if (!_intel_fb_driver.constructed() && has_intel_graphics) {
		_intel_fb_driver.construct();
		_vesa_fb_driver.destruct();
		_boot_fb_driver.destruct();
		_generate_init_config(_init_config);
	}

	if (!_boot_fb_driver.constructed() && boot_fb_mode.valid() && !has_intel_graphics) {
		_intel_fb_driver.destruct();
		_vesa_fb_driver.destruct();
		_boot_fb_driver.construct(boot_fb_mode);
		_generate_init_config(_init_config);
	}

	if (!_vesa_fb_driver.constructed() && has_vga && !has_intel_graphics &&
	    !boot_fb_mode.valid()) {
		_intel_fb_driver.destruct();
		_boot_fb_driver.destruct();
		_vesa_fb_driver.construct();
		_generate_init_config(_init_config);
	}

	if (!_ahci_driver.constructed() && has_ahci) {
		_ahci_driver.construct();
		_generate_init_config(_init_config);
	}
}


void Driver_manager::Main::_handle_ahci_ports_update()
{
	_ahci_ports.update();
	_generate_block_devices(_block_devices);
}


void Driver_manager::Main::_handle_usb_devices_update()
{
	_usb_devices.update();

	_generate_usb_drv_config(_usb_drv_config, _usb_devices.xml());
}


void Driver_manager::Main::_generate_init_config(Reporter &init_config) const
{
	Reporter::Xml_generator xml(init_config, [&] () {

		xml.attribute("verbose", false);
		xml.attribute("prio_levels", 2);

		xml.node("report", [&] () { xml.attribute("child_ram", true); });

		xml.node("parent-provides", [&] () {
			_gen_parent_service_xml(xml, Rom_session::service_name());
			_gen_parent_service_xml(xml, Io_mem_session::service_name());
			_gen_parent_service_xml(xml, Io_port_session::service_name());
			_gen_parent_service_xml(xml, Cpu_session::service_name());
			_gen_parent_service_xml(xml, Pd_session::service_name());
			_gen_parent_service_xml(xml, Rm_session::service_name());
			_gen_parent_service_xml(xml, Log_session::service_name());
			_gen_parent_service_xml(xml, Timer::Session::service_name());
			_gen_parent_service_xml(xml, Platform::Session::service_name());
			_gen_parent_service_xml(xml, Report::Session::service_name());
			_gen_parent_service_xml(xml, Usb::Session::service_name());
		});


		if (_intel_fb_driver.constructed())
			_intel_fb_driver->generate_start_node(xml);

		if (_vesa_fb_driver.constructed())
			_vesa_fb_driver->generate_start_node(xml);

		if (_boot_fb_driver.constructed())
			_boot_fb_driver->generate_start_node(xml);

		if (_ahci_driver.constructed())
			_ahci_driver->generate_start_node(xml);

		/* block-service forwarding rules */
		xml.node("service", [&] () {
			xml.attribute("name", Block::Session::service_name());
			if (_ahci_driver.constructed())
				_ahci_driver->generate_block_service_forwarding_policy(xml);
		});
	});
}


void Driver_manager::Main::_generate_block_devices(Reporter &block_devices) const
{
	Reporter::Xml_generator xml(block_devices, [&] () {

		_ahci_ports.xml().for_each_sub_node([&] (Xml_node ahci_port) {

			xml.node("device", [&] () {

				unsigned long const
					num         = ahci_port.attribute_value("num",         0UL),
					block_count = ahci_port.attribute_value("block_count", 0UL),
					block_size  = ahci_port.attribute_value("block_size",  0UL);

				typedef String<80> Model;
				Model const model = ahci_port.attribute_value("model", Model());

				xml.attribute("label",       String<64>("ahci-", num));
				xml.attribute("block_count", block_count);
				xml.attribute("block_size",  block_size);
				xml.attribute("model",       model);
			});
		});
	});
}


void Driver_manager::Main::_generate_usb_drv_config(Reporter &usb_drv_config,
                                                    Xml_node devices) const
{
	Reporter::Xml_generator xml(usb_drv_config, [&] () {

		xml.attribute("uhci", true);
		xml.attribute("ehci", true);
		xml.attribute("xhci", true);
		xml.node("hid", [&] () { });
		xml.node("raw", [&] () {
			xml.node("report", [&] () { xml.attribute("devices", true); });

			devices.for_each_sub_node("device", [&] (Xml_node device) {

				typedef String<64> Label;
				typedef String<32> Id;

				Label const label      = device.attribute_value("label", Label());
				Id    const vendor_id  = device.attribute_value("vendor_id",  Id());
				Id    const product_id = device.attribute_value("product_id", Id());

				/*
				 * Limit USB sessions to storage in order to avoid conflicts with
				 * the USB driver's built-in HID drivers.
				 */
				unsigned long const class_code = device.attribute_value("class", 0UL);

				enum { USB_CLASS_MASS_STORAGE = 8 };

				bool const expose_as_usb_raw = (class_code == USB_CLASS_MASS_STORAGE);
				if (!expose_as_usb_raw)
					return;

				xml.node("policy", [&] () {
					xml.attribute("label_suffix", label);
					xml.attribute("vendor_id",  vendor_id);
					xml.attribute("product_id", product_id);

					/* annotate policy to make storage devices easy to spot */
					if (class_code == USB_CLASS_MASS_STORAGE)
						xml.attribute("class", "storage");
				});
			});
		});
	});
}


void Component::construct(Genode::Env &env) { static Driver_manager::Main main(env); }
