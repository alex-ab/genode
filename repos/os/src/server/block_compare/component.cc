/*
 * \brief  Block comparator
 * \author Alexander Boettcher
 * \date   2020-03-15
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/* Genode includes */
#include <base/attached_ram_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <base/component.h>
#include <base/log.h>
#include <block/request_stream.h>
#include <block_session/connection.h>
#include <os/session_policy.h>

namespace Cmp {
	struct Main;
	struct Block_session_handler;
	struct Block_session_component;
	using namespace Genode;
}

struct Cmp::Block_session_handler : Interface
{
	Genode::Env             &env;

	Signal_handler<Block_session_handler> request_handler {
		env.ep(), *this, &Block_session_handler::handle};

	Block_session_handler(Env &env) : env(env) { }

	~Block_session_handler() { }

	virtual void handle_requests()= 0;

	void handle()
	{
		handle_requests();
	}

};

struct Cmp::Block_session_component : Rpc_object<::Block::Session>,
                                      Block_session_handler,
                                      ::Block::Request_stream
{
	Heap                &heap;
	Block::Connection<> &block_a;
	Block::Connection<> &block_b;

	Signal_handler<Block_session_component> _block_io_a {
		env.ep(), *this, &Block_session_component::_io_a };

	Signal_handler<Block_session_component> _block_io_b {
		env.ep(), *this, &Block_session_component::_io_b };

	void _io_a()
	{
		if (block_a.update_jobs(*this))
			Signal_transmitter(request_handler).submit();
	}

	void _io_b()
	{
		if (block_b.update_jobs(*this))
			Signal_transmitter(request_handler).submit();
	}

	Block_session_component(Env &env, Ram_dataspace_capability ram_cap,
	                        Heap &heap,
	                        Block::Connection<> &a,
	                        Block::Connection<> &b)
	:
	  Block_session_handler(env),
	  /* using b.info(), since it may be smaller than a, see checks on session creation */
	  Request_stream(env.rm(), ram_cap, env.ep(), request_handler, b.info()),
	  heap(heap), block_a(a), block_b(b)
	{
		env.ep().manage(*this);

		block_a.sigh(_block_io_a);
		block_b.sigh(_block_io_b);
	}

	~Block_session_component()
	{
		env.ep().dissolve(*this);
	}

	Info info() const override { return Request_stream::info(); }
	Capability<Tx> tx_cap() override { return Request_stream::tx_cap(); }

	typedef Block::Connection<>::Job Job;

	Block::Request client_request { { Block::Operation::Type::INVALID, 0, 0 },
	                                false, 0, 0 };

	enum State { NONE, WAIT_FOR_A, CONTINUE_WITH_B, WAIT_FOR_B, DONE_WITH_B } state { NONE };

	bool failure { false };

	void handle_requests() override
	{
		bool progress = true;

		while (progress) {
			progress = handle_request();
		}

		/* poke */
		wakeup_client_if_needed();
	}

	bool handle_request()
	{
		bool progress = false;

		with_requests([&] (::Block::Request request) {

			Block::Operation const operation = request.operation;

			if (request.operation.type != Block::Operation::Type::READ &&
			    request.operation.type != Block::Operation::Type::WRITE)
				Genode::log("pong ", request.operation, " ",
				            (int)request.operation.type, " current=",
				            (int)client_request.operation.type);

			if (client_request.operation.valid() &&
				(request.operation.type != client_request.operation.type ||
				 request.operation.block_number != client_request.operation.block_number ||
				 request.operation.count != client_request.operation.count)) {
				Genode::error("unexpected operation");
				return Response::RETRY;
			}

			if (failure)
				return Response::RETRY; //Response::REJECTED;

			if (!client_request.operation.valid()) {
				client_request = request;

				state = State::WAIT_FOR_A;

				new (heap) Job(block_a, operation);
				block_a.update_jobs(*this);
			}

			if (state == State::WAIT_FOR_A)
				return Response::RETRY;

			if (state == CONTINUE_WITH_B) {
				state = State::WAIT_FOR_B;

				new (heap) Job(block_b, operation);
				block_b.update_jobs(*this);
			}

			if (state == State::WAIT_FOR_B)
				return Response::RETRY;

			if (state != DONE_WITH_B)
				return Response::RETRY;

			/* done */
			request.success = true;

			bool ack_done = false;
			try_acknowledge([&](Ack &ack) {

				if (ack_done) return;

				ack.submit(request);
				ack_done = true;
			});

			if (!ack_done)
				Genode::log("tong ack?=", ack_done);

			/* reset */
			if (ack_done) {
				progress = true;
				state = NONE;
				client_request.operation.type = Block::Operation::Type::INVALID;
			}

			return ack_done ? Response::ACCEPTED : Response::RETRY;
		});

		return progress;
	}

	void produce_write_content(Job & job, off_t const offset,
	                           char * const dst, size_t const length)
	{
		with_payload([&] (Request_stream::Payload const &payload) {
			payload.with_content(client_request, [&] (void *src, size_t src_size) {
				if (uint64_t(offset) < job.operation().block_number * info().block_size) {
					Genode::error("offset vs block_number error");
					failure = true;
					return;
				}
				uint64_t const off = offset - (job.operation().block_number * info().block_size);
				if (off > src_size || length > src_size - off) {
					Genode::error("offset vs src_size vs length error");
					failure = true;
					return;
				}
				char * src_offset = (char *)src + off;

				if (state == State::WAIT_FOR_A) {
					memcpy(dst, src_offset, length);

					if (off + length == src_size) {
						state = CONTINUE_WITH_B;
					}
					return;
				}

				if (state == State::WAIT_FOR_B) {
					memcpy(dst, src_offset, length);

					if (off + length == src_size) {
						state = DONE_WITH_B;
					}
				}
			});
		});
	}

	void consume_read_result(Job &job, off_t const offset,
	                         char const * src, size_t const length)
	{
		if (job.operation().type != Block::Operation::Type::READ) {
			Genode::error("unsupported operation ", (int)job.operation().type);
			return;
		}

		if ((state != State::WAIT_FOR_A) && (state != State::WAIT_FOR_B)) {
			Genode::warning("not in right state called io_*");
			return;
		}

		with_payload([&] (Request_stream::Payload const &payload) {
			payload.with_content(client_request, [&] (void *dst, size_t dst_size) {
				if (uint64_t(offset) < job.operation().block_number * 512) {
					Genode::error("offset vs block_number error");
					failure = true;
					return;
				}
				uint64_t const off = offset - (job.operation().block_number * 512);
				if (off > dst_size || length > dst_size - off) {
					Genode::error("offset vs dst_size vs length error");
					failure = true;
					return;
				}
				char * dst_offset = (char *)dst + off;

				if (state == State::WAIT_FOR_A) {
					memcpy(dst_offset, src, length);

					if (off + length == dst_size) {
						state = CONTINUE_WITH_B;
					}
					return;
				}

				if (state == State::WAIT_FOR_B) {
					if (memcmp(dst_offset, src, length)) {
						Genode::error("compare failed ", job.operation());

						failure = true;
					} else {
						if (off + length == dst_size) {
							state = DONE_WITH_B;
						}
					}
				}
			});
		});
	}

	void completed(Job &job, bool success)
	{
		if (!success) {
			Genode::error(__func__, " ", job.operation(), " success=", success);
			failure = true;
		}

		destroy(heap, &job);
	}
};

struct Cmp::Main : Rpc_object<Typed_root<::Block::Session>>
{
	Env                                    &env;
	Attached_rom_dataspace                  config { env, "config" };
	Constructible<Attached_ram_dataspace>   block_ds { };
	Constructible<Block_session_component>  client   { };

	Genode::Heap                            heap { env.ram(), env.rm() };

	Allocator_avl                           alloc_a { &heap };
	Allocator_avl                           alloc_b { &heap };

	Block::Connection<>      server_a { env, &alloc_a, buffer_size(), "a" };
	Block::Connection<>      server_b { env, &alloc_b, buffer_size(), "b" };

	Main(Env &env) : env(env)
	{
		env.parent().announce(env.ep().manage(*this));
	}

	size_t buffer_size()
	{
		Number_of_bytes block_default { 128 * 1024 };
		return config.xml().attribute_value("buffer_size", block_default);
	}

	Session_capability session(Root::Session_args const &args,
	                           Affinity const &) override
	{
		if (client.constructed())
			throw Service_denied();

		Session_label  const label = label_from_args(args.string());
		Session_policy const policy(label, config.xml());

		Ram_quota const ram_quota = ram_quota_from_args(args.string());
		size_t const tx_buf_size =
			Arg_string::find_arg(args.string(), "tx_buf_size").ulong_value(0);

		if (!tx_buf_size)
			throw Service_denied();

		if (tx_buf_size > ram_quota.value) {
			error("insufficient 'ram_quota' from '", label, "',"
			      " got ", ram_quota, ", need ", tx_buf_size);
			throw Insufficient_ram_quota();
		}

		if (server_a.info().block_size != server_b.info().block_size) {
			error("block size of both block connections unequal ",
			      server_a.info().block_size, "!=", server_b.info().block_size);
			throw Service_denied();
		}

		if (server_a.info().block_count != server_b.info().block_count) {
			if (server_a.info().block_count < server_b.info().block_count) {
				error("block count of Block A smaller then from B");
				throw Service_denied();
			}
			warning("block count not equal"
			        " - Block A=", server_a.info().block_count,
			        " - Block B=", server_b.info().block_count,
			        " -> using block count from B reported to client");
		}

		bool const writeable = policy.attribute_value("writeable", false);

		if (!writeable || !server_a.info().writeable || !server_b.info().writeable) {
			error("block connection not writable");
			throw Service_denied();
		}

		try {
			if (!block_ds.constructed())
				block_ds.construct(env.ram(), env.rm(), tx_buf_size);
			if (!client.constructed())
				client.construct(env, block_ds->cap(),
				                 heap,
				                 server_a, server_b);
			return client->cap();
		} catch (...) {
			error("rejecting session request, no matching policy for '", label, "'");
		}

		throw Service_denied();
	}

	void upgrade(Session_capability, Root::Upgrade_args const&) override {
		Genode::warning(__func__, " not implemented"); }

	void close(Session_capability cap) override {
		if (!client.constructed() || !(client->cap() == cap))
			return;

		if (block_ds.constructed()) 
			block_ds.destruct();

		client.destruct();
	}
};

void Component::construct(Genode::Env &env) { static Cmp::Main server(env); }
