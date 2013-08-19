/*
 * Copyright 2003-2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 */
#ifndef KERNEL_BOOT_ELF_H
#define KERNEL_BOOT_ELF_H


#include <boot/addr_range.h>
#include <sys/stat.h>
#include <elf_priv.h>
#include <util/FixedWidthPointer.h>

typedef struct elf32_region {
	area_id		id;
	uint32		start;
	uint32		size;
	int32		delta;
} _PACKED elf32_region;

typedef struct elf64_region {
	area_id		id;
	uint64		start;
	uint64		size;
	int64		delta;
} _PACKED elf64_region;

struct preloaded_image {
	FixedWidthPointer<struct preloaded_image> next;
	FixedWidthPointer<char> name;
	uint8		elf_class;
	addr_range	dynamic_section;

	FixedWidthPointer<char> debug_string_table;
	uint32		num_debug_symbols;
	uint32		debug_string_table_size;

	ino_t		inode;
	image_id	id;
		// the ID field will be filled out in the kernel
	bool		is_module;
		// set by the module initialization code
} _PACKED;

struct preloaded_elf32_image : public preloaded_image {
	Elf32_Ehdr	elf_header;
	elf32_region text_region;
	elf32_region data_region;

	FixedWidthPointer<Elf32_Sym> syms;
	FixedWidthPointer<Elf32_Rel> rel;
	int32		rel_len;
	FixedWidthPointer<Elf32_Rela> rela;
	int32		rela_len;
	FixedWidthPointer<Elf32_Rel> pltrel;
	int32		pltrel_len;
	int32		pltrel_type;

	FixedWidthPointer<Elf32_Sym> debug_symbols;
} _PACKED;

struct preloaded_elf64_image : public preloaded_image {
	Elf64_Ehdr	elf_header;
	elf64_region text_region;
	elf64_region data_region;

	FixedWidthPointer<Elf64_Sym> syms;
	FixedWidthPointer<Elf64_Rel> rel;
	int64		rel_len;
	FixedWidthPointer<Elf64_Rela> rela;
	int64		rela_len;
	FixedWidthPointer<Elf64_Rel> pltrel;
	int64		pltrel_len;
	int64		pltrel_type;

	FixedWidthPointer<Elf64_Sym> debug_symbols;
} _PACKED;


#if B_HAIKU_64_BIT
typedef preloaded_elf64_image preloaded_elf_image;
#else
typedef preloaded_elf32_image preloaded_elf_image;
#endif


#ifdef _BOOT_MODE
extern status_t boot_elf_resolve_symbol(preloaded_elf32_image* image,
	struct Elf32_Sym* symbol, Elf32_Addr* symbolAddress);
extern status_t boot_elf_resolve_symbol(preloaded_elf64_image* image,
	struct Elf64_Sym* symbol, Elf64_Addr* symbolAddress);
extern void boot_elf64_set_relocation(Elf64_Addr resolveAddress,
	Elf64_Addr finalAddress);
#endif

#endif	/* KERNEL_BOOT_ELF_H */
