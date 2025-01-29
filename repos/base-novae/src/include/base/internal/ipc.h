/*
 * \brief  IPC utility functions
 * \author Alexander Boettcher
 * \date   2024-11-04
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

/* NOVAe includes */
#include <novae/capability_space.h>
#include <novae/cap_map.h>
#include <novae/native_thread.h>
#include <novae/syscalls.h>

/**
 * Copy message registers from UTCB to destination message buffer
 *
 * \return  protocol word delivered via the first UTCB message register
 *
 * The caller of this function must ensure that num_msg_words is greater
 * than 0.
 */
static inline Novae::mword_t copy_utcb_to_msgbuf(auto const    transaction_id,
                                                 Novae::Utcb & utcb,
                                                 auto        & rcv_msg,
                                                 auto const    num_msg_words,
                                                 auto const    rcv_cap_count)
{
	using namespace Genode;
	using namespace Novae;

	/* the UTCB contains the protocol word, caps to receive and payload */
	mword_t const protocol_word   = utcb.msg()[0];
	auto    const caps_to_receive = utcb.msg()[1];

	/* handle the reception of a malformed message */
	if (num_msg_words < 2)
		return 0;

	auto num_data_words = num_msg_words - 2;

	if (num_data_words * sizeof(mword_t) > rcv_msg.capacity())
		num_data_words = unsigned(rcv_msg.capacity() / sizeof(mword_t));

	/* read message payload into destination message buffer */
	mword_t *src = (mword_t *)(void *)(&utcb.msg()[2]);
	mword_t *dst = (mword_t *)rcv_msg.data();
	for (unsigned i = 0; i < num_data_words; i++)
		*dst++ = *src++;

	/* sanitize caps_to_receive received from other sided */
	auto const max_caps = min(caps_to_receive, rcv_cap_count);

	/* request caps by calling PT_SEL_DELEGATE */
	for (unsigned i = 0; i < max_caps; i++) {
		auto sel = cap_map().insert(0 /* log2 count */);

		if (sel == Capability_space::INVALID_INDEX)
			break;

		utcb.msg()[0] = transaction_id;
		utcb.msg()[1] = 1; /* protocol - take capability */
		utcb.msg()[2] = sel;

		auto mtd = 3u - 1;
		auto res = Novae::call(Novae::PT_SEL_DELEGATE, mtd);

		auto ok = (res == Novae::NOVA_OK) && mtd == 0 && utcb.msg()[0] == 1;
		auto tr = (res == Novae::NOVA_OK) && mtd == 1;

		/* free pre-allocated selector since unused */
		if (!ok)
			cap_map().remove(sel, 0);

		/* translate case, got a former selector instead of new one */
		if (tr)
			sel = utcb.msg()[1];
		else
			if (!ok)
				sel = Capability_space::INVALID_INDEX;

		auto cap = Capability_space::import(sel);
		rcv_msg.insert(cap);
	}

	return protocol_word;
}


static inline void rpc_id_cancel(Novae::Utcb & utcb,
                                 auto const    pt_sel_delegate,
                                 auto const    transaction_id)
{
	utcb.msg()[0] = transaction_id;
	utcb.msg()[1] = 4; /* protocol */

	auto mtd = 2u - 1;

	Novae::call(pt_sel_delegate, mtd);
}


static inline void rpc_id_register(Novae::Utcb & utcb,
                                   auto const    pt_sel_delegate,
                                   auto const    transaction_id,
                                   auto const    pt_dst)
{
	utcb.msg()[0] = transaction_id;
	utcb.msg()[1] = 2; /* protocol */
	utcb.msg()[2] = pt_dst;

	auto mtd = 3u - 1;

	Novae::call(pt_sel_delegate, mtd);
}


/**
 * Copy message payload to UTCB message registers
 */
static inline unsigned copy_msgbuf_to_utcb(auto const    pt_dst,
                                           auto const    transaction_id,
                                           auto const    pt_sel_delegate,
                                           Novae::Utcb & utcb,
                                           auto const  & snd_msg,
                                           auto const    protocol_value)
{
	using namespace Genode;
	using namespace Novae;

	/* look up address and size of message payload */
	mword_t *msg_buf = (mword_t *)snd_msg.data();

	/* size of message payload in machine words */
	auto const num_data_words = snd_msg.data_size() / sizeof(mword_t);

	/* account for protocol value in front of the message */
	auto num_msg_words = 2 + num_data_words;

	auto const num_max_regs = sizeof(Utcb::mr) / sizeof(Utcb::mr[0]);
	if (num_msg_words + snd_msg.used_caps() > num_max_regs)
		num_msg_words = num_max_regs - snd_msg.used_caps();

	/* announce caps which have to be delegated */
	for (unsigned i = 0; i < snd_msg.used_caps(); i++) {
		Native_capability const & cap = snd_msg.cap(i);

		utcb.msg()[0] = transaction_id;
		utcb.msg()[1] = 0; /* protocol - grant cap */
		utcb.msg()[2] = cap.local_name();
		utcb.msg()[3] = pt_dst;

		auto mtd = 4u - 1;
		Novae::call(pt_sel_delegate, mtd);
	}

	/* do the actual Genode RPC */
	utcb.msg()[0] = protocol_value;
	utcb.msg()[1] = snd_msg.used_caps();

	/* store message into UTCB message registers */
	mword_t *src = (mword_t *)&msg_buf[0];
	mword_t *dst = (mword_t *)(void *)&utcb.msg()[2];
	for (unsigned i = 0; i < num_data_words; i++)
		*dst++ = *src++;

	return unsigned(num_msg_words);
}


static inline Novae::mword_t init_transaction_id(Novae::Utcb & utcb,
                                                 auto const    pt_sel_delegate)
{
	utcb.msg()[0] = 0;
	utcb.msg()[1] = 3; /* protocol - transaction_id */

	auto mtd = 2u - 1;
	Novae::call(pt_sel_delegate, mtd);

	return utcb.msg()[0];
}
