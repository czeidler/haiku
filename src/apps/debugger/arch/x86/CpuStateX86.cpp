/*
 * Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2011-2013, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */

#include "CpuStateX86.h"

#include <new>

#include <string.h>

#include "Register.h"


CpuStateX86::CpuStateX86()
	:
	fSetRegisters(),
	fInterruptVector(0)
{
}


CpuStateX86::CpuStateX86(const x86_debug_cpu_state& state)
	:
	fSetRegisters(),
	fInterruptVector(0)
{
	SetIntRegister(X86_REGISTER_EIP, state.eip);
	SetIntRegister(X86_REGISTER_ESP, state.user_esp);
	SetIntRegister(X86_REGISTER_EBP, state.ebp);
	SetIntRegister(X86_REGISTER_EAX, state.eax);
	SetIntRegister(X86_REGISTER_EBX, state.ebx);
	SetIntRegister(X86_REGISTER_ECX, state.ecx);
	SetIntRegister(X86_REGISTER_EDX, state.edx);
	SetIntRegister(X86_REGISTER_ESI, state.esi);
	SetIntRegister(X86_REGISTER_EDI, state.edi);
	SetIntRegister(X86_REGISTER_CS, state.cs);
	SetIntRegister(X86_REGISTER_DS, state.ds);
	SetIntRegister(X86_REGISTER_ES, state.es);
	SetIntRegister(X86_REGISTER_FS, state.fs);
	SetIntRegister(X86_REGISTER_GS, state.gs);
	SetIntRegister(X86_REGISTER_SS, state.user_ss);

	fInterruptVector = state.vector;
}


CpuStateX86::~CpuStateX86()
{
}


status_t
CpuStateX86::Clone(CpuState*& _clone) const
{
	CpuStateX86* newState = new(std::nothrow) CpuStateX86();
	if (newState == NULL)
		return B_NO_MEMORY;


	memcpy(newState->fIntRegisters, fIntRegisters, sizeof(fIntRegisters));
	newState->fSetRegisters = fSetRegisters;
	newState->fInterruptVector = fInterruptVector;

	_clone = newState;

	return B_OK;
}


status_t
CpuStateX86::UpdateDebugState(void* state, size_t size) const
{
	if (size != sizeof(x86_debug_cpu_state))
		return B_BAD_VALUE;

	x86_debug_cpu_state* x86State = (x86_debug_cpu_state*)state;

	x86State->eip = InstructionPointer();
	x86State->user_esp = StackPointer();
	x86State->ebp = StackFramePointer();
	x86State->eax = IntRegisterValue(X86_REGISTER_EAX);
	x86State->ebx = IntRegisterValue(X86_REGISTER_EBX);
	x86State->ecx = IntRegisterValue(X86_REGISTER_ECX);
	x86State->edx = IntRegisterValue(X86_REGISTER_EDX);
	x86State->esi = IntRegisterValue(X86_REGISTER_ESI);
	x86State->edi = IntRegisterValue(X86_REGISTER_EDI);
	x86State->cs = IntRegisterValue(X86_REGISTER_CS);
	x86State->ds = IntRegisterValue(X86_REGISTER_DS);
	x86State->es = IntRegisterValue(X86_REGISTER_ES);
	x86State->fs = IntRegisterValue(X86_REGISTER_FS);
	x86State->gs = IntRegisterValue(X86_REGISTER_GS);
	x86State->user_ss = IntRegisterValue(X86_REGISTER_SS);
	x86State->vector = fInterruptVector;

	return B_OK;
}


target_addr_t
CpuStateX86::InstructionPointer() const
{
	return IsRegisterSet(X86_REGISTER_EIP)
		? IntRegisterValue(X86_REGISTER_EIP) : 0;
}


void
CpuStateX86::SetInstructionPointer(target_addr_t address)
{
	SetIntRegister(X86_REGISTER_EIP, (uint32)address);
}


target_addr_t
CpuStateX86::StackFramePointer() const
{
	return IsRegisterSet(X86_REGISTER_EBP)
		? IntRegisterValue(X86_REGISTER_EBP) : 0;
}


target_addr_t
CpuStateX86::StackPointer() const
{
	return IsRegisterSet(X86_REGISTER_ESP)
		? IntRegisterValue(X86_REGISTER_ESP) : 0;
}


bool
CpuStateX86::GetRegisterValue(const Register* reg, BVariant& _value) const
{
	int32 index = reg->Index();
	if (!IsRegisterSet(index))
		return false;

	if (index >= X86_INT_REGISTER_END)
		return false;

	if (reg->BitSize() == 16)
		_value.SetTo((uint16)fIntRegisters[index]);
	else
		_value.SetTo(fIntRegisters[index]);

	return true;
}


bool
CpuStateX86::SetRegisterValue(const Register* reg, const BVariant& value)
{
	int32 index = reg->Index();
	if (index >= X86_INT_REGISTER_END)
		return false;

	fIntRegisters[index] = value.ToUInt32();
	fSetRegisters[index] = 1;
	return true;
}


bool
CpuStateX86::IsRegisterSet(int32 index) const
{
	return index >= 0 && index < X86_REGISTER_COUNT && fSetRegisters[index];
}


uint32
CpuStateX86::IntRegisterValue(int32 index) const
{
	if (!IsRegisterSet(index) || index >= X86_INT_REGISTER_END)
		return 0;

	return fIntRegisters[index];
}


void
CpuStateX86::SetIntRegister(int32 index, uint32 value)
{
	if (index < 0 || index >= X86_INT_REGISTER_END)
		return;

	fIntRegisters[index] = value;
	fSetRegisters[index] = 1;
}


void
CpuStateX86::UnsetRegister(int32 index)
{
	if (index < 0 || index >= X86_REGISTER_COUNT)
		return;

	fSetRegisters[index] = 0;
}
