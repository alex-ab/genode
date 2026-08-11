/*
 * \brief  Integration of the Tresor block encryption
 * \author Martin Stein
 * \author Josef Soentgen
 * \date   2020-11-10
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _TRESOR__VFS__IO_JOB_H_
#define _TRESOR__VFS__IO_JOB_H_

/* base includes */
#include <vfs/types.h>
#include <vfs/vfs_handle.h>

namespace Util {

	using namespace Genode;

	struct Io_job
	{
		struct Buffer
		{
			char   *base;
			size_t  size;
		};

		enum class Operation { INVALID, READ, WRITE, SYNC };

		static char const *to_string(Operation op)
		{
			using Op = Operation;
			 
			switch (op) {
			case Op::READ:  return "READ";
			case Op::WRITE: return "WRITE";
			case Op::SYNC:  return "SYNC";
			default:        return "INVALID";
			}
		}

		struct Unsupported_Operation : Exception { };
		struct Invalid_state         : Exception { };

		enum State { PENDING, IN_PROGRESS, COMPLETE, };

		static State _initial_state(Operation const op)
		{
			using Op = Operation;

			switch (op) {
			case Op::READ:  return State::PENDING;
			case Op::WRITE: return State::PENDING;
			case Op::SYNC:  return State::PENDING;
			default:        throw Unsupported_Operation();
			}
		}

		static char const *_state_to_string(State s)
		{
			switch (s) {
			case State::PENDING:      return "PENDING";
			case State::IN_PROGRESS:  return "IN_PROGRESS";
			case State::COMPLETE:     return "COMPLETE";
			}

			throw Invalid_state();
		}

		enum class Partial_result { ALLOW, DENY };

		Vfs::Vfs_handle        &_handle;
		Operation        const  _op;
		State                   _state;
		char                   *_data;
		Vfs::file_size   const  _base_offset;
		size_t                  _current_offset;
		size_t                  _current_count;
		bool             const  _allow_partial;
		bool                    _success;
		bool                    _complete;

		bool _read()
		{
			bool progress = false;

			switch (_state) {
			case State::PENDING:
				_state = State::IN_PROGRESS;
				progress = true;
				[[fallthrough]];

			case State::IN_PROGRESS:
				{
					Byte_range_ptr const dst { _data + _current_offset, _current_count };
					Vfs::At const at { .pos = _base_offset + _current_offset };

					Vfs::Read_result const result = _handle.read(at, dst);

					if (result == Vfs::Read_error::RETRY)
						return progress;

					result.with_result(
						[&] (size_t num_bytes) {
							_current_offset += num_bytes;
							_current_count  -= num_bytes;
							_success = true;
							if (_current_count == 0 || (num_bytes == 0 && _allow_partial))
								_state = State::COMPLETE;
						},
						[&] (Vfs::Read_error) {
							_success = false;
						});
				}
				[[fallthrough]];

			case State::COMPLETE:
				_complete = true;
				progress = true;

			default: break;
			}

			return progress;
		}
 
		bool _write()
		{
			bool progress = false;

			switch (_state) {
			case State::PENDING:
				_state = State::IN_PROGRESS;
				progress = true;

			[[fallthrough]];
			case State::IN_PROGRESS:
			{
				Const_byte_range_ptr const src { _data + _current_offset, _current_count };
				Vfs::At const at { .pos = _base_offset + _current_offset };

				Vfs::Write_result result = _handle.write(at, src);
				if (result == Vfs::Write_error::RETRY) {
					if (_allow_partial) {
						_state = State::COMPLETE;
						return true;
					}
					return progress;
				}

				result.with_result(
					[&] (size_t num_bytes) {
						_current_offset += num_bytes;
						_current_count  -= num_bytes;
						_success = true;
						_state = (_current_count == 0)
						       ? State::COMPLETE
						       : State::PENDING; /* partial write, keep trying */
					},
					[&] (Vfs::Write_error) {
						_success = false;
						_state = State::COMPLETE;
					});

				progress = true;
			}
			[[fallthrough]];
			case State::COMPLETE:

				_complete = true;
				progress = true;
			default: break;
			}

			return progress;
		}

		bool _sync()
		{
			bool progress = false;

			switch (_state) {
			case State::PENDING:

				_state = State::IN_PROGRESS;
				progress = true;
			[[fallthrough]];
			case State::IN_PROGRESS:
			{
				using Result = Vfs::Sync_result;
				Result const result = _handle.sync();

				if (result == Result::RETRY)
					return progress;

				if (result == Result::OK)
					_success = true;

				_state = State::COMPLETE;
				progress = true;
			}
			[[fallthrough]];
			case State::COMPLETE:

				_complete = true;
				progress = true;
			default: break;	
			}

			return progress;
		}

		Io_job(Vfs::Vfs_handle &handle,
		       Operation        op,
		       Buffer          &buffer,
		       Vfs::file_size   base_offset,
		       Partial_result   partial_result = Partial_result::DENY)
		:
			_handle(handle), _op(op), _state(_initial_state(op)), _data(buffer.base),
			_base_offset(base_offset), _current_offset(0), _current_count(buffer.size),
			_allow_partial(partial_result == Partial_result::ALLOW), _success(false),
			_complete(false)
		{ }

		bool completed() const { return _complete; }
		bool succeeded() const { return _success; }
		Operation op() const { return _op; }

		void print(Output &out) const
		{
			Genode::print(out, "(", to_string(_op), ")",
				" state: ",          _state_to_string(_state),
				" current_offset: ", _current_offset,
				" current_count: ",  _current_count,
				" success: ",        _success,
				" complete: ",       _complete);
		}

		bool execute()
		{
			using Op = Operation;

			switch (_op) {
			case Op::READ:  return _read();
			case Op::WRITE: return _write();
			case Op::SYNC:  return _sync();
			default:        return false;
			}
		}

		size_t current_offset() const { return _current_offset; }
	};
}

#endif /* _TRESOR__VFS__IO_JOB_H_ */
