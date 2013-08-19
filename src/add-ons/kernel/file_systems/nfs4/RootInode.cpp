/*
 * Copyright 2012 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, pdziepak@quarnos.org
 */


#include "RootInode.h"

#include <string.h>

#include "MetadataCache.h"
#include "Request.h"


RootInode::RootInode()
	:
	fInfoCacheExpire(0),
	fName(NULL),
	fIOSize(0)
{
	mutex_init(&fInfoCacheLock, NULL);
}


RootInode::~RootInode()
{
	free(const_cast<char*>(fName));
	mutex_destroy(&fInfoCacheLock);
}


status_t
RootInode::ReadInfo(struct fs_info* info)
{
	ASSERT(info != NULL);

	status_t result = _UpdateInfo();
	if (result != B_OK)
		return result;

	memcpy(info, &fInfoCache, sizeof(struct fs_info));

	return B_OK;
}


status_t
RootInode::_UpdateInfo(bool force)
{
	if (!force && fInfoCacheExpire > time(NULL))
		return B_OK;

	MutexLocker _(fInfoCacheLock);

	if (fInfoCacheExpire > time(NULL))
		return B_OK;

	uint32 attempt = 0;
	do {
		RPC::Server* server = fFileSystem->Server();
		Request request(server, fFileSystem);
		RequestBuilder& req = request.Builder();

		req.PutFH(fInfo.fHandle);
		Attribute attr[] = { FATTR4_FILES_FREE, FATTR4_FILES_TOTAL,
			FATTR4_MAXREAD, FATTR4_MAXWRITE, FATTR4_SPACE_FREE,
			FATTR4_SPACE_TOTAL };
		req.GetAttr(attr, sizeof(attr) / sizeof(Attribute));

		status_t result = request.Send();
		if (result != B_OK)
			return result;

		ReplyInterpreter& reply = request.Reply();

		if (HandleErrors(attempt, reply.NFS4Error(), server))
			continue;

		reply.PutFH();

		AttrValue* values;
		uint32 count, next = 0;
		result = reply.GetAttr(&values, &count);
		if (result != B_OK)
			return result;

		if (count >= next && values[next].fAttribute == FATTR4_FILES_FREE) {
			fInfoCache.free_nodes = values[next].fData.fValue64;
			next++;
		}

		if (count >= next && values[next].fAttribute == FATTR4_FILES_TOTAL) {
			fInfoCache.total_nodes = values[next].fData.fValue64;
			next++;
		}

		uint64 ioSize = LONGLONG_MAX;
		if (count >= next && values[next].fAttribute == FATTR4_MAXREAD) {
			ioSize = min_c(ioSize, values[next].fData.fValue64);
			next++;
		}

		if (count >= next && values[next].fAttribute == FATTR4_MAXWRITE) {
			ioSize = min_c(ioSize, values[next].fData.fValue64);
			next++;
		}

		if (ioSize == LONGLONG_MAX)
			ioSize = 32768;
		fInfoCache.io_size = ioSize;
		fInfoCache.block_size = ioSize;
		fIOSize = ioSize;

		if (count >= next && values[next].fAttribute == FATTR4_SPACE_FREE) {
			fInfoCache.free_blocks = values[next].fData.fValue64 / ioSize;
			next++;
		}

		if (count >= next && values[next].fAttribute == FATTR4_SPACE_TOTAL) {
			fInfoCache.total_blocks = values[next].fData.fValue64 / ioSize;
			next++;
		}

		delete[] values;

		break;
	} while (true);

	fInfoCache.flags = 	B_FS_IS_PERSISTENT | B_FS_IS_SHARED
		| B_FS_SUPPORTS_NODE_MONITORING;

	if (fFileSystem->NamedAttrs()
		|| fFileSystem->GetConfiguration().fEmulateNamedAttrs)
		fInfoCache.flags |= B_FS_HAS_MIME | B_FS_HAS_ATTR;

	strlcpy(fInfoCache.volume_name, fName, sizeof(fInfoCache.volume_name));

	fInfoCacheExpire = time(NULL) + MetadataCache::kExpirationTime;

	return B_OK;
}


bool
RootInode::ProbeMigration()
{
	uint32 attempt = 0;
	do {
		RPC::Server* server = fFileSystem->Server();
		Request request(server, fFileSystem);
		RequestBuilder& req = request.Builder();

		req.PutFH(fInfo.fHandle);
		req.Access();

		status_t result = request.Send();
		if (result != B_OK)
			continue;

		ReplyInterpreter& reply = request.Reply();

		if (reply.NFS4Error() == NFS4ERR_MOVED)
			return true;

		if (HandleErrors(attempt, reply.NFS4Error(), server))
			continue;

		return false;
	} while (true);
}


status_t
RootInode::GetLocations(AttrValue** attrv)
{
	ASSERT(attrv != NULL);

	uint32 attempt = 0;
	do {
		RPC::Server* server = fFileSystem->Server();
		Request request(server, fFileSystem);
		RequestBuilder& req = request.Builder();

		req.PutFH(fInfo.fHandle);
		Attribute attr[] = { FATTR4_FS_LOCATIONS };
		req.GetAttr(attr, sizeof(attr) / sizeof(Attribute));

		status_t result = request.Send();
		if (result != B_OK)
			return result;

		ReplyInterpreter& reply = request.Reply();

		if (HandleErrors(attempt, reply.NFS4Error(), server))
			continue;

		reply.PutFH();

		uint32 count;
		result = reply.GetAttr(attrv, &count);
		if (result != B_OK)
			return result;
		if (count < 1)
			return B_ERROR;
		return B_OK;

	} while (true);

	return B_OK;
}


const char*
RootInode::Name() const
{
	ASSERT(fName != NULL);
	return fName;
}

