/*
 * \brief  Board driver for core
 * \author Stefan Kalkowski
 * \date   2019-05-10
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__SPEC__VIRT_QEMU__BOARD_H_
#define _CORE__SPEC__VIRT_QEMU__BOARD_H_

#include <hw/spec/arm_64/virt_qemu_board.h>

namespace Board {
	using namespace Hw::Virt_qemu_board;

	static constexpr bool SMP = false;
};

#endif /* _CORE__SPEC__VIRT_QEMU__BOARD_H_ */
