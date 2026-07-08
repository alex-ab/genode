/*
 * \brief  Linux emulation environment: input event sink
 * \author Christian Helmuth
 * \date   2022-06-23
 *
 * This implementation is derived from drivers/input/evbug.c and
 * drivers/input/evdev.c.
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <lx_emul.h>
#include <lx_emul/event.h>
#include <genode_c_api/event.h>

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/init.h>
#include <linux/device.h>

/*
 * Input devices with motion events
 *
 * (from Documentation/input/event-codes.rst and multi-touch-protocol.rst)
 *
 * The INPUT_PROP_DIRECT property indicates that device coordinates should be
 * directly mapped to screen coordinates (not taking into account trivial
 * transformations, such as scaling, flipping and rotating).
 * -> touchscreen, tablet (stylus/pen)
 *
 * Non-direct input devices may require non-trivial transformation, such as
 * absolute to relative transformation.
 * -> mouse, touchpad
 *
 * Historically a touch device with BTN_TOOL_FINGER and BTN_TOUCH was
 * interpreted as a touchpad by userspace, while a similar device without
 * BTN_TOOL_FINGER was interpreted as a touchscreen. For backwards
 * compatibility with current userspace it is recommended to follow this
 * distinction.
 *
 * In Linux, stylus/pen tool proximity is reported by BTN_TOOL_PEN/RUBBER plus
 * ABS_DISTANCE events. The actual contact to the surface emits an additional
 * BTN_TOUCH event. For multi-touch devices, the "tool" is also reported via
 * BTN_TOOL_FINGER/DOUBLETAP etc.
 *
 * Thus, these devices must be differentiated.
 *
 *   Mouse:       relative motion
 *   Pointer:     absolute motion (Qemu usb-tablet and IP-KVM devices)
 *   Touchpad:    relative motion via absolute touchpad coordinates
 *   Touchtool:   absolute motion (e.g., stylus)
 *   Touchscreen: absolute motion and finger (multi-) touch
 */

static bool is_rel_dev(struct input_dev *dev)
{
	return test_bit(EV_REL, dev->evbit) && test_bit(REL_X, dev->relbit);
}

static bool is_abs_dev(struct input_dev *dev)
{
	return test_bit(EV_ABS, dev->evbit) && test_bit(ABS_X, dev->absbit);
}

static bool is_touch_dev(struct input_dev *dev)
{
	return test_bit(BTN_TOUCH, dev->keybit);
}

static bool is_tool_dev(struct input_dev *dev)
{
	return test_bit(BTN_TOOL_PEN,      dev->keybit)
	    || test_bit(BTN_TOOL_RUBBER,   dev->keybit)
	    || test_bit(BTN_TOOL_BRUSH,    dev->keybit)
	    || test_bit(BTN_TOOL_PENCIL,   dev->keybit)
	    || test_bit(BTN_TOOL_AIRBRUSH, dev->keybit)
	    || test_bit(BTN_TOOL_MOUSE,    dev->keybit)
	    || test_bit(BTN_TOOL_LENS,     dev->keybit);
}

enum evdev_motion {
	MOTION_NONE,
	MOTION_MOUSE,       /* relative motion */
	MOTION_POINTER,     /* absolute motion */
	MOTION_TOUCHPAD,    /* relative motion based on absolute axes */
	MOTION_TOUCHTOOL,   /* absolute motion */
	MOTION_TOUCHSCREEN, /* absolute motion */
};

static enum evdev_motion evdev_motion(struct input_dev const *dev)
{
	if (is_rel_dev(dev))
		return MOTION_MOUSE;

	if (!is_abs_dev(dev))
		return MOTION_NONE;

	if (!is_touch_dev(dev))
		return MOTION_POINTER;

	if (test_bit(BTN_TOOL_FINGER, dev->keybit))
		return MOTION_TOUCHPAD;

	if (is_tool_dev(dev))
		return MOTION_TOUCHTOOL;

	return MOTION_TOUCHSCREEN;
}

struct evdev_mt_pos
{
	bool set;   /* value was set */
	int  value; /* actual value */
};

static void reset_mt_pos(struct evdev_mt_pos *p)
{
	p->set = false;
}

static void set_mt_pos(struct evdev_mt_pos *p, int v)
{
	p->set   = true;
	p->value = v;
}

struct evdev_mt_slot
{
	bool pending;
	bool touch;
	int  finger;

	struct evdev_mt_pos x, y, ox, oy;
};

static void reset_mt_slot(struct evdev_mt_slot *s)
{
	*s = (struct evdev_mt_slot){ false, false, -1, { }, { }, { }, { } };
}

static void update_mt_slot(struct evdev_mt_slot *s)
{
	if (s->x.set) set_mt_pos(&s->ox, s->x.value);
	if (s->y.set) set_mt_pos(&s->oy, s->y.value);
}

static bool pending_mt_slot(struct evdev_mt_slot *s)
{
	return s->pending && s->x.set && s->y.set;
}


/*
 * Maximum number of touch slots supported.
 *
 * Many Linux drivers report 2 to 10 slots, the Magic Trackpad reports 16. The
 * Surface driver reports 64, which we just ignore.
 */
enum { MAX_MT_SLOTS = 16 };

struct evdev_mt
{
	unsigned             pending;
	unsigned             num_slots;
	unsigned             cur_slot;
	struct evdev_mt_slot slots[MAX_MT_SLOTS];
};

#define array_for_each_element(element, array) \
	for ((element) = (array); \
	     (element) < ((array) + ARRAY_SIZE((array))); \
	     (element)++)

#define for_each_mt_slot(slot, mt) \
	array_for_each_element(slot, (mt)->slots)

static void charge_mt_slot(struct evdev_mt *mt, struct evdev_mt_slot *s)
{
	if (!s->pending) {
		s->pending = true;
		mt->pending++;
	}
}

static void complete_mt_slot(struct evdev_mt *mt, struct evdev_mt_slot *s)
{
	if (s->pending) {
		s->pending = false;
		mt->pending--;
	}
}


struct evdev_key
{
	bool     pending;
	unsigned code;
	bool     press;

	typeof(jiffies) jiffies;
};

static void reset_key(struct evdev_key *k)
{
	*k = (struct evdev_key){ false, 0, false };
}

static void set_key(struct evdev_key *k, unsigned code, bool press)
{
	*k = (struct evdev_key){ true, code, press, jiffies };
}

struct evdev_keys
{
	unsigned         pending; /* pending keys counter */
	struct evdev_key key[16]; /* max 16 keys per packet */
};

#define for_each_key(key, keys, pending_only) \
	array_for_each_element(key, (keys)->key) \
		if (!(pending_only) || (key)->pending)

#define for_each_pending_key(key, keys) \
	if ((keys)->pending) \
		for_each_key(key, keys, true)

static void charge_key(struct evdev_keys *keys, unsigned code, bool press)
{
	struct evdev_key *key;
	for_each_key(key, keys, false) {
		if (!key->pending) {
			set_key(key, code, press);
			keys->pending++;
			break;
		}
	}
}

static void complete_key(struct evdev_keys *keys, struct evdev_key *k)
{
	k->pending = false;
	keys->pending--;
}


struct evdev_xy
{
	bool pending;
	int  x, y;
};

static void reset_xy(struct evdev_xy *xy)
{
	*xy = (struct evdev_xy){ false, 0, 0 };
}


struct evdev_touchpad_buttons
{
	bool active;
	int left, right, top;       /* area dimensions */
	unsigned touched, pressed;  /* 0 if none, key code otherwise */
};

struct evdev_touchpad
{
	typeof(jiffies) touch_time;
	bool            btn_left_pressed; /* state of (physical) BTN_LEFT */
	bool            palm;             /* hardware detected palm */

	struct { double x, y; } normalize;

	struct evdev_touchpad_buttons buttons;
};


struct evdev_touchscreen
{
	bool touched; /* track contacts to report BTN_TOUCH press/release */
};


struct evdev
{
	struct genode_event *event;
	struct input_handle  handle;
	enum evdev_motion    motion;

	/* record of all events in one packet - submitted on SYN */
	unsigned          tool;  /* BTN_TOOL_* or 0 */
	struct evdev_keys keys;
	struct evdev_xy   rel;
	struct evdev_xy   wheel;
	struct evdev_xy   abs;
	struct evdev_mt   mt;

	/* motion-device-specific state machine */
	union {
		struct evdev_touchpad    touchpad;
		struct evdev_touchscreen touchscreen;
	};
};


/* helper functions (require declarations above) */
#include "evdev.h"


static bool record_mouse(struct evdev *evdev, struct input_value const *v)
{
	if (v->type != EV_REL || evdev->motion != MOTION_MOUSE)
		return false;

	switch (v->code) {
	case REL_X:      evdev->rel.pending   = true; evdev->rel.x   += v->value; break;
	case REL_Y:      evdev->rel.pending   = true; evdev->rel.y   += v->value; break;
	case REL_HWHEEL: evdev->wheel.pending = true; evdev->wheel.x += v->value; break;
	case REL_WHEEL:  evdev->wheel.pending = true; evdev->wheel.y += v->value; break;

	default:
		return false;
	}

	return true;
}


static bool record_abs(struct evdev *evdev, struct input_value const *v)
{
	if (v->type != EV_ABS)
		return false;

	switch (v->code) {
	case ABS_X: evdev->abs.pending = true; evdev->abs.x = v->value; break;
	case ABS_Y: evdev->abs.pending = true; evdev->abs.y = v->value; break;

	default:
		return false;
	}

	return true;
}


static bool record_wheel(struct evdev *evdev, struct input_value const *v)
{
	if (v->type != EV_REL)
		return false;

	switch (v->code) {
	case REL_HWHEEL: evdev->wheel.pending = true; evdev->wheel.x += v->value; break;
	case REL_WHEEL:  evdev->wheel.pending = true; evdev->wheel.y += v->value; break;

	default:
		return false;
	}

	return true;
}


static bool record_pointer(struct evdev *evdev, struct input_value const *v)
{
	if (evdev->motion != MOTION_POINTER)
		return false;

	return record_abs(evdev, v) || record_wheel(evdev, v);
}


static bool record_touchtool(struct evdev *evdev, struct input_value const *v)
{
	if (evdev->motion != MOTION_TOUCHTOOL)
		return false;

	return record_abs(evdev, v) || record_wheel(evdev, v);
}


static bool record_mt(struct evdev_mt *mt, struct input_value const *v)
{
	if (v->type != EV_ABS || !mt->num_slots)
		return false;

	struct evdev_mt_slot * const cur_slot = &mt->slots[mt->cur_slot];

	switch (v->code) {
	case ABS_MT_SLOT:
		mt->cur_slot = (v->value >= 0 ? v->value : 0);
		/* nothing pending yet */
		break;

	case ABS_MT_TRACKING_ID:
		if (mt->cur_slot < mt->num_slots) {
			cur_slot->touch  = v->value >= 0;
			cur_slot->finger = mt->cur_slot;
			charge_mt_slot(mt, cur_slot);
		}
		break;

	case ABS_MT_POSITION_X:
		if (mt->cur_slot < mt->num_slots) {
			set_mt_pos(&cur_slot->x, v->value);
			charge_mt_slot(mt, cur_slot);
		}
		break;

	case ABS_MT_POSITION_Y:
		if (mt->cur_slot < mt->num_slots) {
			set_mt_pos(&cur_slot->y, v->value);
			charge_mt_slot(mt, cur_slot);
		}
		break;

	default:
		return false;
	}

	return true;
}


static bool record_touchpad(struct evdev *evdev, struct input_value const *v)
{
	if (evdev->motion != MOTION_TOUCHPAD)
		return false;

	/* monitor (physical) button state clashing with tap-to-click */
	if (v->type == EV_KEY && v->code == BTN_LEFT)
		evdev->touchpad.btn_left_pressed = !!v->value;

	if (v->type == EV_ABS && v->code == ABS_MT_TOOL_TYPE) {
		evdev->touchpad.palm = v->value == MT_TOOL_PALM;
		return true;
	}

	/* only multi-touch pads supported currently */
	return evdev->touchpad.palm || record_mt(&evdev->mt, v);
}


static bool record_touchscreen(struct evdev *evdev, struct input_value const *v)
{
	if (evdev->motion != MOTION_TOUCHSCREEN)
		return false;

	/* only multi-touch screens supported currently */
	return record_mt(&evdev->mt, v);
}


static bool is_tool_key(unsigned code)
{
	switch (code) {
	case BTN_TOOL_PEN:
	case BTN_TOOL_RUBBER:
	case BTN_TOOL_BRUSH:
	case BTN_TOOL_PENCIL:
	case BTN_TOOL_AIRBRUSH:
	case BTN_TOOL_FINGER:
	case BTN_TOOL_MOUSE:
	case BTN_TOOL_LENS:
	case BTN_TOOL_QUINTTAP:
	case BTN_TOOL_DOUBLETAP:
	case BTN_TOOL_TRIPLETAP:
	case BTN_TOOL_QUADTAP:
		return true;

	default:
		return false;
	}
}

static bool record_key(struct evdev *evdev, struct input_value const *v)
{
	struct evdev_keys * const keys = &evdev->keys;

	if (v->type != EV_KEY)
		return false;

	/* silently drop KEY_FN as hardware switch */
	if (v->code == KEY_FN)
		return true;

	if (is_tool_key(v->code))
		evdev->tool = v->value ? v->code : 0;
	else
		charge_key(keys, v->code, !!v->value);

	return true;
}


static void submit_press_release(struct evdev_key *key, struct evdev_keys *keys,
                                 struct genode_event_submit *submit)
{
	if (!key->pending)
		return;

	if (key->press)
		submit->press(submit, lx_emul_event_keycode(key->code));
	else
		submit->release(submit, lx_emul_event_keycode(key->code));

	complete_key(keys, key);
}


static void submit_keys(struct evdev_keys *keys, struct genode_event_submit *submit)
{
	struct evdev_key *key;

	if (!keys->pending)
		return;

	for_each_pending_key(key, keys)
		submit_press_release(key, keys, submit);
}


static void submit_mouse(struct evdev *evdev, struct genode_event_submit *submit)
{
	if (evdev->motion != MOTION_MOUSE)
		return;

	if (evdev->rel.pending) {
		submit->rel_motion(submit, evdev->rel.x, evdev->rel.y);
		reset_xy(&evdev->rel);
	}

	if (evdev->wheel.pending) {
		submit->wheel(submit, evdev->wheel.x, evdev->wheel.y);
		reset_xy(&evdev->wheel);
	}
}


static void submit_pointer(struct evdev *evdev, struct genode_event_submit *submit)
{
	if (evdev->motion != MOTION_POINTER)
		return;

	if (evdev->abs.pending) {
		submit->abs_motion(submit, evdev->abs.x, evdev->abs.y);
		reset_xy(&evdev->abs);
	}

	if (evdev->wheel.pending) {
		submit->wheel(submit, evdev->wheel.x, evdev->wheel.y);
		reset_xy(&evdev->wheel);
	}
}


static void submit_touchtool(struct evdev *evdev, struct genode_event_submit *submit)
{
	struct evdev_keys * const keys = &evdev->keys;

	struct evdev_key *key;

	if (evdev->motion != MOTION_TOUCHTOOL)
		return;

	if (evdev->abs.pending) {
		submit->abs_motion(submit, evdev->abs.x, evdev->abs.y);
		reset_xy(&evdev->abs);
	}

	/* submit recorded tool on BTN_TOUCH */
	for_each_pending_key(key, keys) {
		if (key->code != BTN_TOUCH)
			continue;

		key->code = evdev->tool;
		submit_press_release(key, keys, submit);
		break;
	}
}


static void touchpad_tap_to_click(struct evdev *evdev, struct genode_event_submit *submit)
{
	struct evdev_keys     * const keys = &evdev->keys;
	struct evdev_touchpad * const tp   = &evdev->touchpad;

	enum { TAP_TIME = 130 /* max touch duration in ms */ };

	/* BTN_TOUCH must be pending to begin/end tap-to-click tracking */
	if (!keys->pending)
		return;

	struct evdev_key *key;
	for_each_pending_key(key, keys) {
		if (key->code != BTN_TOUCH)
			continue;

		if (key->press && !tp->btn_left_pressed) {
			tp->touch_time = key->jiffies;
		} else {
			if (time_before(key->jiffies, tp->touch_time + msecs_to_jiffies(TAP_TIME))) {
				submit->press(submit, lx_emul_event_keycode(BTN_LEFT));
				submit->release(submit, lx_emul_event_keycode(BTN_LEFT));
			}
			tp->touch_time = 0;
		}

		complete_key(keys, key);
		break;
	}
}


static void touchpad_relative_motion(struct evdev *evdev, struct evdev_mt_slot *slot,
                                     struct genode_event_submit *submit)
{
	if (!slot->pending)
		return;

	bool const  x = slot->x.set;
	bool const  y = slot->y.set;
	bool const ox = slot->ox.set;
	bool const oy = slot->oy.set;

	int dx = 0, dy = 0;
	if (ox && oy) {
		/* translate position changes to relative motion and update ox/oy */
		if (x) {
			dx = (int)(evdev->touchpad.normalize.x*(slot->x.value - slot->ox.value));

			if (dx) set_mt_pos(&slot->ox, slot->x.value);
		}
		if (y) {
			dy = (int)(evdev->touchpad.normalize.y*(slot->y.value - slot->oy.value));

			if (dy) set_mt_pos(&slot->oy, slot->y.value);
		}

		if (dx || dy )
			submit->rel_motion(submit, dx, dy);
	} else {
		/* initial position */
		if (x && !ox) set_mt_pos(&slot->ox, slot->x.value);
		if (y && !oy) set_mt_pos(&slot->oy, slot->y.value);
	}

	complete_mt_slot(&evdev->mt, slot);
}


static void touchpad_buttons_update(struct evdev *evdev, struct evdev_mt_slot *slot)
{
	if (!slot->pending)
		return;

	struct evdev_touchpad_buttons * const buttons = &evdev->touchpad.buttons;

	if (!buttons->left)
		return;
	if (!slot->x.set || !slot->y.set)
		return;

	if (slot->y.value < buttons->top) {
		if (slot->oy.set && slot->oy.value >= buttons->top)
			update_mt_slot(slot);
		buttons->touched = 0;
		return;
	}

	update_mt_slot(slot);

	buttons->touched = (slot->x.value <= buttons->left)  ? BTN_LEFT
                     : (slot->x.value >= buttons->right) ? BTN_RIGHT : BTN_MIDDLE;

	complete_mt_slot(&evdev->mt, slot);
}


static void touchpad_buttons_process(struct evdev *evdev, struct genode_event_submit *submit)
{
	struct evdev_touchpad_buttons * const buttons = &evdev->touchpad.buttons;
	struct evdev_keys             * const keys    = &evdev->keys;

	if (!buttons->left)
		return;

	struct evdev_key *btn_touch = NULL, *btn_left = NULL;

	struct evdev_key *key;

	for_each_pending_key(key, &evdev->keys) {
		if (key->code == BTN_TOUCH) btn_touch = key;
		if (key->code == BTN_LEFT)  btn_left  = key;
	}

	if (btn_touch && buttons->touched)
		complete_key(keys, btn_touch);

	if (btn_left) {
		/* process press in button area */
		if (btn_left->press && buttons->touched) {
			buttons->pressed = buttons->touched;
			submit->press(submit, lx_emul_event_keycode(buttons->pressed));
			complete_key(keys, btn_left);
		}
		/* release pressed button even after motion left it */
		else if (!btn_left->press && buttons->pressed) {
			submit->release(submit, lx_emul_event_keycode(buttons->pressed));
			buttons->pressed = 0;
			complete_key(keys, btn_left);
		}
	}
}


/*
 * Future device-state model additions
 *
 * - click without small motion (if pad is pressable button)
 * - two-finger scrolling
 * - edge scrolling
 *
 * https://wayland.freedesktop.org/libinput/doc/latest/features.html
 */
static void submit_touchpad(struct evdev *evdev, struct genode_event_submit *submit)
{
	if (evdev->motion != MOTION_TOUCHPAD)
		return;

	struct evdev_mt * const mt = &evdev->mt;

	struct evdev_mt_slot *slot;

	for_each_mt_slot(slot, mt) {
		if (!mt->pending)
			break;

		if (!slot->pending)
			continue;

		/* reset slot on release */
		if (!slot->touch) {
			reset_mt_slot(slot);
			continue;
		}

		touchpad_buttons_update(evdev, slot);
		touchpad_relative_motion(evdev, slot, submit);
	}

	touchpad_buttons_process(evdev, submit);
	touchpad_tap_to_click(evdev, submit);
}


static void submit_touchscreen(struct evdev *evdev, struct genode_event_submit *submit)
{
	struct evdev_mt * const mt = &evdev->mt;
	struct evdev_keys * const keys = &evdev->keys;

	if (evdev->motion != MOTION_TOUCHSCREEN)
		return;

	struct evdev_mt_slot *slot;
	for_each_mt_slot(slot, mt) {
		if (!mt->pending)
			break;

		if (!slot->touch && slot->ox.set && slot->oy.set) {
			submit->touch_release(submit, slot->finger);

			complete_mt_slot(mt, slot);
			reset_mt_slot(slot);
			continue;
		}

		/* skip unchanged slots */
		if (slot->ox.value == slot->x.value && slot->oy.value == slot->y.value) {
			complete_mt_slot(mt, slot);
			continue;
		}

		if (pending_mt_slot(slot)) {
			struct genode_event_touch_args args = {
				.finger = slot->finger,
				.xpos   = slot->x.value,
				.ypos   = slot->y.value,
				.width  = 1
			};
			submit->touch(submit, &args);
		}

		update_mt_slot(slot);
		complete_mt_slot(mt, slot);
	}

	/* filter low-level BTN_TOUCH */
	struct evdev_key *key;
	for_each_pending_key(key, keys) {
		if (key->code != BTN_TOUCH)
			continue;

		complete_key(keys, key);
		break;
	}

	/* report BTN_TOUCH if touch count changes from/to 0 */
	bool touched = false;
	for_each_mt_slot(slot, mt)
		touched |= slot->touch;

	if (evdev->touchscreen.touched != touched) {
		if (touched)
			submit->press(submit, lx_emul_event_keycode(BTN_TOUCH));
		else
			submit->release(submit, lx_emul_event_keycode(BTN_TOUCH));
		evdev->touchscreen.touched = touched;
	}
}


static bool submit_on_syn(struct evdev *evdev, struct input_value const *v,
                          struct genode_event_submit *submit)
{
	if (v->type != EV_SYN || v->code != SYN_REPORT)
		return false;

	/* motion devices */
	submit_mouse(evdev, submit);
	submit_pointer(evdev, submit);
	submit_touchpad(evdev, submit);
	submit_touchtool(evdev, submit);
	submit_touchscreen(evdev, submit);

	/* submit keys not handled above */
	submit_keys(&evdev->keys, submit);

	return true;
}


struct genode_event_generator_ctx
{
	struct evdev *evdev;
	struct input_value const *values;
	unsigned count;
};


static void evdev_event_generator(struct genode_event_generator_ctx *ctx,
                                  struct genode_event_submit *submit)
{
	for (int i = 0; i < ctx->count; i++) {
		struct evdev *evdev = ctx->evdev;

		struct input_value const *v = &ctx->values[i];

		bool processed = false;

		/* filter injected EV_LED updates */
		if (v->type == EV_LED) continue;

		/* filter input_repeat_key() */
		if ((v->type == EV_KEY) && (v->value > 1)) continue;

		processed |= record_mouse(evdev, v);
		processed |= record_pointer(evdev, v);
		processed |= record_touchpad(evdev, v);
		processed |= record_touchtool(evdev, v);
		processed |= record_touchscreen(evdev, v);
		processed |= record_key(evdev, v);
		processed |= submit_on_syn(evdev, v, submit);

		if (!processed)
			printk("Dropping unsupported Event[%u/%u] device=%s type=%s code=%s value=%d\n",
			       i + 1, ctx->count, evdev->handle.dev->name,
			       NAME_OF_TYPE(v->type), NAME_OF_CODE(v->type, v->code), v->value);
	}
}


static unsigned evdev_events(struct input_handle *handle,
                             struct input_value *values, unsigned int count)
{
	struct evdev *evdev = handle->private;

	struct genode_event_generator_ctx ctx = {
		.evdev = evdev, .values = values, .count = count };

	genode_event_generate(evdev->event, &evdev_event_generator, &ctx);

	return count;
}


static void init_evdev_mt(struct evdev *evdev, struct input_dev *dev)
{
	struct evdev_mt *mt = &evdev->mt;

	struct evdev_mt_slot *slot;

	mt->num_slots = min(dev->mt->num_slots, MAX_MT_SLOTS);
	mt->cur_slot = 0;
	for_each_mt_slot(slot, mt)
		reset_mt_slot(slot);
}


static void init_touchpad(struct evdev *evdev)
{
	if (evdev->motion != MOTION_TOUCHPAD)
		return;

	struct input_dev *dev = evdev->handle.dev;

	/* only multi-touch pads supported currently */
	if (!dev->mt)
		return;

	init_evdev_mt(evdev, dev);

	/* normalize sensitivity */
	enum { NORMALIZED_DPI = 600 };

	int x_res = input_abs_get_res(dev, ABS_MT_POSITION_X); /* dpmm */
	int y_res = input_abs_get_res(dev, ABS_MT_POSITION_Y); /* dpmm */

	evdev->touchpad.normalize.x = NORMALIZED_DPI / (x_res*25.4);
	evdev->touchpad.normalize.y = NORMALIZED_DPI / (y_res*25.4);

	/* software button areas on clickpads */
	if (test_bit(INPUT_PROP_BUTTONPAD, dev->propbit)) {
		evdev->touchpad.buttons.active = true;

		int x_max = input_abs_get_max(dev, ABS_MT_POSITION_X);
		int x_min = input_abs_get_min(dev, ABS_MT_POSITION_X);
		int y_max = input_abs_get_max(dev, ABS_MT_POSITION_Y);
		int y_min = input_abs_get_min(dev, ABS_MT_POSITION_Y);
		int w = (int)((x_max - x_min) * 0.375);             /* width of left/right button */
		int h = max(10*x_res, (int)(0.15*(y_max - y_min))); /* height of area */

		evdev->touchpad.buttons.left  = x_min + w;
		evdev->touchpad.buttons.right = x_max - w;
		evdev->touchpad.buttons.top   = y_max - h;

		if (0)
			printk("CLICKPAD %dx%d phys %dx%d mm - button area [%d %d %d]x%d phys %d-%d-%dx%d mm\n",
			       x_max - x_min, y_max - y_min, (x_max - x_min)/x_res, (y_max - y_min)/y_res,
			       evdev->touchpad.buttons.left - x_min,
			       evdev->touchpad.buttons.right - evdev->touchpad.buttons.left,
			       x_max - evdev->touchpad.buttons.right,
			       y_max - evdev->touchpad.buttons.top,
			       w/x_res, ((x_max - x_min) - 2*w)/x_res, (w)/x_res, h/y_res);
	}

	/* disable undesired events */
	clear_bit(ABS_X,              dev->absbit);
	clear_bit(ABS_Y,              dev->absbit);
	clear_bit(ABS_PRESSURE,       dev->absbit);
	clear_bit(ABS_MT_TOUCH_MAJOR, dev->absbit);
	clear_bit(ABS_MT_TOUCH_MINOR, dev->absbit);
	clear_bit(ABS_MT_WIDTH_MAJOR, dev->absbit);
	clear_bit(ABS_MT_WIDTH_MINOR, dev->absbit);
	clear_bit(ABS_MT_ORIENTATION, dev->absbit);
	clear_bit(ABS_MT_PRESSURE,    dev->absbit);
	clear_bit(ABS_MT_TOOL_X,      dev->absbit);
	clear_bit(ABS_MT_TOOL_Y,      dev->absbit);
}


static void init_motion_direct(struct evdev *evdev)
{
	struct input_dev *dev = evdev->handle.dev;

	/* multi-touch screens and absolute touch tools supported */
	if (dev->mt) {
		init_evdev_mt(evdev, dev);

		/* disable undesired events */
		clear_bit(ABS_X,              dev->absbit);
		clear_bit(ABS_Y,              dev->absbit);
		clear_bit(ABS_PRESSURE,       dev->absbit);
		clear_bit(ABS_DISTANCE,       dev->absbit);
		clear_bit(ABS_MT_TOUCH_MAJOR, dev->absbit);
		clear_bit(ABS_MT_TOUCH_MINOR, dev->absbit);
		clear_bit(ABS_MT_WIDTH_MAJOR, dev->absbit);
		clear_bit(ABS_MT_WIDTH_MINOR, dev->absbit);
		clear_bit(ABS_MT_ORIENTATION, dev->absbit);
		clear_bit(ABS_MT_PRESSURE,    dev->absbit);
		clear_bit(ABS_MT_TOOL_TYPE,   dev->absbit);
		clear_bit(ABS_MT_TOOL_X,      dev->absbit);
		clear_bit(ABS_MT_TOOL_Y,      dev->absbit);
	} else {
		/* disable undesired events */
		clear_bit(ABS_PRESSURE,       dev->absbit);
		clear_bit(ABS_DISTANCE,       dev->absbit);
	}
}


static void init_touchtool(struct evdev *evdev)
{
	if (evdev->motion != MOTION_TOUCHTOOL)
		return;

	init_motion_direct(evdev);
}


static void init_touchscreen(struct evdev *evdev)
{
	if (evdev->motion != MOTION_TOUCHSCREEN)
		return;

	init_motion_direct(evdev);
}


unsigned evdev_count;

static int evdev_connect(struct input_handler *handler, struct input_dev *dev,
                         const struct input_device_id *id)
{
	struct evdev *evdev;
	int error;
	struct genode_event_args args = { .label = dev->name };

	evdev = kzalloc(sizeof(*evdev), GFP_KERNEL);
	if (!evdev)
		return -ENOMEM;

	evdev->event = genode_event_create(&args);

	evdev->handle.private = evdev;
	evdev->handle.dev     = dev;
	evdev->handle.handler = handler;
	evdev->handle.name    = dev->name;

	evdev->motion = evdev_motion(dev);
	init_touchpad(evdev);
	init_touchtool(evdev);
	init_touchscreen(evdev);

	/* disable undesired events */
	clear_bit(EV_MSC,            dev->evbit);
	clear_bit(REL_HWHEEL_HI_RES, dev->relbit);
	clear_bit(REL_WHEEL_HI_RES,  dev->relbit);

	error = input_register_handle(&evdev->handle);
	if (error)
		goto err_free_handle;

	error = input_open_device(&evdev->handle);
	if (error)
		goto err_unregister_handle;

	printk("Connected device: %s (%s at %s) %s%s\n",
	       dev_name(&dev->dev),
	       dev->name ?: "unknown",
	       dev->phys ?: "unknown",
	       dev->mt ? "MULTITOUCH " : "",
	       evdev->motion != MOTION_NONE ? NAME_OF_MOTION(evdev->motion) : "");

	evdev_count++;
	return 0;

err_unregister_handle:
	input_unregister_handle(&evdev->handle);

err_free_handle:
	genode_event_destroy(evdev->event);
	kfree(evdev);
	return error;
}


static void evdev_disconnect(struct input_handle *handle)
{
	struct evdev *evdev = handle->private;

	evdev_count--;
	printk("Disconnected device: %s\n", dev_name(&handle->dev->dev));

	input_close_device(handle);
	input_unregister_handle(handle);
	genode_event_destroy(evdev->event);
	kfree(evdev);
}


static const struct input_device_id evdev_ids[] = {
	{
		/* Matches all devices */
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_SYN) }
	},
	{ }, /* Terminating zero entry */
};


MODULE_DEVICE_TABLE(input, evdev_ids);

static struct input_handler evdev_handler = {
	/* .event      = we cannot define both .event and .events */
	.events     = evdev_events,
	.connect    = evdev_connect,
	.disconnect = evdev_disconnect,
	.name       = "evdev",
	.id_table   = evdev_ids,
};


static int __init evdev_init(void)
{
	return input_register_handler(&evdev_handler);
}


static void __exit evdev_exit(void)
{
	input_unregister_handler(&evdev_handler);
}


/**
 * Let's hook into the evdev initcall, so we do not need to register
 * an additional one
 */
module_init(evdev_init);
module_exit(evdev_exit);
