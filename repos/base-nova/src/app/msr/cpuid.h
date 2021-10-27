/*
 * \brief  CPUID support
 * \author Martin Stein
 */

/*
 * Copyright (C) 2020-2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#pragma once

namespace Msr
{
	struct Cpuid;

	void
	cpuid(unsigned *ax, unsigned *bx, unsigned *cx, unsigned *dx)
	{
		asm volatile ("cpuid" : "+a" (*ax), "+d" (*dx), "+b" (*bx), "+c"(*cx)
		                      :: "memory");
	}

	void cpuid(unsigned const idx, unsigned &a, unsigned &b,
	           unsigned &c, unsigned &d)
	{
		a = idx;
		b = c = d = 0;
		cpuid(&a, &b, &c, &d);
	}
}

struct Msr::Cpuid
{
	enum { MAX_LEAF_IDX = 8 };

	unsigned eax[MAX_LEAF_IDX];
	unsigned ebx[MAX_LEAF_IDX];
	unsigned ecx[MAX_LEAF_IDX];
	unsigned edx[MAX_LEAF_IDX];

	void init_leaf(unsigned idx) {
		cpuid(idx, eax[idx], ebx[idx], ecx[idx], edx[idx]);
	}

	Cpuid() {
		cpuid (0, eax[0], ebx[0], ecx[0], edx[0]);
		for (unsigned idx = 1; idx <= eax[0] && idx < MAX_LEAF_IDX; idx++) {
			init_leaf(idx);
		}
	}

	enum class Vendor {
		INTEL,
		UNKNOWN,
	};

	enum { VENDOR_STRING_LENGTH = 12 };

	Vendor vendor() const
	{
		char intel[VENDOR_STRING_LENGTH] { 'G', 'e', 'n', 'u', 'i', 'n', 'e',
		                                   'I', 'n', 't', 'e', 'l' };
        unsigned idx { 0 };

        for (unsigned shift = 0; shift <= 24; shift += 8, idx++) {
            char str = static_cast<char>(ebx[0] >> shift);
            if (intel[idx] != str)
                return Vendor::UNKNOWN;
        }

        for (unsigned shift = 0; shift <= 24; shift += 8, idx++) {
            char str = static_cast<char>(edx[0] >> shift);
            if (intel[idx] != str)
                return Vendor::UNKNOWN;
        }

        for (unsigned shift = 0; shift <= 24; shift += 8, idx++) {
            char str = static_cast<char>(ecx[0] >> shift);
            if (intel[idx] != str)
               return Vendor::UNKNOWN;
        }

        return Vendor::INTEL;
    }

    using Family_id = unsigned;
    enum { FAMILY_ID_UNKNOWN = ~static_cast<unsigned>(0) };

    Family_id family_id() const
    {
        if (eax[0] < 1) {
            return FAMILY_ID_UNKNOWN;
        }
        enum { FAMILY_ID_SHIFT = 8 };
        enum { FAMILY_ID_MASK = 0xf };
        enum { EXT_FAMILY_ID_SHIFT = 20 };
        enum { EXT_FAMILY_ID_MASK = 0xff };
        Family_id family_id {
            (eax[1] >> FAMILY_ID_SHIFT) & FAMILY_ID_MASK };

        if (family_id == 15) {
            family_id += (eax[1] >> EXT_FAMILY_ID_SHIFT) & EXT_FAMILY_ID_MASK;
        }
        return family_id;
    }

    enum class Model {
        KABY_LAKE_DESKTOP,
        UNKNOWN,
    };

    Model model() const
    {
        if (eax[0] < 1) {
            return Model::UNKNOWN;
        }
        enum { MODEL_ID_SHIFT = 4 };
        enum { MODEL_ID_MASK = 0xf };
        enum { EXT_MODEL_ID_SHIFT = 16 };
        enum { EXT_MODEL_ID_MASK = 0xf };
        unsigned const fam_id { family_id() };
        unsigned model_id { (eax[1] >> MODEL_ID_SHIFT) & MODEL_ID_MASK };
        if (fam_id == 6 ||
            fam_id == 15)
        {
            model_id +=
                ((eax[1] >> EXT_MODEL_ID_SHIFT) & EXT_MODEL_ID_MASK) << 4;
        }
        switch (model_id) {
        case 0x9e: return Model::KABY_LAKE_DESKTOP;
        default:   return Model::UNKNOWN;
        }
    }

    bool hwp() const
    {
        if (eax[0] < 6) {
            return false;
        }
        return ((eax[6] >> 7) & 1) == 1;
    }

    bool hwp_notification() const
    {
        if (eax[0] < 6) {
            return false;
        }
        return ((eax[6] >> 8) & 1) == 1;
    }

    bool hwp_energy_perf_pref() const
    {
        if (eax[0] < 6) {
            return false;
        }
        return ((eax[6] >> 10) & 1) == 1;
    }

    bool hardware_coordination_feedback_cap() const
    {
        if (eax[0] < 6) {
            return false;
        }
        return ((ecx[6] >> 0) & 1) == 1;
    }

    bool hwp_energy_perf_bias() const
    {
        if (eax[0] < 6) {
            return false;
        }
        return ((ecx[6] >> 3) & 1) == 1;
    }
};
