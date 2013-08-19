/*
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de.
 * Based on code written by Travis Geiselbrecht for NewOS.
 *
 * Distributed under the terms of the MIT License.
 */


#include "arch_mmu.h"

#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/kernel_args.h>
#include <boot/stage2.h>
#include <arch/cpu.h>
#include <arch_kernel.h>
#include <arm_mmu.h>
#include <kernel.h>

#include <board_config.h>

#include <OS.h>

#include <string.h>


//#define TRACE_MMU
#ifdef TRACE_MMU
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define TRACE_MEMORY_MAP
	// Define this to print the memory map to serial debug,
	// You also need to define ENABLE_SERIAL in serial.cpp
	// for output to work.


/*
TODO:
	-recycle bit!
*/

/*!	The (physical) memory layout of the boot loader is currently as follows:
	 0x00000000			u-boot (run from NOR flash)
	 0xa0000000			u-boot stuff like kernel arguments afaik
	 0xa0100000 - 0xa0ffffff	boot.tgz (up to 15MB probably never needed so big...)
	 0xa1000000 - 0xa1ffffff	pagetables
	 0xa2000000 - ?			code (up to 1MB)
	 0xa2100000			boot loader heap / free physical memory

	The kernel is mapped at KERNEL_BASE, all other stuff mapped by the
	loader (kernel args, modules, driver settings, ...) comes after
	0x80020000 which means that there is currently only 2 MB reserved for
	the kernel itself (see kMaxKernelSize).
*/


/*
*defines a block in memory
*/
struct memblock {
	const char name[16];
		// the name will be used for debugging etc later perhaps...
	addr_t	start;
		// start of the block
	addr_t	end;
		// end of the block
	uint32	flags;
		// which flags should be applied (device/normal etc..)
};


static struct memblock LOADER_MEMORYMAP[] = {
	{
		"devices",
		DEVICE_BASE,
		DEVICE_BASE + DEVICE_SIZE - 1,
		ARM_MMU_L2_FLAG_B,
	},
	{
		"RAM_loader", // 1MB loader
		SDRAM_BASE + 0,
		SDRAM_BASE + 0x0fffff,
		ARM_MMU_L2_FLAG_C,
	},
	{
		"RAM_pt", // Page Table 1MB
		SDRAM_BASE + 0x100000,
		SDRAM_BASE + 0x1FFFFF,
		ARM_MMU_L2_FLAG_C,
	},
	{
		"RAM_free", // 16MB free RAM (more but we don't map it automaticaly)
		SDRAM_BASE + 0x0200000,
		SDRAM_BASE + 0x11FFFFF,
		ARM_MMU_L2_FLAG_C,
	},
	{
		"RAM_stack", // stack
		SDRAM_BASE + 0x1200000,
		SDRAM_BASE + 0x2000000,
		ARM_MMU_L2_FLAG_C,
	},
	{
		"RAM_initrd", // stack
		SDRAM_BASE + 0x2000000,
		SDRAM_BASE + 0x2500000,
		ARM_MMU_L2_FLAG_C,
	},

#ifdef FB_BASE
	{
		"framebuffer", // 2MB framebuffer ram
		FB_BASE,
		FB_BASE + FB_SIZE - 1,
		ARM_MMU_L2_FLAG_AP_RW | ARM_MMU_L2_FLAG_C,
	},
#endif
};


//static const uint32 kDefaultPageTableFlags = MMU_FLAG_READWRITE;
	// not cached not buffered, R/W
static const size_t kMaxKernelSize = 0x200000;		// 2 MB for the kernel

static addr_t sNextPhysicalAddress = 0; //will be set by mmu_init
static addr_t sNextVirtualAddress = KERNEL_BASE + kMaxKernelSize;

static addr_t sNextPageTableAddress = 0;
//the page directory is in front of the pagetable
static uint32 kPageTableRegionEnd = 0;

// working page directory and page table
static uint32 *sPageDirectory = 0 ;
//page directory has to be on a multiple of 16MB for
//some arm processors


static addr_t
get_next_virtual_address(size_t size)
{
	addr_t address = sNextVirtualAddress;
	sNextVirtualAddress += size;

	return address;
}


static addr_t
get_next_virtual_address_aligned(size_t size, uint32 mask)
{
	addr_t address = (sNextVirtualAddress) & mask;
	sNextVirtualAddress = address + size;

	return address;
}


static addr_t
get_next_physical_address(size_t size)
{
	addr_t address = sNextPhysicalAddress;
	sNextPhysicalAddress += size;

	return address;
}


static addr_t
get_next_physical_address_aligned(size_t size, uint32 mask)
{
	addr_t address = sNextPhysicalAddress & mask;
	sNextPhysicalAddress = address + size;

	return address;
}


static addr_t
get_next_virtual_page(size_t pagesize)
{
	return get_next_virtual_address_aligned(pagesize, 0xffffffc0);
}


static addr_t
get_next_physical_page(size_t pagesize)
{
	return get_next_physical_address_aligned(pagesize, 0xffffffc0);
}


/*
 * Set translation table base
 */
void
mmu_set_TTBR(uint32 ttb)
{
	ttb &= 0xffffc000;
	asm volatile("MCR p15, 0, %[adr], c2, c0, 0"::[adr] "r" (ttb));
}


/*
 * Flush the TLB
 */
void
mmu_flush_TLB()
{
	uint32 value = 0;
	asm volatile("MCR p15, 0, %[c8format], c8, c7, 0"::[c8format] "r" (value));
}


/*
 * Read MMU Control Register
 */
uint32
mmu_read_C1()
{
	uint32 controlReg = 0;
	asm volatile("MRC p15, 0, %[c1out], c1, c0, 0":[c1out] "=r" (controlReg));
	return controlReg;
}


/*
 * Write MMU Control Register
 */
void
mmu_write_C1(uint32 value)
{
	asm volatile("MCR p15, 0, %[c1in], c1, c0, 0"::[c1in] "r" (value));
}


void
mmu_write_DACR(uint32 value)
{
	asm volatile("MCR p15, 0, %[c1in], c3, c0, 0"::[c1in] "r" (value));
}


static uint32 *
get_next_page_table(uint32 type)
{
	TRACE(("get_next_page_table, sNextPageTableAddress 0x%" B_PRIxADDR
		", kPageTableRegionEnd 0x%" B_PRIxADDR ", type 0x%" B_PRIx32 "\n",
		sNextPageTableAddress, kPageTableRegionEnd, type));

	size_t size = 0;
	size_t entryCount = 0;
	switch (type) {
		case ARM_MMU_L1_TYPE_COARSE:
			size = ARM_MMU_L2_COARSE_TABLE_SIZE;
			entryCount = ARM_MMU_L2_COARSE_ENTRY_COUNT;
			break;
		case ARM_MMU_L1_TYPE_FINE:
			size = ARM_MMU_L2_FINE_TABLE_SIZE;
			entryCount = ARM_MMU_L2_FINE_ENTRY_COUNT;
			break;
		case ARM_MMU_L1_TYPE_SECTION:
			// TODO: Figure out parameters for section types.
			size = 16384;
			break;
		default:
			panic("asked for unknown page table type: %#" B_PRIx32 "\n", type);
			return NULL;
	}

	addr_t address = sNextPageTableAddress;
	if (address < kPageTableRegionEnd)
		sNextPageTableAddress += size;
	else {
		TRACE(("page table allocation outside of pagetable region!\n"));
		address = get_next_physical_address_aligned(size, 0xffffffc0);
	}

	uint32 *pageTable = (uint32 *)address;
	for (size_t i = 0; i < entryCount; i++)
		pageTable[i] = 0;

	return pageTable;
}


static uint32 *
get_or_create_page_table(addr_t address, uint32 type)
{
	uint32 *pageTable = NULL;
	uint32 pageDirectoryIndex = VADDR_TO_PDENT(address);
	uint32 pageDirectoryEntry = sPageDirectory[pageDirectoryIndex];

	uint32 entryType = pageDirectoryEntry & ARM_PDE_TYPE_MASK;
	if (entryType == ARM_MMU_L1_TYPE_FAULT) {
		// This page directory entry has not been set yet, allocate it.
		pageTable = get_next_page_table(type);
		sPageDirectory[pageDirectoryIndex] = (uint32)pageTable | type;
		return pageTable;
	}

	if (entryType != type) {
		// This entry has been allocated with a different type!
		panic("tried to reuse page directory entry %" B_PRIu32
			" with different type (entry: %#" B_PRIx32 ", new type: %#" B_PRIx32
			")\n", pageDirectoryIndex, pageDirectoryEntry, type);
		return NULL;
	}

	return (uint32 *)(pageDirectoryEntry & ARM_PDE_ADDRESS_MASK);
}


void
init_page_directory()
{
	TRACE(("init_page_directory\n"));
	uint32 smallType;

	// see if subpages are disabled
	if (mmu_read_C1() & (1 << 23))
		smallType = ARM_MMU_L2_TYPE_SMALLNEW;
	else
		smallType = ARM_MMU_L2_TYPE_SMALLEXT;

	gKernelArgs.arch_args.phys_pgdir = (uint32)sPageDirectory;

	// clear out the page directory
	for (uint32 i = 0; i < ARM_MMU_L1_TABLE_ENTRY_COUNT; i++)
		sPageDirectory[i] = 0;

	for (uint32 i = 0; i < ARRAY_SIZE(LOADER_MEMORYMAP); i++) {

		TRACE(("BLOCK: %s START: %lx END %lx\n", LOADER_MEMORYMAP[i].name,
			LOADER_MEMORYMAP[i].start, LOADER_MEMORYMAP[i].end));

		addr_t address = LOADER_MEMORYMAP[i].start;
		ASSERT((address & ~ARM_PTE_ADDRESS_MASK) == 0);

		uint32 *pageTable = NULL;
		uint32 pageTableIndex = 0;

		while (address < LOADER_MEMORYMAP[i].end) {
			if (pageTable == NULL
				|| pageTableIndex >= ARM_MMU_L2_COARSE_ENTRY_COUNT) {
				pageTable = get_or_create_page_table(address,
					ARM_MMU_L1_TYPE_COARSE);
				pageTableIndex = VADDR_TO_PTENT(address);
			}

			pageTable[pageTableIndex++]
				= address | LOADER_MEMORYMAP[i].flags | smallType;
			address += B_PAGE_SIZE;
		}
	}

	// Map the page directory itself.
	addr_t virtualPageDirectory = mmu_map_physical_memory(
		(addr_t)sPageDirectory, ARM_MMU_L1_TABLE_SIZE, kDefaultPageFlags);

	mmu_flush_TLB();

	/* set up the translation table base */
	mmu_set_TTBR((uint32)sPageDirectory);

	mmu_flush_TLB();

	/* set up the domain access register */
	mmu_write_DACR(0xFFFFFFFF);

	/* turn on the mmu */
	mmu_write_C1(mmu_read_C1() | 0x1);

	// Use the mapped page directory from now on.
	sPageDirectory = (uint32 *)virtualPageDirectory;
	gKernelArgs.arch_args.vir_pgdir = virtualPageDirectory;
}


/*!	Creates an entry to map the specified virtualAddress to the given
	physicalAddress.
	If the mapping goes beyond the current page table, it will allocate
	a new one. If it cannot map the requested page, it panics.
*/
static void
map_page(addr_t virtualAddress, addr_t physicalAddress, uint32 flags)
{
	TRACE(("map_page: vaddr 0x%lx, paddr 0x%lx\n", virtualAddress,
		physicalAddress));

	if (virtualAddress < KERNEL_BASE) {
		panic("map_page: asked to map invalid page %p!\n",
			(void *)virtualAddress);
	}

	physicalAddress &= ~(B_PAGE_SIZE - 1);

	// map the page to the correct page table
	uint32 *pageTable = get_or_create_page_table(virtualAddress,
		ARM_MMU_L1_TYPE_COARSE);

	uint32 pageTableIndex = VADDR_TO_PTENT(virtualAddress);
	TRACE(("map_page: inserting pageTable %p, tableEntry 0x%" B_PRIx32
		", physicalAddress 0x%" B_PRIxADDR "\n", pageTable, pageTableIndex,
		physicalAddress));

	pageTable[pageTableIndex] = physicalAddress | flags;

	mmu_flush_TLB();

	TRACE(("map_page: done\n"));
}


//	#pragma mark -


extern "C" addr_t
mmu_map_physical_memory(addr_t physicalAddress, size_t size, uint32 flags)
{
	addr_t address = sNextVirtualAddress;
	addr_t pageOffset = physicalAddress & (B_PAGE_SIZE - 1);

	physicalAddress -= pageOffset;

	for (addr_t offset = 0; offset < size; offset += B_PAGE_SIZE) {
		map_page(get_next_virtual_page(B_PAGE_SIZE), physicalAddress + offset,
			flags);
	}

	return address + pageOffset;
}


static void
unmap_page(addr_t virtualAddress)
{
	TRACE(("unmap_page(virtualAddress = %p)\n", (void *)virtualAddress));

	if (virtualAddress < KERNEL_BASE) {
		panic("unmap_page: asked to unmap invalid page %p!\n",
			(void *)virtualAddress);
	}

	// unmap the page from the correct page table
	uint32 *pageTable
		= (uint32 *)(sPageDirectory[VADDR_TO_PDENT(virtualAddress)]
			& ARM_PDE_ADDRESS_MASK);

	pageTable[VADDR_TO_PTENT(virtualAddress)] = 0;

	mmu_flush_TLB();
}


extern "C" void *
mmu_allocate(void *virtualAddress, size_t size)
{
	TRACE(("mmu_allocate: requested vaddr: %p, next free vaddr: 0x%lx, size: "
		"%ld\n", virtualAddress, sNextVirtualAddress, size));

	size = (size + B_PAGE_SIZE - 1) / B_PAGE_SIZE;
		// get number of pages to map

	if (virtualAddress != NULL) {
		// This special path is almost only useful for loading the
		// kernel into memory; it will only allow you to map the
		// 'kMaxKernelSize' bytes following the kernel base address.
		// Also, it won't check for already mapped addresses, so
		// you better know why you are here :)
		addr_t address = (addr_t)virtualAddress;

		// is the address within the valid range?
		if (address < KERNEL_BASE || address + size * B_PAGE_SIZE
			>= KERNEL_BASE + kMaxKernelSize) {
			TRACE(("mmu_allocate in illegal range\n address: %" B_PRIx32
				"  KERNELBASE: %" B_PRIx32 " KERNEL_BASE + kMaxKernelSize: %"
				B_PRIx32 "  address + size : %" B_PRIx32 "\n", (uint32)address,
				(uint32)KERNEL_BASE, (uint32)KERNEL_BASE + kMaxKernelSize,
				(uint32)(address + size)));
			return NULL;
		}
		for (uint32 i = 0; i < size; i++) {
			map_page(address, get_next_physical_page(B_PAGE_SIZE),
				kDefaultPageFlags);
			address += B_PAGE_SIZE;
		}

		return virtualAddress;
	}

	void *address = (void *)sNextVirtualAddress;

	for (uint32 i = 0; i < size; i++) {
		map_page(get_next_virtual_page(B_PAGE_SIZE),
			get_next_physical_page(B_PAGE_SIZE), kDefaultPageFlags);
	}

	return address;
}


/*!	This will unmap the allocated chunk of memory from the virtual
	address space. It might not actually free memory (as its implementation
	is very simple), but it might.
*/
extern "C" void
mmu_free(void *virtualAddress, size_t size)
{
	TRACE(("mmu_free(virtualAddress = %p, size: %ld)\n", virtualAddress, size));

	addr_t address = (addr_t)virtualAddress;
	size = (size + B_PAGE_SIZE - 1) / B_PAGE_SIZE;
		// get number of pages to map

	// is the address within the valid range?
	if (address < KERNEL_BASE
		|| address + size >= KERNEL_BASE + kMaxKernelSize) {
		panic("mmu_free: asked to unmap out of range region (%p, size %lx)\n",
			(void *)address, size);
	}

	// unmap all pages within the range
	for (uint32 i = 0; i < size; i++) {
		unmap_page(address);
		address += B_PAGE_SIZE;
	}

	if (address == sNextVirtualAddress) {
		// we can actually reuse the virtual address space
		sNextVirtualAddress -= size;
	}
}


/*!	Sets up the final and kernel accessible GDT and IDT tables.
	BIOS calls won't work any longer after this function has
	been called.
*/
extern "C" void
mmu_init_for_kernel(void)
{
	TRACE(("mmu_init_for_kernel\n"));

	// save the memory we've physically allocated
	gKernelArgs.physical_allocated_range[0].size
		= sNextPhysicalAddress - gKernelArgs.physical_allocated_range[0].start;

	// Save the memory we've virtually allocated (for the kernel and other
	// stuff)
	gKernelArgs.virtual_allocated_range[0].start = KERNEL_BASE;
	gKernelArgs.virtual_allocated_range[0].size
		= sNextVirtualAddress - KERNEL_BASE;
	gKernelArgs.num_virtual_allocated_ranges = 1;

#ifdef TRACE_MEMORY_MAP
	{
		uint32 i;

		dprintf("phys memory ranges:\n");
		for (i = 0; i < gKernelArgs.num_physical_memory_ranges; i++) {
			dprintf("    base 0x%08" B_PRIx64 ", length 0x%08" B_PRIx64 "\n",
				gKernelArgs.physical_memory_range[i].start,
				gKernelArgs.physical_memory_range[i].size);
		}

		dprintf("allocated phys memory ranges:\n");
		for (i = 0; i < gKernelArgs.num_physical_allocated_ranges; i++) {
			dprintf("    base 0x%08" B_PRIx64 ", length 0x%08" B_PRIx64 "\n",
				gKernelArgs.physical_allocated_range[i].start,
				gKernelArgs.physical_allocated_range[i].size);
		}

		dprintf("allocated virt memory ranges:\n");
		for (i = 0; i < gKernelArgs.num_virtual_allocated_ranges; i++) {
			dprintf("    base 0x%08" B_PRIx64 ", length 0x%08" B_PRIx64 "\n",
				gKernelArgs.virtual_allocated_range[i].start,
				gKernelArgs.virtual_allocated_range[i].size);
		}
	}
#endif
}


extern "C" void
mmu_init(void)
{
	TRACE(("mmu_init\n"));

	mmu_write_C1(mmu_read_C1() & ~((1 << 29) | (1 << 28) | (1 << 0)));
		// access flag disabled, TEX remap disabled, mmu disabled

	uint32 highestRAMAddress = SDRAM_BASE;

	// calculate lowest RAM adress from MEMORYMAP
	for (uint32 i = 0; i < ARRAY_SIZE(LOADER_MEMORYMAP); i++) {
		if (strcmp("RAM_free", LOADER_MEMORYMAP[i].name) == 0)
			sNextPhysicalAddress = LOADER_MEMORYMAP[i].start;

		if (strcmp("RAM_pt", LOADER_MEMORYMAP[i].name) == 0) {
			sNextPageTableAddress = LOADER_MEMORYMAP[i].start
				+ ARM_MMU_L1_TABLE_SIZE;
			kPageTableRegionEnd = LOADER_MEMORYMAP[i].end;
			sPageDirectory = (uint32 *)LOADER_MEMORYMAP[i].start;
		}

		if (strncmp("RAM_", LOADER_MEMORYMAP[i].name, 4) == 0) {
			if (LOADER_MEMORYMAP[i].end > highestRAMAddress)
				highestRAMAddress = LOADER_MEMORYMAP[i].end;
		}
	}

	gKernelArgs.physical_memory_range[0].start = SDRAM_BASE;
	gKernelArgs.physical_memory_range[0].size = highestRAMAddress - SDRAM_BASE;
	gKernelArgs.num_physical_memory_ranges = 1;

	gKernelArgs.physical_allocated_range[0].start = SDRAM_BASE;
	gKernelArgs.physical_allocated_range[0].size = 0;
	gKernelArgs.num_physical_allocated_ranges = 1;
		// remember the start of the allocated physical pages

	init_page_directory();

	// map in a kernel stack
	gKernelArgs.cpu_kstack[0].size = KERNEL_STACK_SIZE
		+ KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE;
	gKernelArgs.cpu_kstack[0].start = (addr_t)mmu_allocate(NULL,
		gKernelArgs.cpu_kstack[0].size);

	TRACE(("kernel stack at 0x%" B_PRIx64 " to 0x%" B_PRIx64 "\n",
		gKernelArgs.cpu_kstack[0].start,
		gKernelArgs.cpu_kstack[0].start + gKernelArgs.cpu_kstack[0].size));
}


//	#pragma mark -


extern "C" status_t
platform_allocate_region(void **_address, size_t size, uint8 protection,
	bool /*exactAddress*/)
{
	void *address = mmu_allocate(*_address, size);
	if (address == NULL)
		return B_NO_MEMORY;

	*_address = address;
	return B_OK;
}


extern "C" status_t
platform_free_region(void *address, size_t size)
{
	mmu_free(address, size);
	return B_OK;
}


void
platform_release_heap(struct stage2_args *args, void *base)
{
	// It will be freed automatically, since it is in the
	// identity mapped region, and not stored in the kernel's
	// page tables.
}


status_t
platform_init_heap(struct stage2_args *args, void **_base, void **_top)
{
	void *heap = (void *)get_next_physical_address(args->heap_size);
	if (heap == NULL)
		return B_NO_MEMORY;

	*_base = heap;
	*_top = (void *)((int8 *)heap + args->heap_size);
	return B_OK;
}
