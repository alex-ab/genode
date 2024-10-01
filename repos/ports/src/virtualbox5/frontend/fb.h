/*
 * \brief  Virtualbox framebuffer implementation for Genode
 * \author Alexander Boettcher
 * \date   2013-10-16
 */

/*
 * Copyright (C) 2013-2017 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

/* Genode includes */
#define Framebuffer Fb_Genode
#include <framebuffer_session/connection.h>
#include <gui_session/connection.h>
#undef Framebuffer

#include <os/texture_rgb888.h>
#include <nitpicker_gfx/texture_painter.h>

/* VirtualBox includes */

#include "Global.h"
#include "VirtualBoxBase.h"
#include "DisplayWrap.h"

class Genodefb :
	VBOX_SCRIPTABLE_IMPL(IFramebuffer)
{
	private:

		Genode::Env        &_env;
		Gui::Connection    &_gui;
		Gui::Top_level_view _view { _gui };
		Gui::Rect           _gui_win { .at = { }, .area = { 1024, 768 } };

		/*
		 * The mode currently used by the VM. Can be smaller than the
		 * framebuffer mode.
		 */
		Gui::Area _virtual_fb_mode;

		void *_attach()
		{
			return _env.rm().attach(_gui.framebuffer.dataspace(), {
				.size = { },  .offset     = { },  .use_at     = { },
				.at   = { },  .executable = { },  .writeable  = true
			}).convert<void *>(
				[&] (Genode::Region_map::Range range)  { return (void *)range.start; },
				[&] (Genode::Region_map::Attach_error) { return nullptr; }
			);
		}

		void *_fb_base = _attach();

		RTCRITSECT _fb_lock;

		ComPtr<IDisplay>             _display;
		ComPtr<IDisplaySourceBitmap> _display_bitmap;

		void _clear_screen()
		{
			if (!_fb_base) return;

			size_t const max_h = Genode::min(_gui_win.area.h, _virtual_fb_mode.h);
			size_t const num_pixels = _gui_win.area.w * max_h;
			memset(_fb_base, 0, num_pixels * sizeof(Genode::Pixel_rgb888));
			_gui.framebuffer.refresh({ _gui_win.at, _virtual_fb_mode });
		}

		void _adjust_buffer()
		{
			_gui.buffer({ .area = _gui_win.area, .alpha = false });
			_view.geometry(_gui_win);
		}

		Gui::Area _initial_setup()
		{
			_adjust_buffer();
			_view.front();
			return _gui_win.area;
		}

	public:

		Genodefb (Genode::Env &env, Gui::Connection &gui, ComPtr<IDisplay> const &display)
		:
			_env(env),
			_gui(gui),
			_virtual_fb_mode(_initial_setup()),
			_display(display)
		{
			int rc = RTCritSectInit(&_fb_lock);
			Assert(rc == VINF_SUCCESS);
		}

		int w() const { return _gui_win.area.w; }
		int h() const { return _gui_win.area.h; }

		void update_mode(Gui::Rect const gui_win)
		{
			Lock();

			_gui_win = gui_win;

			if (_fb_base)
				_env.rm().detach(Genode::addr_t(_fb_base));

			_adjust_buffer();

			_fb_base = _attach();

			Unlock();
		}

		STDMETHODIMP Lock()
		{
			return Global::vboxStatusCodeToCOM(RTCritSectEnter(&_fb_lock));
		}
	
		STDMETHODIMP Unlock()
		{
			return Global::vboxStatusCodeToCOM(RTCritSectLeave(&_fb_lock));
		}

		STDMETHODIMP NotifyChange(PRUint32 screen, PRUint32, PRUint32,
		                          PRUint32 w, PRUint32 h) override
		{
			HRESULT result = E_FAIL;

			Lock();

			/* save the new bitmap reference */
			_display->QuerySourceBitmap(screen, _display_bitmap.asOutParam());

			bool const ok = (w <= (ULONG)_gui_win.area.w) &&
			                (h <= (ULONG)_gui_win.area.h);

			bool const changed = (w != (ULONG)_virtual_fb_mode.w) ||
			                     (h != (ULONG)_virtual_fb_mode.h);

			if (ok && changed) {
				Genode::log("fb resize : [", screen, "] ",
				            _virtual_fb_mode, " -> ",
				            w, "x", h,
				            " (host: ", _gui_win.area, ")");

				if ((w < (ULONG)_gui_win.area.w) ||
				    (h < (ULONG)_gui_win.area.h)) {
					/* clear the old content around the new, smaller area. */
				    _clear_screen();
				}

				_virtual_fb_mode = { w, h };

				result = S_OK;
			} else if (changed) {
				Genode::log("fb resize : [", screen, "] ",
				            _virtual_fb_mode, " -> ",
				            w, "x", h, " ignored"
				            " (host: ", _gui_win.area, ")");
			}

			Unlock();

			/* request appropriate NotifyUpdate() */
			_display->InvalidateAndUpdateScreen(screen);

			return result;
		}

		STDMETHODIMP COMGETTER(Capabilities)(ComSafeArrayOut(FramebufferCapabilities_T, enmCapabilities)) override
		{
			if (ComSafeArrayOutIsNull(enmCapabilities))
				return E_POINTER;

			return S_OK;
		}

		STDMETHODIMP COMGETTER(HeightReduction) (ULONG *reduce) override
		{
			if (!reduce)
				return E_POINTER;

			*reduce = 0;
			return S_OK;
		}

		HRESULT NotifyUpdate(ULONG o_x, ULONG o_y, ULONG width, ULONG height) override
		{
			if (!_fb_base) return S_OK;

			Lock();

			if (_display_bitmap.isNull()) {
				_clear_screen();
				Unlock();
				return S_OK;
			}

			BYTE *pAddress = NULL;
			ULONG ulWidth = 0;
			ULONG ulHeight = 0;
			ULONG ulBitsPerPixel = 0;
			ULONG ulBytesPerLine = 0;
			BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;
			_display_bitmap->QueryBitmapInfo(&pAddress,
			                                 &ulWidth,
			                                 &ulHeight,
			                                 &ulBitsPerPixel,
			                                 &ulBytesPerLine,
			                                 &bitmapFormat);

			Gui::Area const area_fb = Gui::Area(_gui_win.area.w,
			                                    _gui_win.area.h);
			Gui::Area const area_vm = Gui::Area(ulWidth, ulHeight);

			using namespace Genode;

			typedef Pixel_rgb888 Pixel_src;
			typedef Pixel_rgb888 Pixel_dst;

			Texture<Pixel_src> texture((Pixel_src *)pAddress, nullptr, area_vm);
			Surface<Pixel_dst> surface((Pixel_dst *)_fb_base, area_fb);

			surface.clip(Surface_base::Rect(Surface_base::Point(o_x, o_y),
                                            Surface_base::Area(width, height)));

			Texture_painter::paint(surface,
			                       texture,
			                       Genode::Color(0, 0, 0),
			                       Surface_base::Point(0, 0),
			                       Texture_painter::SOLID,
			                       false);

			_gui.framebuffer.refresh(o_x, o_y, width, height);

			Unlock();

			return S_OK;
		}

		STDMETHODIMP NotifyUpdateImage(PRUint32 o_x, PRUint32 o_y,
		                               PRUint32 width, PRUint32 height,
		                               PRUint32 imageSize,
		                               PRUint8 *image) override
		{
			if (!_fb_base) return S_OK;

			Lock();

			Gui::Area const area_fb = _gui_win.area;
			Gui::Area const area_vm = Gui::Area(width, height);

			using namespace Genode;

			typedef Pixel_rgb888 Pixel_src;
			typedef Pixel_rgb888 Pixel_dst;

			Texture<Pixel_src> texture((Pixel_src *)image, nullptr, area_vm);
			Surface<Pixel_dst> surface((Pixel_dst *)_fb_base, area_fb);

			Texture_painter::paint(surface,
			                       texture,
			                       Genode::Color(0, 0, 0),
			                       Gui::Point(o_x, o_y),
			                       Texture_painter::SOLID,
			                       false);

			_gui.framebuffer.refresh(o_x, o_y, area_vm.w, area_vm.h);

			Unlock();

			return S_OK;
		}

		STDMETHODIMP COMGETTER(Overlay) (IFramebufferOverlay **) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(WinId) (PRInt64 *winId) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP VideoModeSupported(ULONG width, ULONG height,
		                                ULONG bpp, BOOL *supported) override
		{
			if (!supported)
				return E_POINTER;

			*supported = ((width  <= (ULONG)_gui_win.area.w) &&
			              (height <= (ULONG)_gui_win.area.h));

			return S_OK;
		}

		STDMETHODIMP Notify3DEvent(PRUint32, PRUint32, PRUint8 *) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP ProcessVHWACommand(BYTE *, LONG, BOOL) override {
			Assert(!"FixMe");
		    return E_NOTIMPL; }

		STDMETHODIMP GetVisibleRegion(BYTE *, ULONG, ULONG *) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }
		
		STDMETHODIMP SetVisibleRegion(BYTE *, ULONG) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(PixelFormat) (ULONG *format) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(BitsPerPixel)(ULONG *bits) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(BytesPerLine)(ULONG *line) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(Width)(ULONG *width) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(Height)(ULONG *height) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }
};
