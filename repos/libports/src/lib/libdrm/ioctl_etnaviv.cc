/*
 * \brief  DRM ioctl backend
 * \author Sebastian Sumpf
 * \author Josef Soentgen
 * \date   2017-05-10
 */

/*
 * Copyright (C) 2017-2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/heap.h>
#include <base/log.h>
#include <base/debug.h>
#include <gpu/connection.h>

extern "C" {
#include <fcntl.h>

#include <drm.h>
#include <etnaviv_drm.h>
#include <libdrm_macros.h>

#include <os/backtrace.h>

#define DRM_NUMBER(req) ((req) & 0xff)
}


enum { verbose_ioctl = true };


extern "C" void dump_backtrace(void)
{
	Genode::backtrace();
}


/**
 * Get DRM command number
 */
static unsigned long constexpr command_number(unsigned long request)
{
	return request & 0xff;
}


/**
 * Get device specific command number
 */
static unsigned long device_number(unsigned long request)
{
	return command_number(request) - DRM_COMMAND_BASE;
}


/**
 * Check if request is device command
 */
static bool device_ioctl(unsigned long request)
{
	long const cmd = command_number(request);
	return cmd >= DRM_COMMAND_BASE && cmd < DRM_COMMAND_END;
}


/**
 * Return name of DRM command
 */
const char *command_name(unsigned long request)
{
	if (IOCGROUP(request) != DRM_IOCTL_BASE)
		return "<non-DRM>";


	if (!device_ioctl(request)) {
		switch (command_number(request)) {
		case command_number(DRM_IOCTL_VERSION):            return "DRM_IOCTL_VERSION";
		case command_number(DRM_IOCTL_GEM_CLOSE):          return "DRM_IOCTL_GEM_CLOSE";
		case command_number(DRM_IOCTL_GEM_FLINK):          return "DRM_IOCTL_GEM_FLINK";
		case command_number(DRM_IOCTL_GEM_OPEN):           return "DRM_IOCTL_GEM_OPEN";
		case command_number(DRM_IOCTL_GET_CAP):            return "DRM_IOCTL_GET_CAP";
		case command_number(DRM_IOCTL_PRIME_HANDLE_TO_FD): return "DRM_IOCTL_PRIME_HANDLE_TO_FD";
		case command_number(DRM_IOCTL_PRIME_FD_TO_HANDLE): return "DRM_IOCTL_PRIME_FD_TO_HANDLE";
		default:                                  return "<unknown drm>";
		}
	}

	switch (device_number(request)) {
	case DRM_ETNAVIV_GET_PARAM:    return "DRM_ETNAVIV_GET_PARAM";
	case DRM_ETNAVIV_GEM_NEW:      return "DRM_ETNAVIV_GEM_NEW";
	case DRM_ETNAVIV_GEM_INFO:     return "DRM_ETNAVIV_GEM_INFO";
	case DRM_ETNAVIV_GEM_CPU_PREP: return "DRM_ETNAVIV_GEM_CPU_PREP";
	case DRM_ETNAVIV_GEM_CPU_FINI: return "DRM_ETNAVIV_GEM_CPU_FINI";
	case DRM_ETNAVIV_GEM_SUBMIT:   return "DRM_ETNAVIV_GEM_SUBMIT";
	case DRM_ETNAVIV_WAIT_FENCE:   return "DRM_ETNAVIV_WAIT_FENCE";
	case DRM_ETNAVIV_GEM_USERPTR:  return "DRM_ETNAVIV_GEM_USERPTR";
	case DRM_ETNAVIV_GEM_WAIT:     return "DRM_ETNAVIV_GEM_WAIT";
	case DRM_ETNAVIV_PM_QUERY_DOM: return "DRM_ETNAVIV_PM_QUERY_DOM";
	case DRM_ETNAVIV_PM_QUERY_SIG: return "DRM_ETNAVIV_PM_QUERY_SIG";
	case DRM_ETNAVIV_NUM_IOCTLS:   return "DRM_ETNAVIV_NUM_IOCTLS";
	default:
		return "<unknown driver>";
	}
}


static bool constexpr req_out(unsigned long request)
{
	return (request & IOC_OUT);
}


static bool constexpr req_in(unsigned long request)
{
	return (request & IOC_IN);
}


static unsigned long to_linux(unsigned long request)
{
	/*
	 * FreeBSD and Linux have swapped IN/OUT values.
	 */
	unsigned long lx = request & 0x0fffffff;
	if (req_out(request)) { lx |= IOC_IN; }
	if (req_in (request)) { lx |= IOC_OUT; }

	return lx;
}


static void dump_ioctl(unsigned long request)
{
	using namespace Genode;

	log("ioctl(request=", Hex(request),
	    (request & 0xe0000000) == IOC_OUT   ? " out"   :
	    (request & 0xe0000000) == IOC_IN    ? " in"    :
	    (request & 0xe0000000) == IOC_INOUT ? " inout" : " void",
	    " len=", IOCPARM_LEN(request),
	    " cmd=", command_name(request), " (", Hex(command_number(request)), ")");
}


namespace Drm {

	size_t get_payload_size(drm_etnaviv_gem_submit const &submit);

	// XXX better implement as 'size_t for_each_object(T const *t, unsigned len, FN const &fn, char *dst)'
	template <typename FN, typename T> void for_each_object(T const *t, unsigned len, FN const &fn)
	{
		for (unsigned i = 0; i < len; i++) {
			T const *obj = &t[i];
			fn(obj);
		}
	}

	void serialize(drm_etnaviv_gem_submit *submit, char *content);

} /* anonymous namespace */



size_t Drm::get_payload_size(drm_etnaviv_gem_submit const &submit)
{
	size_t size = 0;

	size += sizeof (drm_etnaviv_gem_submit_reloc) * submit.nr_relocs;
	size += sizeof (drm_etnaviv_gem_submit_bo) * submit.nr_bos;
	size += sizeof (drm_etnaviv_gem_submit_pmr) * submit.nr_pmrs;

	return size;
}


void Drm::serialize(drm_etnaviv_gem_submit *submit, char *content)
{
	size_t offset = 0;
	__u64 start = 0;

	/* leave place for object itself first */
	offset += sizeof (*submit);

	/* next are the buffer-objects */
	if (submit->nr_bos) {
		start = reinterpret_cast<__u64>(content) + offset;
		auto copy_bos = [&] (drm_etnaviv_gem_submit_bo const *bo) {
			Genode::warning("bo: ", bo, " handle: ", bo->handle, " presumed: ", Genode::Hex(bo->presumed));
			char * const dst = content + offset;
			Genode::memcpy(dst, bo, sizeof (*bo));
			offset += sizeof (*bo);
		};
		for_each_object((drm_etnaviv_gem_submit_bo*)submit->bos,
		                submit->nr_bos, copy_bos);
		Genode::warning("nr_bos: ", submit->nr_bos, " bos: ", Genode::Hex(submit->bos), " -> ", Genode::Hex(start));
		submit->bos = reinterpret_cast<__u64>(start);
	}

	/* next are the relocs */
	if (submit->nr_relocs) {
		start = reinterpret_cast<__u64>(content) + offset;
		auto copy_relocs = [&] (drm_etnaviv_gem_submit_reloc const *reloc) {
			Genode::warning("reloc: ", reloc, " submit_offset: ", Genode::Hex(reloc->submit_offset),
			                "reloc_offset: ", Genode::Hex(reloc->reloc_offset));
			char * const dst = content + offset;
			Genode::memcpy(dst, reloc, sizeof (*reloc));
			offset += sizeof (*reloc);
		};
		for_each_object((drm_etnaviv_gem_submit_reloc*)submit->relocs,
		                submit->nr_relocs, copy_relocs);
		Genode::warning("nr_relocs: ", submit->nr_relocs, " relocs: ", Genode::Hex(submit->relocs), " -> ", Genode::Hex(start));
		submit->relocs = reinterpret_cast<__u64>(start);
	}

	/* next are the pmrs */
	if (submit->nr_pmrs) {
		start = reinterpret_cast<__u64>(content) + offset;
		auto copy_pmrs = [&] (drm_etnaviv_gem_submit_pmr const *pmr) {
			char * const dst = content + offset;
			Genode::memcpy(dst, pmr, sizeof (*pmr));
			offset += sizeof (*pmr);
		};
		for_each_object((drm_etnaviv_gem_submit_pmr*)submit->pmrs,
		                submit->nr_pmrs, copy_pmrs);
		Genode::warning("pmrs: ", Genode::Hex(submit->pmrs), " -> ", Genode::Hex(start));
		submit->pmrs = reinterpret_cast<__u64>(start);
	}

	/* next is the cmd stream */
	start = reinterpret_cast<__u64>(content) + offset;
	char * const dst = content + offset;
	Genode::memcpy(dst, reinterpret_cast<void const*>(submit->stream), submit->stream_size);
	offset += submit->stream_size;
	Genode::warning("stream_size: ", submit->stream_size, " stream: ", Genode::Hex(submit->stream), " -> ", Genode::Hex(start));
	submit->stream = reinterpret_cast<__u64>(start);

	/* copy submit object last but into the front
 	 * XXX content + bos = start would work as well... */
	Genode::memcpy(content, submit, sizeof (*submit));
}


class Drm_call
{
	private:

		Genode::Env          &_env;
		Genode::Heap          _heap { _env.ram(), _env.rm() };
		Genode::Allocator_avl _drm_alloc { &_heap };
		Drm::Connection       _drm_session { _env, &_drm_alloc, 1024*1024 };

		int _gem_mmap(void *arg)
		{
			(void)arg;
			return -1;
#if 0
			drm_i915_gem_mmap *data = (drm_i915_gem_mmap *)arg;

			Genode::Ram_dataspace_capability ds = _drm_session.object_dataspace(data->handle);
			data->addr_ptr = (__u64)_env.rm().attach(ds);
			return 0;
#endif
		}

		int _gem_mmap_gtt(void *arg)
		{
			(void)arg;
			return -1;
#if 0
			drm_i915_gem_mmap_gtt *data = (drm_i915_gem_mmap_gtt *)arg;

			Genode::Dataspace_capability ds = _drm_session.object_dataspace_gtt(data->handle);
			data->offset = (__u64)_env.rm().attach(ds);
			return 0;
#endif
		}

	public:

		Drm_call(Genode::Env &env) : _env(env) { }

		int ioctl(unsigned long request, void *arg)
		{
			(void)arg;

			size_t size = IOCPARM_LEN(request);

			// Genode::log(__func__, ":", __LINE__, ": request: ", command_name(request),
			//             " size: ", size, " arg: ", arg);

			bool const in  = req_in(request);
			bool const out = req_out(request);

			unsigned long const lx_request = to_linux(request);

			if (command_number(request) == DRM_ETNAVIV_GEM_SUBMIT) {
				/* account for the arrays */
				drm_etnaviv_gem_submit *submit =
					reinterpret_cast<drm_etnaviv_gem_submit*>(arg);
				size_t const payload_size = Drm::get_payload_size(*submit);
				size += payload_size;
			}

			if (device_number(request) == DRM_ETNAVIV_GEM_CPU_PREP) {
				struct drm_etnaviv_gem_cpu_prep *v = reinterpret_cast<struct drm_etnaviv_gem_cpu_prep*>(arg);
				Genode::log(__func__, ":", __LINE__, ": DRM_ETNAVIV_GEM_CPU_PREP: handle: ", v->handle);
			}

			/* submit */
			Drm::Session::Tx::Source &src = *_drm_session.tx();
			Drm::Packet_descriptor p { src.alloc_packet(size), lx_request };

			/*
			 * Copy each array flat to the packet buffer and adjust the
			 * addresses in the submit object.
			 */
			if (device_number(request) == DRM_ETNAVIV_GEM_SUBMIT) {
				drm_etnaviv_gem_submit *submit =
					reinterpret_cast<drm_etnaviv_gem_submit*>(arg);
				Genode::error(__func__, ": serialize submit buffer fence: ", submit->fence, " fence_fd: ", submit->fence_fd);
				char *content = src.packet_content(p);
				Drm::serialize(submit, content);
			} else

			if (in) {
				Genode::log(__func__, ":", __LINE__, ": IN request: ", command_name(request),
				            " size: ", size, " arg: ", arg);
				Genode::memcpy(src.packet_content(p), arg, size);

				if (device_number(request) == DRM_ETNAVIV_GEM_INFO) {
					struct drm_etnaviv_gem_info *v = reinterpret_cast<struct drm_etnaviv_gem_info*>(arg);
					Genode::log(__func__, ":", __LINE__, ": DRM_ETNAVIV_GEM_INFO: handle: ", v->handle);
				}

				if (command_number(request) == DRM_IOCTL_PRIME_HANDLE_TO_FD) {
					drm_prime_handle *v = reinterpret_cast<drm_prime_handle*>(arg);
					Genode::log(__func__, ":", __LINE__, ": DRM_IOCTL_PRIME_HANDLE_TO_FD: handle: ", v->handle);
				}

				if (command_number(request) == DRM_IOCTL_PRIME_FD_TO_HANDLE) {
					drm_prime_handle *v = reinterpret_cast<drm_prime_handle*>(arg);
					Genode::log(__func__, ":", __LINE__, ": DRM_IOCTL_PRIME_FD_TO_HANDLE: ",
								"fd: ", v->fd);
				}

				if (device_number(request) == DRM_ETNAVIV_GEM_NEW) {
					struct drm_etnaviv_gem_new *v = reinterpret_cast<struct drm_etnaviv_gem_new*>(arg);
					Genode::log(__func__, ":", __LINE__, ": DRM_ETNAVIV_GEM_NEW: size: ", v->size,
					            " flags: ", Genode::Hex(v->flags));
				}

				if (device_number(request) == DRM_ETNAVIV_WAIT_FENCE) {

					drm_etnaviv_wait_fence *fence =
						reinterpret_cast<drm_etnaviv_wait_fence*>(arg);
					Genode::error(__func__, ": DRM_ETNAVIV_WAIT_FENCE fence: ",
					fence->fence);
				}
			}

			src.submit_packet(p);

			/* receive */
			p = src.get_acked_packet();

			if (out && arg) {
				Genode::log(__func__, ":", __LINE__, ": OUT request: ", command_name(request),
				            " size: ", size, " arg: ", arg);
				Genode::memcpy(arg, src.packet_content(p), size);

				if (device_number(request) == DRM_ETNAVIV_GEM_SUBMIT) {

					drm_etnaviv_gem_submit *submit =
						reinterpret_cast<drm_etnaviv_gem_submit*>(arg);
					Genode::error(__func__, ": DRM_ETNAVIV_GEM_SUBMIT fence: ",
					submit->fence, " fence_fd: ", submit->fence_fd);
				}

				if (device_number(request) == DRM_ETNAVIV_GEM_NEW) {
					struct drm_etnaviv_gem_new *v = reinterpret_cast<struct drm_etnaviv_gem_new*>(arg);
					Genode::log(__func__, ":", __LINE__, ": DRM_ETNAVIV_GEM_NEW: handle: ", v->handle);
				}
				if (device_number(request) == DRM_ETNAVIV_GEM_INFO) {
					struct drm_etnaviv_gem_info *v = reinterpret_cast<struct drm_etnaviv_gem_info*>(arg);
					Genode::log(__func__, ":", __LINE__, ": DRM_ETNAVIV_GEM_INFO: handle: ", v->handle,
					            " offset: ", Genode::Hex(v->offset));
				}

				if (command_number(request) == DRM_IOCTL_PRIME_HANDLE_TO_FD) {
					drm_prime_handle *v = reinterpret_cast<drm_prime_handle*>(arg);
					Genode::log(__func__, ":", __LINE__, ": DRM_IOCTL_PRIME_HANDLE_TO_FD: ",
								" handle: ", v->handle, " fd: ", v->fd);
				}
					if (command_number(request) == DRM_IOCTL_PRIME_FD_TO_HANDLE) {
						drm_prime_handle *v = reinterpret_cast<drm_prime_handle*>(arg);
					Genode::log(__func__, ":", __LINE__, ": DRM_IOCTL_PRIME_FD_TO_HANDLE: ",
								" handle: ", v->handle, " fd: ", v->fd);
				}

			}

			src.release_packet(p);
			return p.error();
		}

		void *mmap(unsigned long offset, unsigned long size)
		{
			Genode::Ram_dataspace_capability cap = _drm_session.object_dataspace(offset, size);
			if (!cap.valid()) {
				return (void *)-1;
			}

			try {
				return _env.rm().attach(cap);
			} catch (...) { }
			
			return (void *)-1;
		}

		void munmap(void *addr)
		{
			_env.rm().detach(addr);
		}
};


static Genode::Constructible<Drm_call> _drm;


void drm_init(Genode::Env &env)
{
	_drm.construct(env);
}


void drm_complete()
{
	Genode::error(__func__, ": called, not implemented yet");
}


extern "C" int genode_ioctl(int /* fd */, unsigned long request, void *arg)
{
	// Genode::error(__func__, ": from: ", __builtin_return_address(0));

	if (0)
		dump_ioctl(request);

	try {
		int ret = _drm->ioctl(request, arg);

		if (verbose_ioctl)
			Genode::log("returned ", ret);

		return ret;
	} catch (...) { }

	return -1;
}


void *drm_mmap(void *addr, size_t length, int prot, int flags,
               int fd, off_t offset)
{
	(void)addr;
	(void)prot;
	(void)flags;
	(void)fd;

	Genode::warning(__func__, ": addr: ", addr, " length: ", length, " prot: ", Genode::Hex(prot),
	                " flags: ", Genode::Hex(flags), " fd: ", fd, " offset: ", Genode::Hex(offset));
	return _drm->mmap(offset, length);
}


int drm_munmap(void *addr, size_t length)
{
	(void)length;
	Genode::warning(__func__, ": addr: ", addr, " length: ", length);
	_drm->munmap(addr);

	return 0;
}
