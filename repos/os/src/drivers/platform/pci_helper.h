/*
 * \brief  Platform driver - PCI helper utilities
 * \author Stefan Kalkowski
 * \date   2022-05-02
 */

/*
 * Copyright (C) 2022-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__DRIVERS__PLATFORM__PCI_HELPER_H_
#define _SRC__DRIVERS__PLATFORM__PCI_HELPER_H_

#include <base/attached_io_mem_dataspace.h>

#include <pci_uhci.h>
#include <pci_ehci.h>
#include <pci_hd_audio.h>

namespace Driver {

	using namespace Genode;
	using namespace Pci;

	struct Config_helper
	{
		Env                              & _env;
		Driver::Device             const & _dev;
		Driver::Device::Pci_config const & _cfg;

		static constexpr size_t IO_MEM_SIZE = 0x1000;

		Attached_io_mem_dataspace _io_mem { _env, _cfg.addr, IO_MEM_SIZE };
		Config                    _config { {_io_mem.local_addr<char>(), IO_MEM_SIZE} };

		Config_helper(Env                              & env,
		              Driver::Device             const & dev,
		              Driver::Device::Pci_config const & cfg)
		: _env(env), _dev(dev), _cfg(cfg) { _config.scan(); }

		void enable(Config::Delayer &delayer)
		{
			_config.power_on(delayer);

			Config::Command::access_t cmd = _config.read<Config::Command>();

			/* always allow DMA operations */
			Config::Command::Bus_master_enable::set(cmd, 1);

			_dev.for_each_io_mem([&] (unsigned, Driver::Device::Io_mem::Range r,
			                          Driver::Device::Pci_bar b, bool)
			{
				_config.set_bar_address(b.number, r.start);

				/* enable memory space when I/O mem is defined */
				Config::Command::Memory_space_enable::set(cmd, 1);
			});

			_dev.for_each_io_port_range([&] (unsigned,
			                                 Driver::Device::Io_port_range::Range r,
			                                 Driver::Device::Pci_bar b)
			{
				_config.set_bar_address(b.number, r.addr);

				/* enable i/o space when I/O ports are defined */
				Config::Command::Io_space_enable::set(cmd, 1);
			});

			_config.write<Config::Command>(cmd);
		}

		void disable()
		{
			Config::Command::access_t cmd = _config.read<Config::Command>();

			Config::Command::Io_space_enable    ::set(cmd, 0);
			Config::Command::Memory_space_enable::set(cmd, 0);
			Config::Command::Bus_master_enable  ::set(cmd, 0);
			Config::Command::Interrupt_enable   ::set(cmd, 0);
			_config.write<Config::Command>(cmd);

			_config.power_off();
		}

		void apply_quirks()
		{
			Config::Command::access_t cmd     = _config.read<Config::Command>();
			Config::Command::access_t cmd_old = cmd;

			/* enable memory space when I/O mem is defined */
			_dev.for_each_io_mem([&] (unsigned, Driver::Device::Io_mem::Range,
			                          Driver::Device::Pci_bar, bool) {
				Config::Command::Memory_space_enable::set(cmd, 1); });

			/* enable i/o space when I/O ports are defined */
			_dev.for_each_io_port_range(
				[&] (unsigned, Driver::Device::Io_port_range::Range,
				     Driver::Device::Pci_bar) {
					Config::Command::Io_space_enable::set(cmd, 1); });

			_config.write<Config::Command>(cmd);

			/* apply different PCI quirks, bios handover etc. */
			Driver::pci_uhci_quirks(_env, _dev, _cfg, _config.range());
			Driver::pci_ehci_quirks(_env, _dev, _cfg, _config.range());
			Driver::pci_hd_audio_quirks(_cfg, _config);

			_config.write<Config::Command>(cmd_old);
		}
	};
}

#endif /* _SRC__DRIVERS__PLATFORM__PCI_HELPER_H_ */
