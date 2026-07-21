/*
 * \brief  Test idle load of timeout framework
 * \author Johannes Schlatow
 * \date   2026-07-21
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License ve sion 3.
 */

/* Genode includes */
#include <base/component.h>
#include <base/mutex.h>
#include <timer_session/connection.h>

using namespace Genode;

class Main;

struct Timeout_component
{
	using One_shot_timeout = Timer::One_shot_timeout<Timeout_component>;

	Timer::Connection  _timer;
	Main              &_main;
	One_shot_timeout   _timeout { _timer, *this, &Timeout_component::handle_timeout };

	void handle_timeout(Duration);

	Timeout_component(Env &env, Main &main, Microseconds timeout)
	:
	  _timer(env), _main(main)
	{
		_timeout.schedule(timeout);
	}
};


class Main
{
	private:

		Env               &_env;
		Mutex              _mutex { };

		enum { NUM_TIMEOUTS = 200 };
		Constructible<Timeout_component> _timeouts[NUM_TIMEOUTS];

		size_t _timeouts_handled { 0 };

	public:

		void handle()
		{
			Mutex::Guard guard(_mutex);
			_timeouts_handled++;

			if (_timeouts_handled == NUM_TIMEOUTS)
				log("Startup complete");
		}

		Main(Env &env)
		: _env { env }
		{
			for (size_t i=0; i < NUM_TIMEOUTS; i++) {
				_timeouts[i].construct(_env, *this,
				                       i ? Microseconds { 1000 } : Microseconds { 6'000'000 });
			}
		}
};

void Timeout_component::handle_timeout(Duration)
{
	_main.handle();
}

void Component::construct(Env &env)
{
	static Main main { env };
}
