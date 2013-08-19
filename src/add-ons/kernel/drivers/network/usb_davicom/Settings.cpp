/*
 *	Davicom DM9601 USB 1.1 Ethernet Driver.
 *	Copyright (c) 2008, 2011 Siarzhuk Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 */


#include "Settings.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <driver_settings.h>
#include <lock.h>

#include "Driver.h"


mutex gLogLock;
static char *gLogFilePath = NULL;

bool gTraceOn = false;
bool gTruncateLogFile = false;
bool gAddTimeStamp = true;
bool gTraceState = false;
bool gTraceRX = false;
bool gTraceTX = false;
bool gTraceStats = false;


static
void create_log()
{
	if (gLogFilePath == NULL)
		return;

	int flags = O_WRONLY | O_CREAT | ((gTruncateLogFile) ? O_TRUNC : 0);
	int fd = open(gLogFilePath, flags, 0666);
	if (fd >= 0)
		close(fd);

	mutex_init(&gLogLock, DRIVER_NAME"-logging");
}


void
load_settings()
{
	void *handle = load_driver_settings(DRIVER_NAME);
	if (handle == 0)
		return;

	gTraceOn = get_driver_boolean_parameter(handle, "trace", gTraceOn, true);
	gTraceState = get_driver_boolean_parameter(handle,
				"trace_state", gTraceState, true);
	gTraceRX = get_driver_boolean_parameter(handle, "trace_rx", gTraceRX, true);
	gTraceTX = get_driver_boolean_parameter(handle, "trace_tx", gTraceTX, true);
	gTraceStats = get_driver_boolean_parameter(handle,
				"trace_stats", gTraceStats, true);
	gTruncateLogFile = get_driver_boolean_parameter(handle,
				"reset_logfile", gTruncateLogFile, true);
	gAddTimeStamp = get_driver_boolean_parameter(handle,
				"add_timestamp", gAddTimeStamp, true);
	const char * logFilePath = get_driver_parameter(handle,
				"logfile", NULL, "/var/log/"DRIVER_NAME".log");
	if (logFilePath != NULL) {
		gLogFilePath = strdup(logFilePath);
	}

	unload_driver_settings(handle);

	create_log();
}


void
release_settings()
{
	if (gLogFilePath != NULL) {
		mutex_destroy(&gLogLock);
		free(gLogFilePath);
	}
}


void usb_davicom_trace(bool force, const char* func, const char *fmt, ...)
{
	if (!(force || gTraceOn)) {
		return;
	}

	va_list arg_list;
	static const char *prefix = "\33[33m"DRIVER_NAME":\33[0m";
	static char buffer[1024];
	char *buf_ptr = buffer;
	if (gLogFilePath == NULL) {
		strlcpy(buffer, prefix, sizeof(buffer));
		buf_ptr += strlen(prefix);
	}

	if (gAddTimeStamp) {
		bigtime_t time = system_time();
		uint32 msec = time / 1000;
		uint32 sec  = msec / 1000;
		sprintf(buf_ptr, "%02" B_PRId32 ".%02" B_PRId32 ".%03" B_PRId32 ":",
					sec / 60, sec % 60, msec % 1000);
		buf_ptr += strlen(buf_ptr);
	}

	if (func	!= NULL) {
		sprintf(buf_ptr, "%s::", func);
		buf_ptr += strlen(buf_ptr);
	}

	va_start(arg_list, fmt);
	vsprintf(buf_ptr, fmt, arg_list);
	va_end(arg_list);

	if (gLogFilePath == NULL) {
		dprintf(buffer);
		return;
	}

	mutex_lock(&gLogLock);
	int fd = open(gLogFilePath, O_WRONLY | O_APPEND);
	if (fd >= 0) {
		write(fd, buffer, strlen(buffer));
		close(fd);
	}
	mutex_unlock(&gLogLock);
}

