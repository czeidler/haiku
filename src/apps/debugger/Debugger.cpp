/*
 * Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2011-2013, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */


#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include <Application.h>
#include <Message.h>

#include <ArgumentVector.h>
#include <AutoDeleter.h>
#include <AutoLocker.h>
#include <ObjectList.h>

#include "debug_utils.h"

#include "CommandLineUserInterface.h"
#include "GraphicalUserInterface.h"
#include "MessageCodes.h"
#include "SettingsManager.h"
#include "SignalSet.h"
#include "TeamDebugger.h"
#include "TeamsWindow.h"
#include "TypeHandlerRoster.h"
#include "ValueHandlerRoster.h"


extern const char* __progname;
const char* kProgramName = __progname;

static const char* const kDebuggerSignature
	= "application/x-vnd.Haiku-Debugger";


static const char* kUsage =
	"Usage: %s [ <options> ]\n"
	"       %s [ <options> ] <command line>\n"
	"       %s [ <options> ] --team <team>\n"
	"       %s [ <options> ] --thread <thread>\n"
	"\n"
	"The first form starts the debugger displaying a requester to choose a\n"
	"running team to debug respectively to specify the program to run and\n"
	"debug.\n"
	"\n"
	"The second form runs the given command line and attaches the debugger to\n"
	"the new team. Unless specified otherwise the program will be stopped at\n"
	"the beginning of its main() function.\n"
	"\n"
	"The third and fourth forms attach the debugger to a running team. The\n"
	"fourth form additionally stops the specified thread.\n"
	"\n"
	"Options:\n"
	"  -h, --help        - Print this usage info and exit.\n"
	"  -c, --cli         - Use command line user interface\n"
	"  -s, --save-report - Save crash report for the targetted team and exit.\n"
	"                      Implies --cli.\n"
;


static void
print_usage_and_exit(bool error)
{
    fprintf(error ? stderr : stdout, kUsage, kProgramName, kProgramName,
    	kProgramName, kProgramName);
    exit(error ? 1 : 0);
}


struct Options {
	int					commandLineArgc;
	const char* const*	commandLineArgv;
	team_id				team;
	thread_id			thread;
	bool				useCLI;
	bool				saveReport;
	const char*			reportPath;

	Options()
		:
		commandLineArgc(0),
		commandLineArgv(NULL),
		team(-1),
		thread(-1),
		useCLI(false),
		saveReport(false),
		reportPath(NULL)
	{
	}
};


struct DebuggedProgramInfo {
	team_id		team;
	thread_id	thread;
	bool		stopInMain;
};


static bool
parse_arguments(int argc, const char* const* argv, bool noOutput,
	Options& options)
{
	optind = 1;

	while (true) {
		static struct option sLongOptions[] = {
			{ "help", no_argument, 0, 'h' },
			{ "cli", no_argument, 0, 'c' },
			{ "save-report", optional_argument, 0, 's' },
			{ "team", required_argument, 0, 't' },
			{ "thread", required_argument, 0, 'T' },
			{ 0, 0, 0, 0 }
		};

		opterr = 0; // don't print errors

		int c = getopt_long(argc, (char**)argv, "+chs", sLongOptions, NULL);
		if (c == -1)
			break;

		switch (c) {
			case 'c':
				options.useCLI = true;
				break;

			case 'h':
				if (noOutput)
					return false;
				print_usage_and_exit(false);
				break;

			case 's':
			{
				options.useCLI = true;
				options.saveReport = true;
				options.reportPath = optarg;
				break;
			}

			case 't':
			{
				options.team = strtol(optarg, NULL, 0);
				if (options.team <= 0) {
					if (noOutput)
						return false;
					print_usage_and_exit(true);
				}
				break;
			}

			case 'T':
			{
				options.thread = strtol(optarg, NULL, 0);
				if (options.thread <= 0) {
					if (noOutput)
						return false;
					print_usage_and_exit(true);
				}
				break;
			}

			default:
				if (noOutput)
					return false;
				print_usage_and_exit(true);
				break;
		}
	}

	if (optind < argc) {
		options.commandLineArgc = argc - optind;
		options.commandLineArgv = argv + optind;
	}

	int exclusiveParams = 0;
	if (options.team > 0)
		exclusiveParams++;
	if (options.thread > 0)
		exclusiveParams++;
	if (options.commandLineArgc > 0)
		exclusiveParams++;

	if (exclusiveParams == 0) {
		return true;
	} else if (exclusiveParams != 1) {
		if (noOutput)
			return false;
		print_usage_and_exit(true);
	}

	return true;
}

static status_t
global_init()
{
	status_t error = TypeHandlerRoster::CreateDefault();
	if (error != B_OK)
		return error;

	error = ValueHandlerRoster::CreateDefault();
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * Finds or runs the program to debug, depending on the command line options.
 * @param options The parsed command line options.
 * @param _info The info for the program to fill in. Will only be filled in
 *		  if successful.
 * @return \c true, if the program has been found or ran.
 */
static bool
get_debugged_program(const Options& options, DebuggedProgramInfo& _info)
{
	team_id team = options.team;
	thread_id thread = options.thread;
	bool stopInMain = false;

	// If command line arguments were given, start the program.
	if (options.commandLineArgc > 0) {
		printf("loading program: \"%s\" ...\n", options.commandLineArgv[0]);
		// TODO: What about the CWD?
		thread = load_program(options.commandLineArgv,
			options.commandLineArgc, false);
		if (thread < 0) {
			// TODO: Notify the user!
			fprintf(stderr, "Error: Failed to load program \"%s\": %s\n",
				options.commandLineArgv[0], strerror(thread));
			return false;
		}

		team = thread;
			// main thread ID == team ID
		stopInMain = true;
	}

	// no parameters given, prompt the user to attach to a team
	if (team < 0 && thread < 0)
		return false;

	// no team, but a thread -- get team
	if (team < 0) {
		printf("no team yet, getting thread info...\n");
		thread_info threadInfo;
		status_t error = get_thread_info(thread, &threadInfo);
		if (error != B_OK) {
			// TODO: Notify the user!
			fprintf(stderr, "Error: Failed to get info for thread \"%" B_PRId32
				"\": %s\n", thread, strerror(error));
			return false;
		}

		team = threadInfo.team;
	}
	printf("team: %" B_PRId32 ", thread: %" B_PRId32 "\n", team, thread);

	_info.team = team;
	_info.thread = thread;
	_info.stopInMain = stopInMain;
	return true;
}


/**
 * Creates a TeamDebugger for the given team. If userInterface is given,
 * that user interface is used (the caller retains its reference), otherwise
 * a graphical user interface is created.
 */
static TeamDebugger*
start_team_debugger(team_id teamID, SettingsManager* settingsManager,
	TeamDebugger::Listener* listener, thread_id threadID = -1,
	bool stopInMain = false, UserInterface* userInterface = NULL,
	status_t* _result = NULL)
{
	if (teamID < 0)
		return NULL;

	BReference<UserInterface> userInterfaceReference;
	if (userInterface == NULL) {
		userInterface = new(std::nothrow) GraphicalUserInterface;
		if (userInterface == NULL) {
			// TODO: Notify the user!
			fprintf(stderr, "Error: Out of memory!\n");
			return NULL;
		}

		userInterfaceReference.SetTo(userInterface, true);
	}

	status_t error = B_NO_MEMORY;

	TeamDebugger* debugger = new(std::nothrow) TeamDebugger(listener,
		userInterface, settingsManager);
	if (debugger)
		error = debugger->Init(teamID, threadID, stopInMain);

	if (error != B_OK) {
		printf("Error: debugger for team %" B_PRId32 " failed to init: %s!\n",
			teamID, strerror(error));
		delete debugger;
		debugger = NULL;
	} else
		printf("debugger for team %" B_PRId32 " created and initialized "
			"successfully!\n", teamID);

	if (_result != NULL)
		*_result = error;
	return debugger;
}


// #pragma mark - Debugger application class


class Debugger : public BApplication, private TeamDebugger::Listener {
public:
								Debugger();
								~Debugger();

			status_t			Init();
	virtual void 				MessageReceived(BMessage* message);
	virtual void 				ReadyToRun();
	virtual void 				ArgvReceived(int32 argc, char** argv);

private:
			typedef BObjectList<TeamDebugger>	TeamDebuggerList;

private:
	// TeamDebugger::Listener
	virtual void 				TeamDebuggerStarted(TeamDebugger* debugger);
	virtual void 				TeamDebuggerQuit(TeamDebugger* debugger);

	virtual bool 				QuitRequested();
	virtual void 				Quit();

			TeamDebugger* 		_FindTeamDebugger(team_id teamID) const;

			status_t			_StartNewTeam(const char* path, const char* args);
			status_t			_StartOrFindTeam(Options& options);

private:
			SettingsManager		fSettingsManager;
			TeamDebuggerList	fTeamDebuggers;
			int32				fRunningTeamDebuggers;
			TeamsWindow*		fTeamsWindow;
};


// #pragma mark - CliDebugger


class CliDebugger : private TeamDebugger::Listener {
public:
								CliDebugger();
								~CliDebugger();

			bool				Run(const Options& options);

private:
	// TeamDebugger::Listener
	virtual void 				TeamDebuggerStarted(TeamDebugger* debugger);
	virtual void 				TeamDebuggerQuit(TeamDebugger* debugger);
};


// #pragma mark - Debugger application class


Debugger::Debugger()
	:
	BApplication(kDebuggerSignature),
	fRunningTeamDebuggers(0),
	fTeamsWindow(NULL)
{
}


Debugger::~Debugger()
{
	ValueHandlerRoster::DeleteDefault();
	TypeHandlerRoster::DeleteDefault();
}


status_t
Debugger::Init()
{
	status_t error = global_init();
	if (error != B_OK)
		return error;

	return fSettingsManager.Init();
}


void
Debugger::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_SHOW_TEAMS_WINDOW:
		{
            if (fTeamsWindow) {
               	fTeamsWindow->Activate(true);
               	break;
            }

           	try {
				fTeamsWindow = TeamsWindow::Create(&fSettingsManager);
				if (fTeamsWindow != NULL)
					fTeamsWindow->Show();
           	} catch (...) {
				// TODO: Notify the user!
				fprintf(stderr, "Error: Failed to create Teams window\n");
           	}
			break;
		}
		case MSG_TEAMS_WINDOW_CLOSED:
		{
			fTeamsWindow = NULL;
			Quit();
			break;
		}
		case MSG_DEBUG_THIS_TEAM:
		{
			int32 teamID;
			if (message->FindInt32("team", &teamID) != B_OK)
				break;

			start_team_debugger(teamID, &fSettingsManager, this);
			break;
		}
		case MSG_START_NEW_TEAM:
		{
			const char* teamPath = NULL;
			const char* args = NULL;

			message->FindString("path", &teamPath);
			message->FindString("arguments", &args);

			status_t result = _StartNewTeam(teamPath, args);
			BMessage reply;
			reply.AddInt32("status", result);
			message->SendReply(&reply);
			break;
		}
		case MSG_TEAM_DEBUGGER_QUIT:
		{
			int32 threadID;
			if (message->FindInt32("thread", &threadID) == B_OK)
				wait_for_thread(threadID, NULL);

			--fRunningTeamDebuggers;
			Quit();
			break;
		}
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


void
Debugger::ReadyToRun()
{
	if (fRunningTeamDebuggers == 0)
	   PostMessage(MSG_SHOW_TEAMS_WINDOW);
}


void
Debugger::ArgvReceived(int32 argc, char** argv)
{
	Options options;
	if (!parse_arguments(argc, argv, true, options)) {
		printf("Debugger::ArgvReceived(): parsing args failed!\n");
		return;
	}

	_StartOrFindTeam(options);

}


void
Debugger::TeamDebuggerStarted(TeamDebugger* debugger)
{
	printf("debugger for team %" B_PRId32 " started...\n", debugger->TeamID());

 	// Note: see TeamDebuggerQuit() note about locking
	AutoLocker<Debugger> locker(this);
	fTeamDebuggers.AddItem(debugger);
	fRunningTeamDebuggers++;
	locker.Unlock();
}


void
Debugger::TeamDebuggerQuit(TeamDebugger* debugger)
{
	// Note: Locking here only works, since we're never locking the other
	// way around. If we even need to do that, we'll have to introduce a
	// separate lock to protect the list.

	printf("debugger for team %" B_PRId32 " quit.\n", debugger->TeamID());

	AutoLocker<Debugger> locker(this);
	fTeamDebuggers.RemoveItem(debugger);
	locker.Unlock();

	if (debugger->Thread() >= 0) {
		BMessage message(MSG_TEAM_DEBUGGER_QUIT);
		message.AddInt32("thread", debugger->Thread());
		PostMessage(&message);
	}
}


bool
Debugger::QuitRequested()
{
	// NOTE: The default implementation will just ask all windows'
	// QuitRequested() hooks. This in turn will ask the TeamWindows.
	// For now, this is what we want. If we have more windows later,
	// like the global TeamsWindow, then we want to just ask the
	// TeamDebuggers, the TeamsWindow should of course not go away already
	// if one or more TeamDebuggers want to stay later. There are multiple
	// ways how to do this. For example, TeamDebugger could get a
	// QuitRequested() hook or the TeamsWindow and other global windows
	// could always return false in their QuitRequested().
	return BApplication::QuitRequested();
		// TODO: This is ugly. The team debuggers own the windows, not the
		// other way around.
}

void
Debugger::Quit()
{
	// don't quit before all team debuggers have been quit
	if (fRunningTeamDebuggers <= 0 && fTeamsWindow == NULL)
		BApplication::Quit();
}


TeamDebugger*
Debugger::_FindTeamDebugger(team_id teamID) const
{
	for (int32 i = 0; TeamDebugger* debugger = fTeamDebuggers.ItemAt(i);
			i++) {
		if (debugger->TeamID() == teamID)
			return debugger;
	}

	return NULL;
}


status_t
Debugger::_StartNewTeam(const char* path, const char* args)
{
	if (path == NULL)
		return B_BAD_VALUE;

	BString data;
	data.SetToFormat("%s %s", path, args);
	if (data.Length() == 0)
		return B_NO_MEMORY;

	ArgumentVector argVector;
	argVector.Parse(data.String());

	Options options;
	options.commandLineArgc = argVector.ArgumentCount();
	if (options.commandLineArgc <= 0)
		return B_BAD_VALUE;

	char** argv = argVector.DetachArguments();

	options.commandLineArgv = argv;
	MemoryDeleter deleter(argv);

	return _StartOrFindTeam(options);
}


status_t
Debugger::_StartOrFindTeam(Options& options)
{
	DebuggedProgramInfo programInfo;
	if (!get_debugged_program(options, programInfo))
		return B_BAD_VALUE;

	TeamDebugger* debugger = _FindTeamDebugger(programInfo.team);
	if (debugger != NULL) {
		printf("There's already a debugger for team: %" B_PRId32 "\n",
			programInfo.team);
		debugger->Activate();
		return B_OK;
	}

	status_t result;
	start_team_debugger(programInfo.team, &fSettingsManager, this,
		programInfo.thread, programInfo.stopInMain, NULL, &result);

	return result;
}


// #pragma mark - CliDebugger


CliDebugger::CliDebugger()
{
}


CliDebugger::~CliDebugger()
{
}


bool
CliDebugger::Run(const Options& options)
{
	// Block SIGINT, in this thread so all threads created by it inherit the
	// a block mask with the signal blocked. In the input loop the signal will
	// be unblocked again.
	SignalSet(SIGINT).BlockInCurrentThread();

	// initialize global objects and settings manager
	status_t error = global_init();
	if (error != B_OK) {
		fprintf(stderr, "Error: Global initialization failed: %s\n",
			strerror(error));
		return false;
	}

	SettingsManager settingsManager;
	error = settingsManager.Init();
	if (error != B_OK) {
		fprintf(stderr, "Error: Settings manager initialization failed: "
			"%s\n", strerror(error));
		return false;
	}

	// create the command line UI
	CommandLineUserInterface* userInterface
		= new(std::nothrow) CommandLineUserInterface(options.saveReport,
			options.reportPath);
	if (userInterface == NULL) {
		fprintf(stderr, "Error: Out of memory!\n");
		return false;
	}
	BReference<UserInterface> userInterfaceReference(userInterface, true);

	// get/run the program to be debugged and start the team debugger
	DebuggedProgramInfo programInfo;
	if (!get_debugged_program(options, programInfo))
		return false;

	TeamDebugger* teamDebugger = start_team_debugger(programInfo.team,
		&settingsManager, this, programInfo.thread, programInfo.stopInMain,
		userInterface);
	if (teamDebugger == NULL)
		return false;

	thread_id teamDebuggerThread = teamDebugger->Thread();

	// run the input loop
	userInterface->Run();

	// wait for the team debugger thread to terminate
	wait_for_thread(teamDebuggerThread, NULL);

	return true;
}


void
CliDebugger::TeamDebuggerStarted(TeamDebugger* debugger)
{
}


void
CliDebugger::TeamDebuggerQuit(TeamDebugger* debugger)
{
}


// #pragma mark -


int
main(int argc, const char* const* argv)
{
	// We test-parse the arguments here, so that, when we're started from the
	// terminal and there's an instance already running, we can print an error
	// message to the terminal, if something's wrong with the arguments.
	// Otherwise, the arguments are reparsed in the actual application,
	// unless the option to use the command line interface was chosen.

	Options options;
	parse_arguments(argc, argv, false, options);

	if (options.useCLI) {
		CliDebugger debugger;
		return debugger.Run(options) ? 0 : 1;
	}

	Debugger app;
	status_t error = app.Init();
	if (error != B_OK) {
		fprintf(stderr, "Error: Failed to init application: %s\n",
			strerror(error));
		return 1;
	}

	app.Run();

	return 0;
}
