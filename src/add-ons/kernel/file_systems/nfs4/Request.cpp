/*
 * Copyright 2012 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, pdziepak@quarnos.org
 */


#include "Request.h"

#include "FileSystem.h"
#include "Inode.h"


status_t
Request::Send(Cookie* cookie)
{
	switch (fServer->ID().fProtocol) {
		case IPPROTO_UDP:	return _SendUDP(cookie);
		case IPPROTO_TCP:	return _SendTCP(cookie);
	}

	return B_BAD_VALUE;
}


status_t
Request::_SendUDP(Cookie* cookie)
{
	RPC::Reply* rpl = NULL;
	RPC::Request* rpc;

	status_t result = fServer->SendCallAsync(fBuilder.Request(), &rpl, &rpc);
	if (result != B_OK)
		return result;

	if (cookie != NULL)
		cookie->RegisterRequest(rpc);

	int requestTimeout = sSecToBigTime(60);
	int retryLimit = 0;
	bool hard = true;

	if (fFileSystem != NULL) {
		requestTimeout = fFileSystem->GetConfiguration().fRequestTimeout;
		retryLimit = fFileSystem->GetConfiguration().fRetryLimit;
		hard = fFileSystem->GetConfiguration().fHard;
	}

	result = fServer->WaitCall(rpc, requestTimeout);
	if (result != B_OK) {
		int attempts = 1;
		while (result != B_OK && (hard || attempts++ < retryLimit)) {
			result = fServer->ResendCallAsync(fBuilder.Request(), rpc);
			if (result != B_OK) {
				if (cookie != NULL)
					cookie->UnregisterRequest(rpc);
				delete rpc;
				return result;
			}

			result = fServer->WaitCall(rpc, requestTimeout);
		}

		if (result != B_OK) {
			if (cookie != NULL)
				cookie->UnregisterRequest(rpc);
			fServer->CancelCall(rpc);
			delete rpc;
			return result;
		}
	}

	if (cookie != NULL)
		cookie->UnregisterRequest(rpc);

	if (rpc->fError != B_OK) {
		delete rpl;
		result = rpc->fError;
		delete rpc;
		return result;
	} else {
		fReply.SetTo(rpl);
		delete rpc;
		return B_OK;
	}
}


status_t
Request::_SendTCP(Cookie* cookie)
{
	RPC::Reply* rpl = NULL;
	RPC::Request* rpc;

	status_t result;
	int attempts = 0;

	int requestTimeout = sSecToBigTime(60);
	int retryLimit = 0;
	bool hard = true;

	if (fFileSystem != NULL) {
		requestTimeout = fFileSystem->GetConfiguration().fRequestTimeout;
		retryLimit = fFileSystem->GetConfiguration().fRetryLimit;
		hard = fFileSystem->GetConfiguration().fHard;
	}

	do {
		result = fServer->SendCallAsync(fBuilder.Request(), &rpl, &rpc);
		if (result == B_NO_MEMORY)
			return result;
		else if (result != B_OK) {
			fServer->Repair();
			continue;
		}

		if (cookie != NULL)
			cookie->RegisterRequest(rpc);

		result = fServer->WaitCall(rpc, requestTimeout);
		if (result != B_OK) {
			if (cookie != NULL)
				cookie->UnregisterRequest(rpc);

			fServer->CancelCall(rpc);
			delete rpc;

			fServer->Repair();	
		}
	} while (result != B_OK && (hard || attempts++ < retryLimit));

	if (result != B_OK)
		return result;

	if (cookie != NULL)
		cookie->UnregisterRequest(rpc);

	if (rpc->fError != B_OK) {
		delete rpl;
		result = rpc->fError;
		delete rpc;
		return result;
	} 

	fReply.SetTo(rpl);
	delete rpc;
	return B_OK;
}


void
Request::Reset()
{
	fBuilder.Reset();
	fReply.Reset();
}

