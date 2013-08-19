/*
 * Copyright 2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2012-2013, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */


#include "UiUtils.h"

#include <ctype.h>
#include <stdio.h>

#include <DateTime.h>
#include <KernelExport.h>
#include <Path.h>
#include <String.h>
#include <Variant.h>

#include <vm_defs.h>

#include "FunctionInstance.h"
#include "Image.h"
#include "RangeList.h"
#include "StackFrame.h"
#include "Team.h"
#include "TeamMemoryBlock.h"
#include "Thread.h"
#include "Type.h"
#include "Value.h"
#include "ValueNode.h"


/*static*/ const char*
UiUtils::ThreadStateToString(int state, int stoppedReason)
{
	switch (state) {
		case THREAD_STATE_RUNNING:
			return "Running";
		case THREAD_STATE_STOPPED:
			break;
		case THREAD_STATE_UNKNOWN:
		default:
			return "?";
	}

	// thread is stopped -- get the reason
	switch (stoppedReason) {
		case THREAD_STOPPED_DEBUGGER_CALL:
			return "Call";
		case THREAD_STOPPED_EXCEPTION:
			return "Exception";
		case THREAD_STOPPED_BREAKPOINT:
		case THREAD_STOPPED_WATCHPOINT:
		case THREAD_STOPPED_SINGLE_STEP:
		case THREAD_STOPPED_DEBUGGED:
		case THREAD_STOPPED_UNKNOWN:
		default:
			return "Debugged";
	}
}


/*static*/ const char*
UiUtils::VariantToString(const BVariant& value, char* buffer,
	size_t bufferSize)
{
	if (!value.IsNumber())
		return value.ToString();

	switch (value.Type()) {
		case B_FLOAT_TYPE:
		case B_DOUBLE_TYPE:
			snprintf(buffer, bufferSize, "%g", value.ToDouble());
			break;
		case B_INT8_TYPE:
		case B_UINT8_TYPE:
			snprintf(buffer, bufferSize, "0x%02x", value.ToUInt8());
			break;
		case B_INT16_TYPE:
		case B_UINT16_TYPE:
			snprintf(buffer, bufferSize, "0x%04x", value.ToUInt16());
			break;
		case B_INT32_TYPE:
		case B_UINT32_TYPE:
			snprintf(buffer, bufferSize, "0x%08" B_PRIx32,
				value.ToUInt32());
			break;
		case B_INT64_TYPE:
		case B_UINT64_TYPE:
		default:
			snprintf(buffer, bufferSize, "0x%016" B_PRIx64,
				value.ToUInt64());
			break;
	}

	return buffer;
}


/*static*/ const char*
UiUtils::FunctionNameForFrame(StackFrame* frame, char* buffer,
	size_t bufferSize)
{
	Image* image = frame->GetImage();
	FunctionInstance* function = frame->Function();
	if (image == NULL && function == NULL) {
		snprintf(buffer, bufferSize, "?");
		return buffer;
	}

	BString name;
	target_addr_t baseAddress;
	if (function != NULL) {
		name = function->PrettyName();
		baseAddress = function->Address();
	} else {
		name = image->Name();
		baseAddress = image->Info().TextBase();
	}

	snprintf(buffer, bufferSize, "%s + %#" B_PRIx64,
		name.String(), frame->InstructionPointer() - baseAddress);

	return buffer;
}


/*static*/ const char*
UiUtils::ImageTypeToString(image_type type, char* buffer, size_t bufferSize)
{
	switch (type) {
		case B_APP_IMAGE:
			snprintf(buffer, bufferSize, "app");
			break;
		case B_LIBRARY_IMAGE:
			snprintf(buffer, bufferSize, "lib");
			break;
		case B_ADD_ON_IMAGE:
			snprintf(buffer, bufferSize, "add-on");
			break;
		case B_SYSTEM_IMAGE:
			snprintf(buffer, bufferSize, "system");
			break;
		default:
			snprintf(buffer, bufferSize, "unknown");
			break;
	}

	return buffer;
}


/*static*/ const char*
UiUtils::AreaLockingFlagsToString(uint32 flags, char* buffer,
	size_t bufferSize)
{
	switch (flags) {
		case B_NO_LOCK:
			snprintf(buffer, bufferSize, "none");
			break;
		case B_LAZY_LOCK:
			snprintf(buffer, bufferSize, "lazy");
			break;
		case B_FULL_LOCK:
			snprintf(buffer, bufferSize, "full");
			break;
		case B_CONTIGUOUS:
			snprintf(buffer, bufferSize, "contiguous");
			break;
		case B_LOMEM:
			snprintf(buffer, bufferSize, "lo-mem");
			break;
		case B_32_BIT_FULL_LOCK:
			snprintf(buffer, bufferSize, "32-bit full");
			break;
		case B_32_BIT_CONTIGUOUS:
			snprintf(buffer, bufferSize, "32-bit contig.");
			break;
		default:
			snprintf(buffer, bufferSize, "unknown");
			break;
	}

	return buffer;
}


/*static*/ const BString&
UiUtils::AreaProtectionFlagsToString(uint32 protection, BString& _output)
{
	#undef ADD_AREA_FLAG_IF_PRESENT
	#define ADD_AREA_FLAG_IF_PRESENT(flag, protection, name, output, missing)\
		if ((protection & flag) != 0) { \
			_output += name; \
			protection &= ~flag; \
		} else \
			_output += missing; \

	_output.Truncate(0);
	uint32 userFlags = protection & B_USER_PROTECTION;
	bool userProtectionPresent = userFlags != 0;
	ADD_AREA_FLAG_IF_PRESENT(B_READ_AREA, protection, "r", _output,
		userProtectionPresent ? "-" : " ");
	ADD_AREA_FLAG_IF_PRESENT(B_WRITE_AREA, protection, "w", _output,
		userProtectionPresent ? "-" : " ");
	ADD_AREA_FLAG_IF_PRESENT(B_EXECUTE_AREA, protection, "x", _output,
		userProtectionPresent ? "-" : " ");

	// if the user versions of these flags are present,
	// filter out their kernel equivalents since they're implied.
	if ((userFlags & B_READ_AREA) != 0)
		protection &= ~B_KERNEL_READ_AREA;
	if ((userFlags & B_WRITE_AREA) != 0)
		protection &= ~B_KERNEL_WRITE_AREA;
	if ((userFlags & B_EXECUTE_AREA) != 0)
		protection &= ~B_KERNEL_EXECUTE_AREA;

	if ((protection & B_KERNEL_PROTECTION) != 0) {
		ADD_AREA_FLAG_IF_PRESENT(B_KERNEL_READ_AREA, protection, "r",
			_output, "-");
		ADD_AREA_FLAG_IF_PRESENT(B_KERNEL_WRITE_AREA, protection, "w",
			_output, "-");
		ADD_AREA_FLAG_IF_PRESENT(B_KERNEL_EXECUTE_AREA, protection, "x",
			_output, "-");
	}

	ADD_AREA_FLAG_IF_PRESENT(B_STACK_AREA, protection, "s", _output, "");
	ADD_AREA_FLAG_IF_PRESENT(B_KERNEL_STACK_AREA, protection, "s", _output, "");
	ADD_AREA_FLAG_IF_PRESENT(B_OVERCOMMITTING_AREA, protection, _output, "o",
		"");
	ADD_AREA_FLAG_IF_PRESENT(B_SHARED_AREA, protection, "S", _output, "");
	ADD_AREA_FLAG_IF_PRESENT(B_KERNEL_AREA, protection, "k", _output, "");

	if (protection != 0) {
		char buffer[32];
		snprintf(buffer, sizeof(buffer), ", u:(%#04" B_PRIx32 ")",
			protection);
		_output += buffer;
	}

	return _output;
}


/*static*/ const char*
UiUtils::ReportNameForTeam(::Team* team, char* buffer, size_t bufferSize)
{
	BPath teamPath(team->Name());
	BDateTime currentTime;
	currentTime.SetTime_t(time(NULL));
	snprintf(buffer, bufferSize, "%s-%" B_PRId32 "-debug-%02" B_PRId32 "-%02"
		B_PRId32 "-%02" B_PRId32 "-%02" B_PRId32 "-%02" B_PRId32 "-%02"
		B_PRId32 ".report", teamPath.Leaf(), team->ID(),
		currentTime.Date().Day(), currentTime.Date().Month(),
		currentTime.Date().Year(), currentTime.Time().Hour(),
		currentTime.Time().Minute(), currentTime.Time().Second());

	return buffer;

}


/*static*/ void
UiUtils::PrintValueNodeGraph(BString& _output, ValueNodeChild* child,
	int32 indentLevel, int32 maxDepth)
{
	_output.Append('\t', indentLevel);
	_output << child->Name();

	ValueNode* node = child->Node();
	if (node == NULL) {
		_output << ": Unavailable\n";
		return;
	}

	if (node->GetType()->Kind() != TYPE_COMPOUND) {
		_output << ": ";
		status_t resolutionState = node->LocationAndValueResolutionState();
		if (resolutionState == VALUE_NODE_UNRESOLVED)
			_output << "Unresolved";
		else if (resolutionState == B_OK) {
			Value* value = node->GetValue();
			if (value != NULL) {
				BString valueData;
				value->ToString(valueData);
				_output << valueData;
			} else
				_output << "Unavailable";
		} else
			_output << strerror(resolutionState);
	}

	if (maxDepth == 0 || node->CountChildren() == 0) {
		_output << "\n";
		return;
	}

	if (node->CountChildren() == 1
		&& node->GetType()->Kind() == TYPE_ADDRESS
		&& node->ChildAt(0)->GetType()->Kind() == TYPE_COMPOUND) {
		// for the case of a pointer to a compound type,
		// we want to hide the intervening compound node and print
		// the children directly.
		node = node->ChildAt(0)->Node();
	}

	if (node != NULL) {
		_output << " {\n";

		for (int32 i = 0; i < node->CountChildren(); i++) {
			// don't dump compound nodes if our depth limit won't allow
			// us to traverse into their children anyways, and the top
			// level node contains no data of intereest.
			if (node->ChildAt(i)->GetType()->Kind() != TYPE_COMPOUND
				|| maxDepth > 1) {
				PrintValueNodeGraph(_output, node->ChildAt(i),
					indentLevel + 1, maxDepth - 1);
			}
		}
		_output.Append('\t', indentLevel);
		_output << "}\n";
	} else
		_output << "\n";

	return;
}


/*static*/ void
UiUtils::DumpMemory(BString& _output, int32 indentLevel,
	TeamMemoryBlock* block, target_addr_t address, int32 itemSize,
	int32 displayWidth, int32 count)
{
	BString data;

	int32 j;
	_output.Append('\t', indentLevel);
	for (int32 i = 0; i < count; i++) {
		uint8* value;

		if ((i % displayWidth) == 0) {
			int32 displayed = min_c(displayWidth, (count-i)) * itemSize;
			if (i != 0) {
				_output.Append("\n");
				_output.Append('\t', indentLevel);
			}

			data.SetToFormat("[%#" B_PRIx64 "]  ", address + i * itemSize);
			_output += data;
			char c;
			for (j = 0; j < displayed; j++) {
				if (!block->Contains(address + displayed))
					break;
				c = *(block->Data() + address - block->BaseAddress()
					+ (i * itemSize) + j);
				if (!isprint(c))
					c = '.';

				_output += c;
			}
			if (count > displayWidth) {
				// make sure the spacing in the last line is correct
				for (j = displayed; j < displayWidth * itemSize; j++)
					_output += ' ';
			}
			_output.Append("  ");
		}

		value = block->Data() + address - block->BaseAddress()
			+ i * itemSize;

		switch (itemSize) {
			case 1:
				data.SetToFormat(" %02" B_PRIx8, *(uint8*)value);
				break;
			case 2:
				data.SetToFormat(" %04" B_PRIx16, *(uint16*)value);
				break;
			case 4:
				data.SetToFormat(" %08" B_PRIx32, *(uint32*)value);
				break;
			case 8:
				data.SetToFormat(" %016" B_PRIx64, *(uint64*)value);
				break;
		}

		_output += data;
	}

	_output.Append("\n");
}


static status_t ParseRangeString(BString& rangeString, int32& lowerBound,
	int32& upperBound)
{
	lowerBound = atoi(rangeString.String());
	int32 index = rangeString.FindFirst('-');
	if (index >= 0) {
		rangeString.Remove(0, index + 1);
		upperBound = atoi(rangeString.String());
	} else
		upperBound = lowerBound;

	if (lowerBound > upperBound)
		return B_BAD_VALUE;

	return B_OK;
}


/*static*/ status_t
UiUtils::ParseRangeExpression(const BString& rangeExpression, int32 lowerBound,
	int32 upperBound, bool fixedRange, RangeList& _output)
{
	if (rangeExpression.IsEmpty())
		return B_BAD_DATA;

	BString dataString = rangeExpression;
	dataString.RemoveAll(" ");

	// first, tokenize the range list to its constituent child ranges.
	int32 index;
	int32 lowValue;
	int32 highValue;
	BString tempRange;
	while (!dataString.IsEmpty()) {
		index = dataString.FindFirst(',');
		if (index == 0)
			return B_BAD_VALUE;
		else if (index > 0) {
			dataString.MoveInto(tempRange, 0, index);
			dataString.Remove(0, 1);
		} else {
			tempRange = dataString;
			dataString.Truncate(0);
		}

		status_t result = ParseRangeString(tempRange, lowValue, highValue);
		if (result != B_OK)
			return result;


		if (fixedRange && (lowValue < lowerBound || highValue > upperBound))
			return B_BAD_VALUE;

		result = _output.AddRange(lowValue, highValue);
		if (result != B_OK)
			return result;

		tempRange.Truncate(0);
	}

	return B_OK;
}
