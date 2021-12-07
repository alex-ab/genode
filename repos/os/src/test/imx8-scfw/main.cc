/*
 * \author Alexander Boettcher
 * \date   2021-12-07
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/component.h>
#include <base/env.h>
#include <os/attached_mmio.h>

extern "C" {
#include <sci/svc/rm/api.h>
#include <asm/mach-imx/sci/rpc.h>
#include <asm/arch-imx8/imx8qm_pads.h>
#include <sci/svc/pm/rpc.h>
#include <sci/svc/pm/api.h>
#include <sci/svc/pad/api.h>
}

using namespace Genode;

struct Main
{
	Env           &env;
	Attached_mmio  mu { env, 0x5d1c0000, 0x10000 };

	void msg_error(char const * const msg, sc_err_t const err)
	{
		switch (err) {
		case SC_ERR_NONE:
			log(msg, "success"); break;
		case SC_ERR_VERSION:
			error(msg, "wrong version"); break;
		case SC_ERR_CONFIG:
			error(msg, "configuration"); break;
		case SC_ERR_PARM:
			error(msg, "bad parameter"); break;
		case SC_ERR_NOACCESS:
			error(msg, "permission error, no access"); break;
		case SC_ERR_LOCKED:
			error(msg, "permission error, locked"); break;
		case SC_ERR_UNAVAILABLE:
			error(msg, "unavailable, out of resources"); break;
		case SC_ERR_NOTFOUND:
			error(msg, "not found"); break;
		case SC_ERR_NOPOWER:
			error(msg, "no power"); break;
		case SC_ERR_IPC:
			error(msg, "general ipc error"); break;
		case SC_ERR_BUSY:
			error(msg, "busy"); break;
		case SC_ERR_FAIL:
			error(msg, "general i/o error"); break;
		default:
			error(msg, "unknown error=", err);
		}
	}

	void power_state(sc_ipc_t const ipc, sc_rsrc_t const resource,
	                 char const * const msg)
	{
		sc_err_t sc_error = SC_ERR_NONE;
		sc_pm_power_mode_t power_mode { };
		sc_error = sc_pm_get_resource_power_mode(ipc, resource, &power_mode);
		if (sc_error != SC_ERR_NONE) {
			msg_error("sc_pm_get_resource_power_mode ", sc_error);
			return;
		} else {
			switch(power_mode) {
			case SC_PM_PW_MODE_OFF:
				log(msg, "power off"); break;
			case SC_PM_PW_MODE_STBY:
				log(msg, "standby"); break;
			case SC_PM_PW_MODE_LP:
				log(msg, "low-power mode"); break;
			case SC_PM_PW_MODE_ON:
				log(msg, "powered on"); break;
			default:
				log(msg, "unknown power state"); break;
			}
		}
	}

	Main(Env &env) : env(env)
	{
		log("--- SCFW test started ---");

		/* XXX the id is used to transfer the mapped mmio address XXX */
		sc_ipc_id_t const id = reinterpret_cast<sc_ipc_id_t>(mu.local_addr<sc_ipc_id_t>());

		sc_err_t sc_error = SC_ERR_NONE;
		sc_ipc_t ipc      = 0;

		sc_error = sc_ipc_open(&ipc, id);
		if (sc_error != SC_ERR_NONE) {
			msg_error("sc_ipc_open failed ", sc_error);
			return;
		}

		power_state(ipc, SC_R_SMMU, "smmu - ");
		power_state(ipc, SC_R_ENET_0, "enet0 - ");

		{
			sc_pad_t pad   = SC_P_SCU_GPIO0_05; /* 41 */
			uint32_t value = 0xd8000021;

			sc_error = sc_pad_set(ipc, pad, value);
			if (sc_error != SC_ERR_NONE)
				msg_error("sc_pad_set ", sc_error);
		}

		{
			sc_pad_t pad   = SC_P_SCU_GPIO0_06; /* 42 */
			uint32_t value = 0xd8000021;

			sc_error = sc_pad_set(ipc, pad, value);
			if (sc_error != SC_ERR_NONE)
				msg_error("sc_pad_set ", sc_error);
		}

		log("-- test done");
	}
};


void Component::construct(Genode::Env &env) { static Main main(env); }
