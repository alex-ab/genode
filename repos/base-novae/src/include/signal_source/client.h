/*
 * \brief  NOVAe-specific signal-source client interface
 * \author Alexander Boettcher
 * \date   2024-11-17
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* Genode includes */
#include <base/rpc_client.h>
#include <cpu_session/cpu_session.h>
#include <signal_source/novae_signal_source.h>

/* base-internal includes */
#include <base/internal/native_thread.h>

/* NOVA includes */
#include <novae/syscalls.h>
#include <novae/util.h>
#include <novae/capability_space.h>


namespace Genode { class Signal_source_client; };


class Genode::Signal_source_client : public Rpc_client<Novae_signal_source>
{
	private:

		/**
		 * Capability referring to a NOVAe semaphore
		 */
		Native_capability _sem { };

	public:

		Signal_source_client(Cpu_session &, Capability<Signal_source> cap)
		: Rpc_client<Novae_signal_source>(static_cap_cast<Novae_signal_source>(cap))
		{
			/* request mapping of semaphore capability selector */
			Thread * myself = Thread::myself();

			auto const &exc_base = myself->native_thread().exc_pt_sel;
			request_signal_sm_cap(exc_base + Novae::PT_SEL_PAGE_FAULT,
			                      exc_base + Novae::SM_SEL_SIGNAL);

			_sem = Capability_space::import(exc_base + Novae::SM_SEL_SIGNAL);
			call<Rpc_register_semaphore>(_sem);
		}

		~Signal_source_client() { }


		/*****************************
		 ** Signal source interface **
		 *****************************/

		Signal wait_for_signal() override
		{
			using namespace Novae;

			Signal signal { };

			do {
				signal = call<Rpc_wait_for_signal>();

				if (!signal.imprint()) {
					/* block on semaphore until signal context was submitted */
					if (uint8_t res = sm_ctrl(_sem.local_name(), SEMAPHORE_DOWN))
						warning("signal reception failed - error ", res);
				}

			} while (!signal.imprint());

			return signal;
		}
};
