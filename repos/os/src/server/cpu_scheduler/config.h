#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <util/xml_node.h>

#include "session.h"

namespace Cpu {
	class Config;

	using Genode::Xml_node;
}

class Cpu::Config {

	public:

		Config() { }

		void apply(Xml_node const &, Child_list &);
};

#endif /* _CONFIG_H_ */
