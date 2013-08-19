/*
 * Copyright 2005-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include <arch/user_debugger.h>

#include <string.h>

#include <debugger.h>
#include <driver_settings.h>
#include <int.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>


//#define TRACE_ARCH_USER_DEBUGGER
#ifdef TRACE_ARCH_USER_DEBUGGER
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define B_NO_MORE_BREAKPOINTS				B_BUSY
#define B_NO_MORE_WATCHPOINTS				B_BUSY
#define B_BAD_WATCHPOINT_ALIGNMENT			B_BAD_VALUE
#define B_WATCHPOINT_TYPE_NOT_SUPPORTED		B_NOT_SUPPORTED
#define B_WATCHPOINT_LENGTH_NOT_SUPPORTED	B_NOT_SUPPORTED
#define B_BREAKPOINT_NOT_FOUND				B_NAME_NOT_FOUND
#define B_WATCHPOINT_NOT_FOUND				B_NAME_NOT_FOUND
	// TODO: Make those real error codes.


extern bool gHasSSE;

// The software breakpoint instruction (int3).
const uint8 kX86SoftwareBreakpoint[1] = { 0xcc };

// maps breakpoint slot index to LEN_i LSB number
static const size_t sDR7Len[4] = {
	X86_DR7_LEN0_LSB, X86_DR7_LEN1_LSB, X86_DR7_LEN2_LSB, X86_DR7_LEN3_LSB
};

// maps breakpoint slot index to R/W_i LSB number
static const size_t sDR7RW[4] = {
	X86_DR7_RW0_LSB, X86_DR7_RW1_LSB, X86_DR7_RW2_LSB, X86_DR7_RW3_LSB
};

// maps breakpoint slot index to L_i bit number
static const size_t sDR7L[4] = {
	X86_DR7_L0, X86_DR7_L1, X86_DR7_L2, X86_DR7_L3
};

// maps breakpoint slot index to G_i bit number
static const size_t sDR7G[4] = {
	X86_DR7_G0, X86_DR7_G1, X86_DR7_G2, X86_DR7_G3
};

// maps breakpoint slot index to B_i bit number
static const size_t sDR6B[4] = {
	X86_DR6_B0, X86_DR6_B1, X86_DR6_B2, X86_DR6_B3
};

// Enables a hack to make single stepping work under qemu. Set via kernel
// driver settings.
static bool sQEmuSingleStepHack = false;


#ifdef __x86_64__


static void
get_iframe_registers(const iframe* frame, debug_cpu_state* cpuState)
{
	// Get general purpose registers.
	cpuState->r15 = frame->r15;
	cpuState->r14 = frame->r14;
	cpuState->r13 = frame->r13;
	cpuState->r12 = frame->r12;
	cpuState->r11 = frame->r11;
	cpuState->r10 = frame->r10;
	cpuState->r9 = frame->r9;
	cpuState->r8 = frame->r8;
	cpuState->rbp = frame->bp;
	cpuState->rsi = frame->si;
	cpuState->rdi = frame->di;
	cpuState->rdx = frame->dx;
	cpuState->rcx = frame->cx;
	cpuState->rbx = frame->bx;
	cpuState->rax = frame->ax;
	cpuState->rip = frame->ip;
	cpuState->cs = frame->cs;
	cpuState->rflags = frame->flags;
	cpuState->rsp = frame->sp;
	cpuState->ss = frame->ss;

	// Other segment registers are not saved or changed on interrupts, so
	// get their value here.
	uint16 seg;
	__asm__ volatile ("movw %%ds, %0" : "=r" (seg));
	cpuState->ds = seg;
	__asm__ volatile ("movw %%es, %0" : "=r" (seg));
	cpuState->es = seg;
	__asm__ volatile ("movw %%fs, %0" : "=r" (seg));
	cpuState->fs = seg;
	__asm__ volatile ("movw %%gs, %0" : "=r" (seg));
	cpuState->gs = seg;
}


static void
set_iframe_registers(iframe* frame, const debug_cpu_state* cpuState)
{
	frame->r15 = cpuState->r15;
	frame->r14 = cpuState->r14;
	frame->r13 = cpuState->r13;
	frame->r12 = cpuState->r12;
	frame->r11 = cpuState->r11;
	frame->r10 = cpuState->r10;
	frame->r9 = cpuState->r9;
	frame->r8 = cpuState->r8;
	frame->bp = cpuState->rbp;
	frame->si = cpuState->rsi;
	frame->di = cpuState->rdi;
	frame->dx = cpuState->rdx;
	frame->cx = cpuState->rcx;
	frame->bx = cpuState->rbx;
	frame->ax = cpuState->rax;
	frame->ip = cpuState->rip;
	frame->flags = (frame->flags & ~X86_EFLAGS_USER_SETTABLE_FLAGS)
		| (cpuState->rflags & X86_EFLAGS_USER_SETTABLE_FLAGS);
	frame->sp = cpuState->rsp;
}


#else	// __x86_64__


static void
get_iframe_registers(const iframe* frame, debug_cpu_state* cpuState)
{
	cpuState->gs = frame->gs;
	cpuState->fs = frame->fs;
	cpuState->es = frame->es;
	cpuState->ds = frame->ds;
	cpuState->edi = frame->di;
	cpuState->esi = frame->si;
	cpuState->ebp = frame->bp;
	cpuState->esp = frame->sp;
	cpuState->ebx = frame->bx;
	cpuState->edx = frame->orig_edx;
	cpuState->ecx = frame->cx;
	cpuState->eax = frame->orig_eax;
	cpuState->vector = frame->vector;
	cpuState->error_code = frame->error_code;
	cpuState->eip = frame->ip;
	cpuState->cs = frame->cs;
	cpuState->eflags = frame->flags;
	cpuState->user_esp = frame->user_sp;
	cpuState->user_ss = frame->user_ss;
}


static void
set_iframe_registers(iframe* frame, const debug_cpu_state* cpuState)
{
//	frame->gs = cpuState->gs;
//	frame->fs = cpuState->fs;
//	frame->es = cpuState->es;
//	frame->ds = cpuState->ds;
	frame->di = cpuState->edi;
	frame->si = cpuState->esi;
	frame->bp = cpuState->ebp;
//	frame->esp = cpuState->esp;
	frame->bx = cpuState->ebx;
	frame->dx = cpuState->edx;
	frame->cx = cpuState->ecx;
	frame->ax = cpuState->eax;
//	frame->vector = cpuState->vector;
//	frame->error_code = cpuState->error_code;
	frame->ip = cpuState->eip;
//	frame->cs = cpuState->cs;
	frame->flags = (frame->flags & ~X86_EFLAGS_USER_SETTABLE_FLAGS)
		| (cpuState->eflags & X86_EFLAGS_USER_SETTABLE_FLAGS);
	frame->user_sp = cpuState->user_esp;
//	frame->user_ss = cpuState->user_ss;
}


#endif	// __x86_64__


static inline void
install_breakpoints(const arch_team_debug_info& teamInfo)
{
	// set breakpoints
	asm("mov %0, %%dr0" : : "r"(teamInfo.breakpoints[0].address));
	asm("mov %0, %%dr1" : : "r"(teamInfo.breakpoints[1].address));
	asm("mov %0, %%dr2" : : "r"(teamInfo.breakpoints[2].address));
#ifdef __x86_64__
	asm("mov %0, %%dr3" : : "r"(teamInfo.breakpoints[3].address));
		// DR3 is used to hold the current Thread* on 32.
#endif

	// enable breakpoints
	asm("mov %0, %%dr7" : : "r"(teamInfo.dr7));
}


static inline void
disable_breakpoints()
{
	asm("mov %0, %%dr7" : : "r"((size_t)X86_BREAKPOINTS_DISABLED_DR7));
}


/*! Sets a break-/watchpoint in the given team info.
	Interrupts must be disabled and the team debug info lock be held.
*/
static inline status_t
set_breakpoint(arch_team_debug_info& info, void* address, size_t type,
	size_t length, bool setGlobalFlag)
{
	// check, if there is already a breakpoint at that address
	bool alreadySet = false;
	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
		if (info.breakpoints[i].address == address
			&& info.breakpoints[i].type == type) {
			alreadySet = true;
			break;
		}
	}

	if (!alreadySet) {
		// find a free slot
		int32 slot = -1;
		for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
			if (!info.breakpoints[i].address) {
				slot = i;
				break;
			}
		}

		// init the breakpoint
		if (slot >= 0) {
			info.breakpoints[slot].address = address;
			info.breakpoints[slot].type = type;
			info.breakpoints[slot].length = length;

			info.dr7 |= (length << sDR7Len[slot])
				| (type << sDR7RW[slot])
				| (1 << sDR7L[slot]);
			if (setGlobalFlag)
				info.dr7 |= (1 << sDR7G[slot]);
		} else {
			if (type == X86_INSTRUCTION_BREAKPOINT)
				return B_NO_MORE_BREAKPOINTS;
			else
				return B_NO_MORE_WATCHPOINTS;
		}
	}

	return B_OK;
}


/*! Clears a break-/watchpoint in the given team info.
	Interrupts must be disabled and the team debug info lock be held.
*/
static inline status_t
clear_breakpoint(arch_team_debug_info& info, void* address, bool watchpoint)
{
	// find the breakpoint
	int32 slot = -1;
	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
		if (info.breakpoints[i].address == address
			&& (watchpoint
				!= (info.breakpoints[i].type == X86_INSTRUCTION_BREAKPOINT))) {
			slot = i;
			break;
		}
	}

	// clear the breakpoint
	if (slot >= 0) {
		info.breakpoints[slot].address = NULL;

		info.dr7 &= ~((0x3 << sDR7Len[slot])
			| (0x3 << sDR7RW[slot])
			| (1 << sDR7L[slot])
			| (1 << sDR7G[slot]));
	} else {
		if (watchpoint)
			return B_WATCHPOINT_NOT_FOUND;
		else
			return B_BREAKPOINT_NOT_FOUND;
	}

	return B_OK;
}


static status_t
set_breakpoint(void* address, size_t type, size_t length)
{
	if (!address)
		return B_BAD_VALUE;

	Thread* thread = thread_get_current_thread();

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);

	status_t error = set_breakpoint(thread->team->debug_info.arch_info, address,
		type, length, false);

	RELEASE_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);
	restore_interrupts(state);

	return error;
}


static status_t
clear_breakpoint(void* address, bool watchpoint)
{
	if (!address)
		return B_BAD_VALUE;

	Thread* thread = thread_get_current_thread();

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);

	status_t error = clear_breakpoint(thread->team->debug_info.arch_info,
		address, watchpoint);

	RELEASE_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);
	restore_interrupts(state);

	return error;
}


#if KERNEL_BREAKPOINTS


static void
install_breakpoints_per_cpu(void* /*cookie*/, int cpu)
{
	Team* kernelTeam = team_get_kernel_team();

	GRAB_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	install_breakpoints(kernelTeam->debug_info.arch_info);

	RELEASE_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);
}


static status_t
set_kernel_breakpoint(void* address, size_t type, size_t length)
{
	if (!address)
		return B_BAD_VALUE;

	Team* kernelTeam = team_get_kernel_team();

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	status_t error = set_breakpoint(kernelTeam->debug_info.arch_info, address,
		type, length, true);

	RELEASE_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	call_all_cpus(install_breakpoints_per_cpu, NULL);

	restore_interrupts(state);

	return error;
}


static status_t
clear_kernel_breakpoint(void* address, bool watchpoint)
{
	if (!address)
		return B_BAD_VALUE;

	Team* kernelTeam = team_get_kernel_team();

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	status_t error = clear_breakpoint(kernelTeam->debug_info.arch_info,
		address, watchpoint);

	RELEASE_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	call_all_cpus(install_breakpoints_per_cpu, NULL);

	restore_interrupts(state);

	return error;
}

#endif	// KERNEL_BREAKPOINTS


static inline status_t
check_watch_point_parameters(void* address, uint32 type, int32 length,
	size_t& archType, size_t& archLength)
{
	// check type
	switch (type) {
		case B_DATA_WRITE_WATCHPOINT:
			archType = X86_DATA_WRITE_BREAKPOINT;
			break;
		case B_DATA_READ_WRITE_WATCHPOINT:
			archType = X86_DATA_READ_WRITE_BREAKPOINT;
			break;
		case B_DATA_READ_WATCHPOINT:
		default:
			return B_WATCHPOINT_TYPE_NOT_SUPPORTED;
			break;
	}

	// check length and alignment
	switch (length) {
		case 1:
			archLength = X86_BREAKPOINT_LENGTH_1;
			break;
		case 2:
			if ((addr_t)address & 0x1)
				return B_BAD_WATCHPOINT_ALIGNMENT;
			archLength = X86_BREAKPOINT_LENGTH_2;
			break;
		case 4:
			if ((addr_t)address & 0x3)
				return B_BAD_WATCHPOINT_ALIGNMENT;
			archLength = X86_BREAKPOINT_LENGTH_4;
			break;
		default:
			return B_WATCHPOINT_LENGTH_NOT_SUPPORTED;
	}

	return B_OK;
}


// #pragma mark - kernel debugger commands


#if KERNEL_BREAKPOINTS

static int
debugger_breakpoints(int argc, char** argv)
{
	Team* kernelTeam = team_get_kernel_team();
	arch_team_debug_info& info = kernelTeam->debug_info.arch_info;

	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
		kprintf("breakpoint[%" B_PRId32 "] ", i);

		if (info.breakpoints[i].address != NULL) {
			kprintf("%p ", info.breakpoints[i].address);
			switch (info.breakpoints[i].type) {
				case X86_INSTRUCTION_BREAKPOINT:
					kprintf("instruction");
					break;
				case X86_IO_READ_WRITE_BREAKPOINT:
					kprintf("io read/write");
					break;
				case X86_DATA_WRITE_BREAKPOINT:
					kprintf("data write");
					break;
				case X86_DATA_READ_WRITE_BREAKPOINT:
					kprintf("data read/write");
					break;
			}

			int length = 1;
			switch (info.breakpoints[i].length) {
				case X86_BREAKPOINT_LENGTH_1:
					length = 1;
					break;
				case X86_BREAKPOINT_LENGTH_2:
					length = 2;
					break;
				case X86_BREAKPOINT_LENGTH_4:
					length = 4;
					break;
			}

			if (info.breakpoints[i].type != X86_INSTRUCTION_BREAKPOINT)
				kprintf(" %d byte%s", length, (length > 1 ? "s" : ""));
		} else
			kprintf("unused");

		kprintf("\n");
	}

	return 0;
}


static int
debugger_breakpoint(int argc, char** argv)
{
	// get arguments

	if (argc < 2 || argc > 3)
		return print_debugger_command_usage(argv[0]);

	addr_t address = strtoul(argv[1], NULL, 0);
	if (address == 0)
		return print_debugger_command_usage(argv[0]);

	bool clear = false;
	if (argc == 3) {
		if (strcmp(argv[2], "clear") == 0)
			clear = true;
		else
			return print_debugger_command_usage(argv[0]);
	}

	// set/clear breakpoint

	arch_team_debug_info& info = team_get_kernel_team()->debug_info.arch_info;

	status_t error;

	if (clear) {
		error = clear_breakpoint(info, (void*)address, false);
	} else {
		error = set_breakpoint(info, (void*)address, X86_INSTRUCTION_BREAKPOINT,
			X86_BREAKPOINT_LENGTH_1, true);
	}

	if (error == B_OK)
		call_all_cpus_sync(install_breakpoints_per_cpu, NULL);
	else
		kprintf("Failed to install breakpoint: %s\n", strerror(error));

	return 0;
}


static int
debugger_watchpoint(int argc, char** argv)
{
	// get arguments

	if (argc < 2 || argc > 4)
		return print_debugger_command_usage(argv[0]);

	addr_t address = strtoul(argv[1], NULL, 0);
	if (address == 0)
		return print_debugger_command_usage(argv[0]);

	bool clear = false;
	bool readWrite = false;
	int argi = 2;
	int length = 1;
	if (argc >= 3) {
		if (strcmp(argv[argi], "clear") == 0) {
			clear = true;
			argi++;
		} else if (strcmp(argv[argi], "rw") == 0) {
			readWrite = true;
			argi++;
		}

		if (!clear && argi < argc)
			length = strtoul(argv[argi++], NULL, 0);

		if (length == 0 || argi < argc)
			return print_debugger_command_usage(argv[0]);
	}

	// set/clear breakpoint

	arch_team_debug_info& info = team_get_kernel_team()->debug_info.arch_info;

	status_t error;

	if (clear) {
		error = clear_breakpoint(info, (void*)address, true);
	} else {
		uint32 type = readWrite ? B_DATA_READ_WRITE_WATCHPOINT
			: B_DATA_WRITE_WATCHPOINT;

		size_t archType, archLength;
		error = check_watch_point_parameters((void*)address, type, length,
			archType, archLength);

		if (error == B_OK) {
			error = set_breakpoint(info, (void*)address, archType, archLength,
				true);
		}
	}

	if (error == B_OK)
		call_all_cpus_sync(install_breakpoints_per_cpu, NULL);
	else
		kprintf("Failed to install breakpoint: %s\n", strerror(error));

	return 0;
}


static int
debugger_single_step(int argc, char** argv)
{
	// TODO: Since we need an iframe, this doesn't work when KDL wasn't entered
	// via an exception.

	iframe* frame = x86_get_current_iframe();
	if (frame == NULL) {
		kprintf("Failed to get the current iframe!\n");
		return 0;
	}

	frame->flags |= (1 << X86_EFLAGS_TF);

	return B_KDEBUG_QUIT;
}


#endif	// KERNEL_BREAKPOINTS


// #pragma mark - in-kernel public interface


void
arch_clear_team_debug_info(arch_team_debug_info* info)
{
	for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++)
		info->breakpoints[i].address = NULL;

	info->dr7 = X86_BREAKPOINTS_DISABLED_DR7;
}


void
arch_destroy_team_debug_info(arch_team_debug_info* info)
{
	arch_clear_team_debug_info(info);
}


void
arch_clear_thread_debug_info(arch_thread_debug_info* info)
{
	info->flags = 0;
}


void
arch_destroy_thread_debug_info(arch_thread_debug_info* info)
{
	arch_clear_thread_debug_info(info);
}


void
arch_update_thread_single_step()
{
	if (iframe* frame = x86_get_user_iframe()) {
		Thread* thread = thread_get_current_thread();

		// set/clear TF in EFLAGS depending on whether single stepping is
		// desired
		if (thread->debug_info.flags & B_THREAD_DEBUG_SINGLE_STEP)
			frame->flags |= (1 << X86_EFLAGS_TF);
		else
			frame->flags &= ~(1 << X86_EFLAGS_TF);
	}
}


void
arch_set_debug_cpu_state(const debug_cpu_state* cpuState)
{
	if (iframe* frame = x86_get_user_iframe()) {
		// For the floating point state to be correct the calling function must
		// not use these registers (not even indirectly).
		if (gHasSSE) {
			// Since fxrstor requires 16-byte alignment and this isn't
			// guaranteed passed buffer, we use our thread's fpu_state field as
			// temporary buffer. We need to disable interrupts to make use of
			// it.
			Thread* thread = thread_get_current_thread();
			InterruptsLocker locker;
			memcpy(thread->arch_info.fpu_state, &cpuState->extended_registers,
				sizeof(cpuState->extended_registers));
			x86_fxrstor(thread->arch_info.fpu_state);
#ifndef __x86_64__
		} else {
			// TODO: Implement! We need to convert the format first.
//			x86_frstor(&cpuState->extended_registers);
#endif
		}
		set_iframe_registers(frame, cpuState);
	}
}


void
arch_get_debug_cpu_state(debug_cpu_state* cpuState)
{
	if (iframe* frame = x86_get_user_iframe()) {
		// For the floating point state to be correct the calling function must
		// not use these registers (not even indirectly).
		if (gHasSSE) {
			// Since fxsave requires 16-byte alignment and this isn't guaranteed
			// passed buffer, we use our thread's fpu_state field as temporary
			// buffer. We need to disable interrupts to make use of it.
			Thread* thread = thread_get_current_thread();
			InterruptsLocker locker;
			x86_fxsave(thread->arch_info.fpu_state);
				// unlike fnsave, fxsave doesn't reinit the FPU state
			memcpy(&cpuState->extended_registers, thread->arch_info.fpu_state,
				sizeof(cpuState->extended_registers));
#ifndef __x86_64__
		} else {
			x86_fnsave(&cpuState->extended_registers);
			x86_frstor(&cpuState->extended_registers);
				// fnsave reinits the FPU state after saving, so we need to
				// load it again
			// TODO: Convert to fxsave format!
#endif
		}
		get_iframe_registers(frame, cpuState);
	}
}


status_t
arch_set_breakpoint(void* address)
{
	return set_breakpoint(address, X86_INSTRUCTION_BREAKPOINT,
		X86_BREAKPOINT_LENGTH_1);
}


status_t
arch_clear_breakpoint(void* address)
{
	return clear_breakpoint(address, false);
}


status_t
arch_set_watchpoint(void* address, uint32 type, int32 length)
{
	size_t archType, archLength;
	status_t error = check_watch_point_parameters(address, type, length,
		archType, archLength);
	if (error != B_OK)
		return error;

	return set_breakpoint(address, archType, archLength);
}


status_t
arch_clear_watchpoint(void* address)
{
	return clear_breakpoint(address, true);
}


bool
arch_has_breakpoints(arch_team_debug_info* info)
{
	// Reading info->dr7 is atomically, so we don't need to lock. The caller
	// has to ensure, that the info doesn't go away.
	return (info->dr7 != X86_BREAKPOINTS_DISABLED_DR7);
}


#if KERNEL_BREAKPOINTS

status_t
arch_set_kernel_breakpoint(void* address)
{
	status_t error = set_kernel_breakpoint(address, X86_INSTRUCTION_BREAKPOINT,
		X86_BREAKPOINT_LENGTH_1);

	if (error != B_OK) {
		panic("arch_set_kernel_breakpoint() failed to set breakpoint: %s",
			strerror(error));
	}

	return error;
}


status_t
arch_clear_kernel_breakpoint(void* address)
{
	status_t error = clear_kernel_breakpoint(address, false);

	if (error != B_OK) {
		panic("arch_clear_kernel_breakpoint() failed to clear breakpoint: %s",
			strerror(error));
	}

	return error;
}


status_t
arch_set_kernel_watchpoint(void* address, uint32 type, int32 length)
{
	size_t archType, archLength;
	status_t error = check_watch_point_parameters(address, type, length,
		archType, archLength);

	if (error == B_OK)
		error = set_kernel_breakpoint(address, archType, archLength);

	if (error != B_OK) {
		panic("arch_set_kernel_watchpoint() failed to set watchpoint: %s",
			strerror(error));
	}

	return error;
}


status_t
arch_clear_kernel_watchpoint(void* address)
{
	status_t error = clear_kernel_breakpoint(address, true);

	if (error != B_OK) {
		panic("arch_clear_kernel_watchpoint() failed to clear watchpoint: %s",
			strerror(error));
	}

	return error;
}

#endif	// KERNEL_BREAKPOINTS


// #pragma mark - x86 implementation interface


/**
 *	Interrupts are disabled. \a frame is unused, i.e. can be \c NULL.
 */
void
x86_init_user_debug_at_kernel_exit(iframe* frame)
{
	Thread* thread = thread_get_current_thread();

	if (!(thread->flags & THREAD_FLAGS_BREAKPOINTS_DEFINED))
		return;

	// disable kernel breakpoints
	disable_breakpoints();

	// install the user breakpoints
	GRAB_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);

	arch_team_debug_info &teamInfo = thread->team->debug_info.arch_info;

	install_breakpoints(teamInfo);

	atomic_or(&thread->flags, THREAD_FLAGS_BREAKPOINTS_INSTALLED);

	RELEASE_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);
}


/**
 *	Interrupts are disabled.
 */
void
x86_exit_user_debug_at_kernel_entry()
{
	Thread* thread = thread_get_current_thread();

	// We need to save the current values of dr6 and dr7 in the CPU structure,
	// since in case of a debug exception we might overwrite them before
	// x86_handle_debug_exception() is called. Debug exceptions occur when
	// hitting a hardware break/watchpoint or when single-stepping.
	asm("mov %%dr6, %0" : "=r"(thread->cpu->arch.dr6));
	asm("mov %%dr7, %0" : "=r"(thread->cpu->arch.dr7));

	// The remainder needs only be done, when user breakpoints are installed.
	if (!(thread->flags & THREAD_FLAGS_BREAKPOINTS_INSTALLED))
		return;

	// disable user breakpoints
	disable_breakpoints();

	// install kernel breakpoints
	Team* kernelTeam = team_get_kernel_team();

	GRAB_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);

	install_breakpoints(kernelTeam->debug_info.arch_info);

	atomic_and(&thread->flags, ~THREAD_FLAGS_BREAKPOINTS_INSTALLED);

	RELEASE_TEAM_DEBUG_INFO_LOCK(kernelTeam->debug_info);
}


/**
 *	Interrupts are disabled and will possibly be enabled by the function.
 */
void
x86_handle_debug_exception(iframe* frame)
{
	Thread* thread = thread_get_current_thread();

	// Get dr6 and dr7. If the given iframe is a userland frame, the exception
	// obviously occurred in userland. In that case
	// x86_exit_user_debug_at_kernel_entry() has already been invoked and dr6
	// and dr7 are stored in the cpu info. Otherwise we need to fetch the
	// current values from the registers.
	size_t dr6;
	size_t dr7;
	if (IFRAME_IS_USER(frame)) {
		dr6 = thread->cpu->arch.dr6;
		dr7 = thread->cpu->arch.dr7;
	} else {
		asm("mov %%dr6, %0" : "=r"(dr6));
		asm("mov %%dr7, %0" : "=r"(dr7));
	}

	TRACE(("x86_handle_debug_exception(): DR6: %lx, DR7: %lx\n", dr6, dr7));

	// check, which exception condition applies
	if (dr6 & X86_DR6_BREAKPOINT_MASK) {
		// breakpoint

		// check which breakpoint was taken
		bool watchpoint = true;
		for (int32 i = 0; i < X86_BREAKPOINT_COUNT; i++) {
			if (dr6 & (1 << sDR6B[i])) {
				size_t type = (dr7 >> sDR7RW[i]) & 0x3;
				if (type == X86_INSTRUCTION_BREAKPOINT)
					watchpoint = false;
			}
		}

		if (IFRAME_IS_USER(frame)) {
			// enable interrupts and notify the debugger
			enable_interrupts();

			if (watchpoint)
				user_debug_watchpoint_hit();
			else
				user_debug_breakpoint_hit(false);
		} else {
			panic("hit kernel %spoint: dr6: 0x%lx, dr7: 0x%lx",
				watchpoint ? "watch" : "break", dr6, dr7);
		}
	} else if (dr6 & (1 << X86_DR6_BD)) {
		// general detect exception
		// Occurs only, if GD in DR7 is set (which we don't do) and someone
		// tries to write to the debug registers.
		if (IFRAME_IS_USER(frame)) {
			dprintf("x86_handle_debug_exception(): ignoring spurious general "
				"detect exception\n");

			enable_interrupts();
		} else
			panic("spurious general detect exception in kernel mode");
	} else if ((dr6 & (1 << X86_DR6_BS)) || sQEmuSingleStepHack) {
		// single step

		if (IFRAME_IS_USER(frame)) {
			// enable interrupts and notify the debugger
			enable_interrupts();

			user_debug_single_stepped();
		} else {
			// Disable single-stepping -- the next "step" command will re-enable
			// it, but we don't want it when continuing otherwise.
			frame->flags &= ~(1 << X86_EFLAGS_TF);

			// Determine whether the exception occurred at a syscall/trap
			// kernel entry or whether this is genuine kernel single-stepping.
			bool inKernel = true;
			if (thread->team != team_get_kernel_team()
				&& x86_get_user_iframe() == NULL) {
				// TODO: This is not yet fully correct, since a newly created
				// thread that hasn't entered userland yet also has this
				// property.
				inKernel = false;
			}

			if (inKernel) {
				panic("kernel single step");
			} else {
				// The thread is a userland thread and it just entered the
				// kernel when the single-step exception occurred. This happens
				// e.g. when sysenter is called with single-stepping enabled.
				// We need to ignore the exception now and send a single-step
				// notification later, when the thread wants to return from the
				// kernel.
				InterruptsSpinLocker threadDebugInfoLocker(
					thread->debug_info.lock);

				// Check whether the team is still being debugged and set
				// the B_THREAD_DEBUG_NOTIFY_SINGLE_STEP and
				// B_THREAD_DEBUG_STOP flags, so that the thread will be
				// stopped when it is going to leave the kernel and notify the
				// debugger about the single-step event.
				int32 teamDebugFlags
					= atomic_get(&thread->team->debug_info.flags);
				if (teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_INSTALLED) {
					atomic_or(&thread->debug_info.flags,
						B_THREAD_DEBUG_NOTIFY_SINGLE_STEP
							| B_THREAD_DEBUG_STOP);

					// also set the respective thread flag
					atomic_or(&thread->flags, THREAD_FLAGS_DEBUG_THREAD);
				}
			}
		}
	} else if (dr6 & (1 << X86_DR6_BT)) {
		// task switch
		// Occurs only, if T in EFLAGS is set (which we don't do).
		if (IFRAME_IS_USER(frame)) {
			dprintf("x86_handle_debug_exception(): ignoring spurious task switch "
				"exception\n");

			enable_interrupts();
		} else
			panic("spurious task switch exception in kernel mode");
	} else {
		if (IFRAME_IS_USER(frame)) {
			TRACE(("x86_handle_debug_exception(): ignoring spurious debug "
				"exception (no condition recognized)\n"));

			enable_interrupts();
		} else {
			panic("spurious debug exception in kernel mode (no condition "
				"recognized)");
		}
	}
}


/**
 *	Interrupts are disabled and will possibly be enabled by the function.
 */
void
x86_handle_breakpoint_exception(iframe* frame)
{
	TRACE(("x86_handle_breakpoint_exception()\n"));

	// reset eip to the int3 instruction
	frame->ip--;

	if (!IFRAME_IS_USER(frame)) {
		panic("breakpoint exception in kernel mode");
		return;
	}

	enable_interrupts();

	user_debug_breakpoint_hit(true);
}


void
x86_init_user_debug()
{
	// get debug settings
	if (void* handle = load_driver_settings("kernel")) {
		sQEmuSingleStepHack = get_driver_boolean_parameter(handle,
			"qemu_single_step_hack", false, false);;

		unload_driver_settings(handle);
	}

#if KERNEL_BREAKPOINTS
	// install debugger commands
	add_debugger_command_etc("breakpoints", &debugger_breakpoints,
		"Lists current break-/watchpoints",
		"\n"
		"Lists the current kernel break-/watchpoints.\n", 0);
	add_debugger_command_alias("watchpoints", "breakpoints", NULL);
	add_debugger_command_etc("breakpoint", &debugger_breakpoint,
		"Set/clears a breakpoint",
		"<address> [ clear ]\n"
		"Sets respectively clears the breakpoint at address <address>.\n", 0);
	add_debugger_command_etc("watchpoint", &debugger_watchpoint,
		"Set/clears a watchpoint",
		"<address> <address> ( [ rw ] [ <size> ] | clear )\n"
		"Sets respectively clears the watchpoint at address <address>.\n"
		"If \"rw\" is given the new watchpoint is a read/write watchpoint\n"
		"otherwise a write watchpoint only.\n", 0);
	add_debugger_command_etc("step", &debugger_single_step,
		"Single-steps to the next instruction",
		"\n"
		"Single-steps to the next instruction.\n", 0);
#endif
}

