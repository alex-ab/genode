/*
 * \brief  Post kernel activity
 * \author Alexander Boettcher
 * \date   2022-03-08
 */

/*
 * Copyright (C) 2022-2026 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <linux/fb.h> /* struct fb_info */
#include <drm/drm_client.h>

#include "i915_drv.h"
#include "i915_vma.h"

#include "display/intel_display_core.h"
#include "display/intel_display_rpm.h"
#include "display/intel_display_types.h"
#include "display/intel_fb.h"
#include "display/intel_fb_pin.h"
#include "display/intel_fbdev_fb.h"


static int lx_register_fb(struct drm_client_dev const * const dev,
                          struct fb_info              * const info,
                          struct drm_framebuffer      * const fb,
                          struct drm_mode_fb_cmd2     * const dumb_fb,
                          unsigned                      const width_mm,
                          unsigned                      const height_mm,
                          unsigned                      const rotate,
                          bool                          const flip,
                          struct i915_vma            ** const vma,
                          unsigned long               * const vma_flags)
{
	struct i915_gtt_view const  view     = { .type = I915_GTT_VIEW_NORMAL };
	struct drm_i915_private   * dev_priv = to_i915(dev->dev);
	struct intel_display      * display  = to_intel_display(dev->dev);
	struct intel_framebuffer  * ifb      = to_intel_framebuffer(fb);
	struct drm_gem_object     * obj      = ifb ? intel_fb_bo(&ifb->base) : NULL;
	struct ref_tracker        * wakeref  = NULL;
	int                         result   = -EINVAL;

	if (!info || !fb || !dev_priv || !vma || !vma_flags || !display || !ifb || !obj) {
		printk("%s:%u error setting up info and fb\n", __func__, __LINE__);
		return -EINVAL;
	}

	/* acquire wakeref */
	wakeref = intel_display_rpm_get(display);

	/* check and release still existing vma region */
	if (*vma) {
		intel_fb_unpin_vma(*vma, *vma_flags);

		*vma       = NULL;
		*vma_flags = 0;
	}

	*vma = intel_fb_pin_to_ggtt(fb, &view, ifb->min_alignment, 0, 0, false, vma_flags);

	if (IS_ERR(*vma)) {
		/* release wakeref */
		intel_display_rpm_put(display, wakeref);

		result     = PTR_ERR(*vma);
		*vma       = NULL;
		*vma_flags = 0;

		return result;
	}

	result = intel_fbdev_fb_fill_info(display, info, obj, *vma);

	/* release wakeref */
	intel_display_rpm_put(display, wakeref);

	if (result)
		return result;

	/* Genode side uses this pointer to copy framebuffer data in */
	if (!info->screen_base)
		return -EINVAL;

	/* fill framebuffer info for kernel_register_fb */
	info->fix.line_length    = fb->pitches[0];
	info->var.bits_per_pixel = drm_format_info_bpp(fb->format, 0);

	return 0;
}


static void lx_intel_fb_unpin_vma(struct i915_vma * vma, unsigned long vma_flags)
{
	intel_fb_unpin_vma(vma, vma_flags);
}


static int lx_i915_register_fb(struct drm_client_dev const * const dev,
                               struct fb_info              * const info,
                               struct drm_framebuffer      * const fb,
                               struct drm_mode_fb_cmd2     * const dumb_fb,
                               unsigned                      const width_mm,
                               unsigned                      const height_mm,
                               unsigned                      const rotate,
                               bool                          const flip,
                               struct i915_vma            ** const vma,
                               unsigned long               * const vma_flags)
{
	return lx_register_fb(dev, info, fb, dumb_fb,
	                      width_mm, height_mm, rotate, flip, vma, vma_flags);
}


static void lx_i915_unpin_vma(struct i915_vma * vma, unsigned long vma_flags)
{
	lx_intel_fb_unpin_vma(vma, vma_flags);
}


/* provided by lx_xe_fb.c in pc_intel_xe.so */
int lx_xe_register_fb(struct drm_client_dev const * const dev,
                      struct fb_info              * const info,
                      struct drm_framebuffer      * const fb,
                      struct drm_mode_fb_cmd2     * const dumb_fb,
                      unsigned                      const width_mm,
                      unsigned                      const height_mm,
                      unsigned                      const rotate,
                      bool                          const flip,
                      struct i915_vma            ** const vma,
                      unsigned long               * const vma_flags);
/* provided by lx_xe_fb.c in pc_intel_xe.so */
void lx_xe_unpin_vma(struct i915_vma * vma, unsigned long vma_flags);
