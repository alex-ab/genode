/*
 * \brief  Novae-specific instance of the IRQ object
 * \author Alexander Boettcher
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

#include <base/thread.h>
#include <novae/syscall-generic.h> /* Gsi_flags */

/* core includes */
#include <types.h>

namespace Core { class Irq_object; class Irq_args; }


class Core::Irq_object : public Thread
{
	private:

		Signal_context_capability _sigh_cap { };

		Irq_session::Type _irq_type { };

		addr_t const _kernel_caps;

		addr_t _msi_addr { };
		addr_t _msi_data { };
		addr_t _bdf      { }; /* PCI bdf */

		Novae::Gsi_flags _gsi_flags { };

		Blockade _wait_for_ack { };

		addr_t irq_sel() const { return _kernel_caps; }

		void entry() override;

	public:

		Irq_object();
		~Irq_object();

		addr_t msi_address() const { return _msi_addr; }
		addr_t msi_value()   const { return _msi_data; }

		void sigh(Signal_context_capability cap);
		void ack_irq();

		Thread::Start_result start(unsigned irq, addr_t, Irq_args const &);

		Thread::Start_result start() override;
};
