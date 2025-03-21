/*
 * \brief  Shared-clipboard service backend
 * \author Christian Helmuth
 * \date   2021-09-01
 *
 * Note, the text strings exchanged with the upper-layers (and therefore the
 * guest) must be null-terminated and sizes have to include the terminator.
 */

/*
 * Copyright (C) 2021-2026 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

/* Genode includes */
#include <base/env.h>
#include <base/attached_rom_dataspace.h>
#include <os/reporter.h>
#include <libc/component.h>

/* VirtualBox includes */
#include <iprt/utf16.h>
#include <VBoxSharedClipboardSvc-internal.h>
#include <VBox/GuestHost/clipboard-helper.h>

/* local includes */
#include "services.h"


using namespace Genode;

namespace {

class Clipboard
{
	private:

		Env &_env;

		Attached_rom_dataspace _rom    { _env, "clipboard" };
		Expanding_reporter     _report { _env, "clipboard", "clipboard" };

		SHCLCLIENT *_client { nullptr };

		Signal_handler<Clipboard> _rom_sigh {
			_env.ep(), *this, &Clipboard::_handle_rom_changed };

		Signal_handler<Clipboard> _focus_sigh {
			_env.ep(), *this, &Clipboard::_handle_focus_and_active };

		bool _sync_pending { false };

		void _handle_rom_changed()
		{
			/*
			 * The clipboard ROM can only be read if we are focused and active, wait
			 * for signal
			 */
			_sync_pending = true;
		}

		void _handle_focus_and_active()
		{
			if (!_sync_pending) return;

			Libc::with_libc([&] () {  ShClBackendSync(nullptr, _client); });
			_sync_pending = false;
		}

	public:

		struct Guard;

		Clipboard(Env &env) : _env(env)
		{
			_rom.sigh(_rom_sigh);
			Services::service().focus_handler(&_focus_sigh);
		}

		SHCLCLIENT * client() const { return _client; }

		int connect(SHCLCLIENT *client)
		{
			if (_client) {
				warning("shared clipboard: only one client supported");
				return VERR_NOT_SUPPORTED;
			}

			_client = client;

			return VINF_SUCCESS;
		}

		void disconnect(SHCLCLIENT *client)
		{
			if (client != _client) {
				warning("shared clipboard: unknown client on disconnect");
				return;
			}

			_client = nullptr;
		}

		void report(char const *content)
		{
			_report.generate([&] (Generator &g) { g.append_quoted(content); });
		}

		template <typename FN>
		void with_content(FN const &fn)
		{
			_rom.update();

			if (!_rom.valid())
				return;

			Node::Quoted_content content { _rom.node() };

			size_t const n   = num_printed_bytes(content);
			char * const ptr = (char *)RTMemAlloc(n + 1);

			bool const ok = Byte_range_ptr(ptr, n)
				.as_output([&] (Output &out) { print(out, content); }).ok();

			if (!ok) warning("shared clipboard: failed to decode content");

			ptr[n] = 0; /* null termination */

			fn(ptr, n + 1);

			RTMemFree(ptr);
		}
};

struct Clipboard::Guard
{
	Guard()  { ShClSvcLock(); }
	~Guard() { ShClSvcUnlock(); }
};

Constructible<Clipboard> clipboard;

} /* unnamed namespace */

int ShClBackendReadData(PSHCLBACKEND, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX,
                        SHCLFORMAT fFormat, void *pvData, uint32_t cbData,
                        uint32_t *pcbActual)

{
	if (!(fFormat & VBOX_SHCL_FMT_UNICODETEXT))
		return VERR_NOT_IMPLEMENTED;

	Clipboard::Guard guard;

	int rc  = VINF_SUCCESS;
	*pcbActual = 0;

	clipboard->with_content([&] (char const *utf8_string, size_t utf8_size) {

		PRTUTF16 utf16_string = (PRTUTF16)pvData;
		size_t   utf16_chars  = cbData/sizeof(RTUTF16);

		rc = RTStrToUtf16Ex(utf8_string, utf8_size, &utf16_string, utf16_chars, &utf16_chars);

		/* VERR_BUFFER_OVERFLOW is handled by the guest if cb < cb_out */
		if (rc == VERR_BUFFER_OVERFLOW)
			rc = VINF_SUCCESS;

		if (RT_SUCCESS(rc)) {
			/* the protocol requires cb_out to include the null terminator */
			*pcbActual = (utf16_chars + 1)*sizeof(RTUTF16);
		}
	});

	return rc;
}


int ShClBackendWriteData(PSHCLBACKEND, PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX,
                         SHCLFORMAT fFormat, void *pvData, uint32_t /* cbData */)
{
	if (!(fFormat & VBOX_SHCL_FMT_UNICODETEXT))
		return VERR_NOT_IMPLEMENTED;

	if (pvData == nullptr)
		return VERR_INVALID_POINTER;

	Clipboard::Guard guard;

	PCRTUTF16 const utf16_string = (PCRTUTF16)pvData;

	char *utf8_string;

	/* allocates buffer and converts string (incl. null terminator) */
	int rc = RTUtf16ToUtf8(utf16_string, &utf8_string);
	if (RT_FAILURE(rc))
		return VINF_SUCCESS;

	clipboard->report(utf8_string);

	RTStrFree(utf8_string);

	return VINF_SUCCESS;
}

/**
 * The guest is taking possession of the shared clipboard
 */
int ShClBackendReportFormats(PSHCLBACKEND, PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
	/*
	 * Eagerly request data from the guest.
	 */
	return ShClSvcReadDataFromGuestAsync(pClient, fFormats, nullptr);
}


/**
 * Synchronize contents of the host clipboard with the guest
 *
 * Called by HGCM svc layer on svcConnect() and svcLoadState() (after resume)
 * as well as on clipboard ROM update.
 */
int ShClBackendSync(PSHCLBACKEND, PSHCLCLIENT pClient)
{
	Clipboard::Guard guard;

	if (!clipboard->client())
		return VINF_NO_CHANGE;

	if (pClient != clipboard->client()) {
		warning("shared clipboard: client mismatch on sync");
		return VINF_NO_CHANGE;
	}

	return ShClSvcReportFormats(clipboard->client(), VBOX_SHCL_FMT_UNICODETEXT);
}


int ShClBackendDisconnect(PSHCLBACKEND, PSHCLCLIENT pClient)
{
	Clipboard::Guard guard;

	clipboard->disconnect(pClient);

	return VINF_SUCCESS;
}


int ShClBackendConnect(PSHCLBACKEND, PSHCLCLIENT pClient, bool /* fHeadless */)
{
	Clipboard::Guard guard;

	int const rc = clipboard->connect(pClient);
	if (RT_FAILURE(rc))
		return rc;

	/* send initial format report to guest */
	return ShClSvcReportFormats(clipboard->client(), VBOX_SHCL_FMT_UNICODETEXT);
}


int ShClBackendInit(PSHCLBACKEND, VBOXHGCMSVCFNTABLE *)
{
	if (clipboard.constructed()) return VERR_NOT_SUPPORTED;

	clipboard.construct(Services::service().env());

	return VINF_SUCCESS;
}


void ShClBackendDestroy(PSHCLBACKEND)
{
	clipboard.destruct();
}
