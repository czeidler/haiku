/*
 * Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2012, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */


#include "DwarfFile.h"

#include <algorithm>
#include <new>

#include <AutoDeleter.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>

#include "AttributeClasses.h"
#include "AttributeValue.h"
#include "AbbreviationTable.h"
#include "CfaContext.h"
#include "CompilationUnit.h"
#include "DataReader.h"
#include "DwarfExpressionEvaluator.h"
#include "DwarfTargetInterface.h"
#include "ElfFile.h"
#include "TagNames.h"
#include "TargetAddressRangeList.h"
#include "Tracing.h"
#include "Variant.h"


// #pragma mark - AutoSectionPutter


class AutoSectionPutter {
public:
	AutoSectionPutter(ElfFile* elfFile, ElfSection* elfSection)
		:
		fElfFile(elfFile),
		fElfSection(elfSection)
	{
	}

	~AutoSectionPutter()
	{
		if (fElfSection != NULL)
			fElfFile->PutSection(fElfSection);
	}

private:
	ElfFile*			fElfFile;
	ElfSection*			fElfSection;
};


// #pragma mark - ExpressionEvaluationContext


struct DwarfFile::ExpressionEvaluationContext
	: DwarfExpressionEvaluationContext {
public:
	ExpressionEvaluationContext(DwarfFile* file, CompilationUnit* unit,
		uint8 addressSize, DIESubprogram* subprogramEntry,
		const DwarfTargetInterface* targetInterface,
		target_addr_t instructionPointer, target_addr_t objectPointer,
		bool hasObjectPointer, target_addr_t framePointer,
		target_addr_t relocationDelta)
		:
		DwarfExpressionEvaluationContext(targetInterface, addressSize,
			relocationDelta),
		fFile(file),
		fUnit(unit),
		fSubprogramEntry(subprogramEntry),
		fInstructionPointer(instructionPointer),
		fObjectPointer(objectPointer),
		fHasObjectPointer(hasObjectPointer),
		fFramePointer(framePointer),
		fFrameBasePointer(0),
		fFrameBaseEvaluated(false)
	{
	}

	virtual bool GetObjectAddress(target_addr_t& _address)
	{
		if (!fHasObjectPointer)
			return false;

		_address = fObjectPointer;
		return true;
	}

	virtual bool GetFrameAddress(target_addr_t& _address)
	{
		if (fFramePointer == 0)
			return false;

		_address = fFramePointer;
		return true;
	}

	virtual bool GetFrameBaseAddress(target_addr_t& _address)
	{
		if (fFrameBaseEvaluated) {
			if (fFrameBasePointer == 0)
				return false;

			_address = fFrameBasePointer;
			return true;
		}

		// set flag already to prevent recursion for a buggy expression
		fFrameBaseEvaluated = true;

		// get the subprogram's frame base location
		if (fSubprogramEntry == NULL)
			return false;
		const LocationDescription* location = fSubprogramEntry->FrameBase();
		if (!location->IsValid())
			return false;

		// get the expression
		const void* expression;
		off_t expressionLength;
		status_t error = fFile->_GetLocationExpression(fUnit, location,
			fInstructionPointer, expression, expressionLength);
		if (error != B_OK)
			return false;

		// evaluate the expression
		DwarfExpressionEvaluator evaluator(this);
		error = evaluator.Evaluate(expression, expressionLength,
			fFrameBasePointer);
		if (error != B_OK)
			return false;

		TRACE_EXPR("  -> frame base: %" B_PRIx64 "\n", fFrameBasePointer);

		_address = fFrameBasePointer;
		return true;
	}

	virtual bool GetTLSAddress(target_addr_t localAddress,
		target_addr_t& _address)
	{
		// TODO:...
		return false;
	}

	virtual status_t GetCallTarget(uint64 offset, bool local,
		const void*& _block, off_t& _size)
	{
		// resolve the entry
		DebugInfoEntry* entry = fFile->_ResolveReference(fUnit, offset, local);
		if (entry == NULL)
			return B_ENTRY_NOT_FOUND;

		// get the location description
		LocationDescription* location = entry->GetLocationDescription();
		if (location == NULL || !location->IsValid()) {
			_block = NULL;
			_size = 0;
			return B_OK;
		}

		// get the expression
		return fFile->_GetLocationExpression(fUnit, location,
			fInstructionPointer, _block, _size);
	}

private:
	DwarfFile*			fFile;
	CompilationUnit*	fUnit;
	DIESubprogram*		fSubprogramEntry;
	target_addr_t		fInstructionPointer;
	target_addr_t		fObjectPointer;
	bool				fHasObjectPointer;
	target_addr_t		fFramePointer;
	target_addr_t		fFrameBasePointer;
	bool				fFrameBaseEvaluated;
};


// #pragma mark - FDEAugmentation


struct DwarfFile::FDEAugmentation {
	// Currently we're ignoring all augmentation data.
};


// #pragma mark - CIEAugmentation


enum {
	CFI_AUGMENTATION_DATA					= 0x01,
	CFI_AUGMENTATION_LANGUAGE_SPECIFIC_DATA	= 0x02,
	CFI_AUGMENTATION_PERSONALITY			= 0x04,
	CFI_AUGMENTATION_ADDRESS_POINTER_FORMAT	= 0x08,
};


// encodings for CFI_AUGMENTATION_ADDRESS_POINTER_FORMAT
enum {
	CFI_ADDRESS_FORMAT_ABSOLUTE			= 0x00,
	CFI_ADDRESS_FORMAT_UNSIGNED_LEB128	= 0x01,
	CFI_ADDRESS_FORMAT_UNSIGNED_16		= 0x02,
	CFI_ADDRESS_FORMAT_UNSIGNED_32		= 0x03,
	CFI_ADDRESS_FORMAT_UNSIGNED_64		= 0x04,
	CFI_ADDRESS_FORMAT_SIGNED			= 0x08,
	CFI_ADDRESS_FORMAT_SIGNED_LEB128	=
		CFI_ADDRESS_FORMAT_UNSIGNED_LEB128 | CFI_ADDRESS_FORMAT_SIGNED,
	CFI_ADDRESS_FORMAT_SIGNED_16		=
		CFI_ADDRESS_FORMAT_UNSIGNED_16 | CFI_ADDRESS_FORMAT_SIGNED,
	CFI_ADDRESS_FORMAT_SIGNED_32		=
		CFI_ADDRESS_FORMAT_UNSIGNED_32 | CFI_ADDRESS_FORMAT_SIGNED,
	CFI_ADDRESS_FORMAT_SIGNED_64		=
		CFI_ADDRESS_FORMAT_UNSIGNED_64 | CFI_ADDRESS_FORMAT_SIGNED
};


enum {
	CFI_ADDRESS_TYPE_PC_RELATIVE		= 0x10,
	CFI_ADDRESS_TYPE_TEXT_RELATIVE		= 0x20,
	CFI_ADDRESS_TYPE_DATA_RELATIVE		= 0x30,
	CFI_ADDRESS_TYPE_FUNCTION_RELATIVE	= 0x40,
	CFI_ADDRESS_TYPE_ALIGNED			= 0x50,
	CFI_ADDRESS_TYPE_INDIRECT			= 0x80
};


struct DwarfFile::CIEAugmentation {
	CIEAugmentation()
		:
		fString(NULL),
		fFlags(0),
		fAddressEncoding(CFI_ADDRESS_FORMAT_ABSOLUTE)
	{
		// we default to absolute address format since that corresponds
		// to the DWARF standard for .debug_frame. In gcc's case, however,
		// .eh_frame will generally override that via augmentation 'R'
	}

	void Init(DataReader& dataReader)
	{
		fFlags = 0;
		fString = dataReader.ReadString();
	}

	status_t Read(DataReader& dataReader)
	{
		if (fString == NULL || *fString == '\0')
			return B_OK;

		if (*fString == 'z') {
			// There are augmentation data.
			fFlags |= CFI_AUGMENTATION_DATA;
			const char* string = fString + 1;

			// read the augmentation data block -- it is preceeded by an
			// LEB128 indicating the length of the data block
			uint64 length = dataReader.ReadUnsignedLEB128(0);
			uint64 remaining = length;
			// let's see what data we have to expect

			TRACE_CFI("    %" B_PRIu64 " bytes of augmentation data\n", length);
			while (*string != '\0') {
				switch (*string) {
					case 'L':
						fFlags |= CFI_AUGMENTATION_LANGUAGE_SPECIFIC_DATA;
						dataReader.Read<char>(0);
						--remaining;
						break;
					case 'P':
					{
						char tempEncoding = fAddressEncoding;
						fAddressEncoding = dataReader.Read<char>(0);
						off_t offset = dataReader.Offset();
						ReadEncodedAddress(dataReader, NULL, NULL, true);
						fAddressEncoding = tempEncoding;
						remaining -= dataReader.Offset() - offset + 1;
 						break;
					}
					case 'R':
						fFlags |= CFI_AUGMENTATION_ADDRESS_POINTER_FORMAT;
						fAddressEncoding = dataReader.Read<char>(0);
						--remaining;
						break;
					default:
						WARNING("Encountered unsupported augmentation '%c' "
							" while parsing CIE augmentation string %s\n",
							*string, fString);
						return B_UNSUPPORTED;
				}
				string++;
			}

			// we should have read through all of the augmentation data
			// at this point, if not, something is wrong.
			if (remaining != 0 || dataReader.HasOverflow()) {
				WARNING("Error while reading CIE Augmentation, expected "
					"%" B_PRIu64 " bytes of augmentation data, but read "
					"%" B_PRIu64 " bytes.\n", length, length - remaining);
				return B_BAD_DATA;
			}

			return B_OK;
		}

		// nothing to do
		if (strcmp(fString, "eh") == 0)
			return B_OK;

		// something we can't handle
		return B_UNSUPPORTED;
	}

	status_t ReadFDEData(DataReader& dataReader,
		FDEAugmentation& fdeAugmentation)
	{
		if (!HasData())
			return B_OK;

		// read the augmentation data block -- it is preceeded by an LEB128
		// indicating the length of the data block
		uint64 length = dataReader.ReadUnsignedLEB128(0);
		dataReader.Skip(length);
			// TODO: Actually read what is interesting for us!

		TRACE_CFI("    %" B_PRIu64 " bytes of augmentation data\n", length);

		if (dataReader.HasOverflow())
			return B_BAD_DATA;

		return B_OK;
	}

	const char* String() const
	{
		return fString;
	}

	bool HasData() const
	{
		return (fFlags & CFI_AUGMENTATION_DATA) != 0;
	}

	bool HasFDEAddressFormat() const
	{
		return (fFlags & CFI_AUGMENTATION_ADDRESS_POINTER_FORMAT) != 0;
	}

	target_addr_t FDEAddressOffset(ElfFile* file,
		ElfSection* debugFrameSection) const
	{
		switch (FDEAddressType()) {
			case CFI_ADDRESS_FORMAT_ABSOLUTE:
				TRACE_CFI("FDE address format: absolute, ");
				return 0;
			case CFI_ADDRESS_TYPE_PC_RELATIVE:
				TRACE_CFI("FDE address format: PC relative, ");
				return debugFrameSection->LoadAddress();
			case CFI_ADDRESS_TYPE_FUNCTION_RELATIVE:
				TRACE_CFI("FDE address format: function relative, ");
				return 0;
			case CFI_ADDRESS_TYPE_TEXT_RELATIVE:
				TRACE_CFI("FDE address format: text relative, ");
				return file->TextSegment()->LoadAddress();
			case CFI_ADDRESS_TYPE_DATA_RELATIVE:
				TRACE_CFI("FDE address format: data relative, ");
				return file->DataSegment()->LoadAddress();
			case CFI_ADDRESS_TYPE_ALIGNED:
			case CFI_ADDRESS_TYPE_INDIRECT:
				TRACE_CFI("FDE address format: UNIMPLEMENTED, ");
				// TODO: implement
				// -- note: type indirect is currently not generated
				return 0;
		}

		return 0;
	}

	uint8 FDEAddressType() const
	{
		return fAddressEncoding & 0x70;
	}

	target_addr_t ReadEncodedAddress(DataReader &reader,
		ElfFile* file, ElfSection* debugFrameSection,
		bool valueOnly = false) const
	{
		target_addr_t address = valueOnly ? 0 : FDEAddressOffset(file,
			debugFrameSection);
		switch (fAddressEncoding & 0x0f) {
			case CFI_ADDRESS_FORMAT_ABSOLUTE:
				address += reader.ReadAddress(0);
				TRACE_CFI(" target address: %" B_PRId64 "\n", address);
				break;
			case CFI_ADDRESS_FORMAT_UNSIGNED_LEB128:
				address += reader.ReadUnsignedLEB128(0);
				TRACE_CFI(" unsigned LEB128: %" B_PRId64 "\n", address);
				break;
			case CFI_ADDRESS_FORMAT_SIGNED_LEB128:
				address += reader.ReadSignedLEB128(0);
				TRACE_CFI(" signed LEB128: %" B_PRId64 "\n", address);
				break;
			case CFI_ADDRESS_FORMAT_UNSIGNED_16:
				address += reader.Read<uint16>(0);
				TRACE_CFI(" unsigned 16-bit: %" B_PRId64 "\n", address);
				break;
			case CFI_ADDRESS_FORMAT_SIGNED_16:
				address += reader.Read<int16>(0);
				TRACE_CFI(" signed 16-bit: %" B_PRId64 "\n", address);
				break;
			case CFI_ADDRESS_FORMAT_UNSIGNED_32:
				address += reader.Read<uint32>(0);
				TRACE_CFI(" unsigned 32-bit: %" B_PRId64 "\n", address);
				break;
			case CFI_ADDRESS_FORMAT_SIGNED_32:
				address += reader.Read<int32>(0);
				TRACE_CFI(" signed 32-bit: %" B_PRId64 "\n", address);
				break;
			case CFI_ADDRESS_FORMAT_UNSIGNED_64:
				address += reader.Read<uint64>(0);
				TRACE_CFI(" unsigned 64-bit: %" B_PRId64 "\n", address);
				break;
			case CFI_ADDRESS_FORMAT_SIGNED_64:
				address += reader.Read<int64>(0);
				TRACE_CFI(" signed 64-bit: %" B_PRId64 "\n", address);
				break;
		}

		return address;
	}


private:
	const char*	fString;
	uint32		fFlags;
	int8		fAddressEncoding;
};


// #pragma mark - DwarfFile


DwarfFile::DwarfFile()
	:
	fName(NULL),
	fAlternateName(NULL),
	fElfFile(NULL),
	fAlternateElfFile(NULL),
	fDebugInfoSection(NULL),
	fDebugAbbrevSection(NULL),
	fDebugStringSection(NULL),
	fDebugRangesSection(NULL),
	fDebugLineSection(NULL),
	fDebugFrameSection(NULL),
	fEHFrameSection(NULL),
	fDebugLocationSection(NULL),
	fDebugPublicTypesSection(NULL),
	fCompilationUnits(20, true),
	fCurrentCompilationUnit(NULL),
	fFinished(false),
	fFinishError(B_OK)
{
}


DwarfFile::~DwarfFile()
{
	while (AbbreviationTable* table = fAbbreviationTables.RemoveHead())
		delete table;

	if (fElfFile != NULL) {
		ElfFile* debugInfoFile = fAlternateElfFile != NULL
			? fAlternateElfFile : fElfFile;

		debugInfoFile->PutSection(fDebugInfoSection);
		debugInfoFile->PutSection(fDebugAbbrevSection);
		debugInfoFile->PutSection(fDebugStringSection);
		debugInfoFile->PutSection(fDebugRangesSection);
		debugInfoFile->PutSection(fDebugLineSection);
		debugInfoFile->PutSection(fDebugFrameSection);
		fElfFile->PutSection(fEHFrameSection);
		debugInfoFile->PutSection(fDebugLocationSection);
		debugInfoFile->PutSection(fDebugPublicTypesSection);
		delete fElfFile;
		delete fAlternateElfFile;
	}

	free(fName);
	free(fAlternateName);
}


status_t
DwarfFile::Load(const char* fileName)
{
	fName = strdup(fileName);
	if (fName == NULL)
		return B_NO_MEMORY;

	// load the ELF file
	fElfFile = new(std::nothrow) ElfFile;
	if (fElfFile == NULL)
		return B_NO_MEMORY;

	status_t error = fElfFile->Init(fileName);
	if (error != B_OK)
		return error;

	error = _LocateDebugInfo();
	if (error != B_OK)
		return error;

	ElfFile* debugInfoFile = fAlternateElfFile != NULL
		? fAlternateElfFile : fElfFile;

	// non mandatory sections
	fDebugStringSection = debugInfoFile->GetSection(".debug_str");
	fDebugRangesSection = debugInfoFile->GetSection(".debug_ranges");
	fDebugLineSection = debugInfoFile->GetSection(".debug_line");
	fDebugFrameSection = debugInfoFile->GetSection(".debug_frame");
	// .eh_frame doesn't appear to get copied into separate debug
	// info files properly, therefore always use it off the main
	// executable image
	if (fEHFrameSection == NULL)
		fEHFrameSection = fElfFile->GetSection(".eh_frame");
	fDebugLocationSection = debugInfoFile->GetSection(".debug_loc");
	fDebugPublicTypesSection = debugInfoFile->GetSection(".debug_pubtypes");

	if (fDebugInfoSection == NULL) {
		fFinished = true;
		return B_OK;
	}

	// iterate through the debug info section
	DataReader dataReader(fDebugInfoSection->Data(),
		fDebugInfoSection->Size(), 4);
			// address size doesn't matter here
	while (dataReader.HasData()) {
		off_t unitHeaderOffset = dataReader.Offset();
		bool dwarf64;
		uint64 unitLength = dataReader.ReadInitialLength(dwarf64);

		off_t unitLengthOffset = dataReader.Offset();
			// the unitLength starts here

		if (unitLengthOffset + unitLength
				> (uint64)fDebugInfoSection->Size()) {
			WARNING("\"%s\": Invalid compilation unit length.\n", fileName);
			break;
		}

		int version = dataReader.Read<uint16>(0);
		off_t abbrevOffset = dwarf64
			? dataReader.Read<uint64>(0)
			: dataReader.Read<uint32>(0);
		uint8 addressSize = dataReader.Read<uint8>(0);

		if (dataReader.HasOverflow()) {
			WARNING("\"%s\": Unexpected end of data in compilation unit "
				"header.\n", fileName);
			break;
		}

		TRACE_DIE("DWARF%d compilation unit: version %d, length: %" B_PRIu64
			", abbrevOffset: %" B_PRIdOFF ", address size: %d\n",
			dwarf64 ? 64 : 32, version, unitLength, abbrevOffset, addressSize);

		if (version != 2 && version != 3) {
			WARNING("\"%s\": Unsupported compilation unit version: %d\n",
				fileName, version);
			break;
		}

		if (addressSize != 4 && addressSize != 8) {
			WARNING("\"%s\": Unsupported address size: %d\n", fileName,
				addressSize);
			break;
		}
		dataReader.SetAddressSize(addressSize);

		off_t unitContentOffset = dataReader.Offset();

		// create a compilation unit object
		CompilationUnit* unit = new(std::nothrow) CompilationUnit(
			unitHeaderOffset, unitContentOffset,
			unitLength + (unitLengthOffset - unitHeaderOffset),
			abbrevOffset, addressSize, dwarf64);
		if (unit == NULL || !fCompilationUnits.AddItem(unit)) {
			delete unit;
			return B_NO_MEMORY;
		}

		// parse the debug info for the unit
		fCurrentCompilationUnit = unit;
		error = _ParseCompilationUnit(unit);
		if (error != B_OK)
			return error;

		dataReader.SeekAbsolute(unitLengthOffset + unitLength);
	}

	return B_OK;
}


status_t
DwarfFile::FinishLoading()
{
	if (fFinished)
		return B_OK;
	if (fFinishError != B_OK)
		return fFinishError;

	for (int32 i = 0; CompilationUnit* unit = fCompilationUnits.ItemAt(i);
			i++) {
		fCurrentCompilationUnit = unit;
		status_t error = _FinishCompilationUnit(unit);
		if (error != B_OK)
			return fFinishError = error;
	}

	_ParsePublicTypesInfo();

	fFinished = true;
	return B_OK;
}


int32
DwarfFile::CountCompilationUnits() const
{
	return fCompilationUnits.CountItems();
}


CompilationUnit*
DwarfFile::CompilationUnitAt(int32 index) const
{
	return fCompilationUnits.ItemAt(index);
}


CompilationUnit*
DwarfFile::CompilationUnitForDIE(const DebugInfoEntry* entry) const
{
	// find the root of the tree the entry lives in
	while (entry != NULL && entry->Parent() != NULL)
		entry = entry->Parent();

	// that should be the compilation unit entry
	const DIECompileUnitBase* unitEntry
		= dynamic_cast<const DIECompileUnitBase*>(entry);
	if (unitEntry == NULL)
		return NULL;

	// find the compilation unit
	for (int32 i = 0; CompilationUnit* unit = fCompilationUnits.ItemAt(i);
			i++) {
		if (unit->UnitEntry() == unitEntry)
			return unit;
	}

	return NULL;
}


TargetAddressRangeList*
DwarfFile::ResolveRangeList(CompilationUnit* unit, uint64 offset) const
{
	if (unit == NULL || fDebugRangesSection == NULL)
		return NULL;

	if (offset < 0 || offset >= (uint64)fDebugRangesSection->Size())
		return NULL;

	TargetAddressRangeList* ranges = new(std::nothrow) TargetAddressRangeList;
	if (ranges == NULL) {
		ERROR("Out of memory.\n");
		return NULL;
	}
	BReference<TargetAddressRangeList> rangesReference(ranges, true);

	target_addr_t baseAddress = unit->AddressRangeBase();
	target_addr_t maxAddress = unit->MaxAddress();

	DataReader dataReader((uint8*)fDebugRangesSection->Data() + offset,
		fDebugRangesSection->Size() - offset, unit->AddressSize());
	while (true) {
		target_addr_t start = dataReader.ReadAddress(0);
		target_addr_t end = dataReader.ReadAddress(0);
		if (dataReader.HasOverflow())
			return NULL;

		if (start == 0 && end == 0)
			break;
		if (start == maxAddress) {
			baseAddress = end;
			continue;
		}
		if (start == end)
			continue;

		if (!ranges->AddRange(baseAddress + start, end - start)) {
			ERROR("Out of memory.\n");
			return NULL;
		}
	}

	return rangesReference.Detach();
}


status_t
DwarfFile::UnwindCallFrame(CompilationUnit* unit, uint8 addressSize,
	DIESubprogram* subprogramEntry, target_addr_t location,
	const DwarfTargetInterface* inputInterface,
	DwarfTargetInterface* outputInterface, target_addr_t& _framePointer)
{
	status_t result = B_ENTRY_NOT_FOUND;

	// first try to find the FDE in .debug_frame
	if (fDebugFrameSection != NULL) {
		result = _UnwindCallFrame(false, unit, addressSize, subprogramEntry,
			location, inputInterface, outputInterface, _framePointer);
	}

	// if .debug_frame isn't present, or if the FDE wasn't found there,
	// try .eh_frame
	if (result == B_ENTRY_NOT_FOUND && fEHFrameSection != NULL) {
		result = _UnwindCallFrame(true, unit, addressSize, subprogramEntry,
			location, inputInterface, outputInterface, _framePointer);
	}

	return result;
}


status_t
DwarfFile::EvaluateExpression(CompilationUnit* unit, uint8 addressSize,
	DIESubprogram* subprogramEntry, const void* expression,
	off_t expressionLength, const DwarfTargetInterface* targetInterface,
	target_addr_t instructionPointer, target_addr_t framePointer,
	target_addr_t valueToPush, bool pushValue, target_addr_t& _result)
{
	ExpressionEvaluationContext context(this, unit, addressSize,
		subprogramEntry, targetInterface, instructionPointer, 0, false,
		framePointer, 0);
	DwarfExpressionEvaluator evaluator(&context);

	if (pushValue && evaluator.Push(valueToPush) != B_OK)
		return B_NO_MEMORY;

	return evaluator.Evaluate(expression, expressionLength, _result);
}


status_t
DwarfFile::ResolveLocation(CompilationUnit* unit, uint8 addressSize,
	DIESubprogram* subprogramEntry, const LocationDescription* location,
	const DwarfTargetInterface* targetInterface,
	target_addr_t instructionPointer, target_addr_t objectPointer,
	bool hasObjectPointer, target_addr_t framePointer,
	target_addr_t relocationDelta, ValueLocation& _result)
{
	// get the expression
	const void* expression;
	off_t expressionLength;
	status_t error = _GetLocationExpression(unit, location, instructionPointer,
		expression, expressionLength);
	if (error != B_OK)
		return error;

	// evaluate it
	ExpressionEvaluationContext context(this, unit, addressSize,
		subprogramEntry, targetInterface, instructionPointer, objectPointer,
		hasObjectPointer, framePointer, relocationDelta);
	DwarfExpressionEvaluator evaluator(&context);
	return evaluator.EvaluateLocation(expression, expressionLength,
		_result);
}


status_t
DwarfFile::EvaluateConstantValue(CompilationUnit* unit, uint8 addressSize,
	DIESubprogram* subprogramEntry, const ConstantAttributeValue* value,
	const DwarfTargetInterface* targetInterface,
	target_addr_t instructionPointer, target_addr_t framePointer,
	BVariant& _result)
{
	if (!value->IsValid())
		return B_BAD_VALUE;

	switch (value->attributeClass) {
		case ATTRIBUTE_CLASS_CONSTANT:
			_result.SetTo(value->constant);
			return B_OK;
		case ATTRIBUTE_CLASS_STRING:
			_result.SetTo(value->string);
			return B_OK;
		case ATTRIBUTE_CLASS_BLOCK:
		{
			target_addr_t result;
			status_t error = EvaluateExpression(unit, addressSize,
				subprogramEntry, value->block.data, value->block.length,
				targetInterface, instructionPointer, framePointer, 0, false,
				result);
			if (error != B_OK)
				return error;

			_result.SetTo(result);
			return B_OK;
		}
		default:
			return B_BAD_VALUE;
	}
}


status_t
DwarfFile::EvaluateDynamicValue(CompilationUnit* unit, uint8 addressSize,
	DIESubprogram* subprogramEntry, const DynamicAttributeValue* value,
	const DwarfTargetInterface* targetInterface,
	target_addr_t instructionPointer, target_addr_t framePointer,
	BVariant& _result, DIEType** _type)
{
	if (!value->IsValid())
		return B_BAD_VALUE;

	DIEType* dummyType;
	if (_type == NULL)
		_type = &dummyType;

	switch (value->attributeClass) {
		case ATTRIBUTE_CLASS_CONSTANT:
			_result.SetTo(value->constant);
			*_type = NULL;
			return B_OK;

		case ATTRIBUTE_CLASS_REFERENCE:
		{
			// TODO: The specs are a bit fuzzy on this one: "the value is a
			// reference to another entity whose value is the value of the
			// attribute". Supposedly that also means e.g. if the referenced
			// entity is a variable, we should read the value of that variable.
			// ATM we only check for the types that can have a DW_AT_const_value
			// attribute and evaluate it, if present.
			DebugInfoEntry* entry = value->reference;
			if (entry == NULL)
				return B_BAD_VALUE;

			const ConstantAttributeValue* constantValue = NULL;
			DIEType* type = NULL;

			switch (entry->Tag()) {
				case DW_TAG_constant:
				{
					DIEConstant* constantEntry
						= dynamic_cast<DIEConstant*>(entry);
					constantValue = constantEntry->ConstValue();
					type = constantEntry->GetType();
					break;
				}
				case DW_TAG_enumerator:
					constantValue = dynamic_cast<DIEEnumerator*>(entry)
						->ConstValue();
					if (DIEEnumerationType* enumerationType
							= dynamic_cast<DIEEnumerationType*>(
								entry->Parent())) {
						type = enumerationType->GetType();
					}
					break;
				case DW_TAG_formal_parameter:
				{
					DIEFormalParameter* parameterEntry
						= dynamic_cast<DIEFormalParameter*>(entry);
					constantValue = parameterEntry->ConstValue();
					type = parameterEntry->GetType();
					break;
				}
				case DW_TAG_template_value_parameter:
				{
					DIETemplateValueParameter* parameterEntry
						= dynamic_cast<DIETemplateValueParameter*>(entry);
					constantValue = parameterEntry->ConstValue();
					type = parameterEntry->GetType();
					break;
				}
				case DW_TAG_variable:
				{
					DIEVariable* variableEntry
						= dynamic_cast<DIEVariable*>(entry);
					constantValue = variableEntry->ConstValue();
					type = variableEntry->GetType();
					break;
				}
				default:
					return B_BAD_VALUE;
			}

			if (constantValue == NULL || !constantValue->IsValid())
				return B_BAD_VALUE;

			status_t error = EvaluateConstantValue(unit, addressSize,
				subprogramEntry, constantValue, targetInterface,
				instructionPointer, framePointer, _result);
			if (error != B_OK)
				return error;

			*_type = type;
			return B_OK;
		}

		case ATTRIBUTE_CLASS_BLOCK:
		{
			target_addr_t result;
			status_t error = EvaluateExpression(unit, addressSize,
				subprogramEntry, value->block.data, value->block.length,
				targetInterface, instructionPointer, framePointer, 0, false,
				result);
			if (error != B_OK)
				return error;

			_result.SetTo(result);
			*_type = NULL;
			return B_OK;
		}

		default:
			return B_BAD_VALUE;
	}
}


status_t
DwarfFile::_ParseCompilationUnit(CompilationUnit* unit)
{
	AbbreviationTable* abbreviationTable;
	status_t error = _GetAbbreviationTable(unit->AbbreviationOffset(),
		abbreviationTable);
	if (error != B_OK)
		return error;

	unit->SetAbbreviationTable(abbreviationTable);

	DataReader dataReader(
		(const uint8*)fDebugInfoSection->Data() + unit->ContentOffset(),
		unit->ContentSize(), unit->AddressSize());

	DebugInfoEntry* entry;
	bool endOfEntryList;
	error = _ParseDebugInfoEntry(dataReader, abbreviationTable, entry,
		endOfEntryList);
	if (error != B_OK)
		return error;

	DIECompileUnitBase* unitEntry = dynamic_cast<DIECompileUnitBase*>(entry);
	if (unitEntry == NULL) {
		WARNING("No compilation unit entry in .debug_info section.\n");
		return B_BAD_DATA;
	}

	unit->SetUnitEntry(unitEntry);

	TRACE_DIE_ONLY(
		TRACE_DIE("remaining bytes in unit: %" B_PRIdOFF "\n",
			dataReader.BytesRemaining());
		if (dataReader.HasData()) {
			TRACE_DIE("  ");
			while (dataReader.HasData())
				TRACE_DIE("%02x", dataReader.Read<uint8>(0));
			TRACE_DIE("\n");
		}
	)
	return B_OK;
}


status_t
DwarfFile::_ParseDebugInfoEntry(DataReader& dataReader,
	AbbreviationTable* abbreviationTable, DebugInfoEntry*& _entry,
	bool& _endOfEntryList, int level)
{
	off_t entryOffset = dataReader.Offset()
		+ fCurrentCompilationUnit->RelativeContentOffset();

	uint32 code = dataReader.ReadUnsignedLEB128(0);
	if (code == 0) {
		if (dataReader.HasOverflow()) {
			WARNING("Unexpected end of .debug_info section.\n");
			return B_BAD_DATA;
		}
		_entry = NULL;
		_endOfEntryList = true;
		return B_OK;
	}

	// get the corresponding abbreviation entry
	AbbreviationEntry abbreviationEntry;
	if (!abbreviationTable->GetAbbreviationEntry(code, abbreviationEntry)) {
		WARNING("No abbreviation entry for code %" B_PRIu32 "\n", code);
		return B_BAD_DATA;
	}

	DebugInfoEntry* entry;
	status_t error = fDebugInfoFactory.CreateDebugInfoEntry(
		abbreviationEntry.Tag(), entry);
	if (error != B_OK) {
		WARNING("Failed to generate entry for tag %" B_PRIu32 ", code %"
			B_PRIu32 "\n", abbreviationEntry.Tag(), code);
		return error;
	}

	ObjectDeleter<DebugInfoEntry> entryDeleter(entry);

	TRACE_DIE("%*sentry %p at %" B_PRIdOFF ": %" B_PRIu32 ", tag: %s (%"
		B_PRIu32 "), children: %d\n", level * 2, "", entry, entryOffset,
		abbreviationEntry.Code(), get_entry_tag_name(abbreviationEntry.Tag()),
		abbreviationEntry.Tag(), abbreviationEntry.HasChildren());

	error = fCurrentCompilationUnit->AddDebugInfoEntry(entry, entryOffset);
	if (error != B_OK)
		return error;

	// parse the attributes (supply NULL entry to avoid adding them yet)
	error = _ParseEntryAttributes(dataReader, NULL, abbreviationEntry);
	if (error != B_OK)
		return error;

	// parse children, if the entry has any
	if (abbreviationEntry.HasChildren()) {
		while (true) {
			DebugInfoEntry* childEntry;
			bool endOfEntryList;
			status_t error = _ParseDebugInfoEntry(dataReader,
				abbreviationTable, childEntry, endOfEntryList, level + 1);
			if (error != B_OK)
				return error;

			// add the child to our entry
			if (childEntry != NULL) {
				if (entry != NULL) {
					error = entry->AddChild(childEntry);
					if (error == B_OK) {
						childEntry->SetParent(entry);
					} else if (error == ENTRY_NOT_HANDLED) {
						error = B_OK;
						TRACE_DIE("%*s  -> child unhandled\n", level * 2, "");
					}

					if (error != B_OK) {
						delete childEntry;
						return error;
					}
				} else
					delete childEntry;
			}

			if (endOfEntryList)
				break;
		}
	}

	entryDeleter.Detach();
	_entry = entry;
	_endOfEntryList = false;
	return B_OK;
}


status_t
DwarfFile::_FinishCompilationUnit(CompilationUnit* unit)
{
	TRACE_DIE("\nfinishing compilation unit %p\n", unit);

	AbbreviationTable* abbreviationTable = unit->GetAbbreviationTable();

	DataReader dataReader(
		(const uint8*)fDebugInfoSection->Data() + unit->HeaderOffset(),
		unit->TotalSize(), unit->AddressSize());

	DebugInfoEntryInitInfo entryInitInfo;

	int entryCount = unit->CountEntries();
	for (int i = 0; i < entryCount; i++) {
		// get the entry
		DebugInfoEntry* entry;
		off_t offset;
		unit->GetEntryAt(i, entry, offset);

		TRACE_DIE("entry %p at %" B_PRIdOFF "\n", entry, offset);

		// seek the reader to the entry
		dataReader.SeekAbsolute(offset);

		// read the entry code
		uint32 code = dataReader.ReadUnsignedLEB128(0);

		// get the respective abbreviation entry
		AbbreviationEntry abbreviationEntry;
		abbreviationTable->GetAbbreviationEntry(code, abbreviationEntry);

		// initialization before setting the attributes
		status_t error = entry->InitAfterHierarchy(entryInitInfo);
		if (error != B_OK) {
			WARNING("Init after hierarchy failed!\n");
			return error;
		}

		// parse the attributes -- this time pass the entry, so that the
		// attribute get set on it
		error = _ParseEntryAttributes(dataReader, entry, abbreviationEntry);
		if (error != B_OK)
			return error;

		// initialization after setting the attributes
		error = entry->InitAfterAttributes(entryInitInfo);
		if (error != B_OK) {
			WARNING("Init after attributes failed!\n");
			return error;
		}
	}

	// set the compilation unit's source language
	unit->SetSourceLanguage(entryInitInfo.languageInfo);

	// resolve the compilation unit's address range list
	if (TargetAddressRangeList* ranges = ResolveRangeList(unit,
			unit->UnitEntry()->AddressRangesOffset())) {
		unit->SetAddressRanges(ranges);
		ranges->ReleaseReference();
	}

	// add compilation dir to directory list
	const char* compilationDir = unit->UnitEntry()->CompilationDir();
	if (!unit->AddDirectory(compilationDir != NULL ? compilationDir : "."))
		return B_NO_MEMORY;

	// parse line info header
	if (fDebugLineSection != NULL)
		_ParseLineInfo(unit);

	return B_OK;
}


status_t
DwarfFile::_ParseEntryAttributes(DataReader& dataReader,
	DebugInfoEntry* entry, AbbreviationEntry& abbreviationEntry)
{
	uint32 attributeName;
	uint32 attributeForm;
	while (abbreviationEntry.GetNextAttribute(attributeName,
			attributeForm)) {
		// resolve attribute form indirection
		if (attributeForm == DW_FORM_indirect)
			attributeForm = dataReader.ReadUnsignedLEB128(0);

		// prepare an AttributeValue
		AttributeValue attributeValue;
		attributeValue.attributeForm = attributeForm;
		bool isSigned = false;

		// Read the attribute value according to the attribute's form. For
		// the forms that don't map to a single attribute class only or
		// those that need additional processing, we read a temporary value
		// first.
		uint64 value = 0;
		off_t blockLength = 0;
		bool localReference = true;

		switch (attributeForm) {
			case DW_FORM_addr:
				value = dataReader.ReadAddress(0);
				break;
			case DW_FORM_block2:
				blockLength = dataReader.Read<uint16>(0);
				break;
			case DW_FORM_block4:
				blockLength = dataReader.Read<uint32>(0);
				break;
			case DW_FORM_data2:
				value = dataReader.Read<uint16>(0);
				break;
			case DW_FORM_data4:
				value = dataReader.Read<uint32>(0);
				break;
			case DW_FORM_data8:
				value = dataReader.Read<uint64>(0);
				break;
			case DW_FORM_string:
				attributeValue.SetToString(dataReader.ReadString());
				break;
			case DW_FORM_block:
				blockLength = dataReader.ReadUnsignedLEB128(0);
				break;
			case DW_FORM_block1:
				blockLength = dataReader.Read<uint8>(0);
				break;
			case DW_FORM_data1:
				value = dataReader.Read<uint8>(0);
				break;
			case DW_FORM_flag:
				attributeValue.SetToFlag(dataReader.Read<uint8>(0) != 0);
				break;
			case DW_FORM_sdata:
				value = dataReader.ReadSignedLEB128(0);
				isSigned = true;
				break;
			case DW_FORM_strp:
			{
				if (fDebugStringSection != NULL) {
					off_t offset = fCurrentCompilationUnit->IsDwarf64()
						? (off_t)dataReader.Read<uint64>(0)
						: (off_t)dataReader.Read<uint32>(0);
					if (offset >= fDebugStringSection->Size()) {
						WARNING("Invalid DW_FORM_strp offset: %" B_PRIdOFF "\n",
							offset);
						return B_BAD_DATA;
					}
					attributeValue.SetToString(
						(const char*)fDebugStringSection->Data() + offset);
				} else {
					WARNING("Invalid DW_FORM_strp: no string section!\n");
					return B_BAD_DATA;
				}
				break;
			}
			case DW_FORM_udata:
				value = dataReader.ReadUnsignedLEB128(0);
				break;
			case DW_FORM_ref_addr:
				value = fCurrentCompilationUnit->IsDwarf64()
					? dataReader.Read<uint64>(0)
					: (uint64)dataReader.Read<uint32>(0);
				localReference = false;
				break;
			case DW_FORM_ref1:
				value = dataReader.Read<uint8>(0);
				break;
			case DW_FORM_ref2:
				value = dataReader.Read<uint16>(0);
				break;
			case DW_FORM_ref4:
				value = dataReader.Read<uint32>(0);
				break;
			case DW_FORM_ref8:
				value = dataReader.Read<uint64>(0);
				break;
			case DW_FORM_ref_udata:
				value = dataReader.ReadUnsignedLEB128(0);
				break;
			case DW_FORM_indirect:
			default:
				WARNING("Unsupported attribute form: %" B_PRIu32 "\n",
					attributeForm);
				return B_BAD_DATA;
		}

		// get the attribute class -- skip the attribute, if we can't handle
		// it
		uint8 attributeClass = get_attribute_class(attributeName,
			attributeForm);
		if (attributeClass == ATTRIBUTE_CLASS_UNKNOWN) {
			TRACE_DIE("skipping attribute with unrecognized class: %s (%#"
				B_PRIx32 ") %s (%#" B_PRIx32 ")\n",
				get_attribute_name_name(attributeName), attributeName,
				get_attribute_form_name(attributeForm), attributeForm);
			continue;
		}
//		attributeValue.attributeClass = attributeClass;

		// set the attribute value according to the attribute's class
		switch (attributeClass) {
			case ATTRIBUTE_CLASS_ADDRESS:
				attributeValue.SetToAddress(value);
				break;
			case ATTRIBUTE_CLASS_BLOCK:
				attributeValue.SetToBlock(dataReader.Data(), blockLength);
				dataReader.Skip(blockLength);
				break;
			case ATTRIBUTE_CLASS_CONSTANT:
				attributeValue.SetToConstant(value, isSigned);
				break;
			case ATTRIBUTE_CLASS_LINEPTR:
				attributeValue.SetToLinePointer(value);
				break;
			case ATTRIBUTE_CLASS_LOCLISTPTR:
				attributeValue.SetToLocationListPointer(value);
				break;
			case ATTRIBUTE_CLASS_MACPTR:
				attributeValue.SetToMacroPointer(value);
				break;
			case ATTRIBUTE_CLASS_RANGELISTPTR:
				attributeValue.SetToRangeListPointer(value);
				break;
			case ATTRIBUTE_CLASS_REFERENCE:
				if (entry != NULL) {
					attributeValue.SetToReference(_ResolveReference(
						fCurrentCompilationUnit, value, localReference));
					if (attributeValue.reference == NULL) {
						// gcc 2 apparently somtimes produces DW_AT_sibling
						// attributes pointing to the end of the sibling list.
						// Just ignore those.
						if (attributeName == DW_AT_sibling)
							continue;

						WARNING("Failed to resolve reference: %s (%#" B_PRIx32
							") %s (%#" B_PRIx32 "): value: %" B_PRIu64 "\n",
							get_attribute_name_name(attributeName),
							attributeName,
							get_attribute_form_name(attributeForm),
							attributeForm, value);
						return B_ENTRY_NOT_FOUND;
					}
				}
				break;
			case ATTRIBUTE_CLASS_FLAG:
			case ATTRIBUTE_CLASS_STRING:
				// already set
				break;
		}

		if (dataReader.HasOverflow()) {
			WARNING("Unexpected end of .debug_info section.\n");
			return B_BAD_DATA;
		}

		// add the attribute
		if (entry != NULL) {
			TRACE_DIE_ONLY(
				char buffer[1024];
				TRACE_DIE("  attr %s %s (%d): %s\n",
					get_attribute_name_name(attributeName),
					get_attribute_form_name(attributeForm), attributeClass,
					attributeValue.ToString(buffer, sizeof(buffer)));
			)

			DebugInfoEntrySetter attributeSetter
				= get_attribute_name_setter(attributeName);
			if (attributeSetter != 0) {
				status_t error = (entry->*attributeSetter)(attributeName,
					attributeValue);

				if (error == ATTRIBUTE_NOT_HANDLED) {
					error = B_OK;
					TRACE_DIE("    -> unhandled\n");
				}

				if (error != B_OK) {
					WARNING("Failed to set attribute: name: %s, form: %s: %s\n",
						get_attribute_name_name(attributeName),
						get_attribute_form_name(attributeForm),
						strerror(error));
				}
			} else
				TRACE_DIE("    -> no attribute setter!\n");
		}
	}

	return B_OK;
}


status_t
DwarfFile::_ParseLineInfo(CompilationUnit* unit)
{
	off_t offset = unit->UnitEntry()->StatementListOffset();

	TRACE_LINES("DwarfFile::_ParseLineInfo(%p), offset: %" B_PRIdOFF "\n", unit,
		offset);

	DataReader dataReader((uint8*)fDebugLineSection->Data() + offset,
		fDebugLineSection->Size() - offset, unit->AddressSize());

	// unit length
	bool dwarf64;
	uint64 unitLength = dataReader.ReadInitialLength(dwarf64);
	if (unitLength > (uint64)dataReader.BytesRemaining())
		return B_BAD_DATA;
	off_t unitOffset = dataReader.Offset();

	// version (uhalf)
	uint16 version = dataReader.Read<uint16>(0);

	// header_length (4/8)
	uint64 headerLength = dwarf64
		? dataReader.Read<uint64>(0) : (uint64)dataReader.Read<uint32>(0);
	off_t headerOffset = dataReader.Offset();

	if ((uint64)dataReader.BytesRemaining() < headerLength)
		return B_BAD_DATA;

	// minimum instruction length
	uint8 minInstructionLength = dataReader.Read<uint8>(0);

	// default is statement
	bool defaultIsStatement = dataReader.Read<uint8>(0) != 0;

	// line_base (sbyte)
	int8 lineBase = (int8)dataReader.Read<uint8>(0);

	// line_range (ubyte)
	uint8 lineRange = dataReader.Read<uint8>(0);

	// opcode_base (ubyte)
	uint8 opcodeBase = dataReader.Read<uint8>(0);

	// standard_opcode_lengths (ubyte[])
	const uint8* standardOpcodeLengths = (const uint8*)dataReader.Data();
	dataReader.Skip(opcodeBase - 1);

	if (dataReader.HasOverflow())
		return B_BAD_DATA;

	if (version != 2 && version != 3)
		return B_UNSUPPORTED;

	TRACE_LINES("  unitLength:           %" B_PRIu64 "\n", unitLength);
	TRACE_LINES("  version:              %u\n", version);
	TRACE_LINES("  headerLength:         %" B_PRIu64 "\n", headerLength);
	TRACE_LINES("  minInstructionLength: %u\n", minInstructionLength);
	TRACE_LINES("  defaultIsStatement:   %d\n", defaultIsStatement);
	TRACE_LINES("  lineBase:             %d\n", lineBase);
	TRACE_LINES("  lineRange:            %u\n", lineRange);
	TRACE_LINES("  opcodeBase:           %u\n", opcodeBase);

	// include directories
	TRACE_LINES("  include directories:\n");
	while (const char* directory = dataReader.ReadString()) {
		if (*directory == '\0')
			break;
		TRACE_LINES("    \"%s\"\n", directory);

		if (!unit->AddDirectory(directory))
			return B_NO_MEMORY;
	}

	// file names
	TRACE_LINES("  files:\n");
	while (const char* file = dataReader.ReadString()) {
		if (*file == '\0')
			break;
		uint64 dirIndex = dataReader.ReadUnsignedLEB128(0);
		TRACE_LINES_ONLY(uint64 modificationTime =)
			dataReader.ReadUnsignedLEB128(0);
		TRACE_LINES_ONLY(uint64 fileLength =)
			dataReader.ReadUnsignedLEB128(0);

		if (dataReader.HasOverflow())
			return B_BAD_DATA;

		TRACE_LINES("    \"%s\", dir index: %" B_PRIu64 ", mtime: %" B_PRIu64
			", length: %" B_PRIu64 "\n", file, dirIndex, modificationTime,
			fileLength);

		if (!unit->AddFile(file, dirIndex))
			return B_NO_MEMORY;
	}

	off_t readerOffset = dataReader.Offset();
	if ((uint64)readerOffset > readerOffset + headerLength)
		return B_BAD_DATA;
	off_t offsetToProgram = headerOffset + headerLength - readerOffset;

	const uint8* program = (uint8*)dataReader.Data() + offsetToProgram;
	size_t programSize = unitLength - (readerOffset - unitOffset);

	return unit->GetLineNumberProgram().Init(program, programSize,
		minInstructionLength, defaultIsStatement, lineBase, lineRange,
			opcodeBase, standardOpcodeLengths);
}


status_t
DwarfFile::_UnwindCallFrame(bool usingEHFrameSection, CompilationUnit* unit,
	uint8 addressSize, DIESubprogram* subprogramEntry, target_addr_t location,
	const DwarfTargetInterface* inputInterface,
	DwarfTargetInterface* outputInterface, target_addr_t& _framePointer)
{
	ElfSection* currentFrameSection = (usingEHFrameSection)
		? fEHFrameSection : fDebugFrameSection;

	if (currentFrameSection == NULL)
		return B_ENTRY_NOT_FOUND;

	bool gcc4EHFrameSection = false;
	if (usingEHFrameSection) {
		gcc4EHFrameSection = !currentFrameSection->IsWritable();
			// Crude heuristic for recognizing GCC 4 (Itanium ABI) style
			// .eh_frame sections. The ones generated by GCC 2 are writable,
			// the ones generated by GCC 4 aren't.
	}

	TRACE_CFI("DwarfFile::_UnwindCallFrame(%#" B_PRIx64 ")\n", location);

	DataReader dataReader((uint8*)currentFrameSection->Data(),
		currentFrameSection->Size(), unit != NULL
			? unit->AddressSize() : addressSize);

	while (dataReader.BytesRemaining() > 0) {
		// length
		bool dwarf64;
		TRACE_CFI_ONLY(off_t entryOffset = dataReader.Offset();)
		uint64 length = dataReader.ReadInitialLength(dwarf64);

		TRACE_CFI("DwarfFile::_UnwindCallFrame(): offset: %" B_PRIdOFF
			", length: %" B_PRId64 "\n", entryOffset, length);

		if (length > (uint64)dataReader.BytesRemaining())
			return B_BAD_DATA;
		off_t lengthOffset = dataReader.Offset();

		// CIE ID/CIE pointer
		uint64 cieID = dwarf64
			? dataReader.Read<uint64>(0) : dataReader.Read<uint32>(0);

		// In .debug_frame ~0 indicates a CIE, in .eh_frame 0 does.
		if (usingEHFrameSection
			? cieID == 0
			: (dwarf64
				? cieID == 0xffffffffffffffffULL
				: cieID == 0xffffffff)) {
			// this is a CIE -- skip it
		} else {
			// this is a FDE
			uint64 initialLocationOffset = dataReader.Offset();
			// In .eh_frame the CIE offset is a relative back offset.
			if (usingEHFrameSection) {
				if (cieID > (uint64)lengthOffset) {
					TRACE_CFI("Invalid CIE offset: %" B_PRIu64 ", max "
						"possible: %" B_PRIu64 "\n", cieID, lengthOffset);
					break;
				}
				// convert to a section relative offset
				cieID = lengthOffset - cieID;
			}


			CfaContext context;
			CIEAugmentation cieAugmentation;
			// when using .eh_frame format, we need to parse the CIE's
			// augmentation up front in order to know how the FDE's addresses
			//  will be represented
			DataReader cieReader;
			off_t cieRemaining;
			status_t error = _ParseCIEHeader(currentFrameSection,
				usingEHFrameSection, unit, addressSize, context, cieID,
				cieAugmentation, cieReader, cieRemaining);
			if (error != B_OK)
				return error;
			if (cieReader.HasOverflow())
				return B_BAD_DATA;
			if (cieRemaining < 0)
				return B_BAD_DATA;

			target_addr_t initialLocation = cieAugmentation.ReadEncodedAddress(
				dataReader, fElfFile, currentFrameSection);
			target_addr_t addressRange = cieAugmentation.ReadEncodedAddress(
				dataReader, fElfFile, currentFrameSection, true);

			if (dataReader.HasOverflow())
				return B_BAD_DATA;

			if ((cieAugmentation.FDEAddressType()
					& CFI_ADDRESS_TYPE_PC_RELATIVE) != 0) {
				initialLocation += initialLocationOffset;
			}

			// TODO: For GCC 2 .eh_frame sections things work differently: The
			// initial locations are relocated by the runtime loader and
			// afterwards point to the absolute addresses. Fortunately the
			// relocations that always seem to be used are R_386_RELATIVE, so
			// that the value we read from the file is already absolute
			// (assuming an unchanged segment load address).

			TRACE_CFI("location: %" B_PRIx64 ", initial location: %" B_PRIx64
				", address range: %" B_PRIx64 "\n", location, initialLocation,
				addressRange);

			if (location >= initialLocation
				&& location < initialLocation + addressRange) {
				// This is the FDE we're looking for.
				off_t remaining = lengthOffset + length
					- dataReader.Offset();
				if (remaining < 0)
					return B_BAD_DATA;

				TRACE_CFI("  found fde: length: %" B_PRIu64 " (%" B_PRIdOFF
					"), CIE offset: %#" B_PRIx64 ", location: %#" B_PRIx64 ", "
					"range: %#" B_PRIx64 "\n", length, remaining, cieID,
					initialLocation, addressRange);

				context.SetLocation(location, initialLocation);
				uint32 registerCount = outputInterface->CountRegisters();
				error = context.Init(registerCount);
				if (error != B_OK)
					return error;

				error = outputInterface->InitRegisterRules(context);
				if (error != B_OK)
					return error;

				// process the CIE's frame info instructions
				cieReader = cieReader.RestrictedReader(cieRemaining);
				error = _ParseFrameInfoInstructions(unit, context,
					cieReader, cieAugmentation);
				if (error != B_OK)
					return error;

				// read the FDE augmentation data (if any)
				FDEAugmentation fdeAugmentation;
				error = cieAugmentation.ReadFDEData(dataReader,
					fdeAugmentation);
				if (error != B_OK) {
					TRACE_CFI("  failed to read FDE augmentation data!\n");
					return error;
				}
				// adjust remaining byte count to take augmentation bytes
				// (if any) into account.
				remaining = lengthOffset + length
					- dataReader.Offset();

				error = context.SaveInitialRuleSet();
				if (error != B_OK)
					return error;

				DataReader restrictedReader =
					dataReader.RestrictedReader(remaining);
				error = _ParseFrameInfoInstructions(unit, context,
					restrictedReader, cieAugmentation);
				if (error != B_OK)
					return error;

				TRACE_CFI("  found row!\n");

				// apply the rules of the final row
				// get the frameAddress first
				target_addr_t frameAddress;
				CfaCfaRule* cfaCfaRule = context.GetCfaCfaRule();
				switch (cfaCfaRule->Type()) {
					case CFA_CFA_RULE_REGISTER_OFFSET:
					{
						BVariant value;
						if (!inputInterface->GetRegisterValue(
								cfaCfaRule->Register(), value)
							|| !value.IsNumber()) {
							return B_UNSUPPORTED;
						}
						frameAddress = value.ToUInt64() + cfaCfaRule->Offset();
						break;
					}
					case CFA_CFA_RULE_EXPRESSION:
					{
						error = EvaluateExpression(unit, addressSize,
							subprogramEntry,
							cfaCfaRule->Expression().block,
							cfaCfaRule->Expression().size,
							inputInterface, location, 0, 0, false,
							frameAddress);
						if (error != B_OK)
							return error;
						break;
					}
					case CFA_CFA_RULE_UNDEFINED:
					default:
						return B_BAD_VALUE;
				}

				TRACE_CFI("  frame address: %#" B_PRIx64 "\n", frameAddress);

				// apply the register rules
				for (uint32 i = 0; i < registerCount; i++) {
					TRACE_CFI("  reg %" B_PRIu32 "\n", i);

					uint32 valueType = outputInterface->RegisterValueType(i);
					if (valueType == 0)
						continue;

					CfaRule* rule = context.RegisterRule(i);
					if (rule == NULL)
						continue;

					// apply the rule
					switch (rule->Type()) {
						case CFA_RULE_SAME_VALUE:
						{
							TRACE_CFI("  -> CFA_RULE_SAME_VALUE\n");

							BVariant value;
							if (inputInterface->GetRegisterValue(i, value))
								outputInterface->SetRegisterValue(i, value);
							break;
						}
						case CFA_RULE_LOCATION_OFFSET:
						{
							TRACE_CFI("  -> CFA_RULE_LOCATION_OFFSET: %"
								B_PRId64 "\n", rule->Offset());

							BVariant value;
							if (inputInterface->ReadValueFromMemory(
									frameAddress + rule->Offset(), valueType,
									value)) {
								outputInterface->SetRegisterValue(i, value);
							}
							break;
						}
						case CFA_RULE_VALUE_OFFSET:
							TRACE_CFI("  -> CFA_RULE_VALUE_OFFSET\n");

							outputInterface->SetRegisterValue(i,
								frameAddress + rule->Offset());
							break;
						case CFA_RULE_REGISTER:
						{
							TRACE_CFI("  -> CFA_RULE_REGISTER\n");

							BVariant value;
							if (inputInterface->GetRegisterValue(
									rule->Register(), value)) {
								outputInterface->SetRegisterValue(i, value);
							}
							break;
						}
						case CFA_RULE_LOCATION_EXPRESSION:
						{
							TRACE_CFI("  -> CFA_RULE_LOCATION_EXPRESSION\n");

							target_addr_t address;
							error = EvaluateExpression(unit, addressSize,
								subprogramEntry,
								rule->Expression().block,
								rule->Expression().size,
								inputInterface, location, frameAddress,
								frameAddress, true, address);
							BVariant value;
							if (error == B_OK
								&& inputInterface->ReadValueFromMemory(address,
									valueType, value)) {
								outputInterface->SetRegisterValue(i, value);
							}
							break;
						}
						case CFA_RULE_VALUE_EXPRESSION:
						{
							TRACE_CFI("  -> CFA_RULE_VALUE_EXPRESSION\n");

							target_addr_t value;
							error = EvaluateExpression(unit, addressSize,
								subprogramEntry,
								rule->Expression().block,
								rule->Expression().size,
								inputInterface, location, frameAddress,
								frameAddress, true, value);
							if (error == B_OK)
								outputInterface->SetRegisterValue(i, value);
							break;
						}
						case CFA_RULE_UNDEFINED:
							TRACE_CFI("  -> CFA_RULE_UNDEFINED\n");
						default:
							break;
					}
				}

				_framePointer = frameAddress;
				return B_OK;
			}
		}

		dataReader.SeekAbsolute(lengthOffset + length);
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
DwarfFile::_ParseCIEHeader(ElfSection* debugFrameSection,
	bool usingEHFrameSection, CompilationUnit* unit, uint8 addressSize,
	CfaContext& context, off_t cieOffset, CIEAugmentation& cieAugmentation,
	DataReader& dataReader, off_t& _cieRemaining)
{
	if (cieOffset < 0 || cieOffset >= debugFrameSection->Size())
		return B_BAD_DATA;

	dataReader.SetTo((uint8*)debugFrameSection->Data() + cieOffset,
		debugFrameSection->Size() - cieOffset, unit != NULL
			? unit->AddressSize() : addressSize);

	// length
	bool dwarf64;
	uint64 length = dataReader.ReadInitialLength(dwarf64);
	if (length > (uint64)dataReader.BytesRemaining())
		return B_BAD_DATA;

	off_t lengthOffset = dataReader.Offset();

	// CIE ID/CIE pointer
	uint64 cieID = dwarf64
		? dataReader.Read<uint64>(0) : dataReader.Read<uint32>(0);
	if (usingEHFrameSection) {
		if (cieID != 0)
			return B_BAD_DATA;
	} else {
		if (dwarf64 ? cieID != 0xffffffffffffffffULL : cieID != 0xffffffff)
			return B_BAD_DATA;
	}

	uint8 version = dataReader.Read<uint8>(0);
	if (version != 1) {
		TRACE_CFI("  cie: length: %" B_PRIu64 ", offset: %#" B_PRIx64 ", "
			"version: %u -- unsupported\n",	length, (uint64)cieOffset, version);
		return B_UNSUPPORTED;
	}

	// read the augmentation string
	cieAugmentation.Init(dataReader);

	// in the cause of augmentation string "eh",
	// the exception table pointer is located immediately before the
	// code/data alignment values. We have no use for it so simply skip.
	if (strcmp(cieAugmentation.String(), "eh") == 0)
		dataReader.Skip(dwarf64 ? sizeof(uint64) : sizeof(uint32));

	context.SetCodeAlignment(dataReader.ReadUnsignedLEB128(0));
	context.SetDataAlignment(dataReader.ReadSignedLEB128(0));
	context.SetReturnAddressRegister(dataReader.ReadUnsignedLEB128(0));

	TRACE_CFI("  cie: length: %" B_PRIu64 ", offset: %#" B_PRIx64 ", version: "
		"%u, augmentation: \"%s\", aligment: code: %" B_PRIu32 ", data: %"
		B_PRId32 ", return address reg: %" B_PRIu32 "\n", length,
		(uint64)cieOffset, version, cieAugmentation.String(),
		context.CodeAlignment(), context.DataAlignment(),
		context.ReturnAddressRegister());

	status_t error = cieAugmentation.Read(dataReader);
	if (error != B_OK) {
		TRACE_CFI("  cie: length: %" B_PRIu64 ", version: %u, augmentation: "
			"\"%s\" -- unsupported\n", length, version,
			cieAugmentation.String());
		return error;
	}

	if (dataReader.HasOverflow())
		return B_BAD_DATA;

	_cieRemaining = length -(dataReader.Offset() - lengthOffset);
	if (_cieRemaining < 0)
		return B_BAD_DATA;

	return B_OK;
}


status_t
DwarfFile::_ParseFrameInfoInstructions(CompilationUnit* unit,
	CfaContext& context, DataReader& dataReader, CIEAugmentation& augmentation)
{
	while (dataReader.BytesRemaining() > 0) {
		TRACE_CFI("    [%2" B_PRId64 "]", dataReader.BytesRemaining());

		uint8 opcode = dataReader.Read<uint8>(0);
		if ((opcode >> 6) != 0) {
			uint32 operand = opcode & 0x3f;

			switch (opcode >> 6) {
				case DW_CFA_advance_loc:
				{
					TRACE_CFI("    DW_CFA_advance_loc: %#" B_PRIx32 "\n",
						operand);

					target_addr_t location = context.Location()
						+ operand * context.CodeAlignment();
					if (location > context.TargetLocation())
						return B_OK;
					context.SetLocation(location);
					break;
				}
				case DW_CFA_offset:
				{
					uint64 offset = dataReader.ReadUnsignedLEB128(0);
					TRACE_CFI("    DW_CFA_offset: reg: %" B_PRIu32 ", offset: "
						"%" B_PRIu64 "\n", operand, offset);

					if (CfaRule* rule = context.RegisterRule(operand)) {
						rule->SetToLocationOffset(
							offset * context.DataAlignment());
					}
					break;
				}
				case DW_CFA_restore:
				{
					TRACE_CFI("    DW_CFA_restore: %#" B_PRIx32 "\n", operand);

					context.RestoreRegisterRule(operand);
					break;
				}
			}
		} else {
			switch (opcode) {
				case DW_CFA_nop:
				{
					TRACE_CFI("    DW_CFA_nop\n");
					break;
				}
				case DW_CFA_set_loc:
				{
					target_addr_t location = augmentation.ReadEncodedAddress(
							dataReader, fElfFile, fDebugFrameSection);

					TRACE_CFI("    DW_CFA_set_loc: %#" B_PRIx64 "\n", location);

					if (location < context.Location())
						return B_BAD_VALUE;
					if (location > context.TargetLocation())
						return B_OK;
					context.SetLocation(location);
					break;
				}
				case DW_CFA_advance_loc1:
				{
					uint32 delta = dataReader.Read<uint8>(0);

					TRACE_CFI("    DW_CFA_advance_loc1: %#" B_PRIx32 "\n",
						delta);

					target_addr_t location = context.Location()
						+ delta * context.CodeAlignment();
					if (location > context.TargetLocation())
						return B_OK;
					context.SetLocation(location);
					break;
				}
				case DW_CFA_advance_loc2:
				{
					uint32 delta = dataReader.Read<uint16>(0);

					TRACE_CFI("    DW_CFA_advance_loc2: %#" B_PRIx32 "\n",
						delta);

					target_addr_t location = context.Location()
						+ delta * context.CodeAlignment();
					if (location > context.TargetLocation())
						return B_OK;
					context.SetLocation(location);
					break;
				}
				case DW_CFA_advance_loc4:
				{
					uint32 delta = dataReader.Read<uint32>(0);

					TRACE_CFI("    DW_CFA_advance_loc4: %#" B_PRIx32 "\n",
						delta);

					target_addr_t location = context.Location()
						+ delta * context.CodeAlignment();
					if (location > context.TargetLocation())
						return B_OK;
					context.SetLocation(location);
					break;
				}
				case DW_CFA_offset_extended:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);
					uint64 offset = dataReader.ReadUnsignedLEB128(0);

					TRACE_CFI("    DW_CFA_offset_extended: reg: %" B_PRIu32 ", "
						"offset: %" B_PRIu64 "\n", reg, offset);

					if (CfaRule* rule = context.RegisterRule(reg)) {
						rule->SetToLocationOffset(
							offset * context.DataAlignment());
					}
					break;
				}
				case DW_CFA_restore_extended:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);

					TRACE_CFI("    DW_CFA_restore_extended: %#" B_PRIx32 "\n",
						reg);

					context.RestoreRegisterRule(reg);
					break;
				}
				case DW_CFA_undefined:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);

					TRACE_CFI("    DW_CFA_undefined: %" B_PRIu32 "\n", reg);

					if (CfaRule* rule = context.RegisterRule(reg))
						rule->SetToUndefined();
					break;
				}
				case DW_CFA_same_value:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);

					TRACE_CFI("    DW_CFA_same_value: %" B_PRIu32 "\n", reg);

					if (CfaRule* rule = context.RegisterRule(reg))
						rule->SetToSameValue();
					break;
				}
				case DW_CFA_register:
				{
					uint32 reg1 = dataReader.ReadUnsignedLEB128(0);
					uint32 reg2 = dataReader.ReadUnsignedLEB128(0);

					TRACE_CFI("    DW_CFA_register: reg1: %" B_PRIu32 ", reg2: "
						"%" B_PRIu32 "\n", reg1, reg2);

					if (CfaRule* rule = context.RegisterRule(reg1))
						rule->SetToValueOffset(reg2);
					break;
				}
				case DW_CFA_remember_state:
				{
					TRACE_CFI("    DW_CFA_remember_state\n");

					status_t error = context.PushRuleSet();
					if (error != B_OK)
						return error;
					break;
				}
				case DW_CFA_restore_state:
				{
					TRACE_CFI("    DW_CFA_restore_state\n");

					status_t error = context.PopRuleSet();
					if (error != B_OK)
						return error;
					break;
				}
				case DW_CFA_def_cfa:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);
					uint64 offset = dataReader.ReadUnsignedLEB128(0);

					TRACE_CFI("    DW_CFA_def_cfa: reg: %" B_PRIu32 ", offset: "
						"%" B_PRIu64 "\n", reg, offset);

					context.GetCfaCfaRule()->SetToRegisterOffset(reg, offset);
					break;
				}
				case DW_CFA_def_cfa_register:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);

					TRACE_CFI("    DW_CFA_def_cfa_register: %" B_PRIu32 "\n",
						reg);

					if (context.GetCfaCfaRule()->Type()
							!= CFA_CFA_RULE_REGISTER_OFFSET) {
						return B_BAD_DATA;
					}
					context.GetCfaCfaRule()->SetRegister(reg);
					break;
				}
				case DW_CFA_def_cfa_offset:
				{
					uint64 offset = dataReader.ReadUnsignedLEB128(0);

					TRACE_CFI("    DW_CFA_def_cfa_offset: %" B_PRIu64 "\n",
						offset);

					if (context.GetCfaCfaRule()->Type()
							!= CFA_CFA_RULE_REGISTER_OFFSET) {
						return B_BAD_DATA;
					}
					context.GetCfaCfaRule()->SetOffset(offset);
					break;
				}
				case DW_CFA_def_cfa_expression:
				{
					uint64 blockLength = dataReader.ReadUnsignedLEB128(0);
					uint8* block = (uint8*)dataReader.Data();
					dataReader.Skip(blockLength);

					TRACE_CFI("    DW_CFA_def_cfa_expression: %p, %" B_PRIu64
						"\n", block, blockLength);

					context.GetCfaCfaRule()->SetToExpression(block,
						blockLength);
					break;
				}
				case DW_CFA_expression:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);
					uint64 blockLength = dataReader.ReadUnsignedLEB128(0);
					uint8* block = (uint8*)dataReader.Data();
					dataReader.Skip(blockLength);

					TRACE_CFI("    DW_CFA_expression: reg: %" B_PRIu32 ", "
						"block: %p, %" B_PRIu64 "\n", reg, block, blockLength);

					if (CfaRule* rule = context.RegisterRule(reg))
						rule->SetToLocationExpression(block, blockLength);
					break;
				}
				case DW_CFA_offset_extended_sf:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);
					int64 offset = dataReader.ReadSignedLEB128(0);

					TRACE_CFI("    DW_CFA_offset_extended: reg: %" B_PRIu32 ", "
						"offset: %" B_PRId64 "\n", reg, offset);

					if (CfaRule* rule = context.RegisterRule(reg)) {
						rule->SetToLocationOffset(
							offset * (int32)context.DataAlignment());
					}
					break;
				}
				case DW_CFA_def_cfa_sf:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);
					int64 offset = dataReader.ReadSignedLEB128(0);

					TRACE_CFI("    DW_CFA_def_cfa_sf: reg: %" B_PRIu32 ", "
						"offset: %" B_PRId64 "\n", reg, offset);

					context.GetCfaCfaRule()->SetToRegisterOffset(reg,
						offset * (int32)context.DataAlignment());
					break;
				}
				case DW_CFA_def_cfa_offset_sf:
				{
					int64 offset = dataReader.ReadSignedLEB128(0);

					TRACE_CFI("    DW_CFA_def_cfa_offset: %" B_PRId64 "\n",
						offset);

					if (context.GetCfaCfaRule()->Type()
							!= CFA_CFA_RULE_REGISTER_OFFSET) {
						return B_BAD_DATA;
					}
					context.GetCfaCfaRule()->SetOffset(
						offset * (int32)context.DataAlignment());
					break;
				}
				case DW_CFA_val_offset:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);
					uint64 offset = dataReader.ReadUnsignedLEB128(0);

					TRACE_CFI("    DW_CFA_val_offset: reg: %" B_PRIu32 ", "
						"offset: %" B_PRIu64 "\n", reg, offset);

					if (CfaRule* rule = context.RegisterRule(reg)) {
						rule->SetToValueOffset(
							offset * context.DataAlignment());
					}
					break;
				}
				case DW_CFA_val_offset_sf:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);
					int64 offset = dataReader.ReadSignedLEB128(0);

					TRACE_CFI("    DW_CFA_val_offset_sf: reg: %" B_PRIu32 ", "
						"offset: %" B_PRId64 "\n", reg, offset);

					if (CfaRule* rule = context.RegisterRule(reg)) {
						rule->SetToValueOffset(
							offset * (int32)context.DataAlignment());
					}
					break;
				}
				case DW_CFA_val_expression:
				{
					uint32 reg = dataReader.ReadUnsignedLEB128(0);
					uint64 blockLength = dataReader.ReadUnsignedLEB128(0);
					uint8* block = (uint8*)dataReader.Data();
					dataReader.Skip(blockLength);

					TRACE_CFI("    DW_CFA_val_expression: reg: %" B_PRIu32 ", "
						"block: %p, %" B_PRIu64 "\n", reg, block, blockLength);

					if (CfaRule* rule = context.RegisterRule(reg))
						rule->SetToValueExpression(block, blockLength);
					break;
				}

				// extensions
				case DW_CFA_MIPS_advance_loc8:
				{
					uint64 delta = dataReader.Read<uint64>(0);

					TRACE_CFI("    DW_CFA_MIPS_advance_loc8: %#" B_PRIx64 "\n",
						delta);

					target_addr_t location = context.Location()
						+ delta * context.CodeAlignment();
					if (location > context.TargetLocation())
						return B_OK;
					context.SetLocation(location);
					break;
				}
				case DW_CFA_GNU_window_save:
				{
					// SPARC specific, no args
					TRACE_CFI("    DW_CFA_GNU_window_save\n");

					// TODO: Implement once we have SPARC support!
					break;
				}
				case DW_CFA_GNU_args_size:
				{
					// Updates the total size of arguments on the stack.
					TRACE_CFI_ONLY(uint64 size =)
						dataReader.ReadUnsignedLEB128(0);

					TRACE_CFI("    DW_CFA_GNU_args_size: %" B_PRIu64 "\n",
						size);
// TODO: Implement!
					break;
				}
				case DW_CFA_GNU_negative_offset_extended:
				{
					// obsolete
					uint32 reg = dataReader.ReadUnsignedLEB128(0);
					int64 offset = dataReader.ReadSignedLEB128(0);

					TRACE_CFI("    DW_CFA_GNU_negative_offset_extended: "
						"reg: %" B_PRIu32 ", offset: %" B_PRId64 "\n", reg,
						offset);

					if (CfaRule* rule = context.RegisterRule(reg)) {
						rule->SetToLocationOffset(
							offset * (int32)context.DataAlignment());
					}
					break;
				}

				default:
					WARNING("    unknown opcode %u!\n", opcode);
					return B_BAD_DATA;
			}
		}
	}

	return B_OK;
}


status_t
DwarfFile::_ParsePublicTypesInfo()
{
	TRACE_PUBTYPES("DwarfFile::_ParsePublicTypesInfo()\n");
	if (fDebugPublicTypesSection == NULL) {
		TRACE_PUBTYPES("  -> no public types section\n");
		return B_ENTRY_NOT_FOUND;
	}

	DataReader dataReader((uint8*)fDebugPublicTypesSection->Data(),
		fDebugPublicTypesSection->Size(), 4);
		// address size doesn't matter at this point

	while (dataReader.BytesRemaining() > 0) {
		bool dwarf64;
		uint64 unitLength = dataReader.ReadInitialLength(dwarf64);

		off_t unitLengthOffset = dataReader.Offset();
			// the unitLength starts here

		if (dataReader.HasOverflow())
			return B_BAD_DATA;

		if (unitLengthOffset + unitLength
				> (uint64)fDebugPublicTypesSection->Size()) {
			WARNING("Invalid public types set unit length.\n");
			break;
		}

		DataReader unitDataReader(dataReader.Data(), unitLength, 4);
			// address size doesn't matter
		_ParsePublicTypesInfo(unitDataReader, dwarf64);

		dataReader.SeekAbsolute(unitLengthOffset + unitLength);
	}

	return B_OK;
}


status_t
DwarfFile::_ParsePublicTypesInfo(DataReader& dataReader, bool dwarf64)
{
	int version = dataReader.Read<uint16>(0);
	if (version != 2) {
		TRACE_PUBTYPES("  pubtypes version %d unsupported\n", version);
		return B_UNSUPPORTED;
	}

	TRACE_PUBTYPES_ONLY(off_t debugInfoOffset =) dwarf64
		? dataReader.Read<uint64>(0)
		: (uint64)dataReader.Read<uint32>(0);
	TRACE_PUBTYPES_ONLY(off_t debugInfoSize =) dwarf64
		? dataReader.Read<uint64>(0)
		: (uint64)dataReader.Read<uint32>(0);

	if (dataReader.HasOverflow())
		return B_BAD_DATA;

	TRACE_PUBTYPES("DwarfFile::_ParsePublicTypesInfo(): compilation unit debug "
		"info: (%" B_PRIdOFF ", %" B_PRIdOFF ")\n", debugInfoOffset,
		debugInfoSize);

	while (dataReader.BytesRemaining() > 0) {
		off_t entryOffset = dwarf64
			? dataReader.Read<uint64>(0)
			: (uint64)dataReader.Read<uint32>(0);
		if (entryOffset == 0)
			return B_OK;

		TRACE_PUBTYPES_ONLY(const char* name =) dataReader.ReadString();

		TRACE_PUBTYPES("  \"%s\" -> %" B_PRIdOFF "\n", name, entryOffset);
	}

	return B_OK;
}


status_t
DwarfFile::_GetAbbreviationTable(off_t offset, AbbreviationTable*& _table)
{
	// check, whether we've already loaded it
	for (AbbreviationTableList::Iterator it
				= fAbbreviationTables.GetIterator();
			AbbreviationTable* table = it.Next();) {
		if (offset == table->Offset()) {
			_table = table;
			return B_OK;
		}
	}

	// create a new table
	AbbreviationTable* table = new(std::nothrow) AbbreviationTable(offset);
	if (table == NULL)
		return B_NO_MEMORY;

	status_t error = table->Init(fDebugAbbrevSection->Data(),
		fDebugAbbrevSection->Size());
	if (error != B_OK) {
		delete table;
		return error;
	}

	fAbbreviationTables.Add(table);
	_table = table;
	return B_OK;
}


DebugInfoEntry*
DwarfFile::_ResolveReference(CompilationUnit* unit, uint64 offset,
	bool localReference) const
{
	if (localReference)
		return unit->EntryForOffset(offset);

	// TODO: Implement program-global references!
	return NULL;
}


status_t
DwarfFile::_GetLocationExpression(CompilationUnit* unit,
	const LocationDescription* location, target_addr_t instructionPointer,
	const void*& _expression, off_t& _length) const
{
	if (!location->IsValid())
		return B_BAD_VALUE;

	if (location->IsExpression()) {
		_expression = location->expression.data;
		_length = location->expression.length;
		return B_OK;
	}

	if (location->IsLocationList() && instructionPointer != 0) {
		return _FindLocationExpression(unit, location->listOffset,
			instructionPointer, _expression, _length);
	}

	return B_BAD_VALUE;
}


status_t
DwarfFile::_FindLocationExpression(CompilationUnit* unit, uint64 offset,
	target_addr_t address, const void*& _expression, off_t& _length) const
{
	if (unit == NULL)
		return B_BAD_VALUE;

	if (fDebugLocationSection == NULL)
		return B_ENTRY_NOT_FOUND;

	if (offset < 0 || offset >= (uint64)fDebugLocationSection->Size())
		return B_BAD_DATA;

	target_addr_t baseAddress = unit->AddressRangeBase();
	target_addr_t maxAddress = unit->MaxAddress();

	DataReader dataReader((uint8*)fDebugLocationSection->Data() + offset,
		fDebugLocationSection->Size() - offset, unit->AddressSize());
	while (true) {
		target_addr_t start = dataReader.ReadAddress(0);
		target_addr_t end = dataReader.ReadAddress(0);
		if (dataReader.HasOverflow())
			return B_BAD_DATA;

		if (start == 0 && end == 0)
			return B_ENTRY_NOT_FOUND;

		if (start == maxAddress) {
			baseAddress = end;
			continue;
		}

		uint16 expressionLength = dataReader.Read<uint16>(0);
		const void* expression = dataReader.Data();
		if (!dataReader.Skip(expressionLength))
			return B_BAD_DATA;

		if (start == end)
			continue;

		start += baseAddress;
		end += baseAddress;

		if (address >= start && address < end) {
			_expression = expression;
			_length = expressionLength;
			return B_OK;
		}
	}
}


status_t
DwarfFile::_LocateDebugInfo()
{
	ElfFile* debugInfoFile = fElfFile;
	ElfSection* debugLinkSection = fElfFile->GetSection(".gnu_debuglink");
	if (debugLinkSection != NULL) {
		AutoSectionPutter putter(fElfFile, debugLinkSection);

		// the file specifies a debug link, look at its target instead
		// for debug information.
		// Format: null-terminated filename, as many 0 padding bytes as
		// needed to reach the next 32-bit address boundary, followed
		// by a 32-bit CRC

		BString debugPath;
		status_t result = _GetDebugInfoPath(
			(const char*)debugLinkSection->Data(), debugPath);
		if (result != B_OK)
			return result;

		fAlternateName = strdup(debugPath.String());

		if (fAlternateName == NULL)
			return B_NO_MEMORY;

/*
		// TODO: validate CRC
		int32 debugCRC = *(int32*)((char*)debugLinkSection->Data()
			+ debugLinkSection->Size() - sizeof(int32));
*/
		fAlternateElfFile = new(std::nothrow) ElfFile;
		if (fAlternateElfFile == NULL)
			return B_NO_MEMORY;

		result = fAlternateElfFile->Init(fAlternateName);
		if (result != B_OK)
			return result;

		debugInfoFile = fAlternateElfFile;
	}

	// get the interesting sections
	fDebugInfoSection = debugInfoFile->GetSection(".debug_info");
	fDebugAbbrevSection = debugInfoFile->GetSection(".debug_abbrev");
	if (fDebugInfoSection == NULL || fDebugAbbrevSection == NULL) {
		WARNING("DwarfManager::File::Load(\"%s\"): no "
			".debug_info or .debug_abbrev.\n", fName);

		// if we at least have an EH frame, use that for stack unwinding
		// if nothing else.
		fEHFrameSection = fElfFile->GetSection(".eh_frame");
		if (fEHFrameSection == NULL)
			return B_ERROR;
	}

	return B_OK;
}


status_t
DwarfFile::_GetDebugInfoPath(const char* debugFileName, BString& _infoPath)
{
	const directory_which dirLocations[] = { B_USER_CONFIG_DIRECTORY,
		B_COMMON_DIRECTORY, B_SYSTEM_DIRECTORY };

	// first, see if we have a relative match to our local directory
	BPath basePath;
	status_t result = basePath.SetTo(fName);
	if (result != B_OK)
		return result;
	basePath.GetParent(&basePath);
	if (strcmp(basePath.Leaf(), "lib") == 0 || strcmp(basePath.Leaf(),
			"add-ons") == 0) {
		_infoPath.SetToFormat("%s/../debug/%s", basePath.Path(),
			debugFileName);
	} else
		_infoPath.SetToFormat("%s/debug/%s", basePath.Path(), debugFileName);

	BEntry entry(_infoPath.String());
	result = entry.InitCheck();
	if (result != B_OK && result != B_ENTRY_NOT_FOUND)
		return result;
	if (entry.Exists())
		return B_OK;

	// See if our image is in any of the system locations.
	// if so, look for its debug info in the corresponding location.
	for (uint16 i = 0; i < sizeof(dirLocations) / sizeof(directory_which);
		i++) {
		result = find_directory(dirLocations[i], &basePath);
		if (result != B_OK)
			return result;

		if (strncmp(fName, basePath.Path(), strlen(basePath.Path())) == 0) {
			_infoPath.SetToFormat("%s/develop/debug/%s", basePath.Path(),
				debugFileName);
			entry.SetTo(_infoPath.String());
			result = entry.InitCheck();
			if (result != B_OK && result != B_ENTRY_NOT_FOUND)
				return result;
			return entry.Exists() ? B_OK : B_ENTRY_NOT_FOUND;
		}
	}

	return B_ENTRY_NOT_FOUND;
}

