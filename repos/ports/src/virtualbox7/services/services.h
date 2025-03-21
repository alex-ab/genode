/*
 * \brief  Service backend
 * \author Christian Helmuth
 * \author Sebastian Sumpf
 * \date   2021-09-01
 */

/*
 * Copyright (C) 2021-2026 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#ifndef _SERVICES__SERVICES_H_
#define _SERVICES__SERVICES_H_

class Services;

namespace Genode {
	struct Env;
}

class Services
{
	private:

		Genode::Env             &_env;
		Genode::Signal_context *_focus_handler { nullptr };
		bool                    _focus         { false };

	public:

		Services(Genode::Env &);

		void focus_handler(Genode::Signal_context *);
		void focus_change(bool);
		void focus_active();

		Genode::Env & env();

		static void init(Genode::Env &);
		static Services &service();
};

#endif /* _SERVICES__SERVICES_H_ */
