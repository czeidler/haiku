/*
 * Copyright 2012 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, pdziepak@quarnos.org
 */
#ifndef RPCREPLY_H
#define RPCREPLY_H


#include "XDR.h"


namespace RPC {

class Reply {
public:
								Reply(void* buffer, int size);
								~Reply();

	inline	uint32				GetXID();

	inline	status_t			Error();
	inline	XDR::ReadStream&	Stream();

private:
			uint32				fXID;

			status_t			fError;

			XDR::ReadStream		fStream;
			void*				fBuffer;
};

inline uint32
Reply::GetXID()
{
	return fXID;
}


inline status_t
Reply::Error()
{
	return fError;
}


inline XDR::ReadStream&
Reply::Stream()
{
	return fStream;
}

}		// namespace RPC


#endif	// RPCREPLY_H

