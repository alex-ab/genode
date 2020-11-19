/*
 * \brief  Component-locale TRACE monitor for debugging purposes
 * \author Josef Soentgen
 * \date   2020-10-29
 */

#ifndef _INCLUDE__TRACE__TRACER_H_
#define _INCLUDE__TRACE__TRACER_H_

/* Genode includes */
#include <base/quota_guard.h>


namespace Tracer {

	using namespace Genode;

	struct Config
	{
		Ram_quota session_quota;
		Ram_quota arg_buffer_quota;
		Ram_quota trace_buffer_quota;
	};

	struct Id
	{
		unsigned value;
	};

	struct Lookup_result
	{
		Id   id;
		bool valid;
	};

	void init(Env &, Config const);

	Lookup_result lookup_subject(char const *label, char const *thread);

	void resume_tracing(Id const);
	void pause_tracing(Id const);

	void dump_trace_buffer(Id const);
}

#endif /* _INCLUDE__TRACE__TRACER_H_ */
