/*
 * Copyright 2010, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Clemens Zeidler <haiku@clemens-zeidler.de>
 */

#include "SessionManager.h"

#include <vector>

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <AppMisc.h>
#include <ServerProtocol.h>

#include "ApplicationPrivate.h"
#include "StackAndTilePrivate.h"

#include "EventMaskWatcher.h"
#include "RosterAppInfo.h"


using namespace std;
using namespace BPrivate;


const char* kSessionDataFile = "session.data";


SATGroupManager::SATGroupManager()
{
	status_t status = create_desktop_connection(&fLink, "SATGroupManager",
		B_LOOPER_PORT_DEFAULT_CAPACITY);
	if (status != B_OK) {
		// TODO: huh?
		debugger("Could not create SATGroupManager's app_server connection!");
		delete this;
		return;
	}
}


SATGroupManager::~SATGroupManager()
{
	delete_port(fLink.ReceiverPort());
}


status_t
SATGroupManager::SaveGroups(BMessage& archive)
{
	status_t status = _StartMessage(kSaveAllGroups);
	if (status != B_OK)
		return status;
	int32 code = B_ERROR;
	fLink.FlushWithReply(code);
	if (code != B_OK)
		return code;
	int32 size;
	if (fLink.Read<int32>(&size) == B_OK) {
		char buffer[size];
		if (fLink.Read(buffer, size) != B_OK)
			return B_ERROR;
		if (archive.Unflatten(buffer) != B_OK)
			return B_ERROR;
	}
	return B_OK;
}


status_t
SATGroupManager::RestoreGroups(const BMessage& archive)
{
	BMessage groupArchive;
	for (int32 i = 0; archive.FindMessage("group", i, &groupArchive) == B_OK;
		i++) {
		status_t status = _StartMessage(kRestoreGroup);
		if (status != B_OK)
			continue;
		int32 size = groupArchive.FlattenedSize();
		char buffer[size];
		if (groupArchive.Flatten(buffer, size) == B_OK) {
			fLink.Attach<int32>(size);
			fLink.Attach(buffer, size);
			int32 code = B_ERROR;
			fLink.FlushWithReply(code);
		}
	}

	return B_OK;
}


status_t
SATGroupManager::_StartMessage(int32 what)
{
	fLink.StartMessage(AS_TALK_TO_DESKTOP_LISTENER);
	fLink.Attach<port_id>(fLink.ReceiverPort());
	fLink.Attach<int32>(kMagicSATIdentifier);
	return fLink.Attach<int32>(what);
}


static status_t
RecursiveDeleteDirectory(BEntry& entry, bool contentOnly)
{
	// empty it
	BDirectory directory(&entry);
	BEntry next;
	while (directory.GetNextEntry(&next) == B_OK) {
		if (next.IsDirectory())
			RecursiveDeleteDirectory(next, false);
		else
			next.Remove();
	}

	if (contentOnly)
		return B_OK;
	return entry.Remove();
};


class SessionSaver : public BLooper, public EventMaskWatcher {
public:
								SessionSaver(TRoster* roster,
									BPath& sessionPath);
								~SessionSaver();

			status_t			Start(AppInfoList& saveApps);
			status_t			WaitTillFinished();

			void				MessageReceived(BMessage* message);

private:
			void				_TiggerSaveSession();
			void				_RemoveTeam(team_id id);
			void				_AppFinished(team_id id, BMessage* appState);
			void				_Finished();

			TRoster*			fRoster;
			BPath&				fSessionPath;
			vector<app_info>	fSaveRequestList;
			team_id				fCurrentTeamId;

			sem_id				fFinishedLock;
			BMessage			fArchive;
};


SessionSaver::SessionSaver(TRoster* roster, BPath& sessionPath)
	:
	EventMaskWatcher(BMessenger(this), B_REQUEST_QUIT),
	fRoster(roster),
	fSessionPath(sessionPath),
	fCurrentTeamId(-1)
{
	fFinishedLock = create_sem(1, "saved finished lock");
}


SessionSaver::~SessionSaver()
{
	fRoster->RemoveWatcher(this);
	delete_sem(fFinishedLock);
}


status_t
SessionSaver::Start(AppInfoList& appList)
{
	status_t status = fRoster->AddWatcher(this);
	if (status != B_OK)
		return status;

	for (AppInfoList::Iterator it = appList.It(); it.IsValid(); ++it) {
		RosterAppInfo* info = *it;
		fSaveRequestList.push_back(*info);
	}

	acquire_sem(fFinishedLock);
	_TiggerSaveSession();
	Run();

	return status;
}


status_t
SessionSaver::WaitTillFinished()
{
	acquire_sem(fFinishedLock);
	return B_OK;
}


void
SessionSaver::MessageReceived(BMessage* message)
{
	team_id team;
	switch(message->what) {
		case kStateSavedMsg:
		{
			if (message->FindInt32("be:team", &team) != B_OK) {
				// should not happen
				break;
			}
			BMessage state;
			message->FindMessage("state", &state);
			_AppFinished(team, &state);
			_RemoveTeam(team);
			break;
		}

		case B_SOME_APP_QUIT:
			if (message->FindInt32("be:team", &team) != B_OK) {
				// should not happen
				break;
			}
			_RemoveTeam(team);
			break;

		case kSetProgressMsg:
		{
			float progress;
			message->FindFloat("progress", &progress);
			// TODO set it somewhere
			break;
		}

		case kSaveAborted:
			message->SendReply(kSaveAborted);
			break;

		default:
			BLooper::MessageReceived(message);
	}
}


void
SessionSaver::_TiggerSaveSession()
{
	if (fSaveRequestList.size() == 0) {
		_Finished();
		return;
	}
	app_info& info = fSaveRequestList[0];

	BMessage saveMessage(kSaveStateMsg);
	saveMessage.AddMessenger("messenger", BMessenger(this));
	saveMessage.AddString("session_path", fSessionPath.Path());

	BMessenger appMessenger(NULL, info.team);
	status_t status = appMessenger.SendMessage(&saveMessage);
	if (status != B_OK) {
		_RemoveTeam(info.team);
		return;
	}
	fCurrentTeamId = info.team;
}


void
SessionSaver::_RemoveTeam(team_id id)
{
	for (vector<app_info>::iterator it = fSaveRequestList.begin();
		 it != fSaveRequestList.end(); it++) {
		if (it->team != id)
			continue;
		fSaveRequestList.erase(it);
		if (fSaveRequestList.size() == 0) {
			_Finished();
			return;
		}
		break;
	}

	if (fCurrentTeamId == id)
		_TiggerSaveSession();
}


void
SessionSaver::_AppFinished(team_id id, BMessage* appState)
{
	app_info* appInfo = NULL;
	for (vector<app_info>::iterator it = fSaveRequestList.begin();
		 it != fSaveRequestList.end(); it++) {
		if (it->team == id) {
			appInfo = &(*it);
			break;
		}
	}
	if (appInfo == NULL)
		return;

	appState->AddInt32("team", appInfo->team);
	BPath path(&appInfo->ref);
	appState->AddString("path", path.Path());
	appState->AddString("signature", appInfo->signature);

	fArchive.AddMessage("application", appState);
}


void
SessionSaver::_Finished()
{
	SATGroupManager satGroupManager;
	BMessage satGroups;
	if (satGroupManager.SaveGroups(satGroups) == B_OK)
		fArchive.AddMessage("sat_groups", &satGroups);

	// first delete old session data
	BEntry entry(fSessionPath.Path());
	RecursiveDeleteDirectory(entry, true);
	// store message to file
	BPath path(fSessionPath);
	path.Append(kSessionDataFile);
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	fArchive.Flatten(&file);

	release_sem(fFinishedLock);
	PostMessage(B_QUIT_REQUESTED);
}


/*! Make restoring of an session asynchronous.This make it also possible to use
the BRoster to start an app otherwise it would deadlock. */
class SessionStarter : public BLooper {
public:
			void				Start(const BPath& sessionPath);

			void				MessageReceived(BMessage* message);
private:
			void				_RestoreSession();

			BMessage			fArchive;
			BPath				fSessionPath;
};


void
SessionStarter::MessageReceived(BMessage* message)
{
	switch(message->what) {
		case kRestoreStateMsg:
			_RestoreSession();
			break;

		default:
			BLooper::MessageReceived(message);
	}
}


void
SessionStarter::Start(const BPath& sessionPath)
{
	fSessionPath.SetTo(sessionPath.Path());

	PostMessage(kRestoreStateMsg);
	Run();
}


void
SessionStarter::_RestoreSession()
{
	BPath dataPath(fSessionPath); 
	dataPath.Append(kSessionDataFile);

	BMessage sessionData;
	BFile file(dataPath.Path(), B_READ_ONLY);
	status_t status = sessionData.Unflatten(&file);
	if (status != B_OK)
		return;

	std::vector<team_id> teams;

	BRoster roster;
	BMessage appState;
	for (int32 i = 0; sessionData.FindMessage("application", i, &appState)
		== B_OK; i++) {
 
		appState.what = kRestoreStateMsg;
		appState.AddMessenger("messenger", BMessenger(this));
		appState.AddString("session_path", fSessionPath.Path());

		// launch by path
		BString path;
		if (appState.FindString("path", &path) != B_OK)
			continue;

		entry_ref ref;
 		team_id team;
		if (get_ref_for_path(path, &ref) == B_OK) {
			if (roster.Launch(&ref, &appState, &team) != B_BAD_VALUE) {
				teams.push_back(team);
				continue;
			}
		}

		// try launch by signature
		BString signature;
		if (appState.FindString("signature", &signature) != B_OK)
			continue;
		if (roster.Launch(signature.String(), &appState, &team) != B_BAD_VALUE)
			teams.push_back(team);
	}

	// restore S&T groups
	/* First ping all teams to make sure everything is ready.
	Getting a reply from the BApplication means restoring of the application
	has been finished and the ReadyToRun function has been called. */
	for (unsigned int i = 0; i < teams.size(); i++) {
		BMessenger messenger(NULL, teams[i]);
		BMessage message(B_GET_PROPERTY);
		message.AddSpecifier("Name");
		BMessage reply;
		messenger.SendMessage(&message, &reply);
	}

	SATGroupManager satGroupManager;
	BMessage satGroups;
	if (sessionData.FindMessage("sat_groups", &satGroups) == B_OK)
		satGroupManager.RestoreGroups(satGroups);
}


SessionManager::SessionManager(TRoster* roster)
	:
	fRoster(roster)
{

}


status_t
SessionManager::SaveSession(const char* sessionName, AppInfoList& apps)
{
	BPath sessionPath;
	_SessionPath(sessionName, sessionPath);

	SessionSaver* saver = new SessionSaver(fRoster, sessionPath);
	saver->Start(apps);

	return saver->WaitTillFinished();
}


status_t
SessionManager::RestoreSession(const char* sessionName)
{
	BMessage sessionData;

	BPath sessionPath;
	_SessionPath(sessionName, sessionPath);

	SessionStarter* starter = new SessionStarter;
	starter->Start(sessionPath);

	return B_OK;
}


void
SessionManager::_SessionPath(const char* sessionName, BPath& path)
{
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		path.SetTo("/home/config/settings");
	path.Append("sessions");
	create_directory(path.Path(), 555);
	path.Append(sessionName);
	create_directory(path.Path(), 555);
}
