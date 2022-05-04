/*
 * \brief  Post kernel activity
 * \author Alexander Boettcher
 * \date   2022-03-08
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <linux/sched/task.h>

#include <drm/drm_fb_helper.h>
#include <drm/drm_print.h>

#include "i915_drv.h"
#include "display/intel_display_types.h"
#include "display/intel_opregion.h"
#include "display/intel_panel.h"

#include "lx_emul.h"


enum { MAX_BRIGHTNESS = 100, INVALID_BRIGHTNESS = MAX_BRIGHTNESS + 1 };

struct task_struct * lx_user_task = NULL;

static struct drm_i915_private *i915 = NULL;

static struct drm_fb_helper * i915_fb(void) { return &i915->fbdev->helper; }


/*
 * Heuristic to calculate max resolution across all connectors
 */
static void preferred_mode(struct drm_display_mode *prefer)
{
	struct drm_connector          *connector       = NULL;
	struct drm_display_mode       *mode            = NULL;
	struct drm_connector_list_iter conn_iter;

	/* read Genode's config per connector */
	drm_connector_list_iter_begin(i915_fb()->dev, &conn_iter);
	drm_client_for_each_connector_iter(connector, &conn_iter) {
		struct genode_mode conf_mode = { .enabled = 1, .mirror = true };

		/* check for connector configuration on Genode side */
		lx_emul_i915_connector_config(connector->name, &conf_mode);

		if (!conf_mode.enabled)
			continue;

		if (conf_mode.id) {
			unsigned mode_id = 0;
			list_for_each_entry(mode, &connector->modes, head) {
				mode_id ++;

				if (!mode || conf_mode.id != mode_id)
					continue;

				conf_mode.width  = mode->hdisplay;
				conf_mode.height = mode->vdisplay;

				break;
			}
		}

		if (!conf_mode.width || !conf_mode.height || !conf_mode.mirror)
			continue;

		if (conf_mode.width * conf_mode.height > prefer->hdisplay * prefer->vdisplay) {
			prefer->hdisplay = conf_mode.width;
			prefer->vdisplay = conf_mode.height;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	/* if nothing was configured by Genode's config, apply heuristic */
	if (!prefer->hdisplay || !prefer->vdisplay) {
		drm_connector_list_iter_begin(i915_fb()->dev, &conn_iter);
		drm_client_for_each_connector_iter(connector, &conn_iter) {
			list_for_each_entry(mode, &connector->modes, head) {
				if (!mode)
					continue;

				if (mode->hdisplay * mode->vdisplay > prefer->hdisplay * prefer->vdisplay) {
					prefer->hdisplay = mode->hdisplay;
					prefer->vdisplay = mode->vdisplay;
				}
			}
		}
		drm_connector_list_iter_end(&conn_iter);
	}
}


static void set_brightness(unsigned brightness, struct drm_connector * connector)
{
	struct intel_connector * intel_c = to_intel_connector(connector);
	if (intel_c)
		intel_panel_set_backlight_acpi(intel_c->base.state, brightness, MAX_BRIGHTNESS);
}


static unsigned get_brightness(struct drm_connector * const connector,
                               unsigned const brightness_error)
{
	struct intel_connector * intel_c = NULL;
	struct intel_panel     * panel   = NULL;
	unsigned ret;

	if (!connector)
		return brightness_error;

	intel_c = to_intel_connector(connector);
	if (!intel_c)
		return brightness_error;

	panel = &intel_c->panel;

	if (!panel || !panel->backlight.device || !panel->backlight.device->ops ||
	    !panel->backlight.device->ops->get_brightness)
		return brightness_error;

	ret = panel->backlight.device->ops->get_brightness(panel->backlight.device);

	/* in percentage */
	return ret * MAX_BRIGHTNESS / panel->backlight.device->props.max_brightness;
}


static void * framebuffer_map(struct drm_framebuffer *fb, bool zero)
{
	struct i915_ggtt_view const view = { .type = I915_GGTT_VIEW_NORMAL };
	struct i915_vma *vma = NULL;
	unsigned long out_flags = 0u;
	void *vaddr = NULL;

	vma = intel_pin_and_fence_fb_obj(fb, false /* phys_cursor */, &view,
	                                 false /* use_fences */, &out_flags);
	if (IS_ERR(vma)) {
		return NULL;
	}

	vaddr = i915_vma_pin_iomap(vma);
	if (IS_ERR(vaddr)) {
		intel_unpin_fb_vma(vma, out_flags);
		return NULL;
	}

	if (vaddr && zero)
		memset_io(vaddr, 0,  vma->node.size);

	return vaddr;
}


static struct drm_framebuffer * allocate_framebuffer(struct drm_device * dev,
                                                     struct drm_display_mode const * const mode,
                                                     void **vaddr)
{
	struct drm_i915_private    * dev_priv = to_i915(dev);
	struct drm_mode_fb_cmd2      fb_cmd   = {};
	struct drm_i915_gem_object * obj      = ERR_PTR(-ENODEV);
	struct drm_framebuffer     * fb       = NULL;

	unsigned long const pitch = ALIGN(mode->hdisplay * DIV_ROUND_UP(32, 8), 64);
	unsigned long const size  = roundup(pitch * mode->vdisplay, PAGE_SIZE);

	if (HAS_LMEM(dev_priv)) {
		obj = i915_gem_object_create_lmem(dev_priv, size,
		                                  I915_BO_ALLOC_CONTIGUOUS);
	} else {
		obj = i915_gem_object_create_stolen(dev_priv, size);

		if (IS_ERR(obj))
			obj = i915_gem_object_create_shmem(dev_priv, size);
	}

	if (IS_ERR(obj))
		return NULL;

	fb_cmd.width        = mode->hdisplay;
	fb_cmd.height       = mode->vdisplay;
	fb_cmd.pixel_format = DRM_FORMAT_XRGB8888;
	fb_cmd.pitches[0]   = pitch;

	fb = intel_framebuffer_create(obj, &fb_cmd);
	if (IS_ERR(fb)) {
		i915_gem_object_put(obj);
		return NULL;
	}

	*vaddr = framebuffer_map(fb, true);
	if (!*vaddr) {
		drm_framebuffer_remove(fb);
		i915_gem_object_put(obj);
		return NULL;
	}

	i915_gem_object_put(obj);

	return fb;
}


static struct drm_framebuffer * probe_framebuffer(struct drm_device * dev,
                                                  struct drm_framebuffer *fb,
                                                  struct drm_display_mode const * const mode,
                                                  void **vaddr)
{
	if (fb && (mode->hdisplay > fb->width || mode->vdisplay > fb->height)) {
		drm_framebuffer_put(fb);
		fb = NULL;
	}

	if (!fb)
		fb = allocate_framebuffer(dev, mode, vaddr);
	else {
		*vaddr = framebuffer_map(fb, false);
	}

	return fb;
}


static struct drm_framebuffer * lookup_framebuffer(struct drm_crtc *crtc,
                                                   struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_plane_state  *plane;
	struct drm_crtc_state   *crtc_state;

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state)
		return NULL;

	state->acquire_ctx = ctx;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		drm_atomic_state_put(state);
		return NULL;
	}

	plane = drm_atomic_get_plane_state(state, crtc->primary);

	drm_atomic_state_put(state);

	return plane ? plane->fb : NULL;
}


struct drm_framebuffer * fb_of_screen(struct genode_mode  const * const conf_mode,
                                      struct drm_mode_set const * const set,
                                      struct fb_info            * const fb_info,
                                      struct drm_framebuffer    * const fb_mirror,
                                      struct drm_display_mode const * const mode,
                                      struct drm_modeset_acquire_ctx * ctx,
                                      struct drm_connector const * const connector)
{
	struct drm_framebuffer *fb         = NULL;
	void                   *fb_mapped  = NULL;
	bool const              mirror     = conf_mode->mirror;

	fb = lookup_framebuffer(set->crtc, ctx);

	/* notify genode side about switch from connector specific fb to mirror fb */
	if (fb && mirror && fb != fb_mirror) {
		struct fb_info info = {};

		drm_framebuffer_put(fb);

		info.var.bits_per_pixel = 32;
		info.node = connector->index;
		register_framebuffer(&info);

		fb = fb_mirror;
	}

	if (!conf_mode->enabled)
		return fb;

	if (!mirror && (fb == fb_mirror || !fb))
		fb = probe_framebuffer(i915_fb()->dev, NULL, mode, &fb_mapped);
	else
		fb = probe_framebuffer(i915_fb()->dev, fb, mode, &fb_mapped);

	if (!fb || fb == fb_mirror) {
		/* keep physical dimension, which may be different from mode-> */
		*fb_info = *i915_fb()->fbdev;
	} else {
		fb_info->var.bits_per_pixel = 32;
		fb_info->fix.line_length    = ALIGN(mode->hdisplay * DIV_ROUND_UP(32, 8), 64);
		fb_info->var.xres           = mode->hdisplay;
		fb_info->var.yres           = mode->vdisplay;
	}

	fb_info->node             = connector->index;
	fb_info->screen_base      = fb_mapped;
	fb_info->var.xoffset      = conf_mode->screen_offsetx;
	fb_info->var.xres_virtual = mode->hdisplay;
	fb_info->var.yres_virtual = mode->vdisplay;

	return fb ? fb : fb_mirror;
}


static bool reconfigure(void * data)
{
	struct drm_display_mode *mode             = NULL;
	struct drm_framebuffer  *fb_mirror        = NULL;
	struct drm_mode_set     *mode_set         = NULL;
	struct drm_display_mode  mode_preferred   = {};
	struct fb_info           fb_info_mirror   = {};
	unsigned                 split_screens    = 0;
	bool                     report_fb_mirror = false;
	bool                     retry            = false;

	if (!i915_fb())
		return retry;

	BUG_ON(!i915_fb()->funcs);
	BUG_ON(!i915_fb()->funcs->fb_probe);

	preferred_mode(&mode_preferred);

	if (mode_preferred.hdisplay && mode_preferred.vdisplay) {
		unsigned err = 0;
		struct drm_fb_helper_surface_size sizes = {};

		sizes.surface_depth  = 24;
		sizes.surface_bpp    = 32;
		sizes.fb_width       = mode_preferred.hdisplay;
		sizes.fb_height      = mode_preferred.vdisplay;
		sizes.surface_width  = sizes.fb_width;
		sizes.surface_height = sizes.fb_height;

		err = (*i915_fb()->funcs->fb_probe)(i915_fb(), &sizes);
		/* i915_fb()->fb contains adjusted drm_framebuffer object */
	}

	if (!i915_fb()->fbdev)
		return retry;

	fb_mirror = i915_fb()->fb;
	if (!fb_mirror)
		return retry;

	drm_client_for_each_modeset(mode_set, &(i915_fb()->client)) {
		struct drm_display_mode *mode_match = NULL;
		unsigned                 mode_id    = 0;
		struct drm_connector    *connector  = NULL;

		struct genode_mode conf_mode = { .enabled    = 1,
		                                 .mirror     = true,
		                                 .brightness = INVALID_BRIGHTNESS };

		if (!mode_set->connectors || !*mode_set->connectors)
			continue;

		BUG_ON(!mode_set->crtc);

		/* set connector */
		connector = *mode_set->connectors;

		/* read configuration of connector */
		lx_emul_i915_connector_config(connector->name, &conf_mode);

		/* heuristics to find matching mode */
		list_for_each_entry(mode, &connector->modes, head) {
			mode_id ++;

			if (!mode)
				continue;

			/* use mode id if configured and matches exactly */
			if (conf_mode.id) {
				if (conf_mode.id != mode_id)
					continue;

				mode_match = mode;
				break;
			}

			/* if invalid, mode is configured in second loop below */
			if (conf_mode.width == 0 || conf_mode.height == 0) {
				break;
			}

			/* no exact match by mode id -> try matching by size */
			if ((mode->hdisplay != conf_mode.width) ||
			    (mode->vdisplay != conf_mode.height))
				continue;

			/* take as default any mode with matching resolution */
			if (!mode_match) {
				mode_match = mode;
				continue;
			}

			/* replace matching mode iif hz matches exactly */
			if ((conf_mode.hz != drm_mode_vrefresh(mode_match)) &&
			    (conf_mode.hz == drm_mode_vrefresh(mode)))
				mode_match = mode;
		}

		/* apply new mode */
		mode_id = 0;
		list_for_each_entry(mode, &connector->modes, head) {
			struct drm_mode_set     set              = {};
			struct fb_info          fb_info          = {};
			struct drm_framebuffer *fb_connector     = fb_mirror;
			int                     err              = -1;
			bool                    no_match         = !mode_match;

			mode_id ++;

			if (!mode)
				continue;

			/* no matching mode ? */
			if (!mode_match) {
				/* use first mode */
				mode_match = mode;

				if (conf_mode.enabled)
					no_match = true;
			}

			if (mode_match != mode)
				continue;

			set.crtc = mode_set->crtc;

			if (set.crtc->funcs && set.crtc->funcs->set_config &&
			    drm_drv_uses_atomic_modeset(i915_fb()->dev)) {

				struct drm_modeset_acquire_ctx ctx;

				DRM_MODESET_LOCK_ALL_BEGIN(i915_fb()->dev, ctx,
				                           DRM_MODESET_ACQUIRE_INTERRUPTIBLE,
				                           err);

				/* check for mirrored fb or specific one for connector */
				fb_connector = fb_of_screen(&conf_mode, &set, &fb_info,
				                            fb_mirror, mode, &ctx, connector);

				set.x              = 0;
				set.y              = 0;
				set.mode           = conf_mode.enabled ? mode : NULL;
				set.connectors     = &connector;
				set.num_connectors = conf_mode.enabled ? 1 : 0;
				set.fb             = conf_mode.enabled ? fb_connector : NULL;

				err = set.crtc->funcs->set_config(&set, &ctx);

				if (!err && conf_mode.enabled && conf_mode.brightness <= MAX_BRIGHTNESS)
					set_brightness(conf_mode.brightness, connector);

				DRM_MODESET_LOCK_ALL_END(i915_fb()->dev, ctx, err);

				if (!err && conf_mode.mirror) {
					if (!report_fb_mirror) {
						report_fb_mirror = true;
						/* use fb_info of first mirrored screen */
						fb_info_mirror = fb_info;
					}

					/* report forced resolution */
					if (conf_mode.preferred) {
						fb_info_mirror.var.xres_virtual = conf_mode.width;
						fb_info_mirror.var.yres_virtual = conf_mode.height;
					}
				}

				if (!retry)
					retry = !!err;
			}

			printk("%s: %s name='%s' mode_id=%u%s %ux%u@%u%s",
			       connector->name ? connector->name : "unnamed",
			       conf_mode.enabled ? " enable" : "disable",
			       mode->name ? mode->name : "noname", mode_id,
			       conf_mode.mirror ? " mirrored" : " extended",
			       mode->hdisplay, mode->vdisplay,
			       drm_mode_vrefresh(mode), (err || no_match) ? "" : "\n");

			if (no_match)
				printk(" - no mode match: %ux%u\n",
				       conf_mode.width,
				       conf_mode.height);
			if (err)
				printk(" - failed, error=%d\n", err);

			if (!err && !conf_mode.mirror) {
				split_screens ++;

				register_framebuffer(&fb_info);
			}

			break;
		}
	}

	{
		/* report disconnected connectors to potentially shrink virtual screen */
		struct drm_connector_list_iter conn_iter;
		struct drm_connector *connector = NULL;

		drm_connector_list_iter_begin(i915_fb()->dev, &conn_iter);
		drm_client_for_each_connector_iter(connector, &conn_iter) {
			if (connector->status != connector_status_connected) {
				struct fb_info fb_info = {};
				fb_info.var.bits_per_pixel = 32;
				fb_info.node = connector->index;
				register_framebuffer(&fb_info);
			}
		}
		drm_connector_list_iter_end(&conn_iter);
	}

	if (report_fb_mirror) {
		/* ignore configured offsets if no split screens are actually in use */
		if (split_screens == 0)
			fb_info_mirror.var.xoffset = 0;
		register_framebuffer(&fb_info_mirror);
	}

	return retry;
}


static int configure_connectors(void * data)
{
	unsigned retry_count = 0;

	while (true) {
		bool retry = reconfigure(data);

		if (retry && retry_count < 3) {
			retry_count ++;

			printk("retry applying configuration in 1s\n");
			msleep(1000);
			continue;
		}

		retry_count = 0;

		lx_emul_task_schedule(true);
	}

	return 0;
}


void lx_user_init(void)
{
	int pid = kernel_thread(configure_connectors, NULL, CLONE_FS | CLONE_FILES);
	lx_user_task = find_task_by_pid_ns(pid, NULL);;
}


static int genode_fb_client_hotplug(struct drm_client_dev *client)
{
	/*
	 * Set deferred_setup to execute codepath of drm_fb_helper_hotplug_event()
	 * on next connector state change that does not drop modes, which are
	 * above the current framebuffer resolution. It is required if the
	 * connected display at runtime is larger than the ones attached already
	 * during boot. Without this quirk, not all modes are reported on displays
	 * connected after boot.
	 */
	i915_fb()->deferred_setup = true;

	lx_emul_i915_hotplug_connector(client);
	return 0;
}


void lx_emul_i915_report(void * lx_data, void * genode_data)
{
	struct drm_client_dev *client = lx_data;

	struct drm_connector_list_iter conn_iter;

	struct drm_device const *dev       = client->dev;
	struct drm_connector    *connector = NULL;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_client_for_each_connector_iter(connector, &conn_iter) {
		lx_emul_i915_report_connector(connector, genode_data,
		                              connector->name,
		                              connector->status != connector_status_disconnected,
		                              get_brightness(connector, INVALID_BRIGHTNESS));
	}
	drm_connector_list_iter_end(&conn_iter);
}


void lx_emul_i915_iterate_modes(void * lx_data, void * genode_data)
{
	struct drm_connector    *connector = lx_data;
	struct drm_display_mode *mode      = NULL;
	struct drm_display_mode *prev_mode = NULL;
	unsigned                 mode_id   = 0;

	list_for_each_entry(mode, &connector->modes, head) {
		bool skip = false;

		mode_id ++;

		if (!mode)
			continue;

		/* skip duplicates - actually not really, some parameters varies ?! */
		if (prev_mode) {
			skip = (mode->hdisplay == prev_mode->hdisplay) &&
			       (mode->vdisplay == prev_mode->vdisplay) &&
			       (drm_mode_vrefresh(mode) == drm_mode_vrefresh(prev_mode)) &&
			       !strncmp(mode->name, prev_mode->name, DRM_DISPLAY_MODE_LEN);
		}

		if (!skip) {
			struct genode_mode conf_mode = { .width = mode->hdisplay,
			                                 .height = mode->vdisplay,
			                                 .preferred = mode->type & (DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DEFAULT),
			                                 .hz = drm_mode_vrefresh(mode),
			                                 .id = mode_id,
			                               };

			static_assert(sizeof(conf_mode.name) == DRM_DISPLAY_MODE_LEN);
			memcpy(conf_mode.name, mode->name, sizeof(conf_mode.name));

			lx_emul_i915_report_modes(genode_data, &conf_mode);
		}

		prev_mode = mode;
	}
}


static const struct drm_client_funcs drm_fbdev_client_funcs = {
	.owner		= THIS_MODULE,
	.hotplug	= genode_fb_client_hotplug,
};


static void hotplug_setup(struct drm_device *dev)
{
	struct drm_fb_helper *hotplug_helper;
	int ret;

	hotplug_helper = kzalloc(sizeof(*hotplug_helper), GFP_KERNEL);
	if (!hotplug_helper) {
		drm_err(dev, "Failed to allocate fb_helper\n");
		return;
	}

	ret = drm_client_init(dev, &hotplug_helper->client, "fbdev",
	                      &drm_fbdev_client_funcs);
	if (ret) {
		kfree(hotplug_helper);
		drm_err(dev, "Failed to register client: %d\n", ret);
		return;
	}

	hotplug_helper->preferred_bpp = 32;

	ret = genode_fb_client_hotplug(&hotplug_helper->client);
	if (ret)
		drm_dbg_kms(dev, "client hotplug ret=%d\n", ret);

	drm_client_register(&hotplug_helper->client);

	hotplug_helper->dev = dev;
}


int i915_switcheroo_register(struct drm_i915_private *i915_private)
{
	/* get hold of the function pointers we need for mode setting */
	i915 = i915_private;

	/* register dummy fb_helper to get notifications about hotplug events */
	hotplug_setup(&i915_private->drm);

	return 0;
}


void i915_switcheroo_unregister(struct drm_i915_private *i915)
{
	lx_emul_trace_and_stop(__func__);
}
