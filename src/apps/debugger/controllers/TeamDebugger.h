/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef TEAM_DEBUGGER_H
#define TEAM_DEBUGGER_H


#include <debugger.h>
#include <Looper.h>

#include <debug_support.h>

#include "DebugEvent.h"
#include "Team.h"
#include "TeamSettings.h"
#include "ThreadHandler.h"
#include "UserInterface.h"
#include "Worker.h"


class DebuggerInterface;
class DebugReportGenerator;
class FileManager;
class SettingsManager;
class TeamDebugInfo;
class TeamMemoryBlockManager;
class WatchpointManager;


class TeamDebugger : public BLooper, private UserInterfaceListener,
	private JobListener, private Team::Listener {
public:
	class Listener;

public:
								TeamDebugger(Listener* listener,
									UserInterface* userInterface,
									SettingsManager* settingsManager);
								~TeamDebugger();

			status_t			Init(team_id teamID, thread_id threadID,
									bool stopInMain);

			void				Activate();

			team_id				TeamID() const	{ return fTeamID; }

	virtual	void				MessageReceived(BMessage* message);

private:
	// UserInterfaceListener
	virtual	void				FunctionSourceCodeRequested(
									FunctionInstance* function);
	virtual void				SourceEntryLocateRequested(
									const char* sourcePath,
									const char* locatedPath);
	virtual	void				ImageDebugInfoRequested(Image* image);
	virtual	void				ValueNodeValueRequested(CpuState* cpuState,
									ValueNodeContainer* container,
									ValueNode* valueNode);
	virtual	void				ThreadActionRequested(thread_id threadID,
									uint32 action, target_addr_t address);
	virtual	void				SetBreakpointRequested(target_addr_t address,
									bool enabled);
	virtual	void				SetBreakpointEnabledRequested(
									UserBreakpoint* breakpoint,
									bool enabled);
	virtual	void				ClearBreakpointRequested(target_addr_t address);
	virtual	void				ClearBreakpointRequested(
									UserBreakpoint* breakpoint);
	virtual	void				SetWatchpointRequested(target_addr_t address,
									uint32 type, int32 length, bool enabled);
	virtual	void				SetWatchpointEnabledRequested(
									Watchpoint *watchpoint, bool enabled);
	virtual	void				ClearWatchpointRequested(target_addr_t address);
	virtual	void				ClearWatchpointRequested(
									Watchpoint* breakpoint);

	virtual void				InspectRequested(target_addr_t address,
									TeamMemoryBlock::Listener* listener);

	virtual void				DebugReportRequested(entry_ref* targetPath);

	virtual	bool				UserInterfaceQuitRequested(
									QuitOption quitOption);

	// JobListener
	virtual	void				JobDone(Job* job);
	virtual	void				JobFailed(Job* job);
	virtual	void				JobAborted(Job* job);

	// Team::Listener
	virtual	void				ThreadStateChanged(
									const ::Team::ThreadEvent& event);
	virtual	void				ThreadCpuStateChanged(
									const ::Team::ThreadEvent& event);
	virtual	void				ThreadStackTraceChanged(
									const ::Team::ThreadEvent& event);
	virtual	void				ImageDebugInfoChanged(
									const ::Team::ImageEvent& event);

private:
	struct ImageHandler;
	struct ImageHandlerHashDefinition;
	struct ImageInfoPendingThread;
	struct ImageInfoPendingThreadHashDefinition;

	typedef BOpenHashTable<ImageHandlerHashDefinition> ImageHandlerTable;
	typedef BOpenHashTable<ImageInfoPendingThreadHashDefinition>
		ImageInfoPendingThreadTable;

private:
	static	status_t			_DebugEventListenerEntry(void* data);
			status_t			_DebugEventListener();

			void				_HandleDebuggerMessage(DebugEvent* event);

			bool				_HandleTeamDeleted(
									TeamDeletedEvent* event);
			bool				_HandleThreadCreated(
									ThreadCreatedEvent* event);
			bool				_HandleThreadRenamed(
									ThreadRenamedEvent* event);
			bool				_HandleThreadPriorityChanged(
									ThreadPriorityChangedEvent* event);
			bool				_HandleThreadDeleted(
									ThreadDeletedEvent* event);
			bool				_HandleImageCreated(
									ImageCreatedEvent* event);
			bool				_HandleImageDeleted(
									ImageDeletedEvent* event);

			void				_HandleImageDebugInfoChanged(image_id imageID);
			void				_HandleImageFileChanged(image_id imageID);

			void				_HandleSetUserBreakpoint(target_addr_t address,
									bool enabled);
			void				_HandleSetUserBreakpoint(
									UserBreakpoint* breakpoint, bool enabled);
			void				_HandleClearUserBreakpoint(
									target_addr_t address);
			void				_HandleClearUserBreakpoint(
									UserBreakpoint* breakpoint);

			void				_HandleSetWatchpoint(target_addr_t address,
									uint32 type, int32 length, 	bool enabled);
			void				_HandleSetWatchpoint(
									Watchpoint* watchpoint, bool enabled);
			void				_HandleClearWatchpoint(	target_addr_t address);
			void				_HandleClearWatchpoint(Watchpoint* watchpoint);

			void				_HandleInspectAddress(
									target_addr_t address,
									TeamMemoryBlock::Listener* listener);

			ThreadHandler*		_GetThreadHandler(thread_id threadID);

			status_t			_AddImage(const ImageInfo& imageInfo,
									Image** _image = NULL);

			void				_LoadSettings();
			void				_SaveSettings();

			void				_NotifyUser(const char* title,
									const char* text,...);

private:
			Listener*			fListener;
			SettingsManager*	fSettingsManager;
			::Team*				fTeam;
			team_id				fTeamID;
			ThreadHandlerTable	fThreadHandlers;
									// protected by the team lock
			ImageHandlerTable*	fImageHandlers;
			ImageInfoPendingThreadTable* fImageInfoPendingThreads;
			DebuggerInterface*	fDebuggerInterface;
			TeamDebugInfo*		fTeamDebugInfo;
			FileManager*		fFileManager;
			Worker*				fWorker;
			BreakpointManager*	fBreakpointManager;
			WatchpointManager*	fWatchpointManager;
			TeamMemoryBlockManager*
								fMemoryBlockManager;
			DebugReportGenerator*
								fReportGenerator;
			thread_id			fDebugEventListener;
			UserInterface*		fUserInterface;
	volatile bool				fTerminating;
			bool				fKillTeamOnQuit;
			TeamSettings		fTeamSettings;
};


class TeamDebugger::Listener {
public:
	virtual						~Listener();

	virtual void				TeamDebuggerStarted(TeamDebugger* debugger) = 0;
	virtual	void				TeamDebuggerQuit(TeamDebugger* debugger) = 0;
};


#endif	// TEAM_DEBUGGER_H
