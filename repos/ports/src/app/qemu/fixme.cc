#include <base/log.h>
#include <base/thread.h>

extern "C" {
	#include <qemu/osdep.h>
}

static void __attribute__((noreturn)) fixme(unsigned const line, char const *name)
{
	Genode::error(line, " ", name, " ", Genode::Thread::myself()->name());
	while (1) { }
}

static void skip(unsigned const line, char const *name)
{
	Genode::error("skip ", line, " ", name, " ", Genode::Thread::myself()->name());
}

static void * emutls [10];
static __attribute__((aligned(4096))) struct { char b [4096]; } xtls [10];

extern "C"
{
	bool kvm_allowed = false;

	void * __emutls_get_address(void *id)
	{
		void * ret = 0;
		unsigned free = ~0U;

		for (unsigned i=0; i < 10; i++) {
			if (emutls[i] == id) {
				ret = &xtls[i];
				break;
			}
			if (free > 10 && emutls[i] == 0)
				free = i;
		}
		if (!ret && free < 10) {
			emutls[free] = id;
			ret = &xtls[free];
		}

		if (!ret)
			Genode::error(__func__, " ", Genode::Thread::myself()->name(), " ", id, " -> ", ret, " free=", free);
		return ret;
	}

	void qemu_vfree(void *) { fixme(__LINE__, __func__); }
	void *qemu_memalign(size_t, size_t) { fixme(__LINE__, __func__); }
	void *qemu_try_memalign(size_t, size_t) {
		fixme(__LINE__, __func__); }
	void *qemu_anon_ram_alloc(size_t, uint64_t *, bool) {
		fixme(__LINE__, __func__); }
	void qemu_anon_ram_free(void *, size_t) {
		fixme(__LINE__, __func__); }

	int qemu_signalfd(const sigset_t *) { fixme(__LINE__, __func__); }
	int qemu_get_thread_id(void) { fixme(__LINE__, __func__); }

	pid_t qemu_fork(Error **) { fixme(__LINE__, __func__); }
	char *qemu_get_pid_name(pid_t) { fixme(__LINE__, __func__); }
	bool  qemu_write_pidfile(const char *, Error **) { fixme(__LINE__, __func__); }
	void  qemu_set_block(int) { fixme(__LINE__, __func__); }
	void  qemu_set_nonblock(int) { fixme(__LINE__, __func__); }
	int   socket_set_fast_reuse(int) { fixme(__LINE__, __func__); }

#if 0
	int event_notifier_init(EventNotifier *, int)
	{
		skip(__LINE__, __func__);
		return 0;
	}
	int event_notifier_set(EventNotifier *) { fixme(__LINE__, __func__); }
	void event_notifier_cleanup(EventNotifier *) { fixme(__LINE__, __func__); }
	int event_notifier_test_and_clear(EventNotifier *) { fixme(__LINE__, __func__); }
	int event_notifier_get_fd(const EventNotifier *) { fixme(__LINE__, __func__); }
#endif

	void *qemu_alloc_stack(size_t *)      { fixme(__LINE__, __func__); }
	void  qemu_free_stack(void *, size_t) { fixme(__LINE__, __func__); }

	void qemu_init_exec_dir(const char *) { skip(__LINE__, __func__); }
	char *qemu_get_exec_dir(void) { fixme(__LINE__, __func__); }

	void os_mem_prealloc(int, char *, size_t, int, Error **) { fixme(__LINE__, __func__); }

	int sigaltstack(const stack_t *, stack_t *) {
		fixme(__LINE__, __func__); }
	void sigaction_invoke(struct sigaction *, struct qemu_signalfd_siginfo *) {
		fixme(__LINE__, __func__); }
	int sigwait(const sigset_t *, int *) { fixme(__LINE__, __func__); }

	int  vnc_display_pw_expire(const char *, time_t) { fixme(__LINE__, __func__); }
	int  vnc_display_password(const char *, const char *) {
		fixme(__LINE__, __func__); }

	int  gdbserver_start(int) { fixme(__LINE__, __func__); }
	void gdb_set_stop_cpu(CPUState *) { fixme(__LINE__, __func__); }
	void gdbserver_cleanup(void) { fixme(__LINE__, __func__); }

	int mlockall(int) { fixme(__LINE__, __func__); }

	struct CaptureState;
	int wav_start_capture (CaptureState *, const char *, int, int, int) {
		fixme(__LINE__, __func__); }

	ssize_t sendmsg(int, const struct msghdr *, int) {
		fixme(__LINE__, __func__); }

	void __pthread_cleanup_push_imp(void (*)(void *), void *, struct _pthread_cleanup_info *) {
		skip(__LINE__, __func__); }

	void __pthread_cleanup_pop_imp(int) {
		fixme(__LINE__, __func__); }

	int fallocate(int, int, off_t, off_t) { fixme(__LINE__, __func__); }
	int fdatasync(int) { fixme(__LINE__, __func__); }

	ssize_t copy_file_range(int, size_t *, int, size_t *, size_t, unsigned int) {
		fixme(__LINE__, __func__); }

	/* from oslib-posix.c */
	int qemu_pipe(int pipefd[2])
	{
	    int ret;

	#ifdef CONFIG_PIPE2
	    ret = pipe2(pipefd, O_CLOEXEC);
	    if (ret != -1 || errno != ENOSYS) {
	        return ret;
	    }
	#endif
	    ret = pipe(pipefd);
	    if (ret == 0) {
	        qemu_set_cloexec(pipefd[0]);
	        qemu_set_cloexec(pipefd[1]);
	    }

	    return ret;
	}

	void qemu_set_cloexec(int fd)
	{
	    int f;
	    f = fcntl(fd, F_GETFD);
	    assert(f != -1);
	    f = fcntl(fd, F_SETFD, f | FD_CLOEXEC);
	    assert(f != -1);
	}
}
