/*
 * Copyright 2012, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */

#include <IPCLooper.h>

#include <AutoDeleter.h>


struct run_method_info {
	IPCLooper*			looper;
	BReference<BObject>	object;
	BMessage*			message;
};


int32 ThreadFunctRunMethod(void* data)
{
	run_method_info* info = (run_method_info*)data;
	info->looper->_RunMethod(info->object, info->message);
	delete info;
	return B_OK;
}


IPCLooper::IPCLooper()
	:
	fNextHandle(0)
{
	
}


int32
IPCLooper::AcquireRemoteReference(const BReference<BObject>& local,
	team_id remoteTeam)
{
	handle_t handle = _GetHandle();
	fStrongReferences[handle] = local;

	std::map<handle_t, std::vector<handle_t> >::iterator it
		= fTeamStrongHandles.find(remoteTeam);
	if (it == fTeamStrongHandles.end())
		fTeamStrongHandles[remoteTeam].push_back(handle);
	else
		it->second.push_back(handle);

	return handle;
}


void
IPCLooper::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRunMethod:
		{
			handle_t handle;
			if (message->FindInt32("handle", (int32*)&handle) != B_OK)
				break;
			BReference<BObject> object = _FindStrongReference(handle);
			if (object.Get() == NULL)
				break;

			int32 priority;
			if (message->FindInt32("priority", &priority) != B_OK)
				priority = B_NORMAL_PRIORITY;

			run_method_info* info = new run_method_info;
			if (info == NULL)
				break;
			info->looper = this;
			info->object = object;
			info->message = DetachCurrentMessage();
			thread_id id = spawn_thread(ThreadFunctRunMethod, "run method",
				priority, info);
			if (resume_thread(id) != B_OK) {
				delete info->message;
				delete info;
			}
			break;
		}
		case kMsgReleaseReference:
		{
			team_id team;
			if (message->FindInt32("team", &team) != B_OK)
				break;
			handle_t handle;
			if (message->FindInt32("handle", (int32*)&handle) != B_OK)
				break;
			_ReleaseRemoteReference(handle, team);
			break;
		}
	}		
}


void
IPCLooper::_RunMethod(BReference<BObject>& object, BMessage* message)
{
	ObjectDeleter<BMessage> _(message);

	BString method;
	if (message->FindString("method", &method) != B_OK)
		return;
	BMessage messageIn;
	if (message->FindMessage("args", &messageIn) != B_OK)
		return;	
	PArgs in;
	in.SetBackend(messageIn);	
	PArgs out;
	object->RunMethod(method, in, out);

	BMessage outMessage = out.GetBackend();
	message->SendReply(&outMessage);	
}


bool
IPCLooper::_ReleaseRemoteReference(handle_t handle, team_id remoteTeam)
{
	std::map<handle_t, std::vector<handle_t> >::iterator teamIt
		= fTeamStrongHandles.find(remoteTeam);
	if (teamIt != fTeamStrongHandles.end()) {
		std::vector<handle_t>& handleList = teamIt->second;
		for (unsigned int i = 0; i < handleList.size(); i++) {
			if (handleList[i] != handle)
				continue;
			handleList.erase(handleList.begin() + i);
			break;
		}
	}

	std::map<handle_t, BReference<BObject> >::iterator it
		= fStrongReferences.find(handle);
	if (it != fStrongReferences.end())
		fStrongReferences.erase(it);

	return true;
}
									

void
IPCLooper::_CheckForDeadTeams()
{
}


void
IPCLooper::_TeamGone(team_id team)
{
}


BReference<BObject>
IPCLooper::_FindStrongReference(handle_t handle)
{
	std::map<handle_t, BReference<BObject> >::iterator it
		= fStrongReferences.find(handle);
	if (it == fStrongReferences.end())
		return NULL;
	return it->second;
}


BWeakReference<BObject>
IPCLooper::_FindWeakReference(handle_t handle)
{
	std::map<handle_t, BWeakReference<BObject> >::iterator it
		= fWeakReferences.find(handle);
	if (it == fWeakReferences.end())
		return NULL;
	return it->second;
}


handle_t
IPCLooper::_GetHandle()
{
	return fNextHandle++;
}
