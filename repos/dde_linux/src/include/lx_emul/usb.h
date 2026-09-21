/*
 * \brief  Lx_emul support for USB
 * \author Stefan Kalkowski
 * \date   2022-03-08
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#ifndef _LX_EMUL__USB_H_
#define _LX_EMUL__USB_H_

#include <genode_c_api/usb.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool lx_emul_usb_release_device(genode_usb_bus_num_t bus,
                                       genode_usb_dev_num_t dev);

extern bool lx_emul_usb_release_interface(genode_usb_bus_num_t bus,
                                          genode_usb_dev_num_t dev,
                                          genode_usb_intf_num_t ifnum);

#ifdef __cplusplus
}
#endif

#endif /* _LX_EMUL__USB_H_ */
