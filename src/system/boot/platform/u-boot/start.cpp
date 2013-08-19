/*
 * Copyright 2003-2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "serial.h"
#include "console.h"
#include "cpu.h"
#include "mmu.h"
#include "smp.h"
#include "uimage.h"
#include "keyboard.h"

#include <KernelExport.h>
#include <boot/platform.h>
#include <boot/heap.h>
#include <boot/stage2.h>
#include <arch/cpu.h>
#include <platform_arch.h>

#include <string.h>

extern "C" {
#include <fdt.h>
#include <libfdt.h>
#include <libfdt_env.h>
};


#define HEAP_SIZE (128 * 1024)


typedef struct uboot_gd {
	// those are the only few members that we can trust
	// others depend on compile-time config
	struct board_data *bd;
	uint32 flags;
	uint32 baudrate;
	// those are ARM-only
	uint32 have_console;
	uint32 reloc_off;
	uint32 env_addr;
	uint32 env_valid;
	uint32 fb_base;
} uboot_gd;


// GCC defined globals
extern void (*__ctor_list)(void);
extern void (*__ctor_end)(void);
extern uint8 __bss_start;
extern uint8 _end;

extern "C" int main(stage2_args *args);
extern "C" void _start(void);
extern "C" int start_raw(int argc, const char **argv);
extern "C" void dump_uimage(struct image_header *image);
extern "C" void dump_fdt(const void *fdt);

// declared in shell.S
// those are initialized to NULL but not in the BSS
extern struct image_header *gUImage;
extern uboot_gd *gUBootGlobalData;
extern uint32 gUBootOS;
extern void *gFDT;

static uint32 sBootOptions;


static void
clear_bss(void)
{
	memset(&__bss_start, 0, &_end - &__bss_start);
}


static void
call_ctors(void)
{
	void (**f)(void);

	for (f = &__ctor_list; f < &__ctor_end; f++) {
		(**f)();
	}
}


/* needed for libgcc unwind XXX */
extern "C" void
abort(void)
{
	panic("abort");
}


extern "C" void
platform_start_kernel(void)
{
	preloaded_elf32_image *image = static_cast<preloaded_elf32_image *>(
		gKernelArgs.kernel_image.Pointer());

	addr_t kernelEntry = image->elf_header.e_entry;
	addr_t stackTop
		= gKernelArgs.cpu_kstack[0].start + gKernelArgs.cpu_kstack[0].size;

//	smp_init_other_cpus();
	serial_cleanup();
	mmu_init_for_kernel();
//	smp_boot_other_cpus();

	dprintf("kernel entry at %lx\n", kernelEntry);

	status_t error = arch_start_kernel(&gKernelArgs, kernelEntry,
		stackTop);

	panic("kernel returned!\n");
}


extern "C" void
platform_exit(void)
{
}


extern "C" int
start_netbsd(struct board_info *bd, struct image_header *image,
	const char *consdev, const char *cmdline)
{
	const char *argv[] = { "haiku", cmdline };
	int argc = 1;
	if (cmdline)
		argc++;
	gUImage = image;
	return start_raw(argc, argv);
}


extern "C" int
start_linux(int argc, int archnum, void *atags)
{
	return 1;
}


extern "C" int
start_linux_ppc_old(void */*board_info*/,
	void */*initrd_start*/, void */*initrd_end*/,
	const char */*cmdline_start*/, const char */*cmdline_end*/)
{
	return 1;
}


extern "C" int
start_linux_ppc_fdt(void *fdt, long/*UNUSED*/, long/*UNUSED*/,
	uint32 epapr_magic, uint32 initial_mem_size)
{
	gFDT = fdt;	//XXX: make a copy?
	return start_raw(0, NULL);
}


extern "C" int
start_raw(int argc, const char **argv)
{
	stage2_args args;

	clear_bss();
		// call C++ constructors before doing anything else
	call_ctors();
	args.heap_size = HEAP_SIZE;
	args.arguments = NULL;
	args.platform.boot_tgz_data = NULL;
	args.platform.boot_tgz_size = 0;
	args.platform.fdt_data = NULL;
	args.platform.fdt_size = 0;

	// if we get passed a uimage, try to find the third blob only if we do not have FDT data yet
	if (gUImage != NULL
		&& !gFDT
		&& image_multi_getimg(gUImage, 2,
			(uint32*)&args.platform.fdt_data,
			&args.platform.fdt_size)) {
		// found a blob, assume it is FDT data, when working on a platform
		// which does not have an FDT enabled U-Boot
		gFDT = args.platform.fdt_data;
	}

	serial_init(gFDT);
	console_init();
	cpu_init();

	if (args.platform.fdt_data) {
		dprintf("Found FDT from uimage @ %p, %" B_PRIu32 " bytes\n",
			args.platform.fdt_data, args.platform.fdt_size);
	} else if (gFDT) {
		/* Fixup args so we can pass the gFDT on to the kernel */
		args.platform.fdt_data = gFDT;
		args.platform.fdt_size = fdt_totalsize(gFDT);
	}

	// if we get passed an FDT, check /chosen for initrd
	if (gFDT != NULL) {
		int node = fdt_path_offset(gFDT, "/chosen");
		const void *prop;
		int len;
		phys_addr_t initrd_start = 0, initrd_end = 0;

		if (node >= 0) {
			prop = fdt_getprop(gFDT, node, "linux,initrd-start", &len);
			if (prop && len == 4)
				initrd_start = fdt32_to_cpu(*(uint32_t *)prop);
			prop = fdt_getprop(gFDT, node, "linux,initrd-end", &len);
			if (prop && len == 4)
				initrd_end = fdt32_to_cpu(*(uint32_t *)prop);
			if (initrd_end > initrd_start) {
				args.platform.boot_tgz_data = (void *)initrd_start;
				args.platform.boot_tgz_size = initrd_end - initrd_start;
		dprintf("Found boot tgz from FDT @ %p, %" B_PRIu32 " bytes\n",
			args.platform.boot_tgz_data, args.platform.boot_tgz_size);
			}
		}
	}

	// if we get passed a uimage, try to find the second blob
	if (gUImage != NULL
		&& image_multi_getimg(gUImage, 1,
			(uint32*)&args.platform.boot_tgz_data,
			&args.platform.boot_tgz_size)) {
		dprintf("Found boot tgz from uimage @ %p, %" B_PRIu32 " bytes\n",
			args.platform.boot_tgz_data, args.platform.boot_tgz_size);
	}

	{ //DEBUG:
		int i;
		dprintf("argc = %d\n", argc);
		for (i = 0; i < argc; i++)
			dprintf("argv[%d] @%lx = '%s'\n", i, (uint32)argv[i], argv[i]);
		dprintf("os: %d\n", (int)gUBootOS);
		dprintf("gd @ %p\n", gUBootGlobalData);
		if (gUBootGlobalData)
			dprintf("gd->bd @ %p\n", gUBootGlobalData->bd);
		//dprintf("fb_base %p\n", (void*)gUBootGlobalData->fb_base);
		if (gUImage)
			dump_uimage(gUImage);
		if (gFDT)
			dump_fdt(gFDT);
	}
	
	mmu_init();

	// wait a bit to give the user the opportunity to press a key
//	spin(750000);

	// reading the keyboard doesn't seem to work in graphics mode
	// (maybe a bochs problem)
//	sBootOptions = check_for_boot_keys();
	//if (sBootOptions & BOOT_OPTION_DEBUG_OUTPUT)
		serial_enable();

	main(&args);
	return 0;
}


extern "C" uint32
platform_boot_options(void)
{
	return sBootOptions;
}
