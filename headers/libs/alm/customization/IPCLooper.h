/*
 * Copyright 2012, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */
#ifndef	IPC_LOOPER_H
#define	IPC_LOOPER_H


#include <Looper.h>

#include <map>
#include <vector>

#include <Object.h>


const int32 kMsgRunMethod = 'RuMe';
const int32 kMsgReleaseReference ='ReRe';


typedef uint32 handle_t;


class IPCLooper : public BLooper {
public:
								IPCLooper();

	virtual	void				MessageReceived(BMessage* message);

			//! Looper must be locked.
			int32				AcquireRemoteReference(
									const BReference<BObject>& local,
									team_id remoteTeam);
private:
	friend int32 ThreadFunctRunMethod(void* data);

			void				_RunMethod(BReference<BObject>& object,
									BMessage* message);

			bool				_ReleaseRemoteReference(handle_t handle,
									team_id remoteTeam);
			void 				_CheckForDeadTeams();
			void				_TeamGone(team_id team);

			BReference<BObject>	_FindStrongReference(handle_t handle);
			BWeakReference<BObject>	_FindWeakReference(handle_t handle);

			handle_t				_GetHandle();

			handle_t				fNextHandle;

			std::map<handle_t, std::vector<handle_t> >		fTeamStrongHandles;
			std::map<handle_t, BReference<BObject> >		fStrongReferences;
			std::map<handle_t, BWeakReference<BObject> >	fWeakReferences;
};


#endif // IPC_LOOPER_H
