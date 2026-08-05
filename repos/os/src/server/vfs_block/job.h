/*
 * \brief  VFS file to Block session
 * \author Josef Soentgen
 * \date   2020-05-05
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _VFS_BLOCK__JOB_
#define _VFS_BLOCK__JOB_

namespace Vfs_block {

	using file_size   = Genode::Vfs::file_size;
	using file_offset = Genode::Vfs::file_offset;
	using size_t      = Genode::size_t;

	struct Job
	{
		struct Unsupported_Operation : Genode::Exception { };
		struct Invalid_state         : Genode::Exception { };

		enum State { PENDING, IN_PROGRESS, COMPLETE, };

		static State _initial_state(Block::Operation::Type type)
		{
			using Type = Block::Operation::Type;

			switch (type) {
			case Type::READ:  return State::PENDING;
			case Type::WRITE: return State::PENDING;
			case Type::TRIM:  return State::PENDING;
			case Type::SYNC:  return State::PENDING;
			default:          throw Unsupported_Operation();
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

		Genode::Vfs::Vfs_handle &_handle;

		Block::Request const request;
		char              *data;
		State              state;
		file_offset const  base_offset;
		file_offset        current_offset;
		size_t             current_count;

		bool success;
		bool complete;

		bool _read()
		{
			bool progress = false;

			switch (state) {
			case State::PENDING:

				_handle.seek(base_offset + current_offset);
				state = State::IN_PROGRESS;
				progress = true;
			[[fallthrough]];
			case State::IN_PROGRESS:
			{
				using Result = Genode::Vfs::Read_result;

				bool completed = false;

				Genode::Byte_range_ptr const dst { data + current_offset,
				                                   current_count };

				Result const result = _handle.read(dst);
				if (result == Genode::Vfs::Read_error::RETRY)
					return progress;

				result.with_result(
					[&] (size_t num_bytes) {
						current_offset += num_bytes;
						current_count  -= num_bytes;
						success = true;
					},
					[&] (Genode::Vfs::Read_error) {
						success   = false;
						completed = true;
					});

				if (current_count == 0 || completed) {
					state = State::COMPLETE;
				} else {
					state = State::PENDING;
					/* partial read, keep trying */
					return true;
				}
				progress = true;
			}
			[[fallthrough]];
			case State::COMPLETE:

				complete = true;
				progress = true;
			default: break;
			}

			return progress;
		}

		bool _write()
		{
			bool progress = false;

			switch (state) {
			case State::PENDING:

				_handle.seek(base_offset + current_offset);
				state = State::IN_PROGRESS;
				progress = true;

			[[fallthrough]];
			case State::IN_PROGRESS:
			{
				using Result = Genode::Vfs::Write_result;

				bool completed = false;
				size_t out = 0;

				Genode::Const_byte_range_ptr const src { data + current_offset,
				                                         current_count };

				Result result = _handle.write(src, out);

				switch (result) {
				case Result::WRITE_ERR_WOULD_BLOCK:
					return progress;

				case Result::WRITE_OK:
					current_offset += out;
					current_count  -= out;
					success = true;
					break;

				case Result::WRITE_ERR_IO:
				case Result::WRITE_ERR_INVALID:
					success = false;
					completed = true;
					break;
				}

				if (current_count == 0 || completed) {
					state = State::COMPLETE;
				} else {
					state = State::PENDING;
					/* partial write, keep trying */
					return true;
				}
				progress = true;
			}
			[[fallthrough]];
			case State::COMPLETE:

				complete = true;
				progress = true;
			default: break;
			}

			return progress;
		}

		bool _sync()
		{
			bool progress = false;

			switch (state) {
			case State::PENDING:
				state = State::IN_PROGRESS;
				progress = true;
			[[fallthrough]];
			case State::IN_PROGRESS:
			{
				if (_handle.sync() == Genode::Vfs::Sync_result::RETRY)
					return progress;

				success = true;
				state = State::COMPLETE;
				progress = true;
			}
			[[fallthrough]];
			case State::COMPLETE:

				complete = true;
				progress = true;
			default: break;
			}

			return progress;
		}

		bool _trim()
		{
			/*
 			 * TRIM is not implemented but nonetheless report success
			 * back to client as it merely is a hint.
			 */
			success  = true;
			complete = true;
			return true;
		}

		Job(Genode::Vfs::Vfs_handle &handle,
		    Block::Request   request,
		    file_offset      base_offset,
		    char            *data,
		    size_t           length)
		:
			_handle        { handle },
			request        { request },
			data           { data },
			state          { _initial_state(request.operation.type) },
			base_offset    { base_offset },
			current_offset { 0 },
			current_count  { length },
			success        { false },
			complete       { false }
		{ }

		bool completed() const { return complete; }
		bool succeeded()   const { return success; }

		void print(Genode::Output &out) const
		{
			Genode::print(out, "(", request.operation, ")",
				" state: ",          _state_to_string(state),
				" base_offset: ",    base_offset,
				" current_offset: ", current_offset,
				" current_count: ",  current_count,
				" success: ",        success,
				" complete: ",       complete);
		}

		bool execute()
		{
			using Type = Block::Operation::Type;

			switch (request.operation.type) {
			case Type::READ:  return _read();
			case Type::WRITE: return _write();
			case Type::SYNC:  return _sync();
			case Type::TRIM:  return _trim();
			default:          return false;
			}
		}
	};

} /* namespace Vfs_block */

#endif /* _VFS_BLOCK__JOB_ */
