/*
 * Copyright 2010, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Clemens Zeidler <haiku@clemens-zeidler.de>
 */
#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H


#include <Path.h>
#include <String.h>

#include <AppServerLink.h>

#include "RosterSettingsCharStream.h"
#include "TRoster.h"


#define kLastSession "LastSession"


class SATGroupManager {
public:
								SATGroupManager();
								~SATGroupManager();

			status_t			SaveGroups(BMessage& archive);
			status_t			RestoreGroups(const BMessage& archive);
private:
			status_t			_StartMessage(int32 what);

			BPrivate::PortLink	fLink;
};


class SessionManagerSettings {
public:
								SessionManagerSettings(const char* path);

			bool				SaveSettings();

			bool				fEnabled;
			BString				fSessionToLoad;
private:
			bool				_LoadSettings();

			BPath				fPath;
};


class SessionManager {
public:
								SessionManager(TRoster* roster);

			status_t			SaveSession(const char* sessionName,
									AppInfoList& apps);
			status_t			RestoreSession(const char* sessionName);

private:
			void				_SessionPath(const char* sessionName,
									BPath& path);

			TRoster*			fRoster;
};

#endif // SESSION_MANAGER_H
