/*
 * \brief  Usb multiplexing component - packet stream tx rpc object
 * \author Stefan Kalkowski
 * \date   2026-08-05
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__SERVER__USB__TX_H_
#define _SRC__SERVER__USB__TX_H_

/* Genode includes */
#include <base/env.h>
#include <base/rpc_server.h>

/* local includes */
#include <dma.h>

namespace Usb {
	template <typename SESSION, typename COMPONENT> class Tx;
}


template <typename SESSION, typename COMPONENT>
class Usb::Tx
:
	public Genode::Rpc_object<typename SESSION::Tx, Tx<SESSION, COMPONENT>>
{
	private:

		Env &_env;
		COMPONENT &_session;
		Dma_alloc_result const _dma_result;
		Signal_context_capability const _sink_sigh_cap;

	public:

		Tx(Env                      &env,
		   COMPONENT                &session,
		   Dma_alloc_result          dma_result,
		   Signal_context_capability sink_sigh)
		:
			_env(env), _session(session), _dma_result(dma_result),
			_sink_sigh_cap(sink_sigh)
		{
			_env.ep().manage(*this);
		}

		~Tx()
		{
			_env.ep().dissolve(*this);
		}

		Dma_alloc_result dma_result() const {
			return _dma_result; }


		/********************************
		 ** Packet Stream TX interface **
		 ********************************/

		Genode::Dataspace_capability dataspace()
		{
			return _dma_result.convert<Dataspace_capability>(
				[&] (auto cap) { return cap; },
				[] (auto) { return Dataspace_capability(); });
		}

		void sigh_ready_to_submit(Signal_context_capability sigh) override {
			_session.acquire(sigh); }

		void sigh_ack_avail(Signal_context_capability sigh) override {
			_session.acquire(sigh); }

		Signal_context_capability sigh_ready_to_ack() {
			return _sink_sigh_cap; }

		Signal_context_capability sigh_packet_avail() {
			return _sink_sigh_cap; }
};

#endif /* _SRC__SERVER__USB__TX_H_ */
