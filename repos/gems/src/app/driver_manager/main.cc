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
#include <capture_session/capture_session.h>
#include <gpu_session/gpu_session.h>
#include <io_mem_session/io_mem_session.h>
#include <io_port_session/io_port_session.h>
#include <timer_session/timer_session.h>
#include <log_session/log_session.h>
#include <usb_session/usb_session.h>
#include <platform_session/platform_session.h>
#include <event_session/event_session.h>

namespace Driver_manager {
	using namespace Genode;
	struct Main;
	struct Block_devices_generator;
	struct Device_driver;
	struct Intel_gpu_driver;
	struct Intel_fb_driver;
	struct Vesa_fb_driver;
	struct Boot_fb_driver;
	struct Ahci_driver;
	struct Nvme_driver;
	struct Ps2_driver;

	struct Priority { int value; };

	struct Version { unsigned value; };
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

		static void _gen_common_start_node_content(Xml_generator &xml,
		                                           Name    const &name,
		                                           Binary  const &binary,
		                                           Ram_quota      ram,
		                                           Cap_quota      caps,
		                                           Priority       priority,
		                                           Version        version)
		{
			xml.attribute("name", name);
			xml.attribute("caps", String<64>(caps));
			xml.attribute("priority", priority.value);
			xml.attribute("version", version.value);
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


struct Driver_manager::Intel_gpu_driver : Device_driver
{
	Version version { 0 };

	void generate_start_node(Xml_generator &xml) const override
	{
		_gen_forwarded_service<Gpu::Session>(xml, "intel_gpu_drv");

		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "intel_gpu_drv", "intel_gpu_drv",
			                               Ram_quota{64*1024*1024}, Cap_quota{1400},
			                               Priority{0}, version);
			xml.node("provides", [&] () {
				xml.node("service", [&] () {
					xml.attribute("name", Gpu::Session::service_name()); });
				xml.node("service", [&] () {
					xml.attribute("name", Platform::Session::service_name()); });
			});
			xml.node("route", [&] () {
				_gen_config_route(xml, "gpu_drv.config");
				_gen_default_parent_route(xml);
			});
		});
	}
};


struct Driver_manager::Intel_fb_driver : Device_driver
{
	Intel_gpu_driver intel_gpu_driver { };

	Version version { 0 };

	void generate_start_node(Xml_generator &xml) const override
	{
		intel_gpu_driver.generate_start_node(xml);

		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "intel_fb_drv", "pc_intel_fb_drv",
			                               Ram_quota{60*1024*1024}, Cap_quota{800},
			                               Priority{0}, version);
			xml.node("heartbeat", [&] () { });
			xml.node("route", [&] () {
				_gen_config_route(xml, "fb_drv.config");
				xml.node("service", [&] () {
					xml.attribute("name", Platform::Session::service_name());
						xml.node("child", [&] () {
							xml.attribute("name", "intel_gpu_drv");
						});
				});
				_gen_default_parent_route(xml);
			});
		});
	}
};


struct Driver_manager::Vesa_fb_driver : Device_driver
{
	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "vesa_fb_drv", "vesa_fb_drv",
			                               Ram_quota{8*1024*1024}, Cap_quota{120},
			                               Priority{-1}, Version{0});
			xml.node("route", [&] () {
				_gen_config_route(xml, "fb_drv.config");
				_gen_default_parent_route(xml);
			});
		});
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
			                               Priority{-1}, Version{0});
			xml.node("route", [&] () {
				_gen_config_route(xml, "fb_drv.config");
				_gen_default_parent_route(xml);
			});
		});
	}
};


struct Driver_manager::Ahci_driver : Device_driver
{
	using Device_name = Genode::String<8>;

	Device_name         _device_name { };
	Device_driver::Name _driver_name { };

	Attached_rom_dataspace _ports;

	Ahci_driver(Env &env, Device_name name)
	:
		_device_name { name },
		_driver_name { "ahci_drv-", _device_name },
		_ports       { env, Genode::String<64>(_driver_name, " -> ports").string() }
	{ }

	Device_name const &name() const { return _device_name; }

	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, _driver_name, "ahci_drv",
			                               Ram_quota{10*1024*1024}, Cap_quota{100},
			                               Priority{-1}, Version{0});
			_gen_provides_node<Block::Session>(xml);
			xml.node("config", [&] () {
				xml.node("report", [&] () { xml.attribute("ports", "yes"); });
				for (unsigned i = 0; i < 6; i++) {
					xml.node("policy", [&] () {
						xml.attribute("label_suffix", String<64>("ahci-", _device_name, "-", i));
						xml.attribute("device", i);
						xml.attribute("writeable", "yes");
					});
				}
			});
			xml.node("heartbeat", [&] () { });
			xml.node("route", [&] () {
				xml.node("service", [&] () {
					xml.attribute("name", "Report");
					xml.node("parent", [&] () { });
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
				xml.attribute("label_suffix", String<64>("ahci-", _device_name, "-", i));
				xml.node("child", [&] () {
					xml.attribute("name", _driver_name); });
			});
		}

		if (default_label.valid()) {
			xml.node("policy", [&] () {
				xml.attribute("label_suffix", " default");
				xml.node("child", [&] () {
					xml.attribute("name", _driver_name);
					xml.attribute("label", default_label);
				});
			});
		}
	}
};


struct Driver_manager::Nvme_driver : Device_driver
{
	using Device_name = Genode::String<8>;
	Device_name         _device_name { };
	Device_driver::Name _driver_name { };

	Attached_rom_dataspace _ns;

	Nvme_driver(Env &env, Device_name name)
	:
		_device_name { name },
		_driver_name { "nvme_drv-", _device_name },
		_ns          { env, Genode::String<64>(_driver_name, " -> controller").string() }
	{ }

	Device_name const &name() const { return _device_name; }

	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, _driver_name, "nvme_drv",
			                               Ram_quota{8*1024*1024}, Cap_quota{100},
			                               Priority{-1}, Version{0});
			_gen_provides_node<Block::Session>(xml);
			xml.node("config", [&] () {
				xml.node("report", [&] () { xml.attribute("namespaces", "yes"); });
				xml.node("policy", [&] () {
					xml.attribute("label_suffix", String<64>("nvme-", _device_name, "-", 1));
					xml.attribute("namespace", 1);
					xml.attribute("writeable", "yes");
				});
			});
			xml.node("route", [&] () {
				xml.node("service", [&] () {
					xml.attribute("name", "Report");
					xml.node("parent", [&] () { });
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
			xml.attribute("label_suffix", String<64>("nvme-", _device_name, "-", 1));
			xml.node("child", [&] () {
				xml.attribute("name", _driver_name); });
		});

		if (default_label.valid()) {
			xml.node("policy", [&] () {
				xml.attribute("label_suffix", " default");
				xml.node("child", [&] () {
					xml.attribute("name", _driver_name);
					xml.attribute("label", default_label);
				});
			});
		}
	}
};


struct Driver_manager::Ps2_driver : Device_driver
{
	void generate_start_node(Xml_generator &xml) const override
	{
		xml.node("start", [&] () {
			_gen_common_start_node_content(xml, "ps2_drv", "ps2_drv",
			                               Ram_quota{1*1024*1024}, Cap_quota{100},
			                               Priority{0}, Version{0});
			xml.node("config", [&] () {
				xml.attribute("capslock_led", "rom");
				xml.attribute("numlock_led",  "rom");
				xml.attribute("system",        true);
			});
			xml.node("route", [&] () {
				xml.node("service", [&] () {
					xml.attribute("name", "ROM");
					xml.attribute("label", "capslock");
					xml.node("parent", [&] () { xml.attribute("label", "ps2_drv -> capslock"); });
				});
				xml.node("service", [&] () {
					xml.attribute("name", "ROM");
					xml.attribute("label", "numlock");
					xml.node("parent", [&] () { xml.attribute("label", "ps2_drv -> numlock"); });
				});
				xml.node("service", [&] () {
					xml.attribute("name", "ROM");
					xml.attribute("label", "system");
					xml.node("parent", [&] () { xml.attribute("label", "ps2_drv -> system"); });
				});
				_gen_default_parent_route(xml);
			});
		});
	}
};


struct Driver_manager::Main : private Block_devices_generator
{
	Env &_env;

	Attached_rom_dataspace _platform      { _env, "platform_info" };
	Attached_rom_dataspace _usb_devices   { _env, "usb_devices"   };
	Attached_rom_dataspace _usb_policy    { _env, "usb_policy"    };
	Attached_rom_dataspace _devices       { _env, "devices"       };
	Attached_rom_dataspace _dynamic_state { _env, "dynamic_state" };

	Reporter _platform_config  { _env, "config", "platform_drv.config" };
	Reporter _init_config      { _env, "config", "init.config", 32u << 10 };
	Reporter _usb_drv_config   { _env, "config", "usb_drv.config" };
	Reporter _block_report_rom { _env, "config", "block_report_rom.config" };
	Reporter _block_devices    { _env, "block_devices", "block_devices", 32u << 10 };

	Constructible<Intel_fb_driver> _intel_fb_driver { };
	Constructible<Vesa_fb_driver>  _vesa_fb_driver  { };
	Constructible<Boot_fb_driver>  _boot_fb_driver  { };
	Constructible<Ps2_driver>      _ps2_driver      { };

	/* arbitrarily support 4 storage controllers of each NVMe and AHCI */
	enum { MAX_CTLS = 4, };
	Constructible<Ahci_driver> _ahci_driver[MAX_CTLS] { };
	Constructible<Nvme_driver> _nvme_driver[MAX_CTLS] { };

	bool _use_ohci { true };

	Boot_fb_driver::Mode _boot_fb_mode() const
	{
		try {
			Xml_node fb = _platform.xml().sub_node("boot").sub_node("framebuffer");
			return Boot_fb_driver::Mode(fb);
		} catch (...) { }
		return Boot_fb_driver::Mode();
	}

	void _handle_devices_update();

	Signal_handler<Main> _devices_update_handler {
		_env.ep(), *this, &Main::_handle_devices_update };

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

	Signal_handler<Main> _dynamic_state_handler {
		_env.ep(), *this, &Main::_handle_dynamic_state };

	void _handle_dynamic_state();

	static void _gen_parent_service_xml(Xml_generator &xml, char const *name)
	{
		xml.node("service", [&] () { xml.attribute("name", name); });
	};

	void _generate_platform_config         (Reporter &) const;
	void _generate_init_config             (Reporter &) const;
	void _generate_usb_drv_config          (Reporter &, Xml_node, Xml_node) const;
	void _generate_block_report_rom_config (Reporter &) const;
	void _generate_block_devices           (Reporter &) const;

	Ahci_driver::Default_label _default_block_device() const;

	/**
	 * Block_devices_generator interface
	 */
	void generate_block_devices() override { _generate_block_devices(_block_devices); }

	Main(Env &env) : _env(env)
	{
		_platform_config.enabled(true);
		_generate_platform_config(_platform_config);

		_block_report_rom.enabled(true);
		_generate_block_report_rom_config(_block_report_rom);

		_init_config.enabled(true);
		_usb_drv_config.enabled(true);
		_block_devices.enabled(true);

		_devices      .sigh(_devices_update_handler);
		_usb_policy   .sigh(_usb_policy_update_handler);
		_dynamic_state.sigh(_dynamic_state_handler);

		_generate_init_config(_init_config);

		_handle_devices_update();
	}
};


void Driver_manager::Main::_handle_devices_update()
{
	_devices.update();

	/* decide about fb not before the first valid pci report is available */
	if (!_devices.valid())
		return;

	/* for now always update the platform_drv config... */
	_generate_platform_config(_platform_config);

	/* and always generate the block report rom config beforehand */
	_generate_block_report_rom_config(_block_report_rom);

	bool has_vga            = false;
	bool has_intel_graphics = false;
	bool has_ps2            = false;

	Boot_fb_driver::Mode const boot_fb_mode = _boot_fb_mode();

	_devices.xml().for_each_sub_node([&] (Xml_node device) {

		using Device_name  = Genode::String<8>;
		Device_name const device_name = device.attribute_value("name", Device_name());
		if (!device_name.valid())
			return;

		if (device_name == "ps2")
			has_ps2 = true;

		device.with_optional_sub_node("pci-config", [&] (Xml_node pci) {

			uint16_t const vendor_id  = (uint16_t)pci.attribute_value("vendor_id",  0U);
			uint16_t const class_code = (uint16_t)(pci.attribute_value("class", 0U) >> 8);

			enum {
				VENDOR_VBOX  = 0x80EEU,
				VENDOR_INTEL = 0x8086U,
				CLASS_VGA    = 0x300U,
				CLASS_AHCI   = 0x106U,
				CLASS_NVME   = 0x108U,
			};

			if (class_code == CLASS_VGA)
				has_vga = true;

			if (vendor_id == VENDOR_INTEL && class_code == CLASS_VGA)
				has_intel_graphics = true;

			if (vendor_id == VENDOR_VBOX)
				_use_ohci = false;

			if (vendor_id == VENDOR_INTEL && class_code == CLASS_AHCI) {
				bool already_constructed = false;
				for (auto & driver : _ahci_driver)
					if (driver.constructed() && driver->name() == device_name)
						already_constructed = true;

				if (!already_constructed)
					for (auto & driver : _ahci_driver) {
						if (driver.constructed()) continue;

						driver.construct(_env, device_name);
						driver->_ports.sigh(_ahci_ports_update_handler);
						_generate_init_config(_init_config);
						break;
					}
			}

			if (class_code == CLASS_NVME) {
				bool already_constructed = false;
				for (auto & driver : _nvme_driver)
					if (driver.constructed() && driver->name() == device_name)
						already_constructed = true;

				if (!already_constructed)
					for (auto & driver : _nvme_driver) {
						if (driver.constructed()) continue;

						driver.construct(_env, device_name);
						driver->_ns.sigh(_nvme_ns_update_handler);
						_generate_init_config(_init_config);
						break;
					}
			}

		});
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

	if (!_ps2_driver.constructed() && has_ps2) {
		_ps2_driver.construct();
		_generate_init_config(_init_config);
	}

	/* generate initial usb driver config not before we know whether ohci should be enabled */
	_generate_usb_drv_config(_usb_drv_config,
	                         Xml_node("<devices/>"),
	                         Xml_node("<usb/>"));

	_usb_devices.sigh(_usb_devices_update_handler);

	_handle_usb_devices_update();
}


void Driver_manager::Main::_handle_ahci_ports_update()
{
	for (auto & driver : _ahci_driver)
		if (driver.constructed())
			driver->_ports.update();

	_generate_block_devices(_block_devices);

	/* update service forwarding rules */
	_generate_init_config(_init_config);
}


void Driver_manager::Main::_handle_nvme_ns_update()
{
	for (auto & driver : _nvme_driver)
		if (driver.constructed())
			driver->_ns.update();

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


void Driver_manager::Main::_generate_block_report_rom_config(Reporter &block_report_rom_config) const
{
	try {
	Reporter::Xml_generator xml(block_report_rom_config, [&] () {
		xml.attribute("verbose", "yes");

		if (_devices.valid()) _devices.xml().for_each_sub_node([&] (Xml_node device) {
			device.with_optional_sub_node("pci-config", [&] (Xml_node pci) {

				uint16_t const class_code = (uint16_t)(pci.attribute_value("class", 0U) >> 8);

				enum {
					CLASS_AHCI   = 0x106U,
					CLASS_NVME   = 0x108U,
				};

				if (class_code != CLASS_AHCI && class_code != CLASS_NVME)
					return;

				using Device_name = Genode::String<8>;
				using Label       = Genode::String<64>;
				using Report      = Genode::String<64>;

				Device_name const device_name {
					device.attribute_value("name", Device_name()) };

				if (class_code == CLASS_AHCI) {
					Report const report {
						"dynamic -> ahci_drv-", device_name, " -> ports" };
					Label const label {
						"driver_manager -> ahci_drv-", device_name, " -> ports"};
					xml.node("policy", [&] () {
						xml.attribute("label", label);
						xml.attribute("report", report);
					});
				}

				if (class_code == CLASS_NVME) {
					Report const report {
						"dynamic -> nvme_drv-", device_name, " -> controller" };
					Label const label {
						"driver_manager -> nvme_drv-", device_name, " -> controller" };
					xml.node("policy", [&] () {
						xml.attribute("label", label);
						xml.attribute("report", report);
					});
				}
			});
		});
	});
	} catch (Xml_generator::Buffer_exceeded) {
		warning("could not generate block report rom config");
	}
}


void Driver_manager::Main::_generate_platform_config(Reporter &platform_config) const
{
	try{
	Reporter::Xml_generator xml(platform_config, [&] () {
		xml.node("report", [&] () {
			xml.attribute("devices", true);
		});

		/* dynamic driver policies */
		if (_devices.valid()) _devices.xml().for_each_sub_node([&] (Xml_node device) {
			device.with_optional_sub_node("pci-config", [&] (Xml_node pci) {

				uint16_t const class_code = (uint16_t)(pci.attribute_value("class", 0U) >> 8);

				enum {
					CLASS_AHCI   = 0x106U,
					CLASS_NVME   = 0x108U,
				};

				if (class_code != CLASS_AHCI && class_code != CLASS_NVME)
					return;

				using Device_name  = Genode::String<8>;
				using Driver_label = Genode::String<32>;

				Device_name const device_name {
					device.attribute_value("name", Device_name()) };

				if (class_code == CLASS_AHCI) {
					Driver_label const driver_label {
						"dynamic -> ahci_drv-", device_name };
					xml.node("policy", [&] () {
						xml.attribute("label_prefix", driver_label);
						xml.node("device", [&] () { xml.attribute("name", device_name); });
					});
				}

				if (class_code == CLASS_NVME) {
					Driver_label const driver_label {
						"dynamic -> nvme_drv-", device_name };
					xml.node("policy", [&] () {
						xml.attribute("label_prefix", driver_label);
						xml.node("device", [&] () { xml.attribute("name", device_name); });
					});
				}
			});
		});

		xml.node("policy", [&] () {
			xml.attribute("label_prefix", "dynamic -> ps2_drv");
			xml.node("device", [&] () { xml.attribute("name", "ps2"); });
		});

		/* catch-all driver policies */
		xml.node("policy", [&] () {
			xml.attribute("label_prefix", "usb_drv");
			xml.attribute("info", true);
			xml.node("pci", [&] () { xml.attribute("class", "USB"); });
		});
		xml.node("policy", [&] () {
			xml.attribute("label_prefix", "dynamic -> vesa_fb_drv");
			xml.attribute("info", true);
			xml.node("pci", [&] () { xml.attribute("class", "VGA"); });
		});
		xml.node("policy", [&] () {
			xml.attribute("label_prefix", "dynamic -> intel_gpu_drv");
			xml.attribute("info", true);
			xml.node("pci", [&] () { xml.attribute("class", "VGA"); });
			xml.node("pci", [&] () { xml.attribute("class", "ISABRIDGE"); });
		});
		xml.node("policy", [&] () {
			xml.attribute("label_suffix", "-> wifi");
			xml.attribute("msix", false);
			xml.attribute("info", true);
			xml.node("pci", [&] () { xml.attribute("class", "WIFI"); });
		});
		xml.node("policy", [&] () {
			xml.attribute("label_suffix", "-> nic");
			xml.node("pci", [&] () { xml.attribute("class", "ETHERNET"); });
		});
		xml.node("policy", [&] () {
			xml.attribute("label_suffix", "-> audio");
			xml.node("pci", [&] () { xml.attribute("class", "AUDIO"); });
			xml.node("pci", [&] () { xml.attribute("class", "HDAUDIO"); });
		});

		/* acpica policy */
		xml.node("policy", [&] () {
			xml.attribute("label", "acpica");
		});
	});
	} catch (Xml_generator::Buffer_exceeded) {
		warning("could not platform driver config");
	}
}


void Driver_manager::Main::_generate_init_config(Reporter &init_config) const
{
	try {
	Reporter::Xml_generator xml(init_config, [&] () {

		xml.attribute("verbose", false);
		xml.attribute("prio_levels", 2);

		xml.node("report", [&] () {
			xml.attribute("child_ram", true);
			xml.attribute("delay_ms", 2500);
		});

		xml.node("heartbeat", [&] () { xml.attribute("rate_ms", 2500); });

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
			_gen_parent_service_xml(xml, Event::Session::service_name());
		});


		if (_intel_fb_driver.constructed())
			_intel_fb_driver->generate_start_node(xml);

		if (_vesa_fb_driver.constructed())
			_vesa_fb_driver->generate_start_node(xml);

		if (_boot_fb_driver.constructed())
			_boot_fb_driver->generate_start_node(xml);

		if (_ps2_driver.constructed())
			_ps2_driver->generate_start_node(xml);

		auto ahci_constructed = [&] () {
			bool result = false;
			for (auto const & driver : _ahci_driver)
				if (driver.constructed()) {
					driver->generate_start_node(xml);
					result = true;
				}
			return result;
		};

		auto ports_avail = [&] () {
			bool result = false;
			for (auto const & driver : _ahci_driver)
				if (driver.constructed() && driver->_ports.xml().has_sub_node("port"))
					result = true;
			return result;
		};

		auto nvme_constructed = [&] () {
			bool result = false;
			for (auto const & driver : _nvme_driver)
				if (driver.constructed()) {
					driver->generate_start_node(xml);
					result = true;
				}
			return result;
		};

		auto ns_avail = [&] () {
			bool result = false;
			for (auto const & driver : _nvme_driver)
				if (driver.constructed() && driver->_ns.xml().has_sub_node("namespace"))
					result = true;
			return result;
		};

		/* block-service forwarding rules */
		bool const ahci = ahci_constructed() && ports_avail();
		bool const nvme = nvme_constructed() && ns_avail();

		if (!ahci && !nvme) return;

		xml.node("service", [&] () {
			xml.attribute("name", Block::Session::service_name());
				if (ahci)
					for (auto const &driver : _ahci_driver)
						if (driver.constructed())
							driver->gen_service_forwarding_policy(xml, Ahci_driver::Default_label());
				if (nvme)
					for (auto const &driver : _nvme_driver)
						if (driver.constructed())
							driver->gen_service_forwarding_policy(xml, Nvme_driver::Default_label());
		});
	});
	} catch (Xml_generator::Buffer_exceeded) {
		warning("could not generate init config");
	}
}


Driver_manager::Ahci_driver::Default_label
Driver_manager::Main::_default_block_device() const
{
	return Ahci_driver::Default_label();
}


void Driver_manager::Main::_generate_block_devices(Reporter &block_devices) const
{
	try {
	Reporter::Xml_generator xml(block_devices, [&] () {

		/* mention default block device in 'default' attribute */
		Ahci_driver::Default_label const default_label = _default_block_device();
		if (default_label.valid())
			xml.attribute("default", default_label);

		for (auto const & driver : _ahci_driver) {
			if (!driver.constructed()) continue;

			driver->_ports.xml().for_each_sub_node([&] (Xml_node ahci_port) {

				xml.node("device", [&] () {

					unsigned long const
						num         = ahci_port.attribute_value("num",         0UL),
						block_count = ahci_port.attribute_value("block_count", 0UL),
						block_size  = ahci_port.attribute_value("block_size",  0UL);

					typedef String<80> Model;
					Model const model = ahci_port.attribute_value("model", Model());

					xml.attribute("label",       String<64>("ahci-", driver->name(), "-", num));
					xml.attribute("block_count", block_count);
					xml.attribute("block_size",  block_size);
					xml.attribute("model",       model);
				});
			});
		}

		/* for now just report the first name space */
		for (auto const & driver : _nvme_driver) {
			if (!driver.constructed()) continue;

			if (driver->_ns.xml().has_sub_node("namespace")) {

				Xml_node nvme_ctrl = driver->_ns.xml();
				Xml_node nvme_ns   = driver->_ns.xml().sub_node("namespace");
				xml.node("device", [&] () {

					unsigned long const
						block_count = nvme_ns.attribute_value("block_count", 0UL),
						block_size  = nvme_ns.attribute_value("block_size",  0UL);

					typedef String<40+1> Model;
					Model const model = nvme_ctrl.attribute_value("model", Model());
					typedef String<20+1> Serial;
					Serial const serial = nvme_ctrl.attribute_value("serial", Serial());

					xml.attribute("label",       String<16>("nvme-", driver->name(), "-", 1));
					xml.attribute("block_count", block_count);
					xml.attribute("block_size",  block_size);
					xml.attribute("model",       model);
					xml.attribute("serial",      serial);
				});
			}
		}
	});
	} catch (Xml_generator::Buffer_exceeded) {
		warning("could not generate block devices");
	}
}


void Driver_manager::Main::_generate_usb_drv_config(Reporter &usb_drv_config,
                                                    Xml_node devices,
                                                    Xml_node policy) const
{
	Reporter::Xml_generator xml(usb_drv_config, [&] () {

		xml.attribute("bios_handoff", true);

		xml.node("report", [&] () {
			xml.attribute("config",  true);
			xml.attribute("devices", true);
		});

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


void Driver_manager::Main::_handle_dynamic_state()
{
	_dynamic_state.update();

	bool reconfigure_dynamic_init = false;

	_dynamic_state.xml().for_each_sub_node([&] (Xml_node child) {

		using Name = Device_driver::Name;

		Name const name = child.attribute_value("name", Name());

		if (name == "intel_fb_drv") {

			unsigned long const skipped_heartbeats =
				child.attribute_value("skipped_heartbeats", 0U);

			if (skipped_heartbeats >= 2) {

				if (_intel_fb_driver.constructed()) {
					_intel_fb_driver->version.value++;
					reconfigure_dynamic_init = true;
				}
			}
		}
	});

	if (reconfigure_dynamic_init)
		_generate_init_config(_init_config);
}


void Component::construct(Genode::Env &env) { static Driver_manager::Main main(env); }
