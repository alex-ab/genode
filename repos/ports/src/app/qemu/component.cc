/*
 * \brief  Qemu for Genode
 * \author Alexander Boettcher
 * \date   2019-05-12
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <libc/component.h>

extern "C" int main(int argc, char const **argv, char **envp);

void Libc::Component::construct(Libc::Env &) {
	int argc = 2;
	char const *argv[] = { "qemu", "-version", 0 };

	Libc::with_libc([&] () {
		main(argc, argv, nullptr);
	});
}
