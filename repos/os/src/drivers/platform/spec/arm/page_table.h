#ifndef _SRC__DRIVERS__PLATFORM__SPEC__ARM__PAGE_TABLE_H_
#define _SRC__DRIVERS__PLATFORM__SPEC__ARM__PAGE_TABLE_H_

#include <hw/spec/arm/lpae.h>

namespace Driver {
	template <Genode::size_t TABLE_SIZE, unsigned COUNT> class Table_allocator;
	template <Genode::size_t TABLE_SIZE, unsigned COUNT> class Page_table_array;
};


template <Genode::size_t TABLE_SIZE, unsigned COUNT>
class Driver::Table_allocator : public Hw::Page_table::Allocator
{
	private:

		using Bit_allocator = Genode::Bit_allocator<COUNT>;
		using Array = typename Page_table_allocator<TABLE_SIZE>::Array<COUNT>;

		Bit_allocator _free_tables { };

		unsigned _alloc() override
		{
			try {
				unsigned idx = _free_tables.alloc();
				return idx;
			} catch (typename Bit_allocator::Out_of_indices&) {}
			throw Hw::Out_of_tables();
		}

		void _free(unsigned idx) override { _free_tables.free(idx); }

	public:

		Table_allocator(Genode::addr_t virt, Genode::addr_t phys)
		: Page_table_allocator(virt, phys) {}
};


template <Genode::size_t TABLE_SIZE, unsigned COUNT>
class Driver::Page_table_array
{
	private:

		Genode::Attached_ram_dataspace _tables;

		Table_allocator<TABLE_SIZE, COUNT> _alloc;

		Genode::addr_t _phys_base()
		{
			Genode::Dataspace_client ds_client(_tables.cap());
			return ds_client.phys_addr();
		}

	public:

		Page_table_array(Genode::Ram_allocator &ram, Genode::Region_map &rm)
		:
			_tables(ram, rm, COUNT * TABLE_SIZE, Genode::Cache::UNCACHED),
			_alloc(reinterpret_cast<Genode::addr_t>(_tables.local_addr<void>()),
			       _phys_base())
		{ }

		Hw::Page_table_allocator<TABLE_SIZE> & allocator() { return _alloc; }
};

#endif /* _SRC__DRIVERS__PLATFORM__SPEC__ARM__PAGE_TABLE_H_ */
