/**
 * \brief  Generic EGL-DRI2 back end
 * \author Sebastian Sumpf
 * \date   2017-08-17
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/*
 * Mesa
 */
#include <egl_dri2.h>

/*
 * Libc
 */
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

EGLBoolean dri2_initialize_genode(_EGLDisplay *disp)
{
	printf("%s:%d\n", __func__, __LINE__);
	while (1) ;
	return false;
}

EGLBoolean
dri2_initialize_surfaceless(_EGLDisplay *disp)
{
	printf("%s:%d\n", __func__, __LINE__);
	while (1) ;
	return false;
}
