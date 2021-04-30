/**
 * \brief  etnaviv (Vivante) EGL-DRI2 back end
 * \author Sebastian Sumpf
 * \date   2021-04-30
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */
/*
 * Mesa
 */
#include <egl_dri2.h>
#include <drivers/dri/common/utils.h>
/*
 * Libc
 */
#include <string.h>
#include <dlfcn.h>

/*
 * Local
 */
#include <platform.h>

static int stride(int value)
{
	/* 32-bit RGB888 */
	return value * 4;
}


static EGLBoolean
dri2_genode_etnaviv_swap_buffers(_EGLDisplay *disp, _EGLSurface *draw)
{
	return EGL_FALSE;
}


static struct dri2_egl_display_vtbl dri2_genode_display_vtbl = {
	.authenticate = NULL,
	.create_window_surface = dri2_genode_create_window_surface,
	.create_pixmap_surface = dri2_genode_create_pixmap_surface,
	.destroy_surface = dri2_genode_destroy_surface,
	.swap_interval = dri2_genode_swap_interval,
	.swap_buffers = dri2_genode_etnaviv_swap_buffers,
	.get_dri_drawable = dri2_surface_get_dri_drawable,
};


static void
dri2_genode_etnaviv_get_image(__DRIdrawable * read,
                             int x, int y, int w, int h,
                             char *data, void *loaderPrivate)
{
	struct dri2_egl_surface *dri2_surf  = loaderPrivate;
	struct Genode_egl_window  *window   = dri2_surf->g_win;
	unsigned char * src                 = window->addr;

	int src_stride = stride(dri2_surf->base.Width);
	int copy_width = stride(w);
	int x_offset = stride(x);
	int dst_stride = copy_width;

	assert(data != (char *)src);

	src += x_offset;
	src += y * src_stride;

	/* copy width over stride boundary */
	if (copy_width > src_stride - x_offset)
		copy_width = src_stride - x_offset;

	/* limit height */
	if (h > dri2_surf->base.Height - y)
		h = dri2_surf->base.Height - y;

	/* copy to surface */
	genode_blit(src, src_stride, data, dst_stride, copy_width, h);
}


static void
dri2_genode_etnaviv_put_image(__DRIdrawable * draw, int op,
                             int x, int y, int w, int h,
                             char *data, void *loaderPrivate)
{
}


static void
dri2_genode_etnaviv_get_drawable_info(__DRIdrawable * draw,
                                 int *x, int *y, int *w, int *h,
                                 void *loaderPrivate)
{
	struct dri2_egl_surface *dri2_surf = loaderPrivate;

	struct Genode_egl_window  *window   = dri2_surf->g_win;

	*x = 0;
	*y = 0;
	*w = window->width;
	*h = window->height;

	dri2_surf->base.Width  = window->width;
	dri2_surf->base.Height = window->height;

}


/*
static const __DRIetnavivLoaderExtension etnaviv_loader_extension = {
	.base = { __DRI_SWRAST_LOADER, 1 },

	.getDrawableInfo = dri2_genode_etnaviv_get_drawable_info,
	.putImage        = dri2_genode_etnaviv_put_image,
	.getImage        = dri2_genode_etnaviv_get_image
};

static const __DRIextension *etnaviv_loader_extensions[] = {
	&etnaviv_loader_extension.base,
	NULL,
	NULL,
};
*/

static EGLBoolean
dri2_initialize_genode_etnaviv(_EGLDisplay *disp)
{
	return EGL_FALSE;
}


EGLBoolean dri2_initialize_genode_backend(_EGLDisplay *disp)
{
	return  EGL_FALSE;
}
