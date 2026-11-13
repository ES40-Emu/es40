/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2023-2026 by gdwnldsKSC
 * Copyright (C) 2026 by Martin Vorländer
 * Copyright (C) 2026 by Antoni Sawicki
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Although this is not required, the author would appreciate being notified of,
 * and receiving any modifications you may make to the source code that might serve
 * the general public.
 *
 */

 /**
  * \file
  * Defines the entry point for the application.
  *
  * $Id$
  *
  * X-1.51       Camiel Vanderhoeven                             12-JUN-2008
  *      Properly initialize DPR memory areas.
  *
  * X-1.49       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.48       Martin Borgman                                  10-APR-2008
  *	    Handle SDL support on OS X through OS_X/SDLMain.m.
  *
  * X-1.47       Camiel Vanderhoeven                             04-APR-2008
  *      Take version number from config.h
  *
  * X-1.46       Camiel Vanderhoeven                             26-MAR-2008
  *      Fix compiler warnings.
  *
  * X-1.45       Camiel Vanderhoeven                             17-MAR-2008
  *      Always set volatile DPR rom contents.
  *
  * X-1.44       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.43       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.42       Camiel Vanderhoeven                             04-MAR-2008
  *      Version updated to 0.18.
  *
  * X-1.41       Brian Wheeler                                   26-FEB-2008
  *   a) Catch SIGUSR1 to trigger the backtracer if es40 seems to have hung.
  *   b) Use _exit() to really really quit when a SIGSEGV is caught.
  *
  * X-1.40       Brian Wheeler                                   07-FEB-2008
  *      On GNU systems, display a backtrace when a segmentation fault
  *      occurs, and DEBUG_BACKTRACE has been defined.
  *
  * X-1.39       Brian Wheeler                                   05-FEB-2008
  *      Display a message when system initialization has failed.
  *
  * X-1.38       Camiel Vanderhoeven                             26-JAN-2008
  *      Lifted hardcoded file-size limit of 10,000 bytes for config file.
  *
  * X-1.37       Camiel Vanderhoeven                             02-JAN-2008
  *      Version updated to 0.17.
  *
  * X-1.35       Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.34       Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.33       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator to read config-file and set up devices.
  *
  * X-1.32       Camiel Vanderhoeven                             7-DEC-2007
  *      Moved SDL_Init to gui implementation.
  *
  * X-1.31       Camiel Vanderhoeven/Brian Wheeler               6-DEC-2007
  *      Use Boch's SDL gui plugin (with thanks to the Bochs project!!)
  *
  * X-1.30       Camiel Vanderhoeven                             2-DEC-2007
  *      Added support for code profiling.
  *
  * X-1.29       Brian Wheeler                                   1-DEC-2007
  *      Added console support if USE_CONSOLE is defined.
  *
  * X-1.28       Brian Wheeler                                   22-NOV-2007
  *      Added nic0.disabled configuration option.
  *
  * X-1.27       Camiel Vanderhoeven                             17-NOV-2007
  *      Use FAILURE.
  *
  * X-1.26       Camiel Vanderhoeven                             17-NOV-2007
  *      Version updated to 0.16. Made it possible to disable the network
  *      by defining NO_NETWORK.
  *
  * X-1.25       Camiel Vanderhoeven                             14-NOV-2007
  *      Added network.
  *
  * X-1.24       Camiel Vanderhoeven                             08-NOV-2007
  *      Version updated to 0.15.
  *
  * X-1.23       Camiel Vanderhoeven                             02-NOV-2007
  *      Version updated to 0.14.
  *
  * X-1.22       Camiel Vanderhoeven                             10-APR-2007
  *      Replaced LoadROM(...) and SelectROM() calls with a single LoadROM()
  *      call. (See System.cpp, X-1.23).
  *
  * X-1.21       Camiel Vanderhoeven                             10-APR-2007
  *      Calls to LoadROM and SelectROM changed to fit X-1.22 of System.cpp.
  *
  * X-1.20       Camiel Vanderhoeven                             31-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.19	Camiel Vanderhoeven				28-FEB-2007
  *	Call lockstep_init() if needed.
  *
  * X-1.18	Camiel Vanderhoeven				16-FEB-2007
  *	No longer load CSV files, this can be done with the LOAD CSV command
  *	in the Interactive De-Bugger.
  *
  * X-1.17	Camiel Vanderhoeven				16-FEB-2007
  *   a)	Removed most of the code from this file. The main function no longer
  *	controls every clocktick, but rather transfers control directly to
  *	CSystem::Run(), or to the Interactive Debugger.
  *   b)	In the IDB-version, the name of a script to execute can be specified
  *	on the command line (es40_idb @script).
  *
  * X-1.16	Camiel Vanderhoeven				13-FEB-2007
  *	The define DISASM_START can be set to the number of processor cycles
  *	after which the disassembly will start.
  *
  * X-1.15	Camiel Vanderhoeven				13-FEB-2007
  *	The define DO_SETPC can be set to the address of the first
  *	instruction to be executed.
  *
  * X-1.14	Camiel Vanderhoeven				12-FEB-2007
  *	Use methods provided by CCPU rather than directly accessing its
  *	member variables.
  *
  * X-1.13	Camiel Vanderhoeven				12-FEB-2007
  *	Added comments.
  *
  * X-1.12	Camiel Vanderhoeven				9-FEB-2007
  *	Removed an obscure hack.
  *
  * X-1.11       Camiel Vanderhoeven                             9-FEB-2007
  *      If DO_DISASM is set, disassembly will be output.
  *
  * X-1.10	Brian Wheeler					7-FEB-2007
  *	Configuration file may be specified on the command line.
  *
  * X-1.9	Camiel Vanderhoeven				7-FEB-2007
  *	Add instantiation of trace engine.
  *
  * X-1.8	Camiel Vanderhoeven				3-FEB-2007
  *	RUN_LT can be set to provide a breakpoint (end if pc < defined value)
  *
  * X-1.7        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.6	Brian Wheeler					3-FEB-2007
  *	Support for execution speed timing on Linux
  *
  * X-1.5	Brian Wheeler					3-FEB-2007
  *	Support for configuration file.
  *
  * X-1.4	Brian Wheeler					3-FEB-2007
  *	Made execution speed timing dependent on _WIN32 (since this code
  *	uses a lot of Windows-specific features, and won't compile on
  *	Linux.
  *
  * X-1.3        Brian Wheeler                                   3-FEB-2007
  *      Scanf calls made compatible with Linux/GCC/glibc.
  *
  * X-1.2        Brian Wheeler                                   3-FEB-2007
  *      Includes are now case-correct (necessary on Linux)
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2007
  *      Initial version in CVS.
  *
  * \Initial author Camiel Vanderhoeven
  **/
#include "StdAfx.h"
#include "system/System.h"
#include "system/Flash.h"
#include "system/DPR.h"
#include "chipset/AliM1543C.h"
#include "banner.h"

#include "network/lockstep.h"
#include "gui/gui.h"
#if defined(_WIN32)
#include "gui/win32_manager.h"
#endif
#if defined(ES40_MCP)
#include "mcp/McpServer.h"
#endif

#include <thread>

#if !defined(ES40_GIT_COMMIT)
#define ES40_GIT_COMMIT "unknown"
#endif

#if defined(HAVE_SDL)
#include "SDL3/SDL.h"
#endif

  /// "standard" locations for a configuration file.  This will be port specific.
const char* path[] = {
#if defined(_WIN32)
  ".\\es40.cfg",
  "c:\\es40.cfg",
  "c:\\windows\\es40.cfg",
#elif defined(__VMS)
  "[]ES40.CFG",
#else
  "./es40.cfg",
  "/etc/es40.cfg",
  "/usr/etc/es40.cfg",
  "/usr/local/etc/es40.cfg",
#endif
  0
};

#ifdef DEBUG_BACKTRACE
#ifdef __GNUG__
#include <execinfo.h>
#include <signal.h>
#define HAS_BACKTRACE

#define BTCOUNT 100
void* btbuffer[BTCOUNT];

void segv_handler(int signum)
{
	int     nptrs = backtrace(btbuffer, BTCOUNT);
	char** strings;

	printf("%%SYS-F-SEGFAULT: The Alpha Simulator has Segfaulted.\n");
	printf("-SYS-F-SEGFAULT: Backtrace follows.\n");

	printf("backtrace() returned %d addresses.\n", nptrs);
	strings = backtrace_symbols(btbuffer, nptrs);
	if (strings == NULL)
	{
		perror("backtrace_symbols");
		exit(1);
	}

	for (int i = 0; i < nptrs; i++)
	{
		printf("%3d %s\n", nptrs - i, strings[i]);
	}

	free(strings);
	if (signum == SIGSEGV)
		_exit(1);
}

#else
#warning "Your compiler isn't configured to support backtraces."
#endif // __GNUG__
#endif

static int run_emulator(int argc, char* argv[]);

static bool detach_gui_guest_input()
{
	try
	{
		if (bx_gui)
			bx_gui->detach_guest_input();
		return true;
	}
	catch (const CException& error)
	{
		printf("GUI input detach failure: %s\n", error.displayText().c_str());
		// Keep stopped devices alive if the backend may still hold its target.
		return false;
	}
}

/**
 * Entry point for the application.
 *
 * Does the following:
 *  - Try to find the configuration file.
 *  - Reads the configuration file, and uses the configurator to instantiate all system components.
 *  - Creates the trace-engine if in debug mode.
 *  - Runs the emulator.
 *  - Cleans up.
 *  .
 **/
int main(int argc, char* argv[])
{
#if defined(_WIN32)
	// Strip --run before normal argument handling.
	const bool direct_run = argc > 1 && strcmp(argv[1], "--run") == 0;
	if (direct_run)
	{
		for (int i = 1; i < argc; ++i) argv[i] = argv[i + 1];
		--argc;
		const char* host = getenv("ES40_MANAGER_HOST");
		if (host && *host)
		{
			setvbuf(stdout, nullptr, _IONBF, 0);
			setvbuf(stderr, nullptr, _IONBF, 0);
		}
	}
#endif

	if (argc == 2 &&
		(strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0))
	{
		int features = 0;
		printf("ES40 %s (GitHub commit %s)\nFeatures:", VERSION, ES40_GIT_COMMIT);
#ifdef ES40_JIT
		printf(" AsmJit"); ++features;
#endif
#ifdef HAVE_SDL
		printf(" SDL"); ++features;
#endif
#ifdef HAVE_X11
		printf(" X11"); ++features;
#endif
#ifdef HAVE_PCAP
		printf(" PCap"); ++features;
#endif
#ifdef HAVE_TAP_NET
		printf(" TAP"); ++features;
#endif
#ifdef HAVE_VMNET
		printf(" vmnet"); ++features;
#endif
#ifdef HAVE_SLIRP
		printf(" slirp"); ++features;
#endif
		if (features == 0)
			printf(" (none)");
		printf("\n");
		return 0;
	}

#if defined(_WIN32)
	if (!direct_run && argc > 1 && strcmp(argv[1], "--manager") == 0)
	{
#if defined(IDB)
		fprintf(stderr, "The machine manager requires a non-IDB emulator build.\n");
		return 1;
#else
		return win32_manager_main();
#endif
	}
#endif

	print_es40_banner("AlphaServer ES40 emulator");

	const char* filename = 0;
	FILE* f;

#ifdef HAS_BACKTRACE
	signal(SIGSEGV, &segv_handler);
	signal(SIGUSR1, &segv_handler);
#endif
	try
	{
#if defined(IDB) && (defined(LS_MASTER) || defined(LS_SLAVE))
		lockstep_init();
#endif
#if defined(IDB)
		if ((argc == 2 || argc == 3) && argv[1][0] != '@')
#else
		if (argc == 2)
#endif
		{
			if (argv[1] && argv[1][0] != '\0')
				filename = argv[1];
		}
		else
		{
			// Probe defaults; only assign on success
			for (int i = 0; path[i]; ++i)
			{
				FILE* probe = fopen(path[i], "rb");
				if (probe)
				{
					fclose(probe);
					filename = path[i];
					break;
				}
			}
			if (!filename) {
				FAILURE(FileNotFound, "configuration file");
			}
		}

		// Open chosen file (print the path on failure)
		f = fopen(filename, "rb");
		if (!f) {
			char buf[1024];
			snprintf(buf, sizeof(buf), "failed to open configuration file: %s (%s)",
				filename, strerror(errno));
			FAILURE(Configuration, buf);
		}

		// Determine size (Windows: 64-bit length; POSIX: fseek/ftell)
		size_t ll1 = 0;
#if defined(_WIN32)
		if (_fseeki64(f, 0, SEEK_END) != 0) {
			char buf[1024];
			fclose(f);
			snprintf(buf, sizeof(buf), "failed to seek end of configuration file: %s", filename);
			FAILURE(Configuration, buf);
		}
		__int64 pos64 = _ftelli64(f);
		if (pos64 < 0) {
			char buf[1024];
			fclose(f);
			snprintf(buf, sizeof(buf), "failed to tell size of configuration file: %s", filename);
			FAILURE(Configuration, buf);
		}
		ll1 = (size_t)pos64;
		if (_fseeki64(f, 0, SEEK_SET) != 0) {
			char buf[1024];
			fclose(f);
			snprintf(buf, sizeof(buf), "failed to rewind configuration file: %s", filename);
			FAILURE(Configuration, buf);
		}
#else
		if (fseek(f, 0, SEEK_END) != 0) {
			char buf[1024];
			fclose(f);
			snprintf(buf, sizeof(buf), "failed to seek end of configuration file: %s", filename);
			FAILURE(Configuration, buf);
		}
		long pos = ftell(f);
		if (pos < 0) {
			char buf[1024];
			fclose(f);
			snprintf(buf, sizeof(buf), "failed to tell size of configuration file: %s", filename);
			FAILURE(Configuration, buf);
		}
		ll1 = (size_t)pos;
		if (fseek(f, 0, SEEK_SET) != 0) {
			char buf[1024];
			fclose(f);
			snprintf(buf, sizeof(buf), "failed to rewind configuration file: %s", filename);
			FAILURE(Configuration, buf);
		}
#endif

		// Allocate buffer (+1 for NUL), read, and NUL-terminate for safety
		char* ch1 = (char*)calloc(ll1 + 1, 1);
		if (!ch1) {
			char buf[1024];
			fclose(f);
			snprintf(buf, sizeof(buf), "out of memory reading configuration file: %s", filename);
			FAILURE(Configuration, buf);
		}

		size_t read_bytes = fread(ch1, 1, ll1, f);
		if (read_bytes != ll1) {
			char buf[1024];
			fclose(f);
			free(ch1);
			snprintf(buf, sizeof(buf), "failed to read entire configuration file: %s", filename);
			FAILURE(Configuration, buf);
		}
		ch1[ll1] = '\0';

#if defined(_WIN32) && !defined(IDB)
		// Inspect startup options before creating devices.
		CConfigurator* c = new CConfigurator(0, 0, 0, ch1, ll1, false);
#else
		CConfigurator* c = new CConfigurator(0, 0, 0, ch1, ll1);
#endif
		fclose(f);
		free(ch1);

#if defined(_WIN32) && !defined(IDB)
		if (!direct_run && c->win32_workstation_enabled())
		{
			delete c;
			return win32_manager_main(filename);
		}
		c->initialize();
#endif

		if (!theSystem)
			FAILURE(Configuration, "no system initialized");

#if defined(IDB)
		trc = new CTraceEngine(theSystem);
#endif
		// Components are initialized and no workers have started. 
		// Restore the whole file before deciding to skip firmware loading.
		// This way, we restore into a resumable state or not at all. 
		const char* restore_file = getenv("ES40_RESTORE");
		bool restored = false;
		if (restore_file && *restore_file)
		{
			printf("%%SYS-I-RESTORE: Restoring state from %s before startup.\n",
				restore_file);
			restored = theSystem->RestoreState(restore_file);
			if (restored)
				printf("%%SYS-I-RESTORED: State restored; firmware loading skipped.\n");
			else
				printf("%%SYS-W-NORESTORE: State was not loaded; cold booting.\n");
		}
		if (!restored)
		{
			theSystem->LoadROM();
			theDPR->init();
		}

#if defined(PROFILE)
		{
			u64 p_i;
			for (p_i = PROFILE_FROM; p_i < PROFILE_TO; p_i += (4 * PROFILE_BUCKSIZE))
				PROFILE_BUCKET(p_i) = 0;
			profiled_insts = 0;
		}
#endif
		if (bx_gui && bx_gui->requires_main_thread())
			bx_gui->main_thread_init();
#if defined(ES40_MCP)
		es40_mcp_start(argv[0], filename);
#endif
	}
	catch (CException& e)
	{
		printf("Emulator Failure: %s\n", e.displayText().c_str());
		if (theSystem)
		{
			theSystem->stop_threads();
			if (detach_gui_guest_input())
				delete theSystem;
		}
		return 1;
	}

	// A GUI that owns main() pumps its event loop there while the emulator,
	// including its shutdown, runs on a worker thread. Everything the GUI
	// thread posts back has to keep being dispatched until that worker has
	// joined the GUI thread, so run_emulator() ends the pump itself.
	int result = 0;
	if (bx_gui && bx_gui->requires_main_thread())
	{
		bx_gui_c* const main_thread_gui = bx_gui;
		std::thread emulator([&] { result = run_emulator(argc, argv); });
		main_thread_gui->main_thread_pump();
		emulator.join();
		// Workers and their synchronous GUI calls have finished. Release host
		// display resources here, on the same main thread that initialized SDL.
		try
		{
			main_thread_gui->exit_application();
		}
		catch (const CException& cleanup_error)
		{
			printf("GUI shutdown failure: %s\n", cleanup_error.displayText().c_str());
			result = 1;
		}
	}
	else
		result = run_emulator(argc, argv);

	return result;
}

/**
 * Run the emulator, and clean up after it terminates.
 **/
static int run_emulator(int argc, char* argv[])
{
	int result = 0;
	try
	{
#if defined(IDB)
		theSystem->start_threads(); // fix from axpbox commit 9ef3473 

		if (argc > 1 && argc < 4 && argv[argc - 1][0] == '@')
			trc->run_script(argv[argc - 1] + 1);
		else
			trc->run_script(NULL);
#else
		theSystem->Run();
#endif
	}
	catch (CGracefulException& e)
	{
		printf("Exiting gracefully: %s\n", e.displayText().c_str());

		try
		{
			theSystem->stop_threads();
			theSystem->flush_storage();

			// save flash and dpr rom only if not terminated with a fatal error
			theSROM->SaveStateF();
			theDPR->SaveStateF();
			if (theAli)
				theAli->save_toy_nvram(true);

#if defined(PROFILE)
			{
				FILE* p_fp;
				u64     p_max = 0;
				u64     p_i;
				int     p_j;

				printf("Writing profile to profile.txt");

				p_fp = fopen("profile.txt", "w");
				for (p_i = PROFILE_FROM; p_i < PROFILE_TO; p_i += (4 * PROFILE_BUCKSIZE))
				{
					if (PROFILE_BUCKET(p_i) > p_max)
						p_max = PROFILE_BUCKET(p_i);
				}
				fprintf
				(
					p_fp, "p_max = %10" PRId64 "; %10" PRId64 " profiled instructions.\n\n", p_max,
					profiled_insts
				);
				for (p_i = PROFILE_FROM; p_i < PROFILE_TO; p_i += (4 * PROFILE_BUCKSIZE))
				{
					if (PROFILE_BUCKET(p_i))
					{
						fprintf(p_fp, "%016" PRIx64 ": %10" PRId64 " ", p_i, PROFILE_BUCKET(p_i));
						for (p_j = 0; p_j < (((float)PROFILE_BUCKET(p_i) / (float)p_max) * 100);
							p_j++)
							fprintf(p_fp, "*");
						fprintf(p_fp, "\n");
					}
				}
				fclose(p_fp);
			}
#endif
		}
		catch (const CException& save_error)
		{
			printf("Shutdown save failure: %s\n", save_error.displayText().c_str());
			result = 1;
		}
	}
	catch (CException& e)
	{
		printf("Emulator Failure: %s\n", e.displayText().c_str());
		result = 1;
	}

	if (theSystem)
	{
		theSystem->stop_threads();
#if defined(ES40_MCP)
		// The MCP calls use the keyboard and the displays: end them first
		es40_mcp_stop();
#endif
		if (detach_gui_guest_input())
			delete theSystem;
		else
			result = 1;
	}
	if (bx_gui)
		bx_gui->main_thread_stop();
	return result;
}
