/*
 * \brief
 * \author Alexander Boettcher
 *
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */


#include <util/mmio.h>

class Pci_bridge : Acpica::Callback<Pci_bridge> {

	private:

		/* call method with two arguments as input */
		static ACPI_STATUS _invoke_acpi(char const * const name,
		                                int  const value0,
		                                int  const value1)
		{
			ACPI_OBJECT_LIST para_in;
			ACPI_OBJECT      values[2];

			values[0].Type = ACPI_TYPE_INTEGER;
			values[1].Type = ACPI_TYPE_INTEGER;

			values[0].Integer.Value = value0;
			values[1].Integer.Value = value1;

			para_in.Count   = 2;
			para_in.Pointer = values;

			Acpica::Buffer<ACPI_OBJECT> result { };
			ACPI_STATUS res;

			res = AcpiEvaluateObjectTyped(ACPI_ROOT_OBJECT,
			                              ACPI_STRING(name), &para_in,
			                              &result, ACPI_TYPE_INTEGER);

			if (ACPI_FAILURE(res)) {
				Genode::error("failed   - '", __func__, "' ", name,
				              "res=", Genode::Hex(res));
				return AE_OK;
			}

			UINT64 value = result.object.Integer.Value;

			Genode::log("pci bridge - ", name, "=", Genode::Hex(value));

			return AE_OK;
		}

	public:

		Pci_bridge(Genode::Env &, Genode::Allocator &, Acpica::Reportstate *)
		{ }

		static ACPI_STATUS detect(ACPI_HANDLE ec, UINT32, void *m, void **)
		{
			Acpica::Main * main = reinterpret_cast<Acpica::Main *>(m);

			Genode::log("pci bridge detected - ", main);

			{
				Acpica::Buffer<ACPI_OBJECT> sta;
				ACPI_STATUS res;

				res = AcpiEvaluateObjectTyped(ec, ACPI_STRING("GPCB"), nullptr,
				                              &sta, ACPI_TYPE_INTEGER);

				if (ACPI_FAILURE(res)) {
					Genode::error("failed   - '", __func__, "' GPCB "
					              "res=", Genode::Hex(res));
					return AE_OK;
				}

				UINT64 gpcb = sta.object.Integer.Value;

				Genode::log("pci bridge GPCB=", Genode::Hex(gpcb));
			}

			_invoke_acpi("MMTB", 0, 0);
			_invoke_acpi("TBTD", 1, 1);
			_invoke_acpi("TBTF", 1, 2);

			return AE_OK;
		}

		void generate(Genode::Xml_generator &)
		{
		}
};
