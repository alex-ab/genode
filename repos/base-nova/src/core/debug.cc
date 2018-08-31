#include <core_env.h>

#include <util/xml_generator.h>

#include <nova/syscalls.h>

using namespace Genode;
using namespace Nova;

extern Rom_module * dump_pd_statistics;
extern Genode::addr_t dump_pd_statistics_addr;

void dump_pd_stats()
{
	try {
		addr_t const core_pd_sel = platform_specific()->core_pd_sel();

		Genode::Xml_generator xml((char *)dump_pd_statistics_addr,
		                          dump_pd_statistics->size, "pds", [&] ()
		{
			addr_t overall_limit = 0, overall_usage = 0;
			Rpc_entrypoint *e = core_env()->entrypoint();
			e->apply_all(
				[&] (Genode::Object_pool<Genode::Rpc_object_base>::Entry* source) {
					Pd_session_component * obj = dynamic_cast<Pd_session_component *>(source);
					if (!obj)
						return;

					addr_t limit = 0; addr_t usage = 0;
					Nova::pd_ctrl_debug(obj->platform_pd().pd_sel(), limit, usage);

					xml.node("pd", [&] () {
						xml.attribute("limit", limit);
						xml.attribute("usage", usage);
						xml.attribute("name", obj->platform_pd().name());
					});

					overall_limit += limit;
					overall_usage += usage;
				}
			);

			addr_t limit = 0; addr_t usage = 0;
			Nova::pd_ctrl_debug(core_pd_sel, limit, usage);

			xml.node("pd", [&] () {
				xml.attribute("limit", limit);
				xml.attribute("usage", usage);
				xml.attribute("name", "core (real)");
			});

			xml.node("summary", [&] () {
				xml.node("user", [&] () {
					xml.attribute("limit", overall_limit);
					xml.attribute("usage", overall_usage);
				});
				xml.node("core", [&] () {
					xml.attribute("limit", limit);
					xml.attribute("usage", usage);
				});
				xml.node("overall", [&] () {
					xml.attribute("limit", limit + overall_limit);
					xml.attribute("usage", usage + overall_usage);
				});
			});
		});

		Signal_transmitter(dump_pd_statistics->cap).submit();
	} catch (...) {
		error(__func__, " exception catched");
	}
}


