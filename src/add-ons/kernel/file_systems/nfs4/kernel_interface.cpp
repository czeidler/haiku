/*
 * Copyright 2012 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, pdziepak@quarnos.org
 */


#include <stdio.h>

#include <AutoDeleter.h>
#include <fs_cache.h>
#include <fs_interface.h>

#include "Connection.h"
#include "FileSystem.h"
#include "IdMap.h"
#include "Inode.h"
#include "NFS4Defs.h"
#include "RequestBuilder.h"
#include "ReplyInterpreter.h"
#include "RootInode.h"
#include "RPCCallbackServer.h"
#include "RPCServer.h"
#include "VnodeToInode.h"
#include "WorkQueue.h"

#ifdef DEBUG
#define TRACE_NFS4
#endif

#ifdef TRACE_NFS4
static mutex	gTraceLock	= MUTEX_INITIALIZER(NULL);

#define TRACE(x...)	\
	{	\
		mutex_lock(&gTraceLock);	\
		dprintf("nfs4: %s(): ", __FUNCTION__);	\
		dprintf(x);	\
		dprintf("\n");	\
		mutex_unlock(&gTraceLock);	\
	}
#else
#define TRACE(x...)	(void)0
#endif

extern fs_volume_ops gNFSv4VolumeOps;
extern fs_vnode_ops gNFSv4VnodeOps;


RPC::ServerManager* gRPCServerManager;


RPC::ProgramData*
CreateNFS4Server(RPC::Server* serv)
{
	return new NFS4Server(serv);
}


// Format: ip{4,6}_address:path options
// Available options:
//	hard		- retry requests until success
//	soft		- retry requests no more than retrans times (default)
//  timeo=X		- request time limit before next retransmission (default: 60s)
//	retrans=X	- retry requests X times (default: 5)
//	ac			- use metadata cache (default)
//	noac		- do not use metadata cache
//	xattr-emu	- emulate named attributes
//	noxattr-emu	- do not emulate named attributes (default)
//	port=X		- connect to port X (default: 2049)
//	proto=X		- user transport protocol X (default: tcp)
//	dirtime=X	- attempt revalidate directory cache not more often than each X
//				  seconds
static status_t
ParseArguments(const char* _args, AddressResolver** address, char** _path,
	MountConfiguration* conf)
{
	if (_args == NULL)
		return B_BAD_VALUE;

	char* args = strdup(_args);
	if (args == NULL)
		return B_NO_MEMORY;
	MemoryDeleter argsDeleter(args);

	char* options = strchr(args, ' ');
	if (options != NULL)
		*options++ = '\0';

	char* path = strrchr(args, ':');
	if (path == NULL)
		return B_MISMATCHED_VALUES;
	*path++ = '\0';

	*address = new AddressResolver(args);
	if (*address == NULL)
		return B_NO_MEMORY;

	*_path = strdup(path);
	if (*_path == NULL) {
		delete *address;
		return B_NO_MEMORY;
	}

	conf->fHard = false;
	conf->fRetryLimit = 5;
	conf->fRequestTimeout = sSecToBigTime(60);
	conf->fEmulateNamedAttrs = false;
	conf->fCacheMetadata = true;
	conf->fDirectoryCacheTime = sSecToBigTime(5);

	char* optionsEnd = NULL;
	if (options != NULL)
		optionsEnd = strchr(options, ' ');
	while (options != NULL && *options != '\0') {
		if (optionsEnd != NULL)
			*optionsEnd++ = '\0';

		if (strcmp(options, "hard") == 0)
			conf->fHard = true;
		else if (strncmp(options, "retrans=", 8) == 0) {
			options += strlen("retrans=");
			conf->fRetryLimit = atoi(options);
		} else if (strncmp(options, "timeo=", 6) == 0) {
			options += strlen("timeo=");
			conf->fRequestTimeout = atoi(options);
		} else if (strcmp(options, "noac") == 0)
			conf->fCacheMetadata = false;
		else if (strcmp(options, "xattr-emu") == 0)
			conf->fEmulateNamedAttrs = true;
		else if (strncmp(options, "port=", 5) == 0) {
			options += strlen("port=");
			(*address)->ForcePort(atoi(options));
		} else if (strncmp(options, "proto=", 6) == 0) {
			options += strlen("proto=");
			(*address)->ForceProtocol(options);
		} else if (strncmp(options, "dirtime=", 8) == 0) {
			options += strlen("dirtime=");
			conf->fDirectoryCacheTime = sSecToBigTime(atoi(options));
		}

		options = optionsEnd;
		if (options != NULL)
			optionsEnd = strchr(options, ' ');
	}

	return B_OK;
}


static status_t
nfs4_mount(fs_volume* volume, const char* device, uint32 flags,
			const char* args, ino_t* _rootVnodeID)
{
	TRACE("volume = %p, device = %s, flags = %" B_PRIu32 ", args = %s", volume,
		device, flags, args);

	status_t result;

	/* prepare idmapper server */
	MutexLocker locker(gIdMapperLock);
	gIdMapper = new(std::nothrow) IdMap;
	if (gIdMapper == NULL)
		return B_NO_MEMORY;

	result = gIdMapper->InitStatus();
	if (result != B_OK) {
		delete gIdMapper;
		gIdMapper = NULL;
		return result;
	}
	locker.Unlock();

	AddressResolver* resolver;
	MountConfiguration config;
	char* path;
	result = ParseArguments(args, &resolver, &path, &config);
	if (result != B_OK)
		return result;
	MemoryDeleter pathDeleter(path);

	RPC::Server* server;
	result = gRPCServerManager->Acquire(&server, resolver, CreateNFS4Server);
	delete resolver;
	if (result != B_OK)
		return result;
	
	FileSystem* fs;
	result = FileSystem::Mount(&fs, server, path, volume->id, config);
	if (result != B_OK) {
		gRPCServerManager->Release(server);
		return result;
	}

	Inode* inode = fs->Root();
	if (inode == NULL) {
		delete fs;
		gRPCServerManager->Release(server);

		return B_IO_ERROR;
	}

	volume->private_volume = fs;
	volume->ops = &gNFSv4VolumeOps;

	VnodeToInode* vti = new VnodeToInode(inode->ID(), fs);
	if (vti == NULL) {
		delete fs;
		gRPCServerManager->Release(server);
		return B_NO_MEMORY;
	}

	vti->Replace(inode);
	result = publish_vnode(volume, inode->ID(), vti, &gNFSv4VnodeOps,
							inode->Type(), 0);
	if (result != B_OK)
		return result;

	*_rootVnodeID = inode->ID();

	TRACE("*_rootVnodeID = %" B_PRIi64, inode->ID());

	return B_OK;
}


static status_t
nfs4_get_vnode(fs_volume* volume, ino_t id, fs_vnode* vnode, int* _type,
	uint32* _flags, bool reenter)
{
	FileSystem* fs = reinterpret_cast<FileSystem*>(volume->private_volume);
	TRACE("volume = %p, id = %" B_PRIi64, volume, id);

	VnodeToInode* vnodeToInode = new VnodeToInode(id, fs);
	if (vnodeToInode == NULL)
		return B_NO_MEMORY;

	Inode* inode;	
	status_t result = fs->GetInode(id, &inode);
	if (result != B_OK) {
		delete vnodeToInode;
		return result;
	}

	vnodeToInode->Replace(inode);
	vnode->ops = &gNFSv4VnodeOps;
	vnode->private_node = vnodeToInode;

	*_type = inode->Type();
	*_flags = 0;

	return B_OK;
}


static status_t
nfs4_unmount(fs_volume* volume)
{
	TRACE("volume = %p", volume);
	FileSystem* fs = reinterpret_cast<FileSystem*>(volume->private_volume);
	RPC::Server* server = fs->Server();

	delete fs;
	gRPCServerManager->Release(server);

	return B_OK;
}


static status_t
nfs4_read_fs_info(fs_volume* volume, struct fs_info* info)
{
	TRACE("volume = %p", volume);

	FileSystem* fs = reinterpret_cast<FileSystem*>(volume->private_volume);
	RootInode* inode = reinterpret_cast<RootInode*>(fs->Root());
	return inode->ReadInfo(info);
}


static status_t
nfs4_lookup(fs_volume* volume, fs_vnode* dir, const char* name, ino_t* _id)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(dir->private_node);

	if (!strcmp(name, ".")) {
		*_id = vti->ID();
		void* ptr;
		return get_vnode(volume, *_id, &ptr);
	}

	VnodeToInodeLocker locker(vti);

	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	TRACE("volume = %p, dir = %" B_PRIi64 ", name = %s", volume, vti->ID(),
		name);

	status_t result = inode->LookUp(name, _id);
	if (result != B_OK)
		return result;
	locker.Unlock();

	TRACE("*_id = %" B_PRIi64, *_id);

	// If VTI holds an outdated Inode next operation performed on it will
	// return either ERR_STALE or ERR_FHEXPIRED. Both of these error codes
	// will cause FileInfo data to be updated (the former will also cause Inode
	// object to be recreated). We are taking an optimistic (an lazy) approach
	// here. The following code just ensures VTI won't be removed too soon.
	void* ptr;
	result = get_vnode(volume, *_id, &ptr);
	if (result == B_OK)
		unremove_vnode(volume, *_id);

	return result;
}


static status_t
nfs4_put_vnode(fs_volume* volume, fs_vnode* vnode, bool reenter)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64, volume, vti->ID());

	delete vti;
	return B_OK;
}


static status_t
nfs4_remove_vnode(fs_volume* volume, fs_vnode* vnode, bool reenter)
{
	FileSystem* fs = reinterpret_cast<FileSystem*>(volume->private_volume);
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64, volume, vti->ID());

	if (fs->Root() == vti->GetPointer())
		return B_OK;

	ASSERT(vti->GetPointer() == NULL);
	delete vti;

	return B_OK;
}


static status_t
nfs4_read_pages(fs_volume* _volume, fs_vnode* vnode, void* _cookie, off_t pos,
	const iovec* vecs, size_t count, size_t* _numBytes)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p, pos = %" B_PRIi64 \
		", count = %lu, numBytes = %lu", _volume, vti->ID(), _cookie, pos,
		count, *_numBytes);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	OpenFileCookie* cookie = reinterpret_cast<OpenFileCookie*>(_cookie);

	status_t result;
	size_t totalRead = 0;
	bool eof = false;
	for (size_t i = 0; i < count && !eof; i++) {
		size_t bytesLeft = vecs[i].iov_len;
		char* buffer = reinterpret_cast<char*>(vecs[i].iov_base);

		do {
			size_t bytesRead = bytesLeft;
			result = inode->ReadDirect(cookie, pos, buffer, &bytesRead, &eof);
			if (result != B_OK)
				return result;

			totalRead += bytesRead;
			pos += bytesRead;
			buffer += bytesRead;
			bytesLeft -= bytesRead;
		} while (bytesLeft > 0 && !eof);
	}

	*_numBytes = totalRead;

	TRACE("*numBytes = %lu", totalRead);

	return B_OK;
}


static status_t
nfs4_write_pages(fs_volume* _volume, fs_vnode* vnode, void* _cookie, off_t pos,
	const iovec* vecs, size_t count, size_t* _numBytes)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p, pos = %" B_PRIi64 \
		", count = %lu, numBytes = %lu", _volume, vti->ID(), _cookie, pos,
		count, *_numBytes);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	OpenFileCookie* cookie = reinterpret_cast<OpenFileCookie*>(_cookie);

	status_t result;
	for (size_t i = 0; i < count; i++) {
		uint64 bytesLeft = vecs[i].iov_len;
		if (pos + bytesLeft > inode->MaxFileSize())
			bytesLeft = inode->MaxFileSize() - pos;

		char* buffer = reinterpret_cast<char*>(vecs[i].iov_base);

		do {
			size_t bytesWritten = bytesLeft;

			result = inode->WriteDirect(cookie, pos, buffer, &bytesWritten);
			if (result != B_OK)
				return result;

			bytesLeft -= bytesWritten;
			pos += bytesWritten;
			buffer += bytesWritten;
		} while (bytesLeft > 0);
	}

	return B_OK;
}


static status_t
nfs4_io(fs_volume* volume, fs_vnode* vnode, void* cookie, io_request* request)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p", volume, vti->ID(),
		cookie);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	IORequestArgs* args = new(std::nothrow) IORequestArgs;
	if (args == NULL) {
		notify_io_request(request, B_NO_MEMORY);
		return B_NO_MEMORY;
	}
	args->fRequest = request;
	args->fInode = inode;

	status_t result = gWorkQueue->EnqueueJob(IORequest, args);
	if (result != B_OK)
		notify_io_request(request, result);

	return result;
}


static status_t
nfs4_get_file_map(fs_volume* volume, fs_vnode* vnode, off_t _offset,
	size_t size, struct file_io_vec* vecs, size_t* _count)
{
	return B_ERROR;
}


static status_t
nfs4_set_flags(fs_volume* volume, fs_vnode* vnode, void* _cookie, int flags)
{
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p, flags = %d", volume,
		reinterpret_cast<VnodeToInode*>(vnode->private_node)->ID(), _cookie,
		flags);

	OpenFileCookie* cookie = reinterpret_cast<OpenFileCookie*>(_cookie);
	cookie->fMode = (cookie->fMode & ~(O_APPEND | O_NONBLOCK)) | flags;
	return B_OK;
}


static status_t
nfs4_fsync(fs_volume* volume, fs_vnode* vnode)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64, volume, vti->ID());

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->SyncAndCommit();
}


static status_t
nfs4_read_symlink(fs_volume* volume, fs_vnode* link, char* buffer,
	size_t* _bufferSize)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(link->private_node);
	TRACE("volume = %p, link = %" B_PRIi64, volume, vti->ID());

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->ReadLink(buffer, _bufferSize);
}


static status_t
nfs4_create_symlink(fs_volume* volume, fs_vnode* dir, const char* name,
	const char* path, int mode)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(dir->private_node);
	TRACE("volume = %p, dir = %" B_PRIi64 ", name = %s, path = %s, mode = %d",
		volume, vti->ID(), name, path, mode);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	ino_t id;
	status_t result = inode->CreateLink(name, path, mode, &id);
	if (result != B_OK)
		return result;

	result = get_vnode(volume, id, reinterpret_cast<void**>(&vti));
	if (result == B_OK) {
		unremove_vnode(volume, id);
		vti->Clear();
		put_vnode(volume, id);
	}

	return B_OK;
}


static status_t
nfs4_link(fs_volume* volume, fs_vnode* dir, const char* name, fs_vnode* vnode)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	VnodeToInode* dirVti = reinterpret_cast<VnodeToInode*>(dir->private_node);
	TRACE("volume = %p, dir = %" B_PRIi64 ", name = %s, vnode = %" B_PRIi64,
		volume, dirVti->ID(), name, vti->ID());

	VnodeToInodeLocker _dir(dirVti);
	Inode* dirInode = dirVti->Get();
	if (dirInode == NULL)
		return B_ENTRY_NOT_FOUND;


	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->Link(dirInode, name);
}


static status_t
nfs4_unlink(fs_volume* volume, fs_vnode* dir, const char* name)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(dir->private_node);

	VnodeToInodeLocker locker(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	TRACE("volume = %p, dir = %" B_PRIi64 ", name = %s", volume, vti->ID(),
		name);

	ino_t id;
	status_t result = inode->Remove(name, NF4REG, &id);
	if (result != B_OK)
		return result;
	locker.Unlock();

	result = acquire_vnode(volume, id);
	if (result == B_OK) {
		result = get_vnode(volume, id, reinterpret_cast<void**>(&vti));
		ASSERT(result == B_OK);
		
		if (vti->Unlink(inode->fInfo.fNames, name))
			remove_vnode(volume, id);

		put_vnode(volume, id);
		put_vnode(volume, id);
	}

	return B_OK;
}


static status_t
nfs4_rename(fs_volume* volume, fs_vnode* fromDir, const char* fromName,
	fs_vnode* toDir, const char* toName)
{
	VnodeToInode* fromVti
		= reinterpret_cast<VnodeToInode*>(fromDir->private_node);
	VnodeToInode* toVti = reinterpret_cast<VnodeToInode*>(toDir->private_node);
	TRACE("volume = %p, fromDir = %" B_PRIi64 ", toDir = %" B_PRIi64 ","	\
		" fromName = %s, toName = %s", volume, fromVti->ID(), toVti->ID(),	\
		fromName, toName);

	VnodeToInodeLocker _from(fromVti);
	Inode* fromInode = fromVti->Get();
	if (fromInode == NULL)
		return B_ENTRY_NOT_FOUND;


	VnodeToInodeLocker _to(toVti);
	Inode* toInode = toVti->Get();
	if (toInode == NULL)
		return B_ENTRY_NOT_FOUND;

	ino_t id;
	ino_t oldID;
	status_t result = Inode::Rename(fromInode, toInode, fromName, toName, false,
		&id, &oldID);
	if (result != B_OK)
		return result;

	VnodeToInode* vti;

	if (oldID != 0) {
		// we have overriden an inode
		result = acquire_vnode(volume, oldID);
		if (result == B_OK) {
			result = get_vnode(volume, oldID, reinterpret_cast<void**>(&vti));
			ASSERT(result == B_OK);
			if (vti->Unlink(toInode->fInfo.fNames, toName))
				remove_vnode(volume, oldID);

			put_vnode(volume, oldID);
			put_vnode(volume, oldID);
		}
	}

	result = get_vnode(volume, id, reinterpret_cast<void**>(&vti));
	if (result == B_OK) {
		Inode* child = vti->Get();
		if (child == NULL) {
			put_vnode(volume, id);
			return B_ENTRY_NOT_FOUND;
		}

		unremove_vnode(volume, id);
		child->fInfo.fNames->RemoveName(fromInode->fInfo.fNames, fromName);
		child->fInfo.fNames->AddName(toInode->fInfo.fNames, toName);
		put_vnode(volume, id);
	}

	return B_OK;
}


static status_t
nfs4_access(fs_volume* volume, fs_vnode* vnode, int mode)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", mode = %d", volume, vti->ID(),
		mode);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->Access(mode);
}


static status_t
nfs4_read_stat(fs_volume* volume, fs_vnode* vnode, struct stat* stat)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64, volume, vti->ID());

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	status_t result = inode->Stat(stat);
	if (inode->GetOpenState() != NULL)
		stat->st_size = inode->MaxFileSize();
	return result;
}


static status_t
nfs4_write_stat(fs_volume* volume, fs_vnode* vnode, const struct stat* stat,
	uint32 statMask)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", statMask = %" B_PRIu32, volume,
		vti->ID(), statMask);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->WriteStat(stat, statMask);
}


static status_t
get_new_vnode(fs_volume* volume, ino_t id, VnodeToInode** _vti)
{
	FileSystem* fs = reinterpret_cast<FileSystem*>(volume->private_volume);
	Inode* inode;
	VnodeToInode* vti;

	status_t result = acquire_vnode(volume, id);
	if (result == B_OK) {
		ASSERT(get_vnode(volume, id, reinterpret_cast<void**>(_vti)) == B_OK);
		unremove_vnode(volume, id);

		// Release after acquire
		put_vnode(volume, id);

		vti = *_vti;

		if (vti->Get() == NULL) {
			result = fs->GetInode(id, &inode);
			if (result != B_OK) {
				put_vnode(volume, id);
				return result;
			}

			vti->Replace(inode);
		}
		return B_OK;
	}

	return get_vnode(volume, id, reinterpret_cast<void**>(_vti));
}


static status_t
nfs4_create(fs_volume* volume, fs_vnode* dir, const char* name, int openMode,
	int perms, void** _cookie, ino_t* _newVnodeID)
{
	OpenFileCookie* cookie = new OpenFileCookie;
	if (cookie == NULL)
		return B_NO_MEMORY;
	*_cookie = cookie;

	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(dir->private_node);
	TRACE("volume = %p, dir = %" B_PRIi64 ", name = %s, openMode = %d,"	\
		" perms = %d", volume, vti->ID(), name, openMode, perms);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	FileSystem* fs = reinterpret_cast<FileSystem*>(volume->private_volume);
	MutexLocker createLocker(fs->CreateFileLock());

	OpenDelegationData data;
	status_t result = inode->Create(name, openMode, perms, cookie, &data,
		_newVnodeID);
	if (result != B_OK) {
		delete cookie;
		return result;
	}

	result = get_new_vnode(volume, *_newVnodeID, &vti);
	if (result != B_OK) {
		delete cookie;
		return result;
	}

	VnodeToInodeLocker _child(vti);
	Inode* child = vti->Get();
	if (child == NULL) {
		delete cookie;
		put_vnode(volume, *_newVnodeID);
		return B_ENTRY_NOT_FOUND;
	}

	child->SetOpenState(cookie->fOpenState);

	if (data.fType != OPEN_DELEGATE_NONE) {
		Delegation* delegation
			= new(std::nothrow) Delegation(data, child,
				cookie->fOpenState->fClientID);
		if (delegation != NULL) {
			delegation->fInfo = cookie->fOpenState->fInfo;
			delegation->fFileSystem = child->GetFileSystem();
			child->SetDelegation(delegation);
		}
	}

	TRACE("*cookie = %p, *newVnodeID = %" B_PRIi64, *_cookie, *_newVnodeID);
	return result;
}


static status_t
nfs4_open(fs_volume* volume, fs_vnode* vnode, int openMode, void** _cookie)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", openMode = %d", volume,
		vti->ID(), openMode);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	if (inode->Type() == S_IFDIR || inode->Type() == S_IFLNK) {
		*_cookie = NULL;
		return B_OK;
	}

	OpenFileCookie* cookie = new OpenFileCookie;
	if (cookie == NULL)
		return B_NO_MEMORY;
	*_cookie = cookie;

	status_t result = inode->Open(openMode, cookie);
	if (result != B_OK)
		delete cookie;

	TRACE("*cookie = %p", *_cookie);

	return result;
}


static status_t
nfs4_close(fs_volume* volume, fs_vnode* vnode, void* _cookie)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);

	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p", volume, vti->ID(),
		_cookie);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;


	if (inode->Type() == S_IFDIR || inode->Type() == S_IFLNK)
		return B_OK;

	Cookie* cookie = reinterpret_cast<Cookie*>(_cookie);
	return cookie->CancelAll();
}


static status_t
nfs4_free_cookie(fs_volume* volume, fs_vnode* vnode, void* _cookie)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);

	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p", volume, vti->ID(),
		_cookie);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	if (inode->Type() == S_IFDIR || inode->Type() == S_IFLNK)
		return B_OK;

	OpenFileCookie* cookie = reinterpret_cast<OpenFileCookie*>(_cookie);

	inode->Close(cookie);
	delete cookie;

	return B_OK;
}


static status_t
nfs4_read(fs_volume* volume, fs_vnode* vnode, void* _cookie, off_t pos,
	void* buffer, size_t* length)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p, pos = %" B_PRIi64 \
		", length = %lu", volume, vti->ID(), _cookie, pos, *length);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	if (inode->Type() == S_IFDIR)
		return B_IS_A_DIRECTORY;

	if (inode->Type() == S_IFLNK)
		return B_BAD_VALUE;

	OpenFileCookie* cookie = reinterpret_cast<OpenFileCookie*>(_cookie);

	return inode->Read(cookie, pos, buffer, length);;
}


static status_t
nfs4_write(fs_volume* volume, fs_vnode* vnode, void* _cookie, off_t pos,
	const void* _buffer, size_t* length)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p, pos = %" B_PRIi64 \
		", length = %lu", volume, vti->ID(), _cookie, pos, *length);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	if (inode->Type() == S_IFDIR)
		return B_IS_A_DIRECTORY;

	if (inode->Type() == S_IFLNK)
		return B_BAD_VALUE;

	OpenFileCookie* cookie = reinterpret_cast<OpenFileCookie*>(_cookie);

	return inode->Write(cookie, pos, _buffer, length);
}


static status_t
nfs4_create_dir(fs_volume* volume, fs_vnode* parent, const char* name,
	int mode)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(parent->private_node);
	TRACE("volume = %p, parent = %" B_PRIi64 ", mode = %d", volume, vti->ID(),
		mode);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	ino_t id;
	status_t result = inode->CreateDir(name, mode, &id);
	if (result != B_OK)
		return result;

	result = get_vnode(volume, id, reinterpret_cast<void**>(&vti));
	if (result == B_OK) {
		unremove_vnode(volume, id);
		vti->Clear();
		put_vnode(volume, id);
	}

	return B_OK;
}


static status_t
nfs4_remove_dir(fs_volume* volume, fs_vnode* parent, const char* name)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(parent->private_node);
	TRACE("volume = %p, parent = %" B_PRIi64 ", name = %s", volume, vti->ID(),
		name);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	ino_t id;
	status_t result = inode->Remove(name, NF4DIR, &id);
	if (result != B_OK)
		return result;

	result = acquire_vnode(volume, id);
	if (result == B_OK) {
		result = get_vnode(volume, id, reinterpret_cast<void**>(&vti));
		ASSERT(result == B_OK);

		if (vti->Unlink(inode->fInfo.fNames, name))
			remove_vnode(volume, id);

		put_vnode(volume, id);
		put_vnode(volume, id);
	}

	return B_OK;
}


static status_t
nfs4_open_dir(fs_volume* volume, fs_vnode* vnode, void** _cookie)
{
	OpenDirCookie* cookie = new(std::nothrow) OpenDirCookie;
	if (cookie == NULL)
		return B_NO_MEMORY;
	*_cookie = cookie;

	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64, volume, vti->ID());

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	status_t result = inode->OpenDir(cookie);
	if (result != B_OK)
		delete cookie;

	TRACE("*cookie = %p", *_cookie);

	return result;
}


static status_t 
nfs4_close_dir(fs_volume* volume, fs_vnode* vnode, void* _cookie)
{
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p", volume,
		reinterpret_cast<VnodeToInode*>(vnode->private_node)->ID(), _cookie);

	Cookie* cookie = reinterpret_cast<Cookie*>(_cookie);
	return cookie->CancelAll();
}


static status_t
nfs4_free_dir_cookie(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p", volume,
		reinterpret_cast<VnodeToInode*>(vnode->private_node)->ID(), cookie);

	delete reinterpret_cast<OpenDirCookie*>(cookie);
	return B_OK;
}


static status_t
nfs4_read_dir(fs_volume* volume, fs_vnode* vnode, void* _cookie,
				struct dirent* buffer, size_t bufferSize, uint32* _num)
{
	OpenDirCookie* cookie = reinterpret_cast<OpenDirCookie*>(_cookie);
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p", volume, vti->ID(),
		_cookie);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->ReadDir(buffer, bufferSize, _num, cookie);
}


static status_t
nfs4_rewind_dir(fs_volume* volume, fs_vnode* vnode, void* _cookie)
{
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p", volume,
		reinterpret_cast<VnodeToInode*>(vnode->private_node)->ID(), _cookie);

	OpenDirCookie* cookie = reinterpret_cast<OpenDirCookie*>(_cookie);
	cookie->fSpecial = 0;
	if (cookie->fSnapshot != NULL)
		cookie->fSnapshot->ReleaseReference();
	cookie->fSnapshot = NULL;
	cookie->fCurrent = NULL;
	cookie->fEOF = false;

	return B_OK;
}


static status_t
nfs4_open_attr_dir(fs_volume* volume, fs_vnode* vnode, void** _cookie)
{
	OpenDirCookie* cookie = new(std::nothrow) OpenDirCookie;
	if (cookie == NULL)
		return B_NO_MEMORY;
	*_cookie = cookie;

	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64, volume, vti->ID());

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	status_t result = inode->OpenAttrDir(cookie);
	if (result != B_OK)
		delete cookie;

	return result;
}


static status_t
nfs4_close_attr_dir(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	return nfs4_close_dir(volume, vnode, cookie);
}


static status_t
nfs4_free_attr_dir_cookie(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	return nfs4_free_dir_cookie(volume, vnode, cookie);
}


static status_t
nfs4_read_attr_dir(fs_volume* volume, fs_vnode* vnode, void* cookie,
	struct dirent* buffer, size_t bufferSize, uint32* _num)
{
	return nfs4_read_dir(volume, vnode, cookie, buffer, bufferSize, _num);
}


static status_t
nfs4_rewind_attr_dir(fs_volume* volume, fs_vnode* vnode, void* cookie)
{
	return nfs4_rewind_dir(volume, vnode, cookie);
}


static status_t
nfs4_create_attr(fs_volume* volume, fs_vnode* vnode, const char* name,
	uint32 type, int openMode, void** _cookie)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	OpenAttrCookie* cookie = new OpenAttrCookie;
	if (cookie == NULL)
		return B_NO_MEMORY;
	*_cookie = cookie;

	status_t result = inode->OpenAttr(name, openMode, cookie, true, type);
	if (result != B_OK)
		delete cookie;

	return result;
}


static status_t
nfs4_open_attr(fs_volume* volume, fs_vnode* vnode, const char* name,
	int openMode, void** _cookie)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	OpenAttrCookie* cookie = new OpenAttrCookie;
	if (cookie == NULL)
		return B_NO_MEMORY;
	*_cookie = cookie;

	status_t result = inode->OpenAttr(name, openMode, cookie, false);
	if (result != B_OK)
		delete cookie;

	return result;
}


static status_t
nfs4_close_attr(fs_volume* volume, fs_vnode* vnode, void* _cookie)
{
	Cookie* cookie = reinterpret_cast<Cookie*>(_cookie);
	return cookie->CancelAll();
}


static status_t
nfs4_free_attr_cookie(fs_volume* volume, fs_vnode* vnode, void* _cookie)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	OpenAttrCookie* cookie = reinterpret_cast<OpenAttrCookie*>(_cookie);
	inode->CloseAttr(cookie);
	delete cookie;

	return B_OK;
}


static status_t
nfs4_read_attr(fs_volume* volume, fs_vnode* vnode, void* _cookie, off_t pos,
	void* buffer, size_t* length)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	OpenAttrCookie* cookie = reinterpret_cast<OpenAttrCookie*>(_cookie);
	bool eof;

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->ReadDirect(cookie, pos, buffer, length, &eof);
}


static status_t
nfs4_write_attr(fs_volume* volume, fs_vnode* vnode, void* _cookie, off_t pos,
	const void* buffer, size_t* length)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	OpenAttrCookie* cookie = reinterpret_cast<OpenAttrCookie*>(_cookie);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->WriteDirect(cookie, pos, buffer, length);
}


static status_t
nfs4_read_attr_stat(fs_volume* volume, fs_vnode* vnode, void* _cookie,
	struct stat* stat)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	OpenAttrCookie* cookie = reinterpret_cast<OpenAttrCookie*>(_cookie);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->Stat(stat, cookie);
}


static status_t
nfs4_write_attr_stat(fs_volume* volume, fs_vnode* vnode, void* _cookie,
	const struct stat* stat, int statMask)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	OpenAttrCookie* cookie = reinterpret_cast<OpenAttrCookie*>(_cookie);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->WriteStat(stat, statMask, cookie);
}


static status_t
nfs4_rename_attr(fs_volume* volume, fs_vnode* fromVnode, const char* fromName,
	fs_vnode* toVnode, const char* toName)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(toVnode->private_node);
	VnodeToInodeLocker to(vti);
	Inode* toInode = vti->Get();
	if (toInode == NULL)
		return B_ENTRY_NOT_FOUND;

	vti = reinterpret_cast<VnodeToInode*>(fromVnode->private_node);
	VnodeToInodeLocker from(vti);
	Inode* fromInode = vti->Get();
	if (fromInode == NULL)
		return B_ENTRY_NOT_FOUND;

	return Inode::Rename(fromInode, toInode, fromName, toName, true);
}


static status_t
nfs4_remove_attr(fs_volume* volume, fs_vnode* vnode, const char* name)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->Remove(name, NF4NAMEDATTR, NULL);
}


static status_t
nfs4_test_lock(fs_volume* volume, fs_vnode* vnode, void* _cookie,
	struct flock* lock)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	OpenFileCookie* cookie = reinterpret_cast<OpenFileCookie*>(_cookie);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p, lock = %p", volume,
		vti->ID(), _cookie, lock);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	return inode->TestLock(cookie, lock);
}


static status_t
nfs4_acquire_lock(fs_volume* volume, fs_vnode* vnode, void* _cookie,
			const struct flock* lock, bool wait)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	OpenFileCookie* cookie = reinterpret_cast<OpenFileCookie*>(_cookie);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p, lock = %p", volume,
		vti->ID(), _cookie, lock);


	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	inode->RevalidateFileCache();
	return inode->AcquireLock(cookie, lock, wait);
}


static status_t
nfs4_release_lock(fs_volume* volume, fs_vnode* vnode, void* _cookie,
			const struct flock* lock)
{
	VnodeToInode* vti = reinterpret_cast<VnodeToInode*>(vnode->private_node);
	TRACE("volume = %p, vnode = %" B_PRIi64 ", cookie = %p, lock = %p", volume,
		vti->ID(), _cookie, lock);

	VnodeToInodeLocker _(vti);
	Inode* inode = vti->Get();
	if (inode == NULL)
		return B_ENTRY_NOT_FOUND;

	if (inode->Type() == S_IFDIR || inode->Type() == S_IFLNK)
		return B_OK;

	OpenFileCookie* cookie = reinterpret_cast<OpenFileCookie*>(_cookie);

	if (lock != NULL)
		return inode->ReleaseLock(cookie, lock);
	else
		return inode->ReleaseAllLocks(cookie);
}


status_t
nfs4_init()
{
	gRPCServerManager = new(std::nothrow) RPC::ServerManager;
	if (gRPCServerManager == NULL)
		return B_NO_MEMORY;

	mutex_init(&gIdMapperLock, "idmapper Init Lock");
	gIdMapper = NULL;

	gWorkQueue = new(std::nothrow) WorkQueue;
	if (gWorkQueue == NULL || gWorkQueue->InitStatus() != B_OK) {
		delete gWorkQueue;
		mutex_destroy(&gIdMapperLock);
		delete gRPCServerManager;
		return B_NO_MEMORY;
	}

	return B_OK;
}


status_t
nfs4_uninit()
{
	RPC::CallbackServer::ShutdownAll();

	delete gIdMapper;
	delete gWorkQueue;
	delete gRPCServerManager;

	mutex_destroy(&gIdMapperLock);

	return B_OK;
}


static status_t
nfs4_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return nfs4_init();
		case B_MODULE_UNINIT:
			return nfs4_uninit();
		default:
			return B_ERROR;
	}
}


fs_volume_ops gNFSv4VolumeOps = {
	nfs4_unmount,
	nfs4_read_fs_info,
	NULL,
	NULL,
	nfs4_get_vnode,
};

fs_vnode_ops gNFSv4VnodeOps = {
	nfs4_lookup,
	NULL,	// get_vnode_name()
	nfs4_put_vnode,
	nfs4_remove_vnode,

	/* VM file access */
	NULL,	// can_page()
	nfs4_read_pages,
	nfs4_write_pages,

	nfs4_io,
	NULL,	// cancel_io()

	nfs4_get_file_map,

	NULL,	// ioctl()
	nfs4_set_flags,
	NULL,	// fs_select()
	NULL,	// fs_deselect()
	nfs4_fsync,

	nfs4_read_symlink,
	nfs4_create_symlink,

	nfs4_link,
	nfs4_unlink,
	nfs4_rename,

	nfs4_access,
	nfs4_read_stat,
	nfs4_write_stat,
	NULL,	// fs_preallocate()

	/* file operations */
	nfs4_create,
	nfs4_open,
	nfs4_close,
	nfs4_free_cookie,
	nfs4_read,
	nfs4_write,

	/* directory operations */
	nfs4_create_dir,
	nfs4_remove_dir,
	nfs4_open_dir,
	nfs4_close_dir,
	nfs4_free_dir_cookie,
	nfs4_read_dir,
	nfs4_rewind_dir,

	/* attribute directory operations */
	nfs4_open_attr_dir,
	nfs4_close_attr_dir,
	nfs4_free_attr_dir_cookie,
	nfs4_read_attr_dir,
	nfs4_rewind_attr_dir,

	/* attribute operations */
	nfs4_create_attr,
	nfs4_open_attr,
	nfs4_close_attr,
	nfs4_free_attr_cookie,
	nfs4_read_attr,
	nfs4_write_attr,

	nfs4_read_attr_stat,
	nfs4_write_attr_stat,
	nfs4_rename_attr,
	nfs4_remove_attr,

	/* support for node and FS layers */
	NULL,	// create_special_node
	NULL,	// get_super_vnode

	/* lock operations */
	nfs4_test_lock,
	nfs4_acquire_lock,
	nfs4_release_lock,
};

static file_system_module_info sNFSv4ModuleInfo = {
	{
		"file_systems/nfs4" B_CURRENT_FS_API_VERSION,
		0,
		nfs4_std_ops,
	},

	"nfs4",								// short_name
	"Network File System version 4",	// pretty_name

	// DDM flags
	0,

	// scanning
	NULL,	// identify_partition()
	NULL,	// scan_partition()
	NULL,	// free_identify_partition_cookie()
	NULL,	// free_partition_content_cookie()

	nfs4_mount,
};

module_info* modules[] = {
	(module_info*)&sNFSv4ModuleInfo,
	NULL,
};

