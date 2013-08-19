/*
 * Copyright 2012 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, pdziepak@quarnos.org
 */


#include "FileSystem.h"
#include "Inode.h"
#include "NFS4Server.h"
#include "Request.h"
#include "WorkQueue.h"


NFS4Server::NFS4Server(RPC::Server* serv)
	:
	fThreadCancel(true),
	fWaitCancel(create_sem(0, NULL)),
	fLeaseTime(0),
	fClientIdLastUse(0),
	fUseCount(0),
	fServer(serv)
{
	ASSERT(serv != NULL);

	mutex_init(&fClientIdLock, NULL);
	mutex_init(&fFSLock, NULL);
	mutex_init(&fThreadStartLock, NULL);

}


NFS4Server::~NFS4Server()
{
	fThreadCancel = true;
	fUseCount = 0;
	release_sem(fWaitCancel);
	status_t result;
	wait_for_thread(fThread, &result);

	delete_sem(fWaitCancel);
	mutex_destroy(&fClientIdLock);
	mutex_destroy(&fFSLock);
	mutex_destroy(&fThreadStartLock);
}


uint64
NFS4Server::ServerRebooted(uint64 clientId)
{
	if (clientId != fClientId)
		return fClientId;

	fClientId = ClientId(clientId, true);

	// reclaim all opened files and held locks from all filesystems
	MutexLocker _(fFSLock);
	FileSystem* fs = fFileSystems.Head();
	while (fs != NULL) {
		DoublyLinkedList<OpenState>::Iterator iterator
			= fs->OpenFilesLock().GetIterator();
		OpenState* current = iterator.Next();
		while (current != NULL) {
			current->Reclaim(fClientId);

			current = iterator.Next();
		}
		fs->OpenFilesUnlock();

		fs = fFileSystems.GetNext(fs);
	}

	return fClientId;
}


void
NFS4Server::AddFileSystem(FileSystem* fs)
{
	ASSERT(fs != NULL);

	MutexLocker _(fFSLock);
	fFileSystems.Add(fs);

	fUseCount += fs->OpenFilesCount();
	if (fs->OpenFilesCount() > 0)
		_StartRenewing();
}


void
NFS4Server::RemoveFileSystem(FileSystem* fs)
{
	ASSERT(fs != NULL);

	MutexLocker _(fFSLock);
	fFileSystems.Remove(fs);
	fUseCount -= fs->OpenFilesCount();
}


uint64
NFS4Server::ClientId(uint64 prevId, bool forceNew)
{
	MutexLocker _(fClientIdLock);
	if ((fUseCount == 0 && fClientIdLastUse + (time_t)LeaseTime() < time(NULL))
		|| (forceNew && fClientId == prevId)) {

		Request request(fServer, NULL);
		request.Builder().SetClientID(fServer);

		status_t result = request.Send();
		if (result != B_OK)
			return fClientId;

		uint64 ver;
		result = request.Reply().SetClientID(&fClientId, &ver);
		if (result != B_OK)
			return fClientId;

		request.Reset();
		request.Builder().SetClientIDConfirm(fClientId, ver);

		result = request.Send();
		if (result != B_OK)
			return fClientId;

		result = request.Reply().SetClientIDConfirm();
		if (result != B_OK)
			return fClientId;
	}

	fClientIdLastUse = time(NULL);
	return fClientId;
}


status_t
NFS4Server::FileSystemMigrated()
{
	// reclaim all opened files and held locks from all filesystems
	MutexLocker _(fFSLock);
	FileSystem* fs = fFileSystems.Head();
	while (fs != NULL) {
		fs->Migrate(fServer);
		fs = fFileSystems.GetNext(fs);
	}

	return B_OK;
}


status_t
NFS4Server::_GetLeaseTime()
{
	Request request(fServer, NULL);
	request.Builder().PutRootFH();
	Attribute attr[] = { FATTR4_LEASE_TIME };
	request.Builder().GetAttr(attr, sizeof(attr) / sizeof(Attribute));

	status_t result = request.Send();
	if (result != B_OK)
		return result;

	ReplyInterpreter& reply = request.Reply();

	reply.PutRootFH();

	AttrValue* values;
	uint32 count;
	result = reply.GetAttr(&values, &count);
	if (result != B_OK)
		return result;

	// FATTR4_LEASE_TIME is mandatory
	if (count < 1 || values[0].fAttribute != FATTR4_LEASE_TIME) {
		delete[] values;
		return B_BAD_VALUE;
	}

	fLeaseTime = values[0].fData.fValue32;

	return B_OK;
}


status_t
NFS4Server::_StartRenewing()
{
	if (!fThreadCancel)
		return B_OK;

	MutexLocker _(fThreadStartLock);

	if (!fThreadCancel)
		return B_OK;

	if (fLeaseTime == 0) {
		status_t result = _GetLeaseTime();
		if (result != B_OK)
			return result;
	}

	fThreadCancel = false;
	fThread = spawn_kernel_thread(&NFS4Server::_RenewalThreadStart,
		"NFSv4 Renewal", B_NORMAL_PRIORITY, this);
	if (fThread < B_OK)
		return fThread;

	status_t result = resume_thread(fThread);
	if (result != B_OK) {
		kill_thread(fThread);
		return result;
	}

	return B_OK;
}


status_t
NFS4Server::_Renewal()
{
	while (!fThreadCancel) {
		// TODO: operations like OPEN, READ, CLOSE, etc also renew leases
		status_t result = acquire_sem_etc(fWaitCancel, 1,
			B_RELATIVE_TIMEOUT, sSecToBigTime(fLeaseTime - 2));
		if (result != B_TIMED_OUT) {
			if (result == B_OK)
				release_sem(fWaitCancel);
			return result;
		}

		uint64 clientId = fClientId;

		if (fUseCount == 0) {
			MutexLocker _(fFSLock);
			if (fUseCount == 0) {
				fThreadCancel = true;
				return B_OK;
			}
		}

		Request request(fServer, NULL);
		request.Builder().Renew(clientId);
		result = request.Send();
		if (result != B_OK)
			continue;

		switch (request.Reply().NFS4Error()) {
			case NFS4ERR_CB_PATH_DOWN:
				RecallAll();
				break;
			case NFS4ERR_STALE_CLIENTID:
				ServerRebooted(clientId);
				break;
			case NFS4ERR_LEASE_MOVED:
				FileSystemMigrated();
				break;
		}
	}

	return B_OK;
}


status_t
NFS4Server::_RenewalThreadStart(void* ptr)
{
	ASSERT(ptr != NULL);
	NFS4Server* server = reinterpret_cast<NFS4Server*>(ptr);
	return server->_Renewal();
}


status_t
NFS4Server::ProcessCallback(RPC::CallbackRequest* request,
	Connection* connection)
{
	ASSERT(request != NULL);
	ASSERT(connection != NULL);

	RequestInterpreter req(request);
	ReplyBuilder reply(request->XID());

	status_t result;
	uint32 count = req.OperationCount();

	for (uint32 i = 0; i < count; i++) {
		switch (req.Operation()) {
			case OpCallbackGetAttr:
				result = CallbackGetAttr(&req, &reply);
				break;
			case OpCallbackRecall:
				result = CallbackRecall(&req, &reply);
				break;
			default:
				result = B_NOT_SUPPORTED;
		}

		if (result != B_OK)
			break;
	}

	XDR::WriteStream& stream = reply.Reply()->Stream();
	connection->Send(stream.Buffer(), stream.Size());

	return B_OK;
}


status_t
NFS4Server::CallbackRecall(RequestInterpreter* request, ReplyBuilder* reply)
{
	ASSERT(request != NULL);
	ASSERT(reply != NULL);

	uint32 stateID[3];
	uint32 stateSeq;
	bool truncate;
	FileHandle handle;

	status_t result = request->Recall(&handle, truncate, &stateSeq, stateID);
	if (result != B_OK)
		return result;

	MutexLocker locker(fFSLock);

	Delegation* delegation = NULL;
	FileSystem* current = fFileSystems.Head();
	while (current != NULL) {
		delegation = current->GetDelegation(handle);
		if (delegation != NULL)
			break;

		current = fFileSystems.GetNext(current);
	}
	locker.Unlock();

	if (delegation == NULL) {
		reply->Recall(B_FILE_NOT_FOUND);
		return B_FILE_NOT_FOUND;
	}

	DelegationRecallArgs* args = new(std::nothrow) DelegationRecallArgs;
	args->fDelegation = delegation;
	args->fTruncate = truncate;
	gWorkQueue->EnqueueJob(DelegationRecall, args);

	reply->Recall(B_OK);

	return B_OK;
}


status_t
NFS4Server::CallbackGetAttr(RequestInterpreter* request, ReplyBuilder* reply)
{
	ASSERT(request != NULL);
	ASSERT(reply != NULL);

	FileHandle handle;
	int mask;

	status_t result = request->GetAttr(&handle, &mask);
	if (result != B_OK)
		return result;

	MutexLocker locker(fFSLock);

	Delegation* delegation = NULL;
	FileSystem* current = fFileSystems.Head();
	while (current != NULL) {
		delegation = current->GetDelegation(handle);
		if (delegation != NULL)
			break;

		current = fFileSystems.GetNext(current);
	}
	locker.Unlock();

	if (delegation == NULL) {
		reply->GetAttr(B_FILE_NOT_FOUND, 0, 0, 0);
		return B_FILE_NOT_FOUND;
	}

	struct stat st;
	delegation->GetInode()->Stat(&st);

	uint64 change;
	change = delegation->GetInode()->Change();
	if (delegation->GetInode()->Dirty())
		change++;
	reply->GetAttr(B_OK, mask, st.st_size, change);

	return B_OK;
}


status_t
NFS4Server::RecallAll()
{
	MutexLocker _(fFSLock);
	FileSystem* fs = fFileSystems.Head();
	while (fs != NULL) {
		DoublyLinkedList<Delegation>& list = fs->DelegationsLock();
		DoublyLinkedList<Delegation>::Iterator iterator = list.GetIterator();

		Delegation* current = iterator.Next();
		while (current != NULL) {
			DelegationRecallArgs* args = new(std::nothrow) DelegationRecallArgs;
			args->fDelegation = current;
			args->fTruncate = false;
			gWorkQueue->EnqueueJob(DelegationRecall, args);

			current = iterator.Next();
		}	
		fs->DelegationsUnlock();

		fs = fFileSystems.GetNext(fs);
	}

	return B_OK;
}

