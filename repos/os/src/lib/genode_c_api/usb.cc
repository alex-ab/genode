/*
 * \brief  Genode USB service provider C-API
 * \author Stefan Kalkowski
 * \date   2021-09-14
 */

/*
 * Copyright (C) 2006-2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/env.h>
#include <base/heap.h>
#include <base/quota_guard.h>
#include <base/session_object.h>
#include <root/component.h>
#include <os/dynamic_rom_session.h>
#include <os/reporter.h>
#include <os/session_policy.h>
#include <usb_session/usb_session.h>
#include <util/bit_allocator.h>

#include <genode_c_api/usb.h>

using namespace Genode;
using namespace Usb;


using String_item = String<64>;

static String_item string_item(genode_usb_dev_string_item_t func, void *opaque_data)
{
	char buf[String_item::capacity()] { };

	func({ buf, sizeof(buf) }, opaque_data);

	char *first = buf;
	char *last  = buf + sizeof(buf) - 1;

	/* skip leading whitespace */
	while (*first == ' ') ++first;

	/* trim trailing whitespace */
	while (last >= first && (*last == 0 || *last == ' '))
		--last;

	return { Cstring(first, last + 1 - first) };
}


/**
 * The API of the Registry class is favored in contrast to the ancient List,
 * but due to different potentially blocking user-level-scheduled threads,
 * e.g., within DDE Linux we cannot use a mutex-guarded registry.
 * Those threads are logical ones, and do not have real concurrency. Moreover,
 * only one and the same threads insert/remove/handle a single device.
 * Therefore, thread safety is not an issue.
 */
template <typename T>
struct Reg_list
{
	public:

		class Element : public List<Element>::Element
		{
			private:

				friend class Reg_list;

				Reg_list &_registry;
				T        &_object;

				/*
				 * Noncopyable
				 */
				Element(Element const &);
				Element &operator = (Element const &);

			public:

				Element(Reg_list &r, T &o);
				~Element();
		};

	private:

		List<Element> _elements { };

	public:

		template <typename FN>
		void for_each(FN const &fn)
		{
			Element *e = _elements.first(), *next = nullptr;
			for ( ; e; e = next) {
				next = e->next();
				fn(e->_object);
			}
		}

		template <typename FN>
		void for_each(FN const &fn) const
		{
			Element const *e = _elements.first(), *next = nullptr;
			for ( ; e; e = next) {
				next = e->next();
				fn(e->_object);
			}
		}

		void apply(auto const &condition, auto const &fn)
		{
			for (Element *e = _elements.first(); e; e = e->next())
				if (condition(e->_object)) {
					fn(e->_object);
					return;
				}
		}
};


template <typename T> Reg_list<T>::Element::Element(Reg_list &r, T &o)
: _registry(r), _object(o) { r._elements.insert(this); }



template <typename T> Reg_list<T>::Element::~Element() {
	_registry._elements.remove(this); }


struct genode_usb_endpoint : Reg_list<genode_usb_endpoint>::Element
{
	genode_usb_endpoint_descriptor desc;

	genode_usb_endpoint(Reg_list<genode_usb_endpoint> &registry,
	                    genode_usb_endpoint_descriptor desc)
	:
		Reg_list<genode_usb_endpoint>::Element(registry, *this),
		desc(desc) {}
};


struct genode_usb_interface : Reg_list<genode_usb_interface>::Element
{
	String_item                     const info;
	genode_usb_interface_descriptor const desc;
	bool                                  active;
	Reg_list<genode_usb_endpoint>   endpoints {};

	genode_usb_interface(Reg_list<genode_usb_interface> &registry,
	                     String_item              const &info,
	                     genode_usb_interface_descriptor desc,
	                     bool                            active)
	:
		Reg_list<genode_usb_interface>::Element(registry, *this),
		info(info), desc(desc), active(active) {}
};


struct genode_usb_configuration : Reg_list<genode_usb_configuration>::Element
{
	genode_usb_config_descriptor const desc;
	bool                               active;
	Reg_list<genode_usb_interface>     interfaces {};

	genode_usb_configuration(Reg_list<genode_usb_configuration> &registry,
	                         genode_usb_config_descriptor        desc,
	                         bool                                active)
	:
		Reg_list<genode_usb_configuration>::Element(registry, *this),
		desc(desc), active(active) {}
};


struct genode_usb_device : Reg_list<genode_usb_device>::Element
{
	genode_usb_bus_num_t               const bus;
	genode_usb_dev_num_t               const dev;
	genode_usb_speed_t                 const speed;
	String_item                        const manufacturer;
	String_item                        const product;
	genode_usb_device_descriptor       const desc;
	Reg_list<genode_usb_configuration> configs {};

	genode_usb_device(Reg_list<genode_usb_device> &registry,
	                  genode_usb_bus_num_t         bus,
	                  genode_usb_dev_num_t         dev,
	                  genode_usb_speed_t           speed,
	                  String_item           const &manufacturer,
	                  String_item           const &product,
	                  genode_usb_device_descriptor desc)
	:
		Reg_list<genode_usb_device>::Element(registry, *this),
		bus(bus), dev(dev), speed(speed),
		manufacturer(manufacturer), product(product), desc(desc) {}

	using Label = String<64>;

	Label label() const { return Label(bus, "-", dev); }

	String<32> speed_to_string() const
	{
		switch (speed) {
		case GENODE_USB_SPEED_LOW:            return "low";
		case GENODE_USB_SPEED_FULL:           return "full";
		case GENODE_USB_SPEED_HIGH:           return "high";
		case GENODE_USB_SPEED_SUPER:          return "super";
		case GENODE_USB_SPEED_SUPER_PLUS:     return "super_plus";
		case GENODE_USB_SPEED_SUPER_PLUS_2X2: return "super_plus_2x2";
		};

		return "full";
	}

	void generate(Generator &) const;
};


template <typename SESSION, typename T>
class Packet_handler
:
	private Reg_list<T>::Element
{
	protected:

		enum State { CONNECTED, DISCONNECTED };

		using Packet_descriptor = typename SESSION::Packet_descriptor;

		static constexpr size_t MAX_PACKETS = SESSION::TX_QUEUE_SIZE;

		State _state { CONNECTED };

		typename SESSION::Tx::Sink _sink;

		Constructible<Packet_descriptor> _packets[MAX_PACKETS] { };

		genode_external_memory_unregister_t _mem_unregister_fn;

		enum  Index_error  { OUT_OF_BOUNDS };
		using Index_result = Attempt<size_t, Index_error>;

		Index_result _idx_avail()
		{
			size_t idx = 0;
			for (auto const &p : _packets) {
				if (!p.constructed())
					break;
				++idx;
			}
			if (idx == MAX_PACKETS)
				return OUT_OF_BOUNDS;
			return idx;
		}

		bool _packet_avail()
		{
			unsigned packets_in_flight = 0;
			for (auto const &p : _packets)
				if (p.constructed()) packets_in_flight++;

			return _sink.packet_avail() &&
			       (_sink.ack_slots_free() > packets_in_flight);
		}

		template <typename FN>
		void _for_each_packet(FN const &fn)
		{
			while (true) {
				size_t idx = ~0UL;
				_idx_avail().with_result([&] (size_t i) { idx = i; },
				                         [&] (Index_error) {});

				if (idx > MAX_PACKETS || !_packet_avail())
					break;

				Packet_descriptor p = _sink.try_get_packet();
				if (!_sink.packet_valid(p))
					break;

				_packets[idx].construct(p);
				fn(_packets[idx]);
			}
		}

		enum  Packet_error { NO_PACKET };
		using Finish_packet_result = Attempt<Packet_descriptor, Packet_error>;

		Finish_packet_result _finish_packet(genode_usb_request_handle_t handle)
		{
			if ((addr_t)handle < (addr_t)&_packets ||
			    (addr_t)handle > ((addr_t)&_packets + sizeof(_packets)))
				return NO_PACKET;

			Constructible<Packet_descriptor> &cp =
				*reinterpret_cast<Constructible<Packet_descriptor>*>(handle);
			if (!cp.constructed())
				return NO_PACKET;

			Packet_descriptor p = *cp;
			cp.destruct();
			return p;
		}

		void _ack(Packet_descriptor::Return_value v,
		          size_t                          actual_size,
                  Packet_descriptor               p)
		{
			p.return_value        = v;
			p.payload_return_size = actual_size;
			if (!_sink.try_ack_packet(p))
				error("USB client's ack queue run full, looses packet ack!");
		}

		virtual void
		_handle_request(Constructible<Packet_descriptor> &cp,
		                genode_buffer                     payload,
		                genode_usb_req_callback_t const   callback,
		                void                             *opaque_data) = 0;
	public:

		Packet_handler(Env &env, Reg_list<T> &registry, T &obj,
		               Dataspace_capability ds_cap, addr_t const dma_addr,
		               Signal_context_capability sigh,
		               genode_external_memory_register_t register_fn,
		               genode_external_memory_unregister_t unregister_fn)
		:
			Reg_list<T>::Element(registry, obj),
			_sink(ds_cap, env.rm()),
			_mem_unregister_fn(unregister_fn)
		{
			_sink.register_sigh_ready_to_submit(sigh);
			_sink.register_sigh_ack_avail(sigh);

			register_fn((void*)dma_addr, _sink.ds_size(),
			            (void*)_sink.ds_local_base());
		}

		virtual ~Packet_handler()
		{
			_mem_unregister_fn((void*)_sink.ds_local_base());
		}

		bool connected() const { return _state == CONNECTED; }

		virtual void disconnect() { _state = DISCONNECTED; }

		virtual void wakeup() { _sink.wakeup(); }

		virtual bool request(genode_usb_req_callback_t const callback,
                             void                           *opaque_data)
		{
			bool ret = false;
			_for_each_packet([&] (Constructible<Packet_descriptor> &cp) {
				char *addr = _sink.packet_content(*cp);
				genode_buffer buf { addr, addr ? cp->size() : 0 };
				_handle_request(cp, buf, callback, opaque_data);
				ret = true;
			});
			return ret;
		}

		virtual void handle_disconnected()
		{
			if (_state != DISCONNECTED)
				return;

			while (_packet_avail()) {
				Packet_descriptor p = _sink.try_get_packet();
				if (_sink.packet_valid(p))
					_ack(Packet_descriptor::NO_DEVICE, 0, p);
			}
		}
};


class Usb_root;
class Device_component;


class Interface_component
: public Packet_handler<Interface_session, Interface_component>
{
	private:

		friend class Usb_root;
		friend class Device_component;

		using Base = Packet_handler<Interface_session, Interface_component>;

		enum { MAX_EPS = 30, UNUSED = 255 };

		Usb::Host_session::Id const _id;
		genode_usb_device::Label const _device_label;
		uint8_t _iface_idx;
		uint8_t _ep_addresses[MAX_EPS] { UNUSED };

		void
		_handle_request(Constructible<Packet_descriptor> &cp,
		                genode_buffer                     payload,
		                genode_usb_req_callback_t const   callback,
		                void                             *opaque_data) override;

	public:

		Usb::Host_session::Id const id() const {
			return _id; }

		genode_usb_device::Label label() const {
			return _device_label; }

		Interface_component(Usb::Host_session::Id          id,
		                    Env                           &env,
		                    Reg_list<Interface_component> &registry,
		                    Reg_list<genode_usb_device>   &devices,
		                    genode_usb_device::Label       label,
		                    Dataspace_capability           ds_cap,
		                    addr_t                   const dma_addr,
		                    Signal_context_capability      sigh_cap,
		                    uint8_t                        iface_idx,
		                    genode_external_memory_register_t register_fn,
		                    genode_external_memory_unregister_t unregister_fn);

		bool handle_response(genode_usb_request_handle_t request_id,
		                     genode_usb_request_ret_t    return_value,
		                     uint32_t                   *actual_sizes);
};


class Device_component
: public Packet_handler<Device_session, Device_component>
{
	private:

		friend class Usb_root;

		using Base = Packet_handler<Device_session, Device_component>;

		Usb::Host_session::Id const _id;

		Usb_root &_root;

		Heap &_heap;

		bool                       const _controls;
		genode_usb_device::Label   const _device_label;
		bool                             _warn_once { true };
		bool                             _warn_once_timeout { true };

		Reg_list<Interface_component> _interfaces {};

		void
		_handle_request(Constructible<Packet_descriptor> &cp,
		                genode_buffer                     payload,
		                genode_usb_req_callback_t const   callback,
		                void                             *opaque_data) override;

	public:

		Device_component(Usb::Host_session::Id           id,
		                 Env                            &env,
		                 Usb_root                       &root,
		                 Heap                           &heap,
		                 Reg_list<Device_component>     &registry,
		                 bool                     const  controls,
		                 genode_usb_device::Label const &device,
		                 Dataspace_capability            ds_cap,
		                 addr_t                   const  dma_addr,
		                 Signal_context_capability       sigh_cap,
		                 genode_external_memory_register_t register_fn,
		                 genode_external_memory_unregister_t unregister_fn);

		~Device_component();

		Usb::Host_session::Id const id() const {
			return _id; }

		genode_usb_device::Label label() const {
			return _device_label; }

		bool request(genode_usb_req_callback_t const callback,
		             void                           *opaque_data) override;
		bool handle_response(genode_usb_request_handle_t request_id,
		                     genode_usb_request_ret_t    return_value,
		                     uint32_t                   *actual_sizes);
		void disconnect() override;
		void handle_disconnected() override;
		void wakeup() override;
};


class Usb_root
:
	private Genode::Connection<Usb::Host_session>,
	private Genode::Rpc_client<Host_session>
{
	private:

		Env &_env;

		Heap _heap { _env.ram(), _env.rm() };

		Signal_context_capability   _sigh_cap;
		Reg_list<genode_usb_device> _devices {};

		Constructible<Expanding_reporter> _device_reporter { };

		genode_external_memory_register_t _mem_register_fn;
		genode_external_memory_unregister_t _mem_unregister_fn;
		genode_usb_dev_release_t _release_dev_fn;
		genode_usb_intf_release_t _release_intf_fn;

		Attached_dataspace _devices_ds { _env.rm(), devices_dataspace() };

		Signal_handler<Usb_root> _requests_handler { _env.ep(), *this,
		                                             &Usb_root::_handle_requests };

		Attached_dataspace _requests_ds { _env.rm(), requests_rom() };

		Reg_list<Device_component> _device_sessions {};

		void _acquire_device(Usb::Host_session::Id const,
		                     genode_usb_device::Label, addr_t);
		void _acquire_interface(Usb::Host_session::Id const,
		                        Usb::Host_session::Id const, addr_t, uint8_t);
		bool _release_device(Usb::Host_session::Id const);
		bool _release_interface(Usb::Host_session::Id const);
		void _handle_requests();

		Usb_root(const Usb_root&);
		Usb_root & operator=(const Usb_root&);

	public:

		Usb_root(Env &env, Signal_context_capability sigh_cap,
		         genode_external_memory_register_t register_fn,
		         genode_external_memory_unregister_t unregister_fn,
		         genode_usb_dev_release_t release_dev_fn,
		         genode_usb_intf_release_t release_intf_fn);

		/*
		 * Update report about existing USB devices
		 */
		void report();

		void device_add_endpoint(struct genode_usb_interface   *iface,
		                         genode_usb_endpoint_descriptor desc);
		void device_add_interface(struct genode_usb_configuration *cfg,
		                          genode_usb_dev_string_item_t     info_string,
		                          genode_usb_interface_descriptor  desc,
		                          genode_usb_dev_add_endp_t        callback,
		                          void                            *opaque_data,
		                          bool                             active);
		void device_add_configuration(struct genode_usb_device    *dev,
		                              genode_usb_config_descriptor desc,
		                              genode_usb_dev_add_iface_t   callback,
		                              void                        *opaque_data,
		                              bool                         active);
		void announce_device(genode_usb_bus_num_t         bus,
		                     genode_usb_dev_num_t         dev,
		                     genode_usb_speed_t           speed,
		                     genode_usb_dev_string_item_t manufacturer_string,
		                     genode_usb_dev_string_item_t product_string,
		                     genode_usb_device_descriptor desc,
		                     genode_usb_dev_add_config_t  callback,
		                     void                        *opaque_data);

		void discontinue_device(genode_usb_bus_num_t bus,
		                        genode_usb_dev_num_t dev);

		bool acquired(genode_usb_bus_num_t bus, genode_usb_dev_num_t dev);
		void released(genode_usb_bus_num_t bus, genode_usb_dev_num_t dev);
		void released(genode_usb_bus_num_t bus, genode_usb_dev_num_t dev,
		              genode_usb_intf_num_t ifnum);

		bool request(genode_usb_bus_num_t            bus,
		             genode_usb_dev_num_t            dev,
		             genode_usb_req_callback_t const callback,
		             void                           *opaque_data);

		void handle_response(genode_usb_request_handle_t request_id,
		                     genode_usb_request_ret_t    return_value,
		                     uint32_t                   *actual_sizes);

		/*
		 * Acknowledge requests from sessions with disconnected devices
		 */
		void handle_disconnected_sessions();

		void wakeup();

		void set_interface(genode_usb_device::Label label,
		                   uint16_t num, uint16_t alt);
		void set_configuration(genode_usb_device::Label label,
		                       uint16_t num);


		/***********************************
		 ** Host session client interface **
		 ***********************************/

		Dataspace_capability devices_dataspace() override {
			return call<Rpc_devices_ds>(); }

		Submit_result devices_submit(size_t size) override {
			return retry(Ram_quota{20*1024}, Cap_quota{6}, [&] () {
				return call<Rpc_devices_submit>(size); }); }

		Rom_dataspace_capability requests_rom() override {
			return call<Rpc_requests_rom>(); }

		void requests_sigh(Signal_context_capability sigh) override {
			call<Rpc_requests_sigh>(sigh); }

		void sigh(Signal_context_capability sigh) override {
			call<Rpc_sigh>(sigh); }

		Dataspace_result client_dataspace(Id client_id) override {
			return call<Rpc_client_ds>(client_id); }

		Signal_result client_sigh(Id client_id) override {
			return call<Rpc_client_sigh>(client_id); }

		void acquired(Id client_id) override {
			call<Rpc_acquired>(client_id); }

		void released(Id client_id) override {
			call<Rpc_released>(client_id); }
};


static Usb_root * _usb_root  = nullptr;


void genode_usb_device::generate(Generator &g) const
{
	using Value = String<64>;

	auto per_endp = [&] (genode_usb_endpoint const &endp)
	{
		g.node("endpoint", [&] {
			g.attribute("address",         Value(Hex(endp.desc.address)));
			g.attribute("attributes",      Value(Hex(endp.desc.attributes)));
			g.attribute("max_packet_size", Value(Hex(endp.desc.max_packet_size)));
		});
	};

	auto per_iface = [&] (genode_usb_interface const &iface)
	{
		g.node("interface", [&] {
			g.attribute("active",      iface.active ? "yes" : "no");
			g.attribute("number",      Value(Hex(iface.desc.number)));
			if (*iface.info.string()) g.attribute("info", iface.info);
			g.attribute("alt_setting", Value(Hex(iface.desc.alt_settings)));
			g.attribute("class",       Value(Hex(iface.desc.iclass)));
			g.attribute("subclass",    Value(Hex(iface.desc.isubclass)));
			g.attribute("protocol",    Value(Hex(iface.desc.iprotocol)));
			g.tabular([&] { iface.endpoints.for_each(per_endp); });
		});
	};

	auto per_config = [&] (genode_usb_configuration const &cfg)
	{
		g.node("config", [&] {
			g.attribute("active", cfg.active ? "yes" : "no");
			g.attribute("value",  Value(Hex(cfg.desc.config_value)));
			cfg.interfaces.for_each(per_iface);
		});
	};

	g.node("device", [&] {
		g.attribute("name",       label());
		g.attribute("class",      Value(Hex(desc.dclass)));
		if (*manufacturer.string()) g.attribute("manufacturer", manufacturer);
		if (*product.string())      g.attribute("product", product);
		g.attribute("vendor_id",  Value(Hex(desc.vendor_id)));
		g.attribute("product_id", Value(Hex(desc.product_id)));
		g.attribute("speed",      speed_to_string());
		configs.for_each(per_config);
	});
}


void
Interface_component::_handle_request(Constructible<Packet_descriptor> &cpd,
                                     genode_buffer                     payload,
                                     genode_usb_req_callback_t const   cbs,
                                     void                             *opaque_data)
{
	genode_usb_request_handle_t handle = &cpd;
	bool granted = false;

	for (unsigned i = 0; i < MAX_EPS && !granted; i++)
		if (_ep_addresses[i] == cpd->index) granted = true;

	if (!granted) {
		handle_response(handle, INVALID, nullptr);
		return;
	}

	switch (cpd->type) {
	case Packet_descriptor::BULK:
		cbs->bulk_fn(handle, cpd->index, payload, opaque_data);
		break;
	case Packet_descriptor::IRQ:
		cbs->irq_fn(handle, cpd->index, payload, opaque_data);
		break;
	case Packet_descriptor::ISOC:
		{
			genode_usb_isoc_transfer_header &hdr =
				*reinterpret_cast<genode_usb_isoc_transfer_header*>(payload.start);

			size_t const header_size = sizeof(genode_usb_isoc_transfer_header) +
			                           (hdr.number_of_packets *
			                           sizeof(genode_usb_isoc_descriptor));

			genode_buffer isoc_payload {
				payload.start + header_size,
				payload.num_bytes - header_size };

			cbs->isoc_fn(handle, cpd->index, hdr.number_of_packets,
			             hdr.packets, isoc_payload, opaque_data);
			break;
		}
	case Packet_descriptor::FLUSH:
		cbs->flush_fn(cpd->index, handle, opaque_data);
		break;
	};
}


bool
Interface_component::handle_response(genode_usb_request_handle_t handle,
                                     genode_usb_request_ret_t    value,
                                     uint32_t                   *actual_sizes)
{
	bool ret = false;
	_finish_packet(handle).with_result(
		[&] (Packet_descriptor p) {
			ret = true;
			Packet_descriptor::Return_value v {};
			switch (value) {
			case OK:        v = Packet_descriptor::OK;        break;
			case NO_DEVICE: v = Packet_descriptor::NO_DEVICE; break;
			case INVALID:   v = Packet_descriptor::INVALID;   break;
			case HALT:      v = Packet_descriptor::HALT;      break;
			case TIMEOUT:
				error("timeout shouldn't be returned for transfer URBs");
			};

			/* return actual isoc frames sizes */
			if ((value == OK) &&
			    (p.type == Packet_descriptor::ISOC)) {
				void * data = _sink.packet_content(p);
				genode_usb_isoc_transfer_header &hdr =
					*reinterpret_cast<genode_usb_isoc_transfer_header*>(data);
				for (size_t i = 0; i < hdr.number_of_packets; i++)
					hdr.packets[i].actual_size = actual_sizes[i+1];
			}

			_ack(v, (value == OK) ? actual_sizes[0] : 0, p);
		},
		[&] (Packet_error) {});
	return ret;
}


Interface_component::Interface_component(Usb::Host_session::Id          id,
                                         Env                           &env,
                                         Reg_list<Interface_component> &registry,
                                         Reg_list<genode_usb_device>   &devices,
                                         genode_usb_device::Label       device,
                                         Dataspace_capability           ds_cap,
                                         addr_t                   const dma_addr,
                                         Signal_context_capability      sigh_cap,
                                         uint8_t                        iface_idx,
                                         genode_external_memory_register_t register_fn,
                                         genode_external_memory_unregister_t unregister_fn)
:
	Base(env, registry, *this, ds_cap, dma_addr, sigh_cap,
	     register_fn, unregister_fn),
	_id(id),
	_device_label(device),
	_iface_idx(iface_idx)
{
	unsigned idx = 0;
	devices.apply(
		[&] (auto const &dev) {
			return dev.label() == _device_label; },
		[&] (auto &dev) {
			dev.configs.apply(
				[&] (auto const & cfg) {
					return cfg.active; },
				[&] (auto &cfg) {
					cfg.interfaces.apply(
						[&] (auto const &iface) {
							return iface.desc.number == iface_idx; },
						[&] (auto const &iface) {
							iface.endpoints.for_each(
								[&] (auto const &ep) {
									if (idx < MAX_EPS)
										_ep_addresses[idx++] = ep.desc.address;
								});
					});
			});
	});
}


void
Device_component::_handle_request(Constructible<Packet_descriptor> &cpd,
                                  genode_buffer                     payload,
                                  genode_usb_req_callback_t const   cbs,
                                  void                             *opaque_data)
{
	using P = Packet_descriptor;

	genode_usb_request_handle_t handle = &cpd;
	bool granted = false;

	switch (cpd->request) {
	case P::GET_STATUS:        [[fallthrough]];
	case P::GET_DESCRIPTOR:    [[fallthrough]];
	case P::GET_CONFIGURATION: [[fallthrough]];
	case P::GET_INTERFACE:
		granted = true;
		break;
	case P::SET_INTERFACE:

		/**
		 * Currently, we check for open interfaces only,
		 * if a client requests an alternate interface setting
		 * for a non-acquired interface it will fail!
		 */
		_interfaces.apply(
			[&] (Interface_component &ic) {
				return ic._iface_idx == cpd->index;
			},
			[&] (Interface_component &) { granted = true; });
		if (granted) break;

		[[fallthrough]];
	default: granted = _controls;
	};

	if (!granted) {
		uint32_t ret = 0;
		handle_response(handle, INVALID, &ret);
		if (_warn_once) {
			warning("Invalid restricted control URB to device ", _device_label);
			_warn_once = false;
		}
		return;
	}

	/**
	 * Choose max timeout based on USB_CTRL_GET_TIMEOUT && USB_CTRL_SET_TIMEOUT
	 * value as defined by linux/include/linux/usb.h, which is 5'000 by now.
	 */
	auto const timeout_ms = (cpd->timeout > 5'000 || !cpd->timeout)
	                      ? 5'000 : cpd->timeout;

	if ((_warn_once_timeout || cpd->timeout) && timeout_ms != cpd->timeout) {
		warning("Control URB timeout adjusted from ",
		        cpd->timeout ? String<16>(cpd->timeout)
		                     : String<16>("infinity "),
		        "ms to ", timeout_ms, "ms of device ", _device_label);
		_warn_once_timeout = false;
	}

	cbs->ctrl_fn(handle, cpd->request, cpd->request_type, cpd->value,
	             cpd->index, timeout_ms, payload, opaque_data);
}


bool Device_component::request(genode_usb_req_callback_t const callback,
                               void                           *opaque_data)
{
	bool ret = false;

	_interfaces.apply(
		[&] (Interface_component &ic) {
			return ic.request(callback, opaque_data); },
		[&] (Interface_component &) { ret = true; });

	return ret ? ret : Base::request(callback, opaque_data);
}


bool
Device_component::handle_response(genode_usb_request_handle_t handle,
                                  genode_usb_request_ret_t    value,
                                  uint32_t                   *actual_sizes)
{
	bool ret = false;

	_finish_packet(handle).with_result(
		[&] (Packet_descriptor p) {
			ret = true;
			Packet_descriptor::Return_value v =
				Packet_descriptor::INVALID;
			switch (value) {
			case OK:        v = Packet_descriptor::OK;        break;
			case NO_DEVICE: v = Packet_descriptor::NO_DEVICE; break;
			case INVALID:   v = Packet_descriptor::INVALID;   break;
			case TIMEOUT:   v = Packet_descriptor::TIMEOUT;   break;
			case HALT:      v = Packet_descriptor::HALT;      break;
			};
			_ack(v, (value == OK) ? actual_sizes[0] : 0, p);

			if (value != OK)
				return;

			if (p.request == Packet_descriptor::SET_INTERFACE &&
			    p.request_type == Packet_descriptor::IFACE) {
				_root.set_interface(_device_label, p.index, p.value);
				return;
			}

			if (p.request == Packet_descriptor::SET_CONFIGURATION &&
			    p.request_type == Packet_descriptor::DEVICE) {
				_root.set_configuration(_device_label, p.value);
				return;
			}

		},
		[&] (Packet_error) {});

	if (!ret)
		_interfaces.apply(
			[&] (auto &ic) {
				return ic.handle_response(handle, value, actual_sizes); },
			[&] (auto &) {
				ret = true; });

	return ret;
}


void Device_component::disconnect()
{
	_interfaces.for_each([&] (auto &iface) { iface.disconnect(); });
	Base::disconnect();
}


void Device_component::handle_disconnected()
{
	_interfaces.for_each([&] (auto &iface) { iface.handle_disconnected(); });
	Base::handle_disconnected();
}


void Device_component::wakeup()
{
	_interfaces.for_each([&] (auto &iface) { iface.wakeup(); });
	Base::wakeup();
}


Device_component::Device_component(Usb::Host_session::Id           id,
                                   Env                            &env,
                                   Usb_root                       &root,
                                   Heap                           &heap,
                                   Reg_list<Device_component>     &registry,
                                   bool                     const  controls,
                                   genode_usb_device::Label const &device,
                                   Dataspace_capability            ds_cap,
                                   addr_t                   const  dma_addr,
                                   Signal_context_capability       sigh,
                                   genode_external_memory_register_t register_fn,
                                   genode_external_memory_unregister_t unregister_fn)
:
	Packet_handler<Device_session, Device_component>(env, registry, *this,
	                                                 ds_cap, dma_addr, sigh,
	                                                 register_fn, unregister_fn),
	_id(id),
	_root(root), _heap(heap),
	_controls(controls),
	_device_label(device) {}


Device_component::~Device_component()
{
	_interfaces.for_each([&] (auto &iface) {
		_root.released(iface.id());
		destroy(_heap, &iface);
	});

	_root.released(id());
}


void Usb_root::_acquire_device(Usb::Host_session::Id const id,
                               genode_usb_device::Label name, addr_t dma_addr)
{
	client_dataspace(id).with_result([&] (auto ds_cap) {
		client_sigh(id).with_result([&] (auto sigh_cap) {
			new (_heap) Device_component(id, _env, *this, _heap,
			                             _device_sessions, true, name, ds_cap,
			                             dma_addr, sigh_cap, _mem_register_fn,
			                             _mem_unregister_fn);
			acquired(id);
		}, [] (auto) { /* ignore invalid ids */ });
	}, [] (auto) { /* ignore invalid ids */ });
}


void Usb_root::_acquire_interface(Usb::Host_session::Id const id,
                                  Usb::Host_session::Id const dev_id,
                                  addr_t dma_addr, uint8_t iface)
{
	client_dataspace(id).with_result([&] (auto ds_cap) {
		client_sigh(id).with_result([&] (auto sigh_cap) {
			_device_sessions.apply(
				[&] (auto & dc) { return dc.id().value == dev_id.value; },
				[&] (auto &dc) {
					new (_heap) Interface_component(id, _env, dc._interfaces,
					                                _devices, dc.label(), ds_cap,
					                                dma_addr, sigh_cap, iface,
					                                _mem_register_fn,
					                                _mem_unregister_fn);
					acquired(id);
				});
		}, [] (auto) { /* ignore invalid ids */ });
	}, [] (auto) { /* ignore invalid ids */ });
}


bool Usb_root::_release_device(Usb::Host_session::Id const id)
{
	bool release_delayed = false;

	_device_sessions.apply(
		[&] (auto & dc) {
			return dc.id().value == id.value; },
		[&] (auto &dc) {
			if (dc.connected())
				_devices.apply(
					[&] (auto &device) {
						return device.label() == dc.label(); },
					[&] (auto &device) {
						release_delayed =
							!_release_dev_fn(device.bus, device.dev);
					});

			if (release_delayed)
				return;

			destroy(_heap, &dc);
		});

	return release_delayed;
}


bool Usb_root::_release_interface(Usb::Host_session::Id const id)
{
	bool release_delayed = false;

	_device_sessions.for_each(
		[&] (auto &dc) {
			dc._interfaces.apply(
				[&] (auto const &ic) {
					return ic.id().value == id.value; },
				[&] (auto &ic) {
					if (ic.connected()) {
						_devices.apply(
							[&] (auto &device) {
								return device.label() == dc.label(); },
							[&] (auto &device) {
								release_delayed =
									!_release_intf_fn(device.bus, device.dev,
									                  ic._iface_idx);
							});
					}

					if (release_delayed)
						return;

					destroy(_heap, &ic);
					released(id);
				});
		});

	return release_delayed;
}


void Usb_root::_handle_requests()
{
	if (!_requests_ds.local_addr<void const>())
		return;

	bool release_delayed = false;

	Node node(Const_byte_range_ptr(_requests_ds.local_addr<char>(),
	                               _requests_ds.size()));
	Buffered_node buffered_node(_heap, node);

	buffered_node.for_each_sub_node([&] (auto const &node) {

		static constexpr uint8_t INVALID_IFACE = 255;

		Usb::Host_session::Id const id { node.attribute_value("name", ~0UL) };
		auto const dev = node.attribute_value("device",
		                                       genode_usb_device::Label());
		uint8_t const iface = node.attribute_value("interface", INVALID_IFACE);
		Usb::Host_session::Id const dev_id {
			node.attribute_value("device_id", ~0UL) };
		addr_t const dma_addr = node.attribute_value("dma_addr", ~0UL);

		bool const iface_request = iface < INVALID_IFACE;

		if (node.has_type("acquire")) {
			if (iface_request) _acquire_interface(id, dev_id, dma_addr, iface);
			else               _acquire_device(id, dev, dma_addr);
			return;
		}

		if (node.has_type("release")) {
			bool delayed = (iface_request) ? _release_interface(id)
			                               : _release_device(id);
			release_delayed = delayed ? delayed : release_delayed;
			return;
		}
	});

	if (release_delayed) Signal_transmitter(_sigh_cap).submit();
};


void Usb_root::set_interface(genode_usb_device::Label label,
                             uint16_t num, uint16_t alt)
{
	bool changed = false;
	_devices.apply(
		[&] (auto &dev) {
			return dev.label() == label; },
		[&] (auto &dev) {
			dev.configs.apply(
				[&] (auto &cfg) {
					return cfg.active; },
				[&] (auto &cfg) {
					cfg.interfaces.for_each(
						[&] (auto &iface) {
							if (iface.desc.number != num)
								return;

							if (iface.active != (iface.desc.alt_settings == alt)) {
								iface.active = (iface.desc.alt_settings == alt);
								changed = true;
							}
					});
			});
	});

	if (changed) report();
}


void Usb_root::set_configuration(genode_usb_device::Label label,
                                 uint16_t num)
{
	bool changed = false;
	_devices.apply(
		[&] (auto &dev) {
			return dev.label() == label; },
		[&] (auto &dev) {
			dev.configs.for_each(
				[&] (auto &cfg) {
					if (cfg.active != (cfg.desc.config_value == num)) {
						cfg.active = (cfg.desc.config_value == num);
						changed = true;

						/* activate all interfaces with alt_setting 0 */
						cfg.interfaces.for_each(
							[&] (auto &iface) {
							if (cfg.active)
								iface.active = (iface.desc.alt_settings == 0);
							else
								iface.active = false;
						});
					}
			});
	});

	if (changed) report();
}


void Usb_root::report()
{
	Byte_range_ptr bytes(_devices_ds.local_addr<char>(), _devices_ds.size());
	Generator::generate(bytes, "devices", [&] (Generator &g) {
		_devices.for_each([&] (genode_usb_device & d) { d.generate(g); });
	}).with_result(
		[&] (size_t n) {
			/* quota errors are already handled in client function */
			(void) devices_submit(n);
		},
		[&] (auto) {
			Genode::error("Device report exceeded buffer!");
		});
}


void Usb_root::device_add_endpoint(struct genode_usb_interface   *iface,
                                   genode_usb_endpoint_descriptor desc)
{
	new (_heap) genode_usb_endpoint(iface->endpoints, desc);
}


void Usb_root::device_add_interface(struct genode_usb_configuration *cfg,
                                    genode_usb_dev_string_item_t     info_string,
                                    genode_usb_interface_descriptor  desc,
                                    genode_usb_dev_add_endp_t        callback,
                                    void                            *opaque_data,
                                    bool                             active)
{
	String_item info { string_item(info_string, opaque_data) };

	genode_usb_interface *iface = new (_heap)
		genode_usb_interface(cfg->interfaces, info, desc, active);
	for (unsigned i = desc.num_endpoints; i > 0; i--)
		callback(iface, i-1, opaque_data);
}


void Usb_root::device_add_configuration(struct genode_usb_device    *dev,
                                        genode_usb_config_descriptor desc,
                                        genode_usb_dev_add_iface_t   callback,
                                        void                        *opaque_data,
                                        bool                         active)
{
	genode_usb_configuration *config = new (_heap)
		genode_usb_configuration(dev->configs, desc, active);
	for (unsigned i = desc.num_interfaces; i > 0; i--)
		callback(config, i-1, opaque_data);
}


void Usb_root::announce_device(genode_usb_bus_num_t         bus,
                               genode_usb_dev_num_t         dev,
                               genode_usb_speed_t           speed,
                               genode_usb_dev_string_item_t manufacturer_string,
                               genode_usb_dev_string_item_t product_string,
                               genode_usb_device_descriptor desc,
                               genode_usb_dev_add_config_t  callback,
                               void                        *opaque_data)
{
	String_item manufacturer { string_item(manufacturer_string, opaque_data) };
	String_item product      { string_item(product_string, opaque_data) };

	genode_usb_device *device = new (_heap)
		genode_usb_device(_devices, bus, dev, speed, manufacturer, product, desc);
	for (unsigned i = desc.num_configs; i > 0; i--)
		callback(device, i-1, opaque_data);
	report();
}


void Usb_root::discontinue_device(genode_usb_bus_num_t bus,
                                  genode_usb_dev_num_t dev)
{
	_devices.apply(
		[&] (genode_usb_device & device) {
			return device.bus == bus && device.dev == dev; },

		[&] (genode_usb_device & device) {

			_device_sessions.apply(
				[&] (auto & dc) {
					return dc.label() == device.label(); },
				[&] (auto &dc) { dc.disconnect(); });

			device.configs.for_each([&] (genode_usb_configuration & cfg) {
				cfg.interfaces.for_each([&] (genode_usb_interface & iface) {
					iface.endpoints.for_each([&] (genode_usb_endpoint & endp) {
						Genode::destroy(_heap, &endp); });
					Genode::destroy(_heap, &iface);
				});
				Genode::destroy(_heap, &cfg);
			});
			Genode::destroy(_heap, &device);
	});
	report();
}


bool Usb_root::acquired(genode_usb_bus_num_t bus, genode_usb_dev_num_t dev)
{
	bool ret = false;

	_devices.apply(
		[&] (genode_usb_device & device) {
			return device.bus == bus && device.dev == dev; },
		[&] (genode_usb_device & device) {
			_device_sessions.apply(
				[&] (auto & dc) {
					return dc.label() == device.label() &&
					       dc.connected(); },
				[&] (auto &) { ret = true; });
	});

	return ret;
}


void Usb_root::released(genode_usb_bus_num_t bus, genode_usb_dev_num_t dev)
{
	_devices.apply(
		[&] (genode_usb_device & device) {
			return device.bus == bus && device.dev == dev; },
		[&] (genode_usb_device & device) {
			_device_sessions.apply(
				[&] (auto & dc) {
					return dc.label() == device.label(); },
				[&] (auto &dc) { destroy(_heap, &dc); });
	});
}


void Usb_root::released(genode_usb_bus_num_t bus, genode_usb_dev_num_t dev,
                        genode_usb_intf_num_t ifnum)
{
	_devices.apply(
		[&] (genode_usb_device & device) {
			return device.bus == bus && device.dev == dev; },
		[&] (genode_usb_device & device) {
			_device_sessions.apply(
				[&] (auto & dc) {
					return dc.label() == device.label(); },
				[&] (auto &dc) {
					dc._interfaces.apply(
						[&] (auto &iface) {
							return iface._iface_idx == ifnum; },
						[&] (auto &iface) {
							released(iface.id());
							destroy(_heap, &iface);
						});
				});
		});
}


bool Usb_root::request(genode_usb_bus_num_t            bus,
                       genode_usb_dev_num_t            dev,
                       genode_usb_req_callback_t const callback,
                       void *                          opaque_data)
{
	bool ret = false;

	_devices.apply(
		[&] (genode_usb_device & device) {
			return device.bus == bus && device.dev == dev; },

		[&] (genode_usb_device & device) {
			_device_sessions.apply(
				[&] (auto &dc) {
					return dc.label() == device.label() &&
					       dc.request(callback, opaque_data); },
				[&] (auto &) { ret = true; });
	});

	return ret;
}


void Usb_root::handle_response(genode_usb_request_handle_t id,
                               genode_usb_request_ret_t    ret,
                               uint32_t*                   actual_sizes)
{
	_device_sessions.apply(
		[&] (auto & dc) {
			return dc.handle_response(id, ret, actual_sizes); },
		[&] (auto &) { });
}


void Usb_root::handle_disconnected_sessions()
{
	_device_sessions.for_each([&] (auto & dc) {
		dc.handle_disconnected(); });
}

void Usb_root::wakeup()
{
	_device_sessions.for_each([&] (auto & dc) { dc.wakeup(); });
}


Usb_root::Usb_root(Env &env, Signal_context_capability global_sigh,
                   genode_external_memory_register_t register_fn,
                   genode_external_memory_unregister_t unregister_fn,
                   genode_usb_dev_release_t release_dev_fn,
                   genode_usb_intf_release_t release_intf_fn)
:
	Genode::Connection<Usb::Host_session>(env, Label("usb"),
	                                      Ram_quota{RAM_QUOTA}, Args()),
	Genode::Rpc_client<Host_session>(cap()),
	_env(env), _sigh_cap(global_sigh),
	_mem_register_fn(register_fn), _mem_unregister_fn(unregister_fn),
	_release_dev_fn(release_dev_fn),
	_release_intf_fn(release_intf_fn)
{
	sigh(global_sigh);
	requests_sigh(_requests_handler);
	_handle_requests();
}


void
Genode_c_api::initialize_usb_service(Env                      &env,
                                     Signal_context_capability sigh,
                                     genode_external_memory_register_t register_fn,
                                     genode_external_memory_unregister_t unregister_fn,
                                     genode_usb_dev_release_t  release_dev_fn,
                                     genode_usb_intf_release_t release_intf_fn)
{
	static Usb_root root(env, sigh, register_fn, unregister_fn, release_dev_fn,
	                     release_intf_fn);
	_usb_root = &root;
}


extern "C"
void genode_usb_device_add_endpoint(struct genode_usb_interface   *iface,
                                    genode_usb_endpoint_descriptor desc)
{
	if (_usb_root) _usb_root->device_add_endpoint(iface, desc);
}


extern "C"
void genode_usb_device_add_interface(struct genode_usb_configuration *cfg,
                                     genode_usb_dev_string_item_t     info_string,
                                     genode_usb_interface_descriptor  desc,
                                     genode_usb_dev_add_endp_t        callback,
                                     void                            *opaque_data,
                                     bool                             active)
{
	if (_usb_root)
		_usb_root->device_add_interface(cfg, info_string, desc, callback,
		                                opaque_data, active);
}


extern "C"
void genode_usb_device_add_configuration(struct genode_usb_device    *dev,
                                         genode_usb_config_descriptor desc,
                                         genode_usb_dev_add_iface_t   callback,
                                         void                        *opaque_data,
                                         bool                         active)
{
	if (_usb_root)
		_usb_root->device_add_configuration(dev, desc, callback,
		                                    opaque_data, active);
}



extern "C"
void genode_usb_announce_device(genode_usb_bus_num_t         bus,
                                genode_usb_dev_num_t         dev,
                                genode_usb_speed_t           speed,
                                genode_usb_dev_string_item_t manufacturer_string,
                                genode_usb_dev_string_item_t product_string,
                                genode_usb_device_descriptor desc,
                                genode_usb_dev_add_config_t  callback,
                                void                        *opaque_data)
{
	if (_usb_root)
		_usb_root->announce_device(bus, dev, speed,
		                           manufacturer_string, product_string,
		                           desc, callback, opaque_data);
}


extern "C" void genode_usb_discontinue_device(genode_usb_bus_num_t bus,
                                              genode_usb_dev_num_t dev)
{
	if (_usb_root) _usb_root->discontinue_device(bus, dev);
}


extern "C" bool
genode_usb_device_acquired(genode_usb_bus_num_t bus,
                           genode_usb_dev_num_t dev)
{
	return _usb_root ? _usb_root->acquired(bus, dev) : false;
}


extern "C" void
genode_usb_device_released(genode_usb_bus_num_t bus,
                           genode_usb_dev_num_t dev)
{
	if (_usb_root) _usb_root->released(bus, dev);
}


extern "C" void
genode_usb_interface_released(genode_usb_bus_num_t  bus,
                              genode_usb_dev_num_t  dev,
                              genode_usb_intf_num_t ifnum)
{
	if (_usb_root) _usb_root->released(bus, dev, ifnum);
}


extern "C" bool
genode_usb_request_by_bus_dev(genode_usb_bus_num_t            bus,
                              genode_usb_dev_num_t            dev,
                              genode_usb_req_callback_t const callback,
                              void                           *opaque_data)
{
	return _usb_root ? _usb_root->request(bus, dev, callback, opaque_data)
	                 : false;
}


extern "C" void
genode_usb_ack_request(genode_usb_request_handle_t request_id,
                       genode_usb_request_ret_t    ret,
                       uint32_t                   *actual_sizes)
{
	if (_usb_root)
		_usb_root->handle_response(request_id, ret, actual_sizes);
}


extern "C" void genode_usb_notify_peers()
{
	if (_usb_root) _usb_root->wakeup();
}


extern "C" void genode_usb_handle_disconnected_sessions()
{
	if (_usb_root) _usb_root->handle_disconnected_sessions();
}
