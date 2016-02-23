/*
 * \brief  Framebuffer driver for Exynos5 HDMI
 * \author Martin Stein
 * \date   2013-08-09
 */

/*
 * Copyright (C) 2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/* Genode includes */
#include <framebuffer_session/connection.h>
#include <base/component.h>
#include <base/heap.h>
#include <util/reconstructible.h>
#include <root/component.h>

using namespace Genode;

namespace Framebuffer
{
	using namespace Genode;
	class Session_component;
	class Root;

	enum { MAX_LABEL_LEN = 128 };
	typedef String<MAX_LABEL_LEN> Label;
	typedef Root_component<Session_component, Multiple_clients> Root_component;
}

class Framebuffer::Session_component
:
	public Rpc_object<Framebuffer::Session>
{
	private:

		Framebuffer::Connection & _fb_con;
		Signal_context_capability _mode_sigh { };
		Signal_context_capability _sync_sigh { };

	public:

		Label label;

		Session_component(char const * label, Framebuffer::Connection & fb_con)
		: _fb_con(fb_con), label(label) { }

		/************************************
		 ** Framebuffer::Session interface **
		 ************************************/

		Dataspace_capability dataspace() override {
			return _fb_con.dataspace(); }

		Mode mode() const override { return _fb_con.mode(); }

		void mode_sigh(Signal_context_capability sigh) override
		{
			/* FIXME: done merely for NOVA and SEL4 */
			_mode_sigh = sigh;
			_fb_con.mode_sigh(sigh);
		}

		void sync_sigh(Signal_context_capability sigh) override
		{
			/* FIXME: done merely for NOVA and SEL4 */
			_sync_sigh = sigh;
			_fb_con.sync_sigh(sigh);
		}

		void refresh(int a, int b, int c, int d) override {
			_fb_con.refresh(a,b,c,d); }
};

class Framebuffer::Root : public Framebuffer::Root_component
{
	private:

		Env                                    &_env;
		Constructible<Framebuffer::Connection>  _fb_con { };

		Session_component *_create_session(const char *args) override
		{
			using namespace Genode;

			char label[MAX_LABEL_LEN];
			Arg_string::find_arg(args, "label").string(label,
			                                           sizeof(label),
			                                           "<noname>");

			size_t ram_quota =
				Arg_string::find_arg(args, "ram_quota").ulong_value(0);

			size_t session_size = align_addr(sizeof(Session_component), 12);

			if (ram_quota < session_size) {
				error("insufficient 'ram_quota', got ",ram_quota,", need ",session_size);
				throw Service_denied();
			}

			if (!_fb_con.constructed()) {
				_fb_con.construct(_env, Mode());
			}

			Session_component *session = new (md_alloc())
				Session_component(label, *_fb_con);

			return session;

		}

	public:

		Root(Env       &env,
		     Allocator &md_alloc)
		:
			Root_component { &env.ep().rpc_ep(), &md_alloc },
			_env           { env }
		{ }
};


class Main
{
	private:

		Env               &_env;
		Heap               _heap { &_env.ram(), &_env.rm() };
		Framebuffer::Root  _root { _env, _heap };

	public:

		Main(Env &env)
		:
			_env(env)
		{
			_env.parent().announce(_env.ep().manage(_root));
		}
};


void Component::construct(Env &env) { static Main main(env); }
