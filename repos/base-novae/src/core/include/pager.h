/*
 * \brief  Paging-server framework
 * \author Norman Feske
 * \date   2006-04-28
 */

/*
 * Copyright (C) 2006-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

#include <base/internal/native_thread.h>

/* Genode includes */
#include <base/thread.h>
#include <base/object_pool.h>
#include <base/capability.h>
#include <base/session_label.h>
#include <cpu_session/cpu_session.h>
#include <pager/capability.h>

/* NOVAe includes */
#include <novae/cap_map.h>
#include <novae/capability_space.h>
#include <novae/syscalls.h>

/* core includes */
#include <ipc_pager.h>
#include <rpc_cap_factory.h>

namespace Core {

	class Pager_entrypoint;
	class Pager_object;
	class Exception_handlers;

	using Pager_capability = Capability<Pager_object>;
}


class Core::Exception_handlers
{
	private:

		template <uint8_t EV>
		__attribute__((regparm(2))) static void _handler(Pager_object &, unsigned);

	public:

		Exception_handlers(Pager_object &);

		template <uint8_t EV>
		void register_handler(Pager_object &, Novae::Mtd,
		                      void (__attribute__((regparm(2)))*)(Pager_object &, unsigned) = nullptr);
};


class Core::Pager_object : public Object_pool<Pager_object>::Entry
{
	private:

		unsigned long _badge;  /* used for debugging */

		/**
		 * User-level signal handler registered for this pager object via
		 * 'Cpu_session::exception_handler()'.
		 */
		Signal_context_capability _exception_sigh { };

		/**
		 * selectors for
		 * - cleanup portal
		 * - semaphore used by caller used to notify paused state
		 * - semaphore used to block during page fault handling or pausing
		 */
		addr_t _selectors;

		addr_t _initial_esp = 0;
		addr_t _initial_eip = 0;
		addr_t _exc_pt_base_core;
		addr_t _exc_pt_base_child { Native_thread::INVALID_INDEX };
		addr_t _pd_target_base    { Native_thread::INVALID_INDEX };

		Mutex  _state_lock { };

		struct
		{
			struct Thread_state thread;
			addr_t sel_client_ec;
			enum {
				BLOCKED          = 0x1U,
				DEAD             = 0x2U,
				SINGLESTEP       = 0x4U,
				SIGNAL_SM        = 0x8U,
				SUBMIT_SIGNAL    = 0x20U,
				BLOCKED_PAUSE_SM = 0x40U,
			};
			uint8_t _status;
			bool modified;

			/* convenience function to access pause/recall state */
			inline bool blocked()          { return _status & BLOCKED;}
			inline void block()            { _status |= BLOCKED; }
			inline void unblock()          { _status &= (uint8_t)(~BLOCKED); }
			inline bool blocked_pause_sm() { return _status & BLOCKED_PAUSE_SM;}
			inline void block_pause_sm()   { _status |= (uint8_t)BLOCKED_PAUSE_SM; }
			inline void unblock_pause_sm() { _status &= (uint8_t)(~BLOCKED_PAUSE_SM); }

			inline void mark_dead() { _status |= DEAD; }
			inline bool is_dead() { return _status & DEAD; }

			inline bool singlestep() { return _status & SINGLESTEP; }

			inline void mark_signal_sm() { _status |= SIGNAL_SM; }
			inline bool has_signal_sm() { return _status & SIGNAL_SM; }

			inline bool to_submit() { return _status & SUBMIT_SIGNAL; }
			inline void submit_signal() { _status |= SUBMIT_SIGNAL; }
			inline void reset_submit() { _status &= (uint8_t)(~SUBMIT_SIGNAL); }
		} _state { };

		Cpu_session_capability   _cpu_session_cap;
		Thread_capability        _thread_cap;
		Affinity::Location       _location;
		Affinity::Location       _next_location { };
		Exception_handlers       _exceptions;

		void _copy_state_from_utcb(Novae::Utcb const &utcb);
		void _copy_state_to_utcb(Novae::Utcb &utcb, unsigned &mtd) const;

		uint8_t _unsynchronized_client_recall(bool get_state_and_block);

		addr_t sel_pt_cleanup()     const { return _selectors; }
		addr_t sel_sm_block_pause() const { return _selectors + 1; }

		__attribute__((regparm(2)))
		static void _page_fault_handler(Pager_object &, unsigned);

		__attribute__((regparm(2)))
		static void _startup_handler(Pager_object &, unsigned);

		__attribute__((regparm(2)))
		static void _invoke_handler(Pager_object &, unsigned);

		__attribute__((regparm(2)))
		static void _recall_handler(Pager_object &, unsigned);

		__attribute__((regparm(2)))
		static void _delegate_handler(Pager_object &, unsigned);

		void _construct_pager();

	public:

		Pager_object(Cpu_session_capability cpu_session_cap,
		             Thread_capability thread_cap,
		             unsigned long badge, Affinity::Location location,
		             Session_label const &,
		             Cpu_session::Name const &);

		virtual ~Pager_object();

		unsigned long badge() const { return _badge; }
		void reset_badge()
		{
			Mutex::Guard guard(_state_lock);
			_badge = 0;
		}

		const char * client_thread() const;
		const char * client_pd() const;

		enum class Pager_result { STOP, CONTINUE };

		virtual Pager_result pager(Ipc_pager &ps) = 0;

		/**
		 * Assign user-level exception handler for the pager object
		 */
		void exception_handler(Signal_context_capability sigh)
		{
			_exception_sigh = sigh;
		}

		Affinity::Location location() const { return _location; }

		/**
		 * Assign PD selector to PD
		 */
		void assign_pd(addr_t sel_base) { _pd_target_base = sel_base; }

		addr_t pd_sel()      const { return _pd_target_base; }
		addr_t pd_sel_obj()  const { return _pd_target_base + 1; }
		addr_t pd_sel_host() const { return _pd_target_base + 2; }

		void exception(uint8_t exit_id, unsigned mtd);

		/**
		 * Return base of initial portal window
		 */
		addr_t exc_pt_sel_core() { return _exc_pt_base_core; }

		/**
		 * Set initial stack pointer used by the startup handler
		 */
		addr_t initial_esp() { return _initial_esp; }

		/**
		 * Set initial instruction pointer used by the startup handler
		 */
		void initial_register(addr_t ip, addr_t sp)
		{
			_initial_eip = ip;
			_initial_esp = sp;
		}

		/**
		 * Continue execution of pager object
		 */
		void wake_up();

		/**
		 * Notify exception handler about the occurrence of an exception
		 */
		bool submit_exception_signal()
		{
			if (!_exception_sigh.valid()) return false;

			_state.reset_submit();

			Signal_transmitter transmitter(_exception_sigh);
			transmitter.submit();

			return true;
		}

		/**
		 * Copy thread state of recalled thread.
		 */
		bool copy_thread_state(Thread_state * state_dst)
		{
			Mutex::Guard _state_lock_guard(_state_lock);

			if (!state_dst || !_state.blocked())
				return false;

			*state_dst = _state.thread;

			return true;
		}

		/*
		 * Copy thread state to recalled thread.
		 */
		bool copy_thread_state(Thread_state state_src)
		{
			Mutex::Guard _state_lock_guard(_state_lock);

			if (!_state.blocked())
				return false;

			_state.thread = state_src;
			_state.modified = true;

			return true;
		}

		uint8_t client_recall(bool get_state_and_block);

		void track_selectors(addr_t ec, addr_t exc_pt_base_child)
		{
			_state.sel_client_ec = ec;
			_exc_pt_base_child   = exc_pt_base_child;
		}

		inline void single_step(bool on)
		{
			_state_lock.acquire();

			if (_state.is_dead() || !_state.blocked() ||
			    (on && (_state._status & _state.SINGLESTEP)) ||
			    (!on && !(_state._status & _state.SINGLESTEP))) {
			    _state_lock.release();
				return;
			}

			if (on)
				_state._status |= _state.SINGLESTEP;
			else
				_state._status &= (uint8_t)(~_state.SINGLESTEP);

			_state_lock.release();

			/* force client in exit and thereby apply single_step change */
			client_recall(false);
		}

		/**
		 * Return CPU session that was used to created the thread
		 */
		Cpu_session_capability cpu_session_cap() const { return _cpu_session_cap; }

		/**
		 * Return thread capability
		 *
		 * This function enables the destructor of the thread's
		 * address-space region map to kill the thread.
		 */
		Thread_capability thread_cap() const { return _thread_cap; }

		/**
		 * Note in the thread state that an unresolved page
		 * fault occurred.
		 */
		void unresolved_page_fault_occurred()
		{
			_state.thread.state = Thread_state::State::PAGE_FAULT;
		}

		/**
		 * Make sure nobody is in the handler anymore by doing an IPC to a
		 * local cap pointing to same serving thread (if not running in the
		 * context of the serving thread). When the call returns
		 * we know that nobody is handled by this object anymore, because
		 * all remotely available portals had been revoked beforehand.
		 */
		void cleanup_call();

		void print(Output &out) const;

		static void enable_delegation(addr_t, bool);

		void track_delegation(uint64_t, uint64_t, unsigned);

		static void track_rpc_cap(addr_t dst_pd, addr_t src_sel,
		                          addr_t dst_sel = Capability_space::INVALID_INDEX);

		static void untrack_rpc_cap(addr_t const core_sel);
		static void wipe_all_caps  (addr_t const pd_sel);

		static bool verbose_rpc_track;
};


/**
 * Paging entry point
 *
 * For a paging entry point can hold only one activation. So, paging is
 * strictly serialized for one entry point.
 */
class Core::Pager_entrypoint : public Object_pool<Pager_object>
{
	public:

		/**
		 * Constructor
		 *
		 * \param cap_factory  factory for creating capabilities
		 *                     for the pager objects managed by this
		 *                     entry point
		 */
		Pager_entrypoint(Rpc_cap_factory &cap_factory);

		/**
		 * Associate Pager_object with the entry point
		 */
		Pager_capability manage(Pager_object &) {
			return Pager_capability(); }

		/**
		 * Dissolve Pager_object from entry point
		 */
		void dissolve(Pager_object &obj);
};
