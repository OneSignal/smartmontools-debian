/*
 * os_win32/syslog_win32.cpp
 *
 * Home page of code is: http://www.smartmontools.org
 *
 * Copyright (C) 2004-15 Christian Franke
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example COPYING); If not, see <http://www.gnu.org/licenses/>.
 *
 */

// Win32 Emulation of syslog() for smartd
// Writes to windows event log on NT4/2000/XP
// (Register syslogevt.exe as event message file)
// If facility is set to LOG_LOCAL[0-7], log is written to
// file "<ident>.log", stdout, stderr, "<ident>[1-5].log".


#include "syslog.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <process.h> // getpid()

#define WIN32_LEAN_AND_MEAN
#include <windows.h> // RegisterEventSourceA(), ReportEventA(), ...

const char *syslog_win32_cpp_cvsid = "$Id$"
  SYSLOG_H_CVSID;

#ifdef _MSC_VER
// MSVC
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif

#define ARGUSED(x) ((void)(x))


#ifndef _MT
//MT runtime not necessary, because thread uses no unsafe lib functions
//#error Program must be linked with multithreaded runtime library
#endif


#ifdef TESTEVT
// Redirect event log to stdout for testing

static BOOL Test_ReportEventA(HANDLE h, WORD type, WORD cat, DWORD id, PSID usid,
						WORD nstrings, WORD datasize, LPCTSTR * strings, LPVOID data)
{
	int i;
	printf("%u %lu:%s", type, id, nstrings != 1?"\n":"");
	for (i = 0; i < nstrings; i++)
		printf(" \"%s\"\n", strings[i]);
	fflush(stdout);
	return TRUE;
}

HANDLE Test_RegisterEventSourceA(LPCTSTR server, LPCTSTR source)
{
	return (HANDLE)42;
}

#define ReportEventA Test_ReportEventA
#define RegisterEventSourceA Test_RegisterEventSourceA
#endif // TESTEVT


// Event message ids,
// should be identical to MSG_SYSLOG* in "syslogevt.h"
// (generated by "mc" from "syslogevt.mc")
#define MSG_SYSLOG                       0x00000000L
#define MSG_SYSLOG_01                    0x00000001L
// ...
#define MSG_SYSLOG_10                    0x0000000AL

static char sl_ident[100];
static char sl_logpath[sizeof(sl_ident) + sizeof("0.log")-1];
static FILE * sl_logfile;
static char sl_pidstr[16];
static HANDLE sl_hevtsrc;


// Ring buffer for event log output via thread
#define MAXLINES 10
#define LINELEN 200

static HANDLE evt_hthread;
static char evt_lines[MAXLINES][LINELEN+1];
static int evt_priorities[MAXLINES];
static volatile int evt_timeout;
static int evt_index_in, evt_index_out;
static HANDLE evt_wait_in, evt_wait_out;


// Map syslog priority to event type

static WORD pri2evtype(int priority)
{
	switch (priority) {
		default:
		case LOG_EMERG: case LOG_ALERT:
		case LOG_CRIT:  case LOG_ERR:
			return EVENTLOG_ERROR_TYPE;
		case LOG_WARNING:
			return EVENTLOG_WARNING_TYPE;
		case LOG_NOTICE: case LOG_INFO:
		case LOG_DEBUG:
			return EVENTLOG_INFORMATION_TYPE;
	}
}


// Map syslog priority to string

static const char * pri2text(int priority)
{
	switch (priority) {
		case LOG_EMERG:   return "EMERG";
		case LOG_ALERT:   return "ALERT";
		case LOG_CRIT:    return "CRIT";
		default:
		case LOG_ERR:     return "ERROR";
		case LOG_WARNING: return "Warn";
		case LOG_NOTICE:  return "Note";
		case LOG_INFO:    return "Info";
		case LOG_DEBUG:   return "Debug";
	}
}


// Output cnt events from ring buffer

static void report_events(int cnt)
{
	const char * msgs[3+MAXLINES];

	int i, pri;
	if (cnt <= 0)
		return;
	if (cnt > MAXLINES)
		cnt = MAXLINES;

	pri = evt_priorities[evt_index_out];

	msgs[0] = sl_ident;
	msgs[1] = sl_pidstr;
	msgs[2] = pri2text(pri);
	for (i = 0; i < cnt; i++) {
		//assert(evt_priorities[evt_index_out] == pri);
		msgs[3+i] = evt_lines[evt_index_out];
		if (++evt_index_out >= MAXLINES)
			evt_index_out = 0;
	}
	ReportEventA(sl_hevtsrc,
		pri2evtype(pri), // type
		0, MSG_SYSLOG+cnt,    // category, message id
		NULL,                 // no security id
		(WORD)(3+cnt), 0,     // 3+cnt strings, ...
		msgs, NULL);          // ...          , no data
}


// Thread to combine several syslog lines into one event log entry

static ULONG WINAPI event_logger_thread(LPVOID arg)
{
	int cnt;
	ARGUSED(arg);

	cnt = 0;
	for (;;) {
		// Wait for first line ...
		int prior, i, rest;
		if (cnt == 0) {
			if (WaitForSingleObject(evt_wait_out, (evt_timeout? INFINITE : 0)) != WAIT_OBJECT_0)
				break;
			cnt = 1;
		}

		// ... wait some time for more lines with same prior
		i = evt_index_out;
		prior = evt_priorities[i];
		rest = 0;
		while (cnt < MAXLINES) {
			long timeout =
				evt_timeout * ((1000L * (MAXLINES-cnt+1))/MAXLINES);
			if (WaitForSingleObject(evt_wait_out, timeout) != WAIT_OBJECT_0)
				break;
			if (++i >= MAXLINES)
				i = 0;
			if (evt_priorities[i] != prior) {
				rest = 1;
				break;
			}
			cnt++;
		}

		// Output all in one event log entry
		report_events(cnt);

		// Signal space
		if (!ReleaseSemaphore(evt_wait_in, cnt, NULL))
			break;
		cnt = rest;
	}
	return 0;
}


static void on_exit_event_logger(void)
{
	// Output lines immediate if exiting
	evt_timeout = 0; 
	// Wait for thread to finish
	WaitForSingleObject(evt_hthread, 1000L);
	CloseHandle(evt_hthread);
#if 0
	if (sl_hevtsrc) {
		DeregisterEventSource(sl_hevtsrc); sl_hevtsrc = 0;
	}
#else
	// Leave event message source open to prevent losing messages during shutdown
#endif
}


static int start_event_logger()
{
	DWORD tid;
	evt_timeout = 1;
	if (   !(evt_wait_in  = CreateSemaphore(NULL,  MAXLINES, MAXLINES, NULL))
		|| !(evt_wait_out = CreateSemaphore(NULL,         0, MAXLINES, NULL))) {
		fprintf(stderr,"CreateSemaphore failed, Error=%ld\n", GetLastError());
		return -1;
	}
	if (!(evt_hthread = CreateThread(NULL, 0, event_logger_thread, NULL, 0, &tid))) {
		fprintf(stderr,"CreateThread failed, Error=%ld\n", GetLastError());
		return -1;
	}
	atexit(on_exit_event_logger);
	return 0;
}


// Write lines to event log ring buffer

static void write_event_log(int priority, const char * lines)
{
	int cnt = 0;
	int i;
	for (i = 0; lines[i]; i++) {
		int len = 0;
		while (lines[i+len] && lines[i+len] != '\n')
			len++;
			;
		if (len > 0) {
			// Wait for space
			if (WaitForSingleObject(evt_wait_in, INFINITE) != WAIT_OBJECT_0)
				return;
			// Copy line
			evt_priorities[evt_index_in] = priority;
			memcpy(evt_lines[evt_index_in], lines+i, (len < LINELEN ? len : LINELEN));
			if (len < LINELEN)
				evt_lines[evt_index_in][len] = 0;
			if (++evt_index_in >= MAXLINES)
				evt_index_in = 0;
			// Signal avail if ring buffer full
			if (++cnt >= MAXLINES) {
				ReleaseSemaphore(evt_wait_out, cnt, NULL);
				cnt = 0;
			}
			i += len;
		}
		if (!lines[i])
			break;
	}

	// Signal avail
	if (cnt > 0)
		ReleaseSemaphore(evt_wait_out, cnt, NULL);
	Sleep(1);
}


// Write lines to logfile

static void write_logfile(FILE * f, int priority, const char * lines)
{
	time_t now; char stamp[sizeof("2004-04-04 10:00:00")+13];
	int i;

	now = time((time_t*)0);
	if (!strftime(stamp, sizeof(stamp)-1, "%Y-%m-%d %H:%M:%S", localtime(&now)))
		strcpy(stamp,"?");

	for (i = 0; lines[i]; i++) {
		int len = 0;
		while (lines[i+len] && lines[i+len] != '\n')
			len++;
		if (len > 0) {
			fprintf(f, "%s %s[%s]: %-5s: ",
				stamp, sl_ident, sl_pidstr, pri2text(priority));
			fwrite(lines+i, len, 1, f);
			fputc('\n', f);
			i += len;
		}
		if (!lines[i])
			break;
	}
}


void openlog(const char *ident, int logopt, int facility)
{
	int pid;
	if (sl_logpath[0] || sl_logfile || sl_hevtsrc)
		return; // Already open

	strncpy(sl_ident, ident, sizeof(sl_ident)-1);
	// logopt==LOG_PID assumed
	ARGUSED(logopt);
	pid = getpid();
	if (snprintf(sl_pidstr, sizeof(sl_pidstr)-1, (pid >= 0 ? "%d" : "0x%X"), pid) < 0)
		strcpy(sl_pidstr,"?");

	if (facility == LOG_LOCAL0) // "ident.log"
		strcat(strcpy(sl_logpath, sl_ident), ".log");
	else if (facility == LOG_LOCAL1) // stdout
		sl_logfile = stdout;
	else if (facility == LOG_LOCAL2) // stderr
		sl_logfile = stderr;
	else if (LOG_LOCAL2 < facility && facility <= LOG_LOCAL7) { // "ident[1-5].log"
		snprintf(sl_logpath, sizeof(sl_logpath)-1, "%s%d.log",
			sl_ident, LOG_FAC(facility)-LOG_FAC(LOG_LOCAL2));
	}
	else // Assume LOG_DAEMON, use event log if possible, else "ident.log"
	if (!(sl_hevtsrc = RegisterEventSourceA(NULL/*localhost*/, sl_ident))) {
		// Cannot open => Use logfile
		long err = GetLastError();
		strcat(strcpy(sl_logpath, sl_ident), ".log");
		fprintf(stderr, "%s: Cannot register event source (Error=%ld), writing to %s\n",
			sl_ident, err, sl_logpath);
	}
	else {
		// Start event log thread
		start_event_logger();
	}
	//assert(sl_logpath[0] || sl_logfile || sl_hevtsrc);

}


void closelog()
{
}


void vsyslog(int priority, const char * message, va_list args)
{
	char buffer[1000];

	// Translation of %m to error text not supported yet
	if (strstr(message, "%m"))
		message = "Internal error: \"%%m\" in log message";

	// Format message
	if (vsnprintf(buffer, sizeof(buffer)-1, message, args) < 0)
		strcpy(buffer, "Internal Error: buffer overflow");

	if (sl_hevtsrc) {
		// Write to event log
		write_event_log(priority, buffer);
	}
	else if (sl_logfile) {
		// Write to stdout/err
		write_logfile(sl_logfile, priority, buffer);
		fflush(sl_logfile);
	}
	else if (sl_logpath[0]) {
		// Append to logfile
		FILE * f;
		if (!(f = fopen(sl_logpath, "a")))
			return;
		write_logfile(f, priority, buffer);
		fclose(f);
	}
}


#ifdef TEST
// Test program

void syslog(int priority, const char *message, ...)
{
	va_list args;
	va_start(args, message);
	vsyslog(priority, message, args);
	va_end(args);
}

int main(int argc, char* argv[])
{
	int i;
	openlog(argc < 2 ? "test" : argv[1], LOG_PID, (argc < 3 ? LOG_DAEMON : LOG_LOCAL1));
	syslog(LOG_INFO,    "Info\n");
	syslog(LOG_WARNING, "Warning %d\n\n", 42);
	syslog(LOG_ERR,     "Error %s", "Fatal");
	for (i = 0; i < 100; i++) {
		char buf[LINELEN];
		if (i % 13 == 0)
			Sleep(1000L);
		sprintf(buf, "Log Line %d\n", i);
		syslog((i % 17) ? LOG_INFO : LOG_ERR, buf);
	}
	closelog();
	return 0;
}

#endif
