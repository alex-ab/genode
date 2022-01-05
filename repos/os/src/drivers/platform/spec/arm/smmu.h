/*
 * \brief  SMMUv2 prototype
 * \author Alexander Boettcher
 * \date   2021-11-04
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__DRIVERS__PLATFORM__SPEC__ARM__SMMU_H_
#define _SRC__DRIVERS__PLATFORM__SPEC__ARM__SMMU_H_

#include <os/attached_mmio.h>

#include <env.h>

namespace Driver { class Smmu; struct Context; }


struct Driver::Context : Mmio
{
	Context(addr_t addr) : Mmio (addr) {}

	struct Sctlr : Register<0x0000, 32> {
		struct Cfcfg: Bitfield< 7, 1> {}; /* Context fault config, terminate or stall */
		struct Cfie : Bitfield< 6, 1> {}; /* Context Fault Interrupt Enable */
		struct Cfre : Bitfield< 5, 1> {}; /* Context Fault Report Enable */
		struct Afe  : Bitfield< 2, 1> {};
		struct Tre  : Bitfield< 1, 1> {};
		struct Muen : Bitfield< 0, 1> {}; /* MMU Enable */
	};

	/* Implementation defined - specific for MMU-500 here */
	struct Actlr : Register<0x0004, 32> {
		struct Cpre  : Bitfield< 1, 1> {};
		struct Cmtlb : Bitfield< 0, 1> {};
	};

	struct Tcr2 : Register<0x010, 32> {};

	struct Ttbr0      : Register<0x020, 64> {
		/* tcr.a1 steers between ttbr0.asid and ttbr1.asid */
		/* asid is 8bit for v7l, 16bit for v8l depending on tcr2.as */
		struct Asid : Bitfield <48, 16> {};
		struct Base : Bitfield < 0, 48> {};
	};
	struct Ttbr1      : Register<0x028, 64> {
		/* tcr.a1 steers between ttbr0.asid and ttbr1.asid */
		/* asid is 8bit for v7l, 16bit for v8l depending on tcr2.as */
		struct Asid : Bitfield <48, 16> {};
		struct Base : Bitfield < 0, 48> {};
	};
	struct Tcr        : Register<0x030, 32> {

		/* 17.3.8 (stage2) vs 16.5.38 (stage1) ! */

		/* 16.5.38 stage1 - valid for SMMU_CBn_CBA2R.VA64 1
		struct Tg1     : Bitfield<30, 2> {};
		struct Sh1     : Bitfield<28, 2> {};
		struct Orgn1   : Bitfield<26, 2> {};
		struct Irgn1   : Bitfield<24, 2> {};
		struct Epd1    : Bitfield<23, 1> {};
		struct A1      : Bitfield<22, 1> {};
		struct T1sz    : Bitfield<16, 5> {};
		struct Tg0     : Bitfield<14, 2> {};
		struct Sh0     : Bitfield<12, 2> {};
		struct Orgn0   : Bitfield<10, 2> {};
		struct Irgn0   : Bitfield< 8, 2> {};
		struct Epd0    : Bitfield< 7, 1> {};
		struct T0sz    : Bitfield< 0, 5> {};
		*/

		/* 17.3.8 stage2 - valid for SMMU_CBn_CBA2R.VA64 1 */
		struct Hd      : Bitfield<22, 1> {};
		struct Ha      : Bitfield<21, 1> {};
		struct Pasize  : Bitfield<16, 2> {};
		struct Tg0     : Bitfield<14, 2> {};
		struct Sh0     : Bitfield<12, 2> {};
		struct Orgn0   : Bitfield<10, 2> {};
		struct Irgn0   : Bitfield< 8, 2> {};
		struct Sl0     : Bitfield< 6, 2> {};
		struct T0sz    : Bitfield< 0, 5> {};
	};

	struct Contextidr : Register<0x034, 32> {};

	struct Fsr        : Register<0x058, 32> {
		struct Upper : Bitfield<11, 21> {};
		struct Lower : Bitfield< 0,  9> {};
		struct Mask_format : Bitset_2 <Lower, Upper> { };

		struct Multi  : Bitfield<31, 1> {};
		struct Ss     : Bitfield<30, 1> {};
		struct Format : Bitfield< 9, 2> {};
		struct Uut    : Bitfield< 8, 1> {};
		struct Asf    : Bitfield< 7, 1> {};
		struct Tlblkf : Bitfield< 6, 1> {};
		struct Tlbmcf : Bitfield< 5, 1> {};
		struct Ef     : Bitfield< 4, 1> {};
		struct Pf     : Bitfield< 3, 1> {};
		struct Aff    : Bitfield< 2, 1> {};
		struct Tf     : Bitfield< 1, 1> {};

		static void dump(Fsr::access_t const &fsr)
		{
			log("fsr=", Hex(fsr),
			    Multi::get(fsr) ? " multi"    : "",
			    Ss::get(fsr)    ? " ss"       : "",
			    " format=", Format::get(fsr),
			    Uut::get(fsr)    ? " uut" : "",
				Asf::get(fsr)    ? " asf" : "",
				Tlblkf::get(fsr) ? " tlblkf" : "",
				Tlbmcf::get(fsr) ? " tlbmcf" : "",
				Ef::get(fsr)     ? " ef"  : "",
				Pf::get(fsr)     ? " pf"  : "",
				Aff::get(fsr)    ? " aff" : "",
				Tf::get(fsr)     ? " tf"  : ""
			);
		}
	};
	struct Far        : Register<0x060, 64> {};
	struct Fsynr0     : Register<0x068, 32> {};
	struct Fsynr1     : Register<0x06c, 32> {};
	struct Ipafar     : Register<0x070, 64> {};

	void disable_translation()
	{
		Sctlr::access_t sctlr = 0;
		/* XXX - disabling MMU let's bypass a streamid, if it points here.
		 * So, we let nothing point here (via smrN and s2crN).
		 *
		 * Otherwise, Muen must be 1 (MMU entry enabled) and an empty
		 * pagetable must be configured, see enable_translation().
		 */
		Sctlr::Muen::set(sctlr, 0);
		write<Sctlr>(sctlr);
	}

	void enable_translation(uint64_t tt_phys_addr)
	{
		//Ttbr0
		//Ttbr1
		//Contextidr
		//Tcr
		//Sctlr

		unsigned asid = 0; /* XXX - management of ASIDs to be done */

		/* we use atm only one stage, but setup for both stages the table pointer */
		{
			Ttbr0::access_t ttbr0 = 0;
			Ttbr0::Base::set(ttbr0, tt_phys_addr);
			Ttbr0::Asid::set(ttbr0, asid);
			write<Ttbr0>(ttbr0);
		}

		{
			Ttbr1::access_t ttbr1 = 0;
			Ttbr1::Base::set(ttbr1, tt_phys_addr);
			Ttbr1::Asid::set(ttbr1, asid);
			write<Ttbr1>(ttbr1);
		}

		/* translation control register SMMU_CBn_TCR for SMMU_CBn_CBA2R.VA64 == 1 and stage2 only (17.3.8) */
		{
			Tcr::access_t tcr = 0;

			/* stage 2 */
			/* using orgn0/irgn0/t0sz values as also used by hw kernel */
			Tcr::Hd     ::set(tcr, 0); /* hw management dirty bit   - update disabled */
			Tcr::Ha     ::set(tcr, 0); /* hw management access flag - update disabled */
			Tcr::Pasize ::set(tcr, 0b010); /* 1 TB */
			Tcr::Tg0    ::set(tcr, 0b00); /* 4k */
			Tcr::Sh0    ::set(tcr, 0b10); /* XXX share ability settings */
			Tcr::Orgn0  ::set(tcr, 1);
			Tcr::Irgn0  ::set(tcr, 1);
			Tcr::Sl0    ::set(tcr, 0b01); /* lookup start Level 1 */
			Tcr::T0sz   ::set(tcr, 25);

			write<Tcr>(tcr);
		}

		Sctlr::access_t sctlr = 0;
		Sctlr::Muen ::set(sctlr, 1); /* MMU entry enable */
		Sctlr::Cfre ::set(sctlr, 1); /* context fault interrupt enable */
		Sctlr::Cfie ::set(sctlr, 1); /* context fault report enable */

		write<Sctlr>(sctlr);
	}
};

class Driver::Smmu : Attached_mmio
{
	private:

		struct S2cr_index { unsigned value; };
		struct Cbar_index { unsigned value; };

		typedef Signal_handler<Smmu> Irq_dispatch;

		Driver::Env    &_env;
		Irq_connection  _irq;
		Irq_dispatch    _dispatch { _env.env.ep(), *this, &Smmu::handle_irq };
		unsigned        _map_groups {};
		unsigned const  _context_banks {};
		size_t          _smmu_size;

		bool           _verbose { false };

		enum { MAXREGN = 128 };

		struct Cr0 : Register<0x0000, 32> {
			struct Vmid16en   : Bitfield<31, 1> {};
			struct Smcfcfg    : Bitfield<21, 1> {};
			struct Ptm        : Bitfield<12, 1> {};
			struct Vmidpne    : Bitfield<11, 1> {};
			struct Usfcfg     : Bitfield<10, 1> {};
			struct Gse        : Bitfield< 9, 1> {};
			struct Stalld     : Bitfield< 8, 1> {};
			struct Gcfgfie    : Bitfield< 5, 1> {};
			struct Gcfgfre    : Bitfield< 4, 1> {};
			struct Exidenable : Bitfield< 3, 1> {};
			struct Gfie       : Bitfield< 2, 1> {};
			struct Gfre       : Bitfield< 1, 1> {};
			struct Clientpd   : Bitfield< 0, 1> {};
		};

		/* Implementation defined - specific for MMU-500 here */
		struct Acr  : Register<0x0010, 32> {
			struct Cache_lock    : Bitfield<26, 1> {};
			struct S2crb_tlben   : Bitfield<10, 1> {};
			struct Mmudisb_tlben : Bitfield< 9, 1> {};
			struct Smtnmb_tlben  : Bitfield< 8, 1> {};
			struct Ipa2pa_cen    : Bitfield< 4, 1> {};
			struct S2wc2en       : Bitfield< 3, 1> {};
			struct S1wc2en       : Bitfield< 2, 1> {};
		};

		struct Idr0 : Register<0x0020, 32> {
			struct Ses       : Bitfield<31, 1> {}; /* 2 security state support */
			struct S1ts      : Bitfield<30, 1> {}; /* S1 translation support */
			struct S2ts      : Bitfield<29, 1> {}; /* S2 translation support */
			struct Nts       : Bitfield<28, 1> {}; /* nested translation support */
			struct Sms       : Bitfield<27, 1> {}; /* Stream Match support */
			struct Exsmrgs   : Bitfield<15, 1> {}; /* Extended Stream Matching */
			struct Cttw      : Bitfield<14, 1> {}; /* Coherent translation table walk */
			struct Numsidb   : Bitfield< 9, 4> {}; /* Stream IDs bit count */
			struct Exids     : Bitfield< 8, 1> {}; /* Extended StreamID support */
			struct Numsmrg   : Bitfield< 0, 8> {}; /* Number of Stream Mapping Register Groups */
		};
		struct Idr1 : Register<0x0024, 32> {
			struct Pagesize    : Bitfield < 31, 1> {};
			struct Numpagendxb : Bitfield < 28, 3> {};
			struct Nums2cb     : Bitfield < 16, 8> {};
			struct Numcb       : Bitfield <  0, 8> {};
		};
		struct Idr2 : Register<0x0028, 32> {
			struct Ubs     : Bitfield < 8, 4> {};
			struct Vmid16s : Bitfield <15, 1> {};
		};
		struct Idr3 : Register<0x002c, 32> {};
		struct Idr4 : Register<0x0030, 32> {};
		struct Idr5 : Register<0x0034, 32> {};
		struct Idr6 : Register<0x0038, 32> {};

		/* Implementation defined - Bitfields are specific for MMU-500 here */
		struct Idr7 : Register<0x003c, 32> {
			struct Major : Bitfield < 4, 4> {};
			struct Minor : Bitfield < 0, 4> {};
		};

		struct Gfar : Register<0x0040, 64> {
			/* for SMMUv2, valid bits depend on SMMU_IDR2.UBS */
			struct Faddr : Bitfield < 0, 64> {}; /* fault address */
		};

		struct Gfsr : Register<0x0048, 32> {
			struct Icf   : Bitfield <  0, 1> {};
			struct Ucf   : Bitfield <  1, 1> {};
			struct Smcf  : Bitfield <  2, 1> {};
			struct Ucbf  : Bitfield <  3, 1> {};
			struct Ucif  : Bitfield <  4, 1> {};
			struct Caf   : Bitfield <  5, 1> {};
			struct Ef    : Bitfield <  6, 1> {};
			struct Pf    : Bitfield <  7, 1> {};
			struct Uut   : Bitfield <  8, 1> {};
			struct Multi : Bitfield < 31, 1> {};
		};

		struct Gfsynr0 : Register<0x050, 32> {
		};

		struct Gfsynr1 : Register<0x054, 32> {
			struct Index     : Bitfield <16, 16> {};
			struct Stream_id : Bitfield < 0, 16> {};
		};
		struct Gfsynr2 : Register<0x058, 32> {};

		struct Tlbiallnsnh : Register<0x0068, 32> {};
		struct Tlbiallh    : Register<0x006c, 32> {};
		struct Tlbgsync    : Register<0x0070, 32> {};
		struct Tlbgstatus  : Register<0x0074, 32> {};

		struct Gpar  : Register<0x180, 64> {};
		struct Gatsr : Register<0x188, 32> {};

		struct Smrn : Register_array<0x0800, 32, MAXREGN, 32> {
			struct Valid : Bitfield <31,  1> {};
			struct Mask  : Bitfield <16, 15> {};
			struct Id    : Bitfield < 0, 15> {};
		};

		struct S2crn : Register_array<0x0c00, 32, MAXREGN, 32> {
			struct Type  : Bitfield<16, 2> {
				enum { TRANSLATE = 0b00, BYPASS = 0b01, FAULT = 0b10 };
			};
			struct Cbndx : Bitfield< 0, 8> {};
		};

		struct Cbarn : Register_array<0x1000, 32, MAXREGN, 32> {
			struct Type : Bitfield<16, 2> {
				enum {
					STAGE2_ONLY               = 0b00,
					STAGE1_WITH_STAGE2_BYPASS = 0b01,
					STAGE1_WITH_STAGE2_FAULT  = 0b10,
					STAGE1_AND_STAGE2         = 0b11
				};
			};
		};

		struct Cba2r : Register_array<0x1800, 32, MAXREGN, 32> {
			struct Vmid16 : Bitfield<16, 16> {};
			struct Monc   : Bitfield< 1,  1> {};
			struct Va64   : Bitfield< 0,  1> {};
		};

		/* Implementation defined - specific for MMU-500 */
		struct Itctrl         : Register<0x2000, 32> {};
		struct Tbu_pwr_status : Register<0x2204, 32> {};

		void enable_smmu()
		{
			if (_verbose)
				dump_cr0(read<Smmu::Cr0>());

			Cr0::access_t cr0 = 0;

			Cr0::Clientpd::set(cr0, 0); /* disable bypass */
			Cr0::Ptm     ::set(cr0, 0); /* XXX */
			Cr0::Vmidpne ::set(cr0, 0); /* XXX */
			Cr0::Usfcfg  ::set(cr0, 1); /* unidentified stream fault */
			Cr0::Smcfcfg ::set(cr0, 1); /* stream match conflict fault */
			Cr0::Gfre    ::set(cr0, 1); /* enable fault reporting */
			Cr0::Gfie    ::set(cr0, 1); /* enable irq raising on fault */
			Cr0::Stalld  ::set(cr0, 0);
			Cr0::Gcfgfre ::set(cr0, 1);
			Cr0::Gcfgfie ::set(cr0, 1);
			Cr0::Gse     ::set(cr0, 0);
			Cr0::Vmid16en::set(cr0, 0); /* usable if idr2.vmid16s set */

			if (_verbose)
				log("cr0=", Hex(cr0), " write");

			write<Cr0>(cr0);

			cr0 = read<Cr0>();

			if (_verbose)
				dump_cr0(read<Smmu::Cr0>());
		}

		template <typename T>
		void for_each_context(T const &fn)
		{
			for (unsigned i = 0; i < _context_banks; i++) {
				for_context(i, fn);
			}
		}

		template <typename T>
		void for_context(unsigned i, T const &fn)
		{
			auto const idr1 = read<Smmu::Idr1>();

			auto const pagetype    = Smmu::Idr1::Pagesize::get(idr1);
			auto const numpagendxb = Smmu::Idr1::Numpagendxb::get(idr1);

			uint64_t const pagesize = pagetype ? 64 * 1024 : 4096;
			uint64_t const numpages = 1ul << (numpagendxb + 1);

			uint64_t smmu_global_size = pagesize * numpages;
			uint64_t smmu_cb_offset   = smmu_global_size; /* translation context bank */

			if (smmu_cb_offset + _context_banks * pagesize > _smmu_size) {
				error("out of bound access");
				throw __LINE__;
			}

			if (i >= _context_banks) {
				error("out of bound access");
				throw __LINE__;
			}

			Context context { reinterpret_cast<addr_t>(local_addr<char>() + smmu_cb_offset + i * pagesize) };

			fn(context);
		}

		void reset_s2crn()
		{
			for (unsigned idx = 0; idx < _map_groups; idx++) {
				S2crn::access_t value = 0;
				S2crn::Type::set(value, S2crn::Type::FAULT);
				write<S2crn>(value, idx);
			}
		}

		void reset_cbarn()
		{
			for (unsigned idx = 0; idx < _context_banks; idx++) {
				{
					Cbarn::access_t value = 0;
					Cbarn::Type::set(value, Cbarn::Type::STAGE1_WITH_STAGE2_FAULT);
					write<Cbarn>(value, idx);
				}

				{
					Cba2r::access_t value = 0;
					Cba2r::Vmid16::set(value, 0); /* RES0 if CR0.vmid16en == 0 */
					Cba2r::Monc::set(value, 0);
					Cba2r::Va64::set(value, 1); /* V7S, V7L == 0, V8L == 1 */
					write<Cba2r>(value, idx);
				}
			}
		}

		void reset_smrn()
		{
			for (unsigned idx = 0; idx < _map_groups; idx++) {
				/* match of idx within SmrX means S2crX is used */
				/* Convention: streamid == idx for S2crX */
				Smrn::access_t value = 0;
				Smrn::Valid::set(value, 1);
				Smrn::Id::set(value, idx);
				Smrn::Mask::set(value, 0x7f80u);
				write<Smrn>(value, idx);
			}
		}

		void dump_cr0(Cr0::access_t const &cr0)
		{
			log("cr0=", Hex(cr0),
			    Cr0::Smcfcfg::get(cr0)    ? " smcfcfg"    : "",
			    Cr0::Ptm::get(cr0)        ? " ptm"        : "",
			    Cr0::Vmidpne::get(cr0)    ? " vmidpne"    : "",
			    Cr0::Usfcfg::get(cr0)     ? " usfcfg"     : "",
			    Cr0::Gse::get(cr0)        ? " gse"        : "",
			    Cr0::Stalld::get(cr0)     ? " stalld"     : "",
			    Cr0::Gcfgfie::get(cr0)    ? " gcfgfie"    : "",
			    Cr0::Gcfgfre::get(cr0)    ? " gcfgfre"    : "",
			    Cr0::Exidenable::get(cr0) ? " exidenable" : "",
			    Cr0::Gfie::get(cr0)       ? " gfie"       : "",
			    Cr0::Gfre::get(cr0)       ? " gfre"       : "",
			    Cr0::Clientpd::get(cr0)   ? " clientpd"   : "");
		}

		void dump_idr0(Idr0::access_t const &idr0)
		{
			log("idr0=", Hex(idr0),
			    Idr0::Ses::get(idr0)   ? " ses,"   : "",
			    Idr0::S1ts::get(idr0)  ? " s1ts,"  : "",
			    Idr0::S2ts::get(idr0)  ? " s2ts,"  : "",
			    Idr0::Nts::get(idr0)   ? " nts,"   : "",
			    Idr0::Exids::get(idr0) ? " exids," : "",
			    Idr0::Cttw::get(idr0)  ? " cttw,"  : "",
			    Idr0::Sms::get(idr0)   ? " Stream match," : " Stream indexing,",
			    Idr0::Exsmrgs::get(idr0)   ? " Extended stream matching," : "",
			    " numsidb=", Idr0::Numsidb::get(idr0),
			    " numsmrg=", Idr0::Numsmrg::get(idr0));
		}

		void dump_idr1(Idr1::access_t const &idr1)
		{
			auto const pagetype    = Smmu::Idr1::Pagesize::get(idr1);
			auto const numpagendxb = Smmu::Idr1::Numpagendxb::get(idr1);
			auto const numcb       = Smmu::Idr1::Numcb::get(idr1);
			auto const nums2cb     = Smmu::Idr1::Nums2cb::get(idr1);

			log("idr1=", Hex(idr1),
			    " pagetype=", pagetype ? "64k" : "4k",
			    " numpagendxb=", numpagendxb,
			    " numcb=", numcb,
			    " nums2cb=", nums2cb);
		}

		void dump_acr(Acr::access_t const &acr)
		{
			log("acr=", Hex(acr),
			    Acr::Cache_lock::get(acr)    ? " cache_lock"    : "",
			    Acr::S2crb_tlben::get(acr)   ? " s2crb_tlben"     : "",
			    Acr::Mmudisb_tlben::get(acr) ? " mmudisb_tlben"        : "",
			    Acr::Smtnmb_tlben::get(acr)  ? " smtnmb_tlben"     : "",
			    Acr::Ipa2pa_cen::get(acr)    ? " ipa2pa_cen"    : "",
			    Acr::S2wc2en::get(acr)       ? " s2wc2en"    : "",
			    Acr::S1wc2en::get(acr)       ? " s1wc2en"    : "");
		}

		void dump_gfsr(Gfsr::access_t const &gfsr)
		{
			log("gfsr=", Hex(gfsr),
			    Gfsr::Icf::get(gfsr)   ? ", invalid context fault" : "",
			    Gfsr::Ucf::get(gfsr)   ? ", unidentified stream fault" : "",
			    Gfsr::Smcf::get(gfsr)  ? ", stream match conflict fault" : "",
			    Gfsr::Ucbf::get(gfsr)  ? ", unimplemented context bank fault" : "",
			    Gfsr::Ucif::get(gfsr)  ? ", unimplemented context irq fault " : "",
			    Gfsr::Caf::get(gfsr)   ? ", config access fault" : "",
			    Gfsr::Ef::get(gfsr)    ? ", external fault" : "",
			    Gfsr::Pf::get(gfsr)    ? ", permission fault" : "",
			    Gfsr::Uut::get(gfsr)   ? ", unsupported upstream transaction" : "",
			    Gfsr::Multi::get(gfsr) ? ", multiple error conditions" : "");
		}

		void handle_irq()
		{
			auto const gfsr = read<Gfsr>();
			auto const gfar = read<Gfar>();

			auto const gfsynr0 = read<Gfsynr0>();
			auto const gfsynr1 = read<Gfsynr1>();
			auto const gfsynr2 = read<Gfsynr2>();
			auto const gpar    = read<Gpar>();
			auto const gatsr   = read<Gatsr>();

			bool context_bank_fault = false;

			unsigned cnt = 0;

			for_each_context([&](Context &context) {
				auto const fsr = context.read<Context::Fsr>();
				if (Context::Fsr::Mask_format::get(fsr)) {
					context_bank_fault = true;

					error("SMMU fault - context ", cnt, " fault: ",
					      " fsr=", Hex(fsr),
					      " far=", Hex(context.read<Context::Far>()), " ",
					      " fsynr0=", Hex(context.read<Context::Fsynr0>()), " ",
					      " fsynr1=", Hex(context.read<Context::Fsynr1>()), " ",
					      " ipafar=", Hex(context.read<Context::Ipafar>()));

					Context::Fsr::dump(fsr);

					/* ack fault */
					context.write<Context::Fsr>(fsr);
				}
				cnt ++;
			});

			if (gfsr || !context_bank_fault) {
				error("SMMU fault - irq ",
				      " gfar=",    Hex(gfar),
				      ", gfsynr0=", Hex(gfsynr0),
				      ", gfsynr1=", Hex(gfsynr1), " Index=", Hex(Gfsynr1::Index::get(gfsynr1)), " StreamID=", Hex(Gfsynr1::Stream_id::get(gfsynr1)),
				      ", gfsynr2=", Hex(gfsynr2),
				      ", gpar="   , Hex(gpar),
				      ", gatsr="  , Hex(gatsr)
				     );

				dump_gfsr(gfsr);
			}

			if (gfsr) {
				/* ack fault */
				write<Smmu::Gfsr>(gfsr);
			}

			_irq.ack_irq();
		}

		Cbar_index _lookup_context_id(S2cr_index const s2cr_index)
		{
			/**
			 * By convention, for now, the s2crX position is used as
			 * index into the context bank registers (see enable_translation()),
			 * which makes the lookup here easy.
			 */
			return Cbar_index { .value = s2cr_index.value };
		}

		S2cr_index _lookup_s2cr_index(unsigned streamid)
		{
			/**
			 * By convention, for now, streamid is used as idx value in the
			 * registers (see reset_smrn()), which makes the lookup here easy.
			 */
			return S2cr_index { .value = streamid };
		}

		void invalidate_tlb()
		{
			/* invalidate all TLB entries */
			write<Tlbiallh>(~0U);
			write<Tlbiallnsnh>(~0U);
			write<Tlbgsync>(~0U);
		}

	public:

		Smmu(Driver::Env &env, uint64_t base, size_t size, unsigned irq)
		:
			Attached_mmio(env.env, base, size),
			_env(env),
			_irq(_env.env, irq),
			_context_banks(read<Smmu::Idr1::Numcb>()),
			_smmu_size(size)
		{
			auto const idr0 = read<Idr0>();
			if (_verbose)
				dump_idr0(idr0);

			if (Idr0::Exids::get(idr0)) {
				_map_groups = 1u << 16;
				error("Extended IDs not supported");
				/* if support is enabled, check also exidenable ! */
				throw __LINE__;
			} else
				_map_groups = 1u << Idr0::Numsidb::get(idr0);

			if (Idr0::Sms::get(idr0))
			    _map_groups = Idr0::Numsmrg::get(idr0);

			if (!_map_groups || _map_groups > MAXREGN) {
				error("stream mapping register group detection failed");
				throw __LINE__;
			}

			if (_verbose) {
				auto const idr7 = read<Idr7>();

				log("idr7=", Hex(idr7),
				    " major=", Idr7::Major::get(idr7),
				    " minor=", Idr7::Minor::get(idr7));
			}

			/* Auxiliary Configuration registers, MMU-500 specific */
			{
				Acr::access_t acr = 0;
				Acr::Cache_lock::set(acr, 0);
				Acr::S2crb_tlben::set(acr, 1);
				Acr::Mmudisb_tlben::set(acr, 1);
				Acr::Smtnmb_tlben::set(acr, 1);
				Acr::Ipa2pa_cen::set(acr, 0);
				Acr::S2wc2en::set(acr, 1); /* caching */
				Acr::S1wc2en::set(acr, 1); /* caching */

				write<Acr>(acr);

				acr = read<Acr>();
				if (_verbose)
					dump_acr(acr);
			}

			/*
			 * Initialize SMMU registers, e.g. smrN, s2crN and cbarN
			 * to let fault un-configured/unknown devices
			 */
			for_each_context([](Context &context) {
				context.disable_translation();
			});

			reset_cbarn();
			reset_smrn();

			reset_s2crn();

			invalidate_tlb();

			_irq.sigh(_dispatch);
			_irq.ack_irq();

			enable_smmu();
		}

		void disable_translation(unsigned streamid)
		{
			auto s2cr_idx   = _lookup_s2cr_index(streamid);
			auto context_id = _lookup_context_id(s2cr_idx);

			{
				S2crn::access_t value = 0;
				S2crn::Type::set(value, S2crn::Type::FAULT);
				S2crn::Cbndx::set(value, context_id.value);
				write<S2crn>(value, s2cr_idx.value);
			}

			{
				Cbarn::access_t value = 0;
				Cbarn::Type::set(value, Cbarn::Type::STAGE1_WITH_STAGE2_FAULT);
				write<Cbarn>(value, s2cr_idx.value);
			}

			for_context(context_id.value, [&](Context &context) {
				context.disable_translation();
			});

			invalidate_tlb();
		}

		void enable_translation(unsigned const streamid,
		                        uint64_t const phys_page_table)
		{
			auto s2cr_idx   = _lookup_s2cr_index(streamid);
			auto context_id = _lookup_context_id(s2cr_idx);

			{
				S2crn::access_t value = 0;
				S2crn::Type::set(value, S2crn::Type::TRANSLATE);
				S2crn::Cbndx::set(value, context_id.value);
				write<S2crn>(value, s2cr_idx.value);
			}

			{
				Cbarn::access_t value = 0;
				Cbarn::Type::set(value, Cbarn::Type::STAGE2_ONLY);
				write<Cbarn>(value, s2cr_idx.value);
			}

			for_context(context_id.value, [&](Context &context) {
				context.enable_translation(phys_page_table);
			});

			invalidate_tlb();
		}
};

#endif /* _SRC__DRIVERS__PLATFORM__SPEC__ARM__SMMU_H_ */
