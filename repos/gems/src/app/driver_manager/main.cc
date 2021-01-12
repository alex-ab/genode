/*
 * \brief  Driver manager
 * \author Norman Feske
 * \date   2017-06-13
 */

/*
 * Copyright (C) 2017-2021 Genode Labs GmbH
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
#include <capture_session/capture_session.h>
#include <io_mem_session/io_mem_session.h>
#include <io_port_session/io_port_session.h>
#include <timer_session/timer_session.h>
#include <timer_session/connection.h>
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
	struct Nvme_driver;

	struct Priority { int value; };
}


struct Driver_manager::Block_devices_generator : Interface
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

		uint16_t _bus      { 0 };
		uint16_t _device   { 0 };
		uint8_t  _function { 0 };

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

		template <typename FUNC>
		static void _generate_platform_policy(Xml_generator &xml,
		                                      uint8_t const bus,
		                                      uint8_t const device,
		                                      uint8_t const function,
		                                      Session_label const &label,
		                                      FUNC const &fn)
		{
			xml.node("policy", [&] () {
				xml.attribute("label", label);
				xml.node("pci", [&] () {
					xml.attribute("bus", bus);
					xml.attribute("device", device);
					xml.attribute("function", function);
				});

				fn();
			});
		}

		static void _generate_platform_policy(Xml_generator &xml,
		                                      uint8_t const bus,
		                                      uint8_t const device,
		                                      uint8_t const function,
		                                      Session_label const &label) {
			_generate_platform_policy(xml, bus, device, function, label,
			                          [](){});
		}

		virtual ~Device_driver() { }

	public:

		virtual void generate_start_node(Xml_generator &xml) const = 0;
		virtual void generate_platform_policy(Xml_generator &) const = 0;
};


struct Driver_manager::Intel_fb_driver : Device_driver
{
	Intel_fb_driver(uint8_t bus, uint8_t device, uint8_t function)
	{
		_bus = bus;
		_device = device;
		_function = function;
	}

	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "intel_fb_drv", "intel_fb_drv",
			                               Ram_quota{42*1024*1024}, Cap_quota{800},
			                               Priority{0});
			xml.node("route", [&] () {
				_gen_config_route(xml, "fb_drv.config");
				_gen_default_parent_route(xml);
			});
		});
	}

	void generate_platform_policy(Xml_generator &xml) const override
	{
		_generate_platform_policy(xml, _bus, _device, _function,
		                          "dynamic -> intel_fb_drv -> ",
		                         [&]() {
			xml.node("pci", [&] () {
				xml.attribute("bus", 0);
				xml.attribute("device", 0);
				xml.attribute("function", 0);
			});
			xml.node("pci", [&] () {
				xml.attribute("class", "ISABRIDGE");
			});
		});
	}
};


struct Driver_manager::Vesa_fb_driver : Device_driver
{
	Vesa_fb_driver(uint8_t bus, uint8_t device, uint8_t function)
	{
		_bus = bus;
		_device = device;
		_function = function;
	}

	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "vesa_fb_drv", "vesa_fb_drv",
			                               Ram_quota{8*1024*1024}, Cap_quota{100},
			                               Priority{-1});
			xml.node("route", [&] () {
				_gen_config_route(xml, "fb_drv.config");
				_gen_default_parent_route(xml);
			});
		});
	}

	void generate_platform_policy(Xml_generator &xml) const override
	{
		_generate_platform_policy(xml, _bus, _device, _function,
		                          "dynamic -> vesa_fb_drv -> ");
	}
};


struct Driver_manager::Boot_fb_driver : Device_driver
{
	Ram_quota const _ram_quota;

	struct Mode
	{
		enum { TYPE_RGB_COLOR = 1 };

		unsigned _pitch = 0, _height = 0;

		Mode() { }

		Mode(Xml_node node)
		:
			_pitch(node.attribute_value("pitch", 0U)),
			_height(node.attribute_value("height", 0U))
		{
			/* check for unsupported type */
			if (node.attribute_value("type", 0U) != TYPE_RGB_COLOR)
				_pitch = _height = 0;
		}

		size_t num_bytes() const { return _pitch * _height + 1024*1024; }

		bool valid() const { return _pitch * _height != 0; }
	};

	Boot_fb_driver(Mode const mode) : _ram_quota(Ram_quota{mode.num_bytes()}) { }

	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "boot_fb_drv", "boot_fb_drv",
			                               _ram_quota, Cap_quota{100},
			                               Priority{-1});
			xml.node("route", [&] () {
				_gen_config_route(xml, "fb_drv.config");
				_gen_default_parent_route(xml);
			});
		});
	}

	void generate_platform_policy(Xml_generator &) const override { }
};


struct Driver_manager::Ahci_driver : Device_driver
{
	Ahci_driver(uint8_t bus, uint8_t device, uint8_t function)
	{
		_bus = bus;
		_device = device;
		_function = function;
	}

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
						xml.attribute("label_suffix", String<64>("ahci-", i));
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

	typedef String<32> Default_label;

	void gen_service_forwarding_policy(Xml_generator &xml,
	                                   Default_label const &default_label) const
	{
		for (unsigned i = 0; i < 6; i++) {
			xml.node("policy", [&] () {
				xml.attribute("label_suffix", String<64>("ahci-", i));
				xml.node("child", [&] () {
					xml.attribute("name", "ahci_drv"); });
			});
		}

		if (default_label.valid()) {
			xml.node("policy", [&] () {
				xml.attribute("label_suffix", " default");
				xml.node("child", [&] () {
					xml.attribute("name", "ahci_drv");
					xml.attribute("label", default_label);
				});
			});
		}
	}

	void generate_platform_policy(Xml_generator &xml) const override
	{
		_generate_platform_policy(xml, _bus, _device, _function,
		                          "dynamic -> ahci_drv -> ");
	}
};


struct Driver_manager::Nvme_driver : Device_driver
{
	Nvme_driver(uint8_t bus, uint8_t device, uint8_t function)
	{
		_bus = bus;
		_device = device;
		_function = function;
	}

	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "nvme_drv", "nvme_drv",
			                               Ram_quota{8*1024*1024}, Cap_quota{100},
			                               Priority{-1});
			_gen_provides_node<Block::Session>(xml);
			xml.node("config", [&] () {
				xml.node("report", [&] () { xml.attribute("namespaces", "yes"); });
				xml.node("policy", [&] () {
					xml.attribute("label_suffix", String<64>("nvme-0"));
					xml.attribute("namespace", 1);
					xml.attribute("writeable", "yes");
				});
			});
			xml.node("route", [&] () {
				xml.node("service", [&] () {
					xml.attribute("name", "Report");
					xml.node("parent", [&] () { xml.attribute("label", "nvme_ns"); });
				});
				_gen_default_parent_route(xml);
			});
		});
	}

	typedef String<32> Default_label;

	void gen_service_forwarding_policy(Xml_generator &xml,
	                                   Default_label const &default_label) const
	{
		xml.node("policy", [&] () {
			xml.attribute("label_suffix", String<64>("nvme-0"));
			xml.node("child", [&] () {
				xml.attribute("name", "nvme_drv"); });
		});

		if (default_label.valid()) {
			xml.node("policy", [&] () {
				xml.attribute("label_suffix", " default");
				xml.node("child", [&] () {
					xml.attribute("name", "nvme_drv");
					xml.attribute("label", default_label);
				});
			});
		}
	}

	void generate_platform_policy(Xml_generator &xml) const override
	{
		_generate_platform_policy(xml, _bus, _device, _function,
		                          "dynamic -> nvme_drv -> ");
	}
};


struct Driver_manager::Main : private Block_devices_generator
{
	Env &_env;

	Attached_rom_dataspace _platform    { _env, "platform_info" };
	Attached_rom_dataspace _usb_devices { _env, "usb_devices" };
	Attached_rom_dataspace _usb_policy  { _env, "usb_policy"  };
	Attached_rom_dataspace _pci_devices { _env, "pci_devices" };
	Attached_rom_dataspace _ahci_ports  { _env, "ahci_ports"  };
	Attached_rom_dataspace _nvme_ns     { _env, "nvme_ns"     };

	Reporter _init_config     { _env, "config", "init.config" };
	Reporter _usb_drv_config  { _env, "config", "usb_drv.config" };
	Reporter _platform_config { _env, "config", "platform.config" };
	Reporter _block_devices   { _env, "block_devices" };

	Constructible<Intel_fb_driver> _intel_fb_driver { };
	Constructible<Vesa_fb_driver>  _vesa_fb_driver  { };
	Constructible<Boot_fb_driver>  _boot_fb_driver  { };
	Constructible<Ahci_driver>     _ahci_driver     { };
	Constructible<Nvme_driver>     _nvme_driver     { };

	Timer::Connection _timer { _env };

	bool _use_ohci { true };

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

	Signal_handler<Main> _usb_policy_update_handler {
		_env.ep(), *this, &Main::_handle_usb_devices_update };

	void _handle_ahci_ports_update();

	Signal_handler<Main> _ahci_ports_update_handler {
		_env.ep(), *this, &Main::_handle_ahci_ports_update };

	void _handle_nvme_ns_update();

	Signal_handler<Main> _nvme_ns_update_handler {
		_env.ep(), *this, &Main::_handle_nvme_ns_update };

	static void _gen_parent_service_xml(Xml_generator &xml, char const *name)
	{
		xml.node("service", [&] () { xml.attribute("name", name); });
	};

	void _generate_init_config     (Reporter &) const;
	void _generate_usb_drv_config  (Reporter &, Xml_node, Xml_node) const;
	void _generate_platform_config (Xml_generator &) const;
	void _generate_block_devices   (Reporter &) const;

	Ahci_driver::Default_label _default_block_device() const;

	/**
	 * Block_devices_generator interface
	 */
	void generate_block_devices() override { _generate_block_devices(_block_devices); }

	Main(Env &env) : _env(env)
	{
		_init_config.enabled(true);
		_usb_drv_config.enabled(true);
		_platform_config.enabled(true);
		_block_devices.enabled(true);

		_pci_devices.sigh(_pci_devices_update_handler);
		_usb_policy .sigh(_usb_policy_update_handler);
		_ahci_ports .sigh(_ahci_ports_update_handler);
		_nvme_ns    .sigh(_nvme_ns_update_handler);

		_timer.sigh(_pci_devices_update_handler);
		_timer.trigger_periodic(10'000'000);

		_generate_init_config(_init_config);

		_handle_pci_devices_update();
		_handle_ahci_ports_update();
		_handle_nvme_ns_update();
	}
};


void Driver_manager::Main::_handle_pci_devices_update()
{
	_pci_devices.update();

	/* decide about fb not before the first valid pci report is available */
	if (!_pci_devices.valid())
		return;

	Boot_fb_driver::Mode const boot_fb_mode = _boot_fb_mode();

	_pci_devices.xml().for_each_sub_node([&] (Xml_node device) {

		uint16_t const vendor_id  = device.attribute_value("vendor_id",  0UL);
		uint16_t const class_code = device.attribute_value("class_code", 0UL) >> 8;
		uint16_t const bus        = device.attribute_value("bus", 256UL);
		uint16_t const dev        = device.attribute_value("device", 32UL);
		uint8_t  const function   = device.attribute_value("function", 0UL);

		enum {
			VENDOR_VBOX  = 0x80EEU,
			VENDOR_INTEL = 0x8086U,
			CLASS_VGA    = 0x300U,
			CLASS_AHCI   = 0x106U,
			CLASS_NVME   = 0x108U,
		};

		if (class_code == CLASS_VGA) {
			if (!_vesa_fb_driver.constructed())
				_vesa_fb_driver.construct(bus, dev, function);

			if (vendor_id == VENDOR_INTEL)
				if (!_intel_fb_driver.constructed())
					_intel_fb_driver.construct(bus, dev, function);
		}

		if (class_code == CLASS_AHCI && vendor_id == VENDOR_INTEL)
			if (!_ahci_driver.constructed())
				_ahci_driver.construct(bus, dev, function);

		if (vendor_id == VENDOR_VBOX)
			_use_ohci = false;

		if (class_code == CLASS_NVME)
			if (!_nvme_driver.constructed())
				_nvme_driver.construct(bus, dev, function);
	});

	bool generate_init_config = false;

	Reporter::Xml_generator xml(_platform_config, [&] () {

		xml.attribute("system", true);
		xml.node("report", [&] () { xml.attribute("report", true); });

		if (_intel_fb_driver.constructed()) {
			_vesa_fb_driver.destruct();
			_boot_fb_driver.destruct();
			_intel_fb_driver->generate_platform_policy(xml);
			generate_init_config = true;
		} else {
			if (!_boot_fb_driver.constructed() && boot_fb_mode.valid()) {
				_vesa_fb_driver.destruct();
				_boot_fb_driver.construct(boot_fb_mode);
				_boot_fb_driver->generate_platform_policy(xml);
				generate_init_config = true;
			}
			if (_vesa_fb_driver.constructed() && !boot_fb_mode.valid()) {
				_boot_fb_driver.destruct();
				_vesa_fb_driver->generate_platform_policy(xml);
				generate_init_config = true;
			}
		}

		if (_ahci_driver.constructed()) {
			_ahci_driver->generate_platform_policy(xml);
			generate_init_config = true;
		}

		if (_nvme_driver.constructed()) {
			_nvme_driver->generate_platform_policy(xml);
			generate_init_config = true;
		}

		_generate_platform_config(xml);
	});

	if (generate_init_config)
		_generate_init_config(_init_config);

	/* generate initial usb driver config not before we know whether ohci should be enabled */
	_generate_usb_drv_config(_usb_drv_config,
	                         Xml_node("<devices/>"),
	                         Xml_node("<usb/>"));

	_usb_devices.sigh(_usb_devices_update_handler);

	_handle_usb_devices_update();
}


void Driver_manager::Main::_handle_ahci_ports_update()
{
	_ahci_ports.update();
	_generate_block_devices(_block_devices);

	/* update service forwarding rules */
	_generate_init_config(_init_config);
}


void Driver_manager::Main::_handle_nvme_ns_update()
{
	_nvme_ns.update();
	_generate_block_devices(_block_devices);

	/* update service forwarding rules */
	_generate_init_config(_init_config);
}


void Driver_manager::Main::_handle_usb_devices_update()
{
	_usb_devices.update();
	_usb_policy.update();

	_generate_usb_drv_config(_usb_drv_config, _usb_devices.xml(), _usb_policy.xml());
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
			_gen_parent_service_xml(xml, Capture::Session::service_name());
		});


		if (_intel_fb_driver.constructed())
			_intel_fb_driver->generate_start_node(xml);

		if (_vesa_fb_driver.constructed())
			_vesa_fb_driver->generate_start_node(xml);

		if (_boot_fb_driver.constructed())
			_boot_fb_driver->generate_start_node(xml);

		if (_ahci_driver.constructed())
			_ahci_driver->generate_start_node(xml);

		if (_nvme_driver.constructed())
			_nvme_driver->generate_start_node(xml);

		/* block-service forwarding rules */
		bool const ahci = _ahci_driver.constructed() && _ahci_ports.xml().has_sub_node("port");
		bool const nvme = _nvme_driver.constructed() && _nvme_ns.xml().has_sub_node("namespace");

		if (!ahci && !nvme) return;

		bool const ahci_and_nvme = ahci && nvme;
		xml.node("service", [&] () {
			xml.attribute("name", Block::Session::service_name());
				if (ahci)
					_ahci_driver->gen_service_forwarding_policy(xml,
						ahci_and_nvme ? Ahci_driver::Default_label() : _default_block_device());
				if (nvme)
					_nvme_driver->gen_service_forwarding_policy(xml,
						ahci_and_nvme ? Nvme_driver::Default_label() : "nvme-0");
		});
	});
}


void Driver_manager::Main::_generate_platform_config(Xml_generator &xml) const
{
	xml.node("policy", [&] () {
		xml.attribute("label_prefix", "ps2_drv");
		xml.node("device", [&] () {
			xml.attribute("name", "PS2");
		});
	});

	xml.node("policy", [&] () {
		xml.attribute("label_prefix", "usb_drv");
		xml.node("pci", [&] () {
			xml.attribute("class", "USB");
		});
	});

	xml.node("policy", [&] () {
		xml.attribute("label_suffix", "-> wifi");
		xml.node("pci", [&] () {
			xml.attribute("class", "WIFI");
		});
	});

	xml.node("policy", [&] () {
		xml.attribute("label_suffix", "-> nic");
		xml.node("pci", [&] () {
			xml.attribute("class", "ETHERNET");
		});
	});
	xml.node("policy", [&] () {
		xml.attribute("label_suffix", "-> audio");
		xml.node("pci", [&] () {
			xml.attribute("class", "AUDIO");
		});
	});
	xml.node("policy", [&] () {
		xml.attribute("label", "apcica");
	});
}


Driver_manager::Ahci_driver::Default_label
Driver_manager::Main::_default_block_device() const
{
	unsigned num_devices = 0;

	Ahci_driver::Default_label result;

	_ahci_ports.xml().for_each_sub_node([&] (Xml_node ahci_port) {

		/* count devices */
		num_devices++;

		unsigned long const num = ahci_port.attribute_value("num", 0UL);
		result = Ahci_driver::Default_label("ahci-", num);
	});

	/* if there is more than one device, we don't return a default device */
	return (num_devices == 1) ? result : Ahci_driver::Default_label();
}


void Driver_manager::Main::_generate_block_devices(Reporter &block_devices) const
{
	Reporter::Xml_generator xml(block_devices, [&] () {

		/* mention default block device in 'default' attribute */
		Ahci_driver::Default_label const default_label = _default_block_device();
		if (default_label.valid())
			xml.attribute("default", default_label);

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

		/* for now just report the first name space */
		if (_nvme_ns.xml().has_sub_node("namespace")) {

			Xml_node nvme_ctrl = _nvme_ns.xml();
			Xml_node nvme_ns   = _nvme_ns.xml().sub_node("namespace");
			xml.node("device", [&] () {

				unsigned long const
					block_count = nvme_ns.attribute_value("block_count", 0UL),
					block_size  = nvme_ns.attribute_value("block_size",  0UL);

				typedef String<40+1> Model;
				Model const model = nvme_ctrl.attribute_value("model", Model());
				typedef String<20+1> Serial;
				Serial const serial = nvme_ctrl.attribute_value("serial", Serial());

				xml.attribute("label",       String<16>("nvme-0"));
				xml.attribute("block_count", block_count);
				xml.attribute("block_size",  block_size);
				xml.attribute("model",       model);
				xml.attribute("serial",      serial);
			});
		}
	});
}


void Driver_manager::Main::_generate_usb_drv_config(Reporter &usb_drv_config,
                                                    Xml_node devices,
                                                    Xml_node policy) const
{
	Reporter::Xml_generator xml(usb_drv_config, [&] () {

		xml.attribute("uhci", true);
		xml.attribute("ehci", true);
		xml.attribute("ohci", _use_ohci);
		xml.attribute("xhci", true);
		xml.attribute("capslock_led", "rom");
		xml.attribute("numlock_led",  "rom");

		xml.node("report", [&] () { xml.attribute("devices", true); });

		/* incorporate user-managed policy */
		policy.with_raw_content([&] (char const *start, size_t length) {
			xml.append(start, length); });

		/* usb hid drv gets all hid devices */
		xml.node("policy", [&] () {
			xml.attribute("label_prefix", "usb_hid_drv");
			xml.attribute("class", "0x3");
		});

		devices.for_each_sub_node("device", [&] (Xml_node device) {

			typedef String<64> Label;
			typedef String<32> Id;

			Label const label      = device.attribute_value("label", Label());
			Id    const vendor_id  = device.attribute_value("vendor_id",  Id());
			Id    const product_id = device.attribute_value("product_id", Id());

			/*
			 * Limit USB sessions to storage and vendor specific in order to avoid
			 * conflicts with the USB driver's built-in HID drivers.
			 */
			unsigned long const class_code = device.attribute_value("class", 0UL);

			enum { USB_CLASS_MASS_STORAGE = 8, USB_CLASS_VENDOR_SPECIFIC = 0xff };

			bool const expose_as_usb_raw = (class_code == USB_CLASS_MASS_STORAGE) ||
			                               (class_code == USB_CLASS_VENDOR_SPECIFIC);
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
}


void Component::construct(Genode::Env &env) { static Driver_manager::Main main(env); }
