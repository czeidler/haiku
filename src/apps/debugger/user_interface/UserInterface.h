/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H


#include <OS.h>

#include <Referenceable.h>

#include "TeamMemoryBlock.h"
#include "Types.h"


class entry_ref;

class CpuState;
class FunctionInstance;
class Image;
class StackFrame;
class Team;
class TeamUiSettings;
class Thread;
class TypeComponentPath;
class UserBreakpoint;
class UserInterfaceListener;
class ValueNode;
class ValueNodeContainer;
class Variable;
class Watchpoint;


enum user_notification_type {
	USER_NOTIFICATION_INFO,
	USER_NOTIFICATION_WARNING,
	USER_NOTIFICATION_ERROR
};


class UserInterface : public BReferenceable {
public:
	virtual						~UserInterface();

	virtual const char*			ID() const = 0;

	virtual	status_t			Init(Team* team,
									UserInterfaceListener* listener) = 0;
	virtual	void				Show() = 0;
	virtual	void				Terminate() = 0;
									// shut down the UI *now* -- no more user
									// feedback

	virtual status_t			LoadSettings(const TeamUiSettings* settings)
									= 0;
	virtual status_t			SaveSettings(TeamUiSettings*& settings)
									const = 0;

	virtual	void				NotifyUser(const char* title,
									const char* message,
									user_notification_type type) = 0;
	virtual	int32				SynchronouslyAskUser(const char* title,
									const char* message, const char* choice1,
									const char* choice2, const char* choice3)
									= 0;
									// returns -1, if not implemented or user
									// cannot be asked
};


class UserInterfaceListener {
public:
			enum QuitOption {
				QUIT_OPTION_ASK_USER,
				QUIT_OPTION_ASK_KILL_TEAM,
				QUIT_OPTION_ASK_RESUME_TEAM
			};

public:
	virtual						~UserInterfaceListener();

	virtual	void				FunctionSourceCodeRequested(
									FunctionInstance* function) = 0;
	virtual void				SourceEntryLocateRequested(
									const char* sourcePath,
									const char* locatedPath) = 0;
	virtual	void				ImageDebugInfoRequested(Image* image) = 0;
	virtual	void				ValueNodeValueRequested(CpuState* cpuState,
									ValueNodeContainer* container,
									ValueNode* valueNode) = 0;
	virtual	void				ThreadActionRequested(thread_id threadID,
									uint32 action,
									target_addr_t address = 0) = 0;

	virtual	void				SetBreakpointRequested(target_addr_t address,
									bool enabled) = 0;
	virtual	void				SetBreakpointEnabledRequested(
									UserBreakpoint* breakpoint,
									bool enabled) = 0;
	virtual	void				ClearBreakpointRequested(
									target_addr_t address) = 0;
	virtual	void				ClearBreakpointRequested(
									UserBreakpoint* breakpoint) = 0;
									// TODO: Consolidate those!

	virtual	void				SetWatchpointRequested(target_addr_t address,
									uint32 type, int32 length,
									bool enabled) = 0;
	virtual	void				SetWatchpointEnabledRequested(
									Watchpoint* watchpoint,
									bool enabled) = 0;
	virtual	void				ClearWatchpointRequested(
									target_addr_t address) = 0;
	virtual	void				ClearWatchpointRequested(
									Watchpoint* watchpoint) = 0;

	virtual void				InspectRequested(
									target_addr_t address,
									TeamMemoryBlock::Listener* listener) = 0;

	virtual void				DebugReportRequested(entry_ref* path) = 0;

	virtual	bool				UserInterfaceQuitRequested(
									QuitOption quitOption
										= QUIT_OPTION_ASK_USER) = 0;
};


#endif	// USER_INTERFACE_H
