/*
 * \brief  Pci domain service for base-nova
 * \author Alexander Boettcher
 * \date   2013-02-10
 */

/*
 * Copyright (C) 2013-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#include <dataspace/client.h>
#include <rm_session/connection.h>

#include <util/touch.h>

#include <nova/syscalls.h>

#include <pd_session/client.h>

#include "../pci_device_pd_ipc.h"

enum {
	UTCB_MAIN_THREAD = 0xaffff000UL,
	PAGE_SIZE        = 0x1000UL,
	SND_BUF_SIZE     = 1024,
	RCV_BUF_SIZE     = 1024
};

static bool init = false;
extern "C" unsigned long _stack_high;

static Genode::Msgbuf<SND_BUF_SIZE> *snd_buffer()
{
	static Genode::Msgbuf<SND_BUF_SIZE> snd_buf;
	return &snd_buf;
}

static Genode::Msgbuf<RCV_BUF_SIZE> *rcv_buffer()
{
	static Genode::Msgbuf<RCV_BUF_SIZE> rcv_buf;
	return &rcv_buf;
}

extern "C" __attribute__((regparm(1)))
void activation_entry(Genode::addr_t portal_id)
{
	using namespace Genode;

	if (!init) {
		init = true;
		asm volatile("mov $0, %esp;"
		             "jmp _start;");	
	}

	static Pci::Pd_control_component server_object;

	Nova::Utcb * utcb = reinterpret_cast<Nova::Utcb *>(UTCB_MAIN_THREAD);

	Ipc_server srv(snd_buffer(), rcv_buffer());
	rcv_buffer()->post_ipc(utcb);

	int opcode = 0;

	srv >> IPC_WAIT >> opcode;

	/* set default return value */
	srv.ret(ERR_INVALID_OBJECT);

	try { srv.ret(server_object.dispatch(opcode, srv, srv)); }
	catch (Blocking_canceled) { }

	rcv_buffer()->rcv_prepare_pt_sel_window(utcb);

	/* XXX does not work since Thread_base::myself returns 0 for main thread */
	//srv << IPC_REPLY;
	Nova::reply(&_stack_high);
}

int main(int argc, char **argv) {

	/* initialize receive and translate window */
	Nova::Utcb * utcb = reinterpret_cast<Nova::Utcb *>(UTCB_MAIN_THREAD);

	/* setup receive window */
	rcv_buffer()->rcv_prepare_pt_sel_window(utcb);

	/* XXX does not work since Thread_base::myself returns 0 for main thread */
	//Nova::reply(Thread_base::myself()->stack_top());
	Nova::reply(&_stack_high);

	return 0;
}

void Pci::Pd_control_component::attach_dma_mem(Genode::Ram_dataspace_capability ds_cap)
{
	using namespace Genode;

	Dataspace_client ds_client(ds_cap);

	addr_t page = env()->rm_session()->attach_at(ds_cap, ds_client.phys_addr());
	/* sanity check */
	if (page != ds_client.phys_addr())
		throw Rm_session::Region_conflict();

	/* trigger mapping of whole memory area */
	for (size_t rounds = (ds_client.size() + 1) / PAGE_SIZE; rounds;
	     page += PAGE_SIZE, rounds --)
		touch_read(reinterpret_cast<unsigned char *>(page));
}

void Pci::Pd_control_component::assign_pci(Genode::Io_mem_dataspace_capability io_mem_cap)
{
	using namespace Genode;

	Dataspace_client ds_client(io_mem_cap);

	addr_t page = env()->rm_session()->attach(io_mem_cap);
	/* sanity check */
	if (!page)
		return;

	/* trigger mapping of whole memory area */
	touch_read(reinterpret_cast<unsigned char *>(page));

	/* try to assign pci device to this protection domain */
	Pd_session_client client(env()->pd_session_cap());
	client.assign_pci(page);

	/* we don't need the mapping anymore */
	env()->rm_session()->detach(page);
}
