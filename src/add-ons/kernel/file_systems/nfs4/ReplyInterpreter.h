/*
 * Copyright 2012 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, pdziepak@quarnos.org
 */
#ifndef REPLYINTERPRETER_H
#define REPLYINTERPRETER_H


#include <SupportDefs.h>

#include "FileInfo.h"
#include "NFS4Defs.h"
#include "RPCReply.h"


struct FSLocation {
	const char**		fRootPath;

	const char**		fLocations;
	uint32				fCount;

						~FSLocation();
};

struct FSLocations {
	const char**		fRootPath;

	FSLocation*			fLocations;
	uint32				fCount;

						~FSLocations();
};

struct AttrValue {
			AttrValue();
			~AttrValue();

	uint8	fAttribute;
	bool	fFreePointer;
	union {
			uint32			fValue32;
			uint64			fValue64;
			void*			fPointer;
			FSLocations*	fLocations;
	} fData;
};

struct DirEntry {
	const char*			fName;
	AttrValue*			fAttrs;
	uint32				fAttrCount;

						DirEntry();
						~DirEntry();
};

class LockInfo;

class ReplyInterpreter {
public:
						ReplyInterpreter(RPC::Reply* reply = NULL);
						~ReplyInterpreter();

	inline	status_t	SetTo(RPC::Reply* reply);
	inline	void		Reset();

	inline	uint32		NFS4Error();

			status_t	Access(uint32* supported, uint32* allowed);
			status_t	Close();
			status_t	Commit();
			status_t	Create(uint64* before, uint64* after, bool& atomic);
	inline	status_t	DelegReturn();
			status_t	GetAttr(AttrValue** attrs, uint32* count);
			status_t	GetFH(FileHandle* fh);
			status_t	Link(uint64* before, uint64* after, bool& atomic);
			status_t	Lock(LockInfo* linfo);
			status_t 	LockT(uint64* pos, uint64* len, LockType* type);
			status_t	LockU(LockInfo* linfo);
	inline	status_t	LookUp();
	inline	status_t	LookUpUp();
	inline	status_t	Nverify();
			status_t	Open(uint32* id, uint32* seq, bool* confirm,
							OpenDelegationData* delegData,
							ChangeInfo* changeInfo = NULL);
	inline	status_t	OpenAttrDir();
			status_t	OpenConfirm(uint32* stateSeq);
	inline	status_t	PutFH();
	inline	status_t	PutRootFH();
			status_t	Read(void* buffer, uint32* size, bool* eof);
			status_t	ReadDir(uint64* cookie, uint64* cookieVerf,
							DirEntry** dirents, uint32* count, bool* eof);
			status_t	ReadLink(void* buffer, uint32* size, uint32 maxSize);
			status_t	Remove(uint64* before, uint64* after, bool& atomic);
			status_t	Rename(uint64* fromBefore, uint64* fromAfter,
							bool& fromAtomic, uint64* toBefore, uint64* toAfter,
							bool& toAtomic);
	inline	status_t	Renew();
	inline	status_t	SaveFH();
			status_t	SetAttr();
			status_t	SetClientID(uint64* clientid, uint64* verifier);
	inline	status_t	SetClientIDConfirm();
	inline	status_t	Verify();
			status_t	Write(uint32* size);
	inline	status_t	ReleaseLockOwner();

private:
			void		_ParseHeader();

	static	const char** _GetPath(XDR::ReadStream& stream);

			status_t	_DecodeAttrs(XDR::ReadStream& stream, AttrValue** attrs,
							uint32* count);
			status_t	_OperationError(Opcode op);

	static	status_t	_NFS4ErrorToHaiku(uint32 x);

			uint32		fNFS4Error;
			bool		fDecodeError;
			RPC::Reply*	fReply;
};


inline status_t
ReplyInterpreter::SetTo(RPC::Reply* _reply)
{
	if (fReply != NULL)
		return B_DONT_DO_THAT;

	fDecodeError = false;
	fReply = _reply;

	if (fReply != NULL)
		_ParseHeader();

	return B_OK;
}


inline void
ReplyInterpreter::Reset()
{
	delete fReply;
	fReply = NULL;
	fDecodeError = false;
}


inline uint32
ReplyInterpreter::NFS4Error()
{
	return fNFS4Error;
}


inline status_t
ReplyInterpreter::DelegReturn()
{
	return _OperationError(OpDelegReturn);
}


inline status_t
ReplyInterpreter::LookUp()
{
	return _OperationError(OpLookUp);
}


inline status_t
ReplyInterpreter::LookUpUp()
{
	return _OperationError(OpLookUpUp);
}


inline status_t
ReplyInterpreter::OpenAttrDir()
{
	return _OperationError(OpOpenAttrDir);
}


inline status_t
ReplyInterpreter::Nverify()
{
	return _OperationError(OpNverify);
}


inline status_t
ReplyInterpreter::PutFH()
{
	return _OperationError(OpPutFH);
}


inline status_t
ReplyInterpreter::PutRootFH()
{
	return _OperationError(OpPutRootFH);
}


inline status_t
ReplyInterpreter::Renew()
{
	return _OperationError(OpRenew);
}


inline status_t
ReplyInterpreter::SaveFH()
{
	return _OperationError(OpSaveFH);
}


inline status_t
ReplyInterpreter::SetClientIDConfirm()
{
	return _OperationError(OpSetClientIDConfirm);
}


inline status_t
ReplyInterpreter::Verify()
{
	return _OperationError(OpVerify);
}


inline status_t
ReplyInterpreter::ReleaseLockOwner()
{
	return _OperationError(OpReleaseLockOwner);
}


#endif	// REPLYINTERPRETER_H

