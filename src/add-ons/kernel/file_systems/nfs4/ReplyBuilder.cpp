/*
 * Copyright 2012 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, pdziepak@quarnos.org
 */


#include "ReplyBuilder.h"

#include "NFS4Defs.h"
#include "RPCCallbackReply.h"


ReplyBuilder::ReplyBuilder(uint32 xid)
	:
	fStatus(B_OK),
	fOpCount(0),
	fReply(RPC::CallbackReply::Create(xid))
{
	_InitHeader();
}


ReplyBuilder::~ReplyBuilder()
{
	delete fReply;
}


void
ReplyBuilder::_InitHeader()
{
	fStatusPosition = fReply->Stream().Current();
	fReply->Stream().AddUInt(0);

	fReply->Stream().AddOpaque(NULL, 0);

	fOpCountPosition = fReply->Stream().Current();
	fReply->Stream().AddUInt(0);

}


RPC::CallbackReply*
ReplyBuilder::Reply()
{
	fReply->Stream().InsertUInt(fStatusPosition, _HaikuErrorToNFS4(fStatus));
	fReply->Stream().InsertUInt(fOpCountPosition, fOpCount);

	if (fReply == NULL || fReply->Stream().Error() == B_OK)
		return fReply;
	else
		return NULL;
}


status_t
ReplyBuilder::GetAttr(status_t status, int mask, uint64 size, uint64 change)
{
	if (fStatus != B_OK)
		return B_ERROR;

	fReply->Stream().AddUInt(OpCallbackGetAttr);
	fReply->Stream().AddUInt(_HaikuErrorToNFS4(fStatus));
	fStatus = status;

	if (status == B_OK) {
		uint32 bitmap = 0;
		if ((mask & CallbackAttrChange) != 0)
			bitmap |= 1 << FATTR4_CHANGE;
		if ((mask & CallbackAttrSize) != 0)
			bitmap |= 1 << FATTR4_SIZE;
		fReply->Stream().AddUInt(1);
		fReply->Stream().AddUInt(bitmap);

		XDR::WriteStream str;
		if ((mask & CallbackAttrChange) != 0)
			str.AddUHyper(change);

		if ((mask & CallbackAttrSize) != 0)
			str.AddUHyper(size);
		fReply->Stream().AddOpaque(str);
	}

	fOpCount++;

	return B_OK;
}


status_t
ReplyBuilder::Recall(status_t status)
{
	if (fStatus != B_OK)
		return B_ERROR;

	fReply->Stream().AddUInt(OpCallbackRecall);
	fReply->Stream().AddUInt(_HaikuErrorToNFS4(fStatus));
	fStatus = status;

	fOpCount++;

	return B_OK;
}


uint32
ReplyBuilder::_HaikuErrorToNFS4(status_t error)
{
	switch (error) {
		case B_OK:				return NFS4_OK;
		case B_FILE_NOT_FOUND:	return NFS4ERR_BADHANDLE;
		case B_NOT_SUPPORTED:	return NFS4ERR_OP_ILLEGAL;
		default:				return NFS4ERR_RESOURCE;
	}
}

