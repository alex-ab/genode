/*
 * \brief  Intel XE framebuffer support
 * \author Alexander Boettcher
 * \date   2026-10-30
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#define KBUILD_MODNAME "genode_xe_user_driver"

#include "lx_register_fb.h"


int lx_xe_register_fb(struct drm_client_dev const * const dev,
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
	return lx_register_fb(dev, info, fb, dumb_fb, width_mm, height_mm, rotate,
	                      flip, vma, vma_flags);
}


void lx_xe_unpin_vma(struct i915_vma * vma, unsigned long vma_flags)
{
	lx_intel_fb_unpin_vma(vma, vma_flags);
}
