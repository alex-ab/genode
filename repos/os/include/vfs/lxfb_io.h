/*
 * \brief   IOCTLs for Linux framebuffer devices
 * \author  Martin Stein
 * \date    2016-02-10
 */

/*
 * Copyright (C) 2016 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#ifndef _INCLUDE__VFS__LXFB_IO_H_
#define _INCLUDE__VFS__LXFB_IO_H_

/**
 * IOCTLs for Linux framebuffer devices
 */
enum {
	FBIOGET_VSCREENINFO=17920,
	FBIOGET_FSCREENINFO=17922,
};

#endif /* _INCLUDE__VFS__LXFB_IO_H_ */
