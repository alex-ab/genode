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

#ifndef _SRC__DRIVERS__PLATFORM__SPEC__ARM__SCFW_H_
#define _SRC__DRIVERS__PLATFORM__SPEC__ARM__SCFW_H_

/* Genode includes */
#include <base/env.h>
#include <os/attached_mmio.h>

extern "C" {
#include <sci/svc/rm/api.h>
#include <asm/mach-imx/sci/rpc.h>
#include <asm/arch-imx8/imx8qm_pads.h>
#include <sci/svc/irq/api.h>
#include <sci/svc/pm/rpc.h>
#include <sci/svc/pm/api.h>
#include <sci/svc/pad/api.h>
}

using namespace Genode;

namespace Driver { struct Scfw; }

struct Driver::Scfw
{
	Driver::Env   &env;
	Attached_mmio  mu { env.env, 0x5d1c0000, 0x10000 };

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
				log(msg, "unknown power state ", power_mode); break;
			}
		}
	}

	void sc_init_irqs(sc_ipc_t const ipc)
	{
		sc_err_t sc_error = SC_ERR_NONE;

		sc_error = sc_irq_enable(ipc, SC_R_MU_1A, SC_IRQ_GROUP_TEMP,
		                         SC_IRQ_TEMP_PMIC0_HIGH, true);
		if (sc_error)
			error("PMIC0_TEMP interrupt");

		sc_error = sc_irq_enable(ipc, SC_R_MU_1A, SC_IRQ_GROUP_TEMP,
		                         SC_IRQ_TEMP_PMIC1_HIGH, true);

		if (sc_error)
			error("PMIC1_TEMP interrupt");
		sc_error = sc_irq_enable(ipc, SC_R_MU_1A, SC_IRQ_GROUP_RTC,
		                         SC_IRQ_RTC, true);

		if (sc_error)
			error("ALARM_RTC interrupt ", sc_error);

		sc_error = sc_irq_enable(ipc, SC_R_MU_1A, SC_IRQ_GROUP_WAKE,
		                         SC_IRQ_BUTTON, true);
		if (sc_error)
			error("ON/OFF interrupt ", sc_error);

		sc_error = sc_irq_enable(ipc, SC_R_MU_1A, SC_IRQ_GROUP_WDOG,
		                         SC_IRQ_WDOG, true);
		if (sc_error)
			error("WDOG interrupt ", sc_error);

		sc_error = sc_irq_enable(ipc, SC_R_MU_1A, SC_IRQ_GROUP_WAKE,
		                         SC_IRQ_PAD, true);
		if (sc_error)
			error("PAD interrupt ", sc_error);
	}

	Scfw(Env &env) : env(env)
	{
		/* XXX the id is used to transfer the mapped mmio address XXX */
		sc_ipc_id_t const id = reinterpret_cast<sc_ipc_id_t>(mu.local_addr<sc_ipc_id_t>());

		sc_err_t sc_error = SC_ERR_NONE;
		sc_ipc_t ipc      = 0;

		sc_error = sc_ipc_open(&ipc, id);
		if (sc_error != SC_ERR_NONE) {
			msg_error("sc_ipc_open failed ", sc_error);
			return;
		}

		power_state(ipc, SC_R_SMMU,   "smmu - ");
		power_state(ipc, SC_R_ENET_0, "enet0 - ");

		sc_init_irqs(ipc);

		power_state(ipc, SC_R_SMMU,   "smmu - ");
		power_state(ipc, SC_R_ENET_0, "enet0 - ");

/*
		sc_error = sc_pm_set_resource_power_mode(ipc, SC_R_SMMU,
		                                         SC_PM_PW_MODE_ON);
//		                                         SC_PM_PW_MODE_OFF);
//		                                         SC_PM_PW_MODE_LP);
		if (sc_error != SC_ERR_NONE)
			msg_error("sc_pm_set_resource ", sc_error);
*/

/*
		sc_error = sc_pm_set_resource_power_mode(ipc, SC_R_ENET_0,
//		                                         SC_PM_PW_MODE_ON);
		                                         SC_PM_PW_MODE_OFF);
//		                                         SC_PM_PW_MODE_LP);
		if (sc_error != SC_ERR_NONE)
			msg_error("sc_pm_set_resource ", sc_error);
*/

/*
		sc_error = sc_pm_set_resource_power_mode(ipc, SC_R_ENET_0,
		                                         SC_PM_PW_MODE_ON);
//		                                         SC_PM_PW_MODE_OFF);
//		                                         SC_PM_PW_MODE_LP);
		if (sc_error != SC_ERR_NONE)
			msg_error("sc_pm_set_resource ", sc_error);
*/
		power_state(ipc, SC_R_SMMU,   "smmu - ");
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
	}
};

#endif /* _SRC__DRIVERS__PLATFORM__SPEC__ARM__SCFW_H_ */
