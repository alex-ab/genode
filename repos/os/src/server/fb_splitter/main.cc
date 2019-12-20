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
#include <util/list.h>
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
	typedef List_element<Session_component> Session_list;
}

class Framebuffer::Session_component
:
	public Rpc_object<Framebuffer::Session>
{
	private:

		Root                      &_root;
		Framebuffer::Connection   &_fb_con;
		List<Session_list>        &_sessions;
		Signal_context_capability  _mode_sigh { };
		Signal_context_capability  _sync_sigh { };
		Session_list               _session_element { this };
		Label const                _label;
		bool const                 _master;

	public:

		Session_component(char const * label, Framebuffer::Connection &fb_con,
		                  List<Session_list> &sessions, bool const master,
		                  Root &root)
		:
			_root(root),
			_fb_con(fb_con),
			_sessions(sessions),
			_label(label),
			_master(master)
		{
			_sessions.insert(&_session_element);
		}

		~Session_component()
		{
			_sessions.remove(&_session_element);
		}

		void mode_changed()
		{
			if (_mode_sigh.valid())
				Signal_transmitter(_mode_sigh).submit();
		}

		bool master() const { return _master; }

		/************************************
		 ** Framebuffer::Session interface **
		 ************************************/

		Dataspace_capability dataspace() override;
		Mode mode() const override { return _fb_con.mode(); }

		void mode_sigh(Signal_context_capability sigh) override {
			_mode_sigh = sigh; }

		void sync_sigh(Signal_context_capability sigh) override
		{
			/* FIXME: done merely for NOVA and SEL4 */
			_sync_sigh = sigh;
			if (_master)
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
		Dataspace_capability                    _dataspace { };

		Signal_handler<Root> _mode_handler { _env.ep(), *this,
		                                     &Root::_mode_sigh };

		List<Session_list> _sessions { };

		void _mode_changed(bool only_master)
		{
			for (Session_list * element = _sessions.first(); element;
			     element = element->next())
			{
				Session_component * obj = element->object();

				if (!obj) continue;

				if (only_master && obj->master()) {
					obj->mode_changed();
					return;
				} else {
					if (!obj->master())
						obj->mode_changed();
				}
			}
		}

		void _mode_sigh() { _mode_changed(true); }

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

			bool const first = !_fb_con.constructed();

			if (first) {
				_fb_con.construct(_env, Mode());
				_fb_con->mode_sigh(_mode_handler);
			}

			Session_component *session = new (md_alloc())
				Session_component(label, *_fb_con, _sessions, first, *this);

			return session;

		}

	public:

		Root(Env       &env,
		     Allocator &md_alloc)
		:
			Root_component { &env.ep().rpc_ep(), &md_alloc },
			_env           { env }
		{ }

		void request_ds() {
			_dataspace = _fb_con->dataspace();
			_mode_changed(false);
		}

		Dataspace_capability dataspace() const { return _dataspace; }
};

Dataspace_capability Framebuffer::Session_component::dataspace()
{
	if (_master)
		_root.request_ds();

	return _root.dataspace();
}

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
