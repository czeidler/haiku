/*
 * Copyright 2012 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, pdziepak@quarnos.org
 */


#include "RPCCall.h"

#include <debug.h>
#include <util/kernel_cpp.h>

#include "RPCDefs.h"


using namespace RPC;


Call::Call()
{
}


Call*
Call::Create(uint32 proc, const Auth* creds, const Auth* ver)
{
	ASSERT(creds != NULL);
	ASSERT(ver != NULL);

	Call* call = new(std::nothrow) Call;
	if (call == NULL)
		return NULL;

	// XID will be determined and set by RPC::Server
	call->fXIDPosition = call->fStream.Current();
	call->fStream.AddUInt(0);

	call->fStream.AddInt(CALL);
	call->fStream.AddUInt(VERSION);
	call->fStream.AddUInt(PROGRAM_NFS);
	call->fStream.AddUInt(NFS_VERSION);
	call->fStream.AddUInt(proc);

	call->fStream.Append(creds->Stream());
	delete creds;

	call->fStream.Append(ver->Stream());
	delete ver;

	if (call->fStream.Error() != B_OK) {
		delete call;
		return NULL;
	}

	return call;
}


Call::~Call()
{
}


void
Call::SetXID(uint32 xid)
{
	fStream.InsertUInt(fXIDPosition, xid);
}

