/*
** Copyright 2003-2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the Haiku License.
*/
#ifndef _KERNEL_ARCH_ARM_CPU_H
#define _KERNEL_ARCH_ARM_CPU_H

#ifndef _ASSEMBLER

#include <arch/arm/arch_thread_types.h>
#include <kernel.h>

/* raw exception frames */
struct iframe {
	uint32 spsr;
	uint32 r0;
	uint32 r1;
	uint32 r2;
	uint32 r3;
	uint32 r4;
	uint32 r5;
	uint32 r6;
	uint32 r7;
	uint32 r8;
	uint32 r9;
	uint32 r10;
	uint32 r11;
	uint32 r12;
	uint32 usr_sp;
	uint32 usr_lr;
	uint32 svc_sp;
	uint32 svc_lr;
	uint32 pc;
} _PACKED;

/**! Values for arch_cpu_info.arch */
enum {
	ARCH_ARM_PRE_ARM7,
	ARCH_ARM_v3,
	ARCH_ARM_v4,
	ARCH_ARM_v4T,
	ARCH_ARM_v5,
	ARCH_ARM_v5T,
	ARCH_ARM_v5TE,
	ARCH_ARM_v5TEJ,
	ARCH_ARM_v6
};

typedef struct arch_cpu_info {
	/* For a detailed interpretation of these values,
	   see "The System Control coprocessor",
	   "Main ID register" in your ARM ARM */
	int implementor;
	int part_number;
	int revision;
	int variant;
	int arch;
} arch_cpu_info;

#ifdef __cplusplus
extern "C" {
#endif

extern addr_t arm_get_far(void);
extern int32 arm_get_fsr(void);

extern int mmu_read_c1(void);
extern int mmu_write_c1(int val);

#ifdef __cplusplus
};
#endif

#endif	// !_ASSEMBLER

#endif	/* _KERNEL_ARCH_ARM_CPU_H */
