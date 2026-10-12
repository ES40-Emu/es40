/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2023-2026 by gdwnldsKSC
 * Copyright (C) 2026 by Scott Klein
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

 //#define CONSTANT_TIME_FACTOR 100

 /**
  * \file
  * Contains the code for the emulated DecChip 21264CB EV68 Alpha processor.
  *
  * $Id$
  *
  * X-1.82       Camiel Vanderhoeven                             15-MAR-2008
  *   a) Added CONSTANT_TIME_FACTOR define to lock the CPU timing.
  *   b) Fixed a bug in SPE0 handling, spotted by David Hittner.
  *
  * X-1.81       Camiel Vanderhoeven                             12-JUN-2008
  *   a) Support to keep secondary CPUs waiting until activated from primary.
  *   b) New unaligned memory access handling.
  *   c) Fixed support for current_pc_physical handling with new icache.
  *
  * X-1.80       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.79       Camiel Vanderhoeven                             26-MAR-2008
  *      Fix compiler warnings.
  *
  * X-1.78       Camiel Vanderhoeven                             19-MAR-2008
  *      IDB versions compileable again.
  *
  * X-1.77       Camiel Vanderhoeven                             15-MAR-2008
  *      Remove confusing outer for-loop in CAlphaCPU::run().
  *
  * X-1.76       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.75       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.74       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.73       Camiel Vanderhoeven                             11-MAR-2008
  *      Don't printf calibration loop.
  *
  * X-1.72       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.71       Camiel Vanderhoeven                             04-MAR-2008
  *      Support some basic MP features. (CPUID read from C-Chip MISC
  *      register, inter-processor interrupts)
  *
  * X-1.70       Camiel Vanderhoeven                             29-FEB-2008
  *      Comments.
  *
  * X-1.69       Brian Wheeler                                   29-FEB-2008
  *      Add BREAKPOINT INSTRUCTION command to IDB.
  *
  * X-1.68       Brian Wheeler                                   27-FEB-2008
  *      Avoid compiler warnings.
  *
  * X-1.67       Camiel Vanderhoeven                             08-FEB-2008
  *      Show originating device name on memory errors.
  *
  * X-1.66       Camiel Vanderhoeven                             05-FEB-2008
  *      Only use new floating-point code when HAVE_NEW_FP has been defined.
  *
  * X-1.65       Camiel Vanderhoeven                             01-FEB-2008
  *      Avoid unnecessary shift-operations to calculate constant values.
  *
  * X-1.64       Camiel Vanderhoeven                             30-JAN-2008
  *      Always use set_pc or add_pc to change the program counter.
  *
  * X-1.63       Camiel Vanderhoeven                             30-JAN-2008
  *      Remember number of instructions left in current memory page, so
  *      that the translation-buffer doens't need to be consulted on every
  *      instruction fetch when the Icache is disabled.
  *
  * X-1.62       Camiel Vanderhoeven                             29-JAN-2008
  *      Comments.
  *
  * X-1.61       Camiel Vanderhoeven                             29-JAN-2008
  *      Undid last change, remember separate last found translation-buffer
  *      entries for read and wrote operations. This should help with memory
  *      copy operations.
  *
  * X-1.60       Camiel Vanderhoeven                             27-JAN-2008
  *      Have GO_PAL throw an exception, so we don't continue doing what we
  *      were doing before the exception was taken.
  *
  * X-1.59       Camiel Vanderhoeven                             26-JAN-2008
  *      Made IDB compile again.
  *
  * X-1.58       Camiel Vanderhoeven                             25-JAN-2008
  *      Added option to disable the icache.
  *
  * X-1.57       Camiel Vanderhoeven                             22-JAN-2008
  *      Nicer initialization of "state" structure.
  *
  * X-1.56       Camiel Vanderhoeven                             22-JAN-2008
  *      Implemented missing /V integer instructions.
  *
  * X-1.55       Camiel Vanderhoeven                             21-JAN-2008
  *      Moved some macro's to cpu_defs.h; implement new floating-point code.
  *
  * X-1.54       Camiel Vanderhoeven                             19-JAN-2008
  *      Run CPU in a separate thread if CPU_THREADS is defined.
  *      NOTA BENE: This is very experimental, and has several problems.
  *
  * X-1.53       Camiel Vanderhoeven                             18-JAN-2008
  *      Replaced sext_64 inlines with sext_u64_<bits> inlines for
  *      performance reasons (thanks to David Hittner for spotting this!);
  *      Process device interrupts after a 100-cpu-cycle delay.
  *
  * X-1.52       David Hittner                                   16-JAN-2008
  *      Added ADDL/V instruction, added MIPS estimate (define MIPS_ESTIMATE)
  *
  * X-1.51       Camiel Vanderhoeven                             08-JAN-2008
  *      Removed last references to IDE disk read SRM replacement.
  *
  * X-1.50       Camiel Vanderhoeven                             30-DEC-2007
  *      Print file id on initialization.
  *
  * X-1.49       Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.48       Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.47       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator.
  *
  * X-1.46       Camiel Vanderhoeven                             2-DEC-2007
  *      Changed the way translation buffers work, the way interrupts work.
  *
  * X-1.45       Brian Wheeler                                   1-DEC-2007
  *      Added support for instruction counting, underlined lines in
  *      listings, corrected some unsigned/signed issues.
  *
  * X-1.44       Camiel Vanderhoeven                             16-NOV-2007
  *      Avoid more compiler warnings.
  *
  * X-1.43       Camiel Vanderhoeven                             16-NOV-2007
  *      Avoid compiler warning about default without any cases.
  *
  * X-1.42       Camiel Vanderhoeven                             08-NOV-2007
  *      Instruction set complete now.
  *
  * X-1.41       Camiel Vanderhoeven                             06-NOV-2007
  *      Performance improvements to ICACHE: last result is kept; cache
  *      lines are larger (512 DWORDS in stead of 16 DWORDS), cache size is
  *      configurable (both number of cache lines and size of each cache
  *      line), memcpy is used to move memory into the ICACHE.
  *      CAVEAT: ICACHE can only be filled from memory (not from I/O).
  *
  * X-1.40       Camiel Vanderhoeven                             02-NOV-2007
  *      Added integer /V instructions.
  *
  * X-1.39       Camiel Vanderhoeven                             02-NOV-2007
  *      Added missing floating point instructions.
  *
  * X-1.38       Eduardo Marcelo Ferrat                          31-OCT-2007
  *      EXC_SUM contained the wrong register (3 in stead of 1) on a DTBM
  *      exception. Added instructions for CVTDG, CVTGD, MULG, CVTGF.
  *
  * X-1.37       Camiel Vanderhoeven                             18-APR-2007
  *      Faster lockstep mechanism (send info 50 cpu cycles at a time)
  *
  * X-1.36       Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.35	Camiel Vanderhoeven				10-APR-2007
  *	New mechanism for SRM replacements. Where these need to be executed,
  *	CSystem::LoadROM() puts a special opcode (a CALL_PAL instruction
  *	with an otherwise illegal operand of 0x01234xx) in memory.
  *	CAlphaCPU::DoClock() recognizes these opcodes and performs the SRM
  *	action.
  *
  * X-1.34	Camiel Vanderhoeven				10-APR-2007
  *	Unintentional version number increase.
  *
  * X-1.33       Camiel Vanderhoeven                             30-MAR-2007
  *      Formatting.
  *
  * X-1.32	Camiel Vanderhoeven				29-MAR-2007
  *	Added AST to the list of conditions that cause the processor to go to
  *	the interrupt PAL vector (680).
  *
  * X-1.31	Brian Wheeler					28-MAR-2007
  *	Fixed missing ) after #if !defined(SRM_NO_SPEEDUPS
  *
  * X-1.30	Camiel Vanderhoeven				26-MAR-2007
  *   a)	Possibility to disable SRM-code replacements with the defines
  *	SRM_NO_IDE, SRM_NO_SRL, and SRM_NO_SPEEDUPS
  *   b) Possibility to send SRM-code replacement debugging messages to the
  *	console, with the defines DEBUG_SRM_IDE and DEBUG_SRM_SRL
  *   c)	Added software-interrupts to the list of conditions that can cause
  *	the processot to go to the interrupt PAL vector (680)
  *
  * X-1.29	Camiel Vanderhoeven				14-MAR-2007
  *	Formatting.
  *
  * X-1.28	Camiel Vanderhoeven				14-MAR-2007
  *	Fixed typo in "case 0x22: OP(CPYSE,F12_f3);"
  *
  * X-1.27	Camiel Vanderhoeven				13-MAR-2007
  *	Added some floating-point opcodes, added es_float.h inclusion
  *
  * X-1.26	Camiel Vanderhoeven				12-MAR-2007
  *   a)	Changed call to CTranslationBuffer::convert_address (arguments list
  *	changed)
  *   b) Set values for EXC_SUM and MM_STAT on various exceptions
  *
  * X-1.25	Camiel Vanderhoeven				9-MAR-2007
  *	In the listing-process, addresses were executed twice
  *
  * X-1.24	Camiel Vanderhoeven				8-MAR-2007
  *   a)	Changed call to CTranslationBuffer::write_pte (arguments list
  *	changed)
  *   b)	Backed-out X-1.23 as real problem was solved. (X-1.3 in cpu_bwx.h)
  *
  * X-1.23	Camiel Vanderhoeven				7-MAR-2007
  *	HACK to stop APB.EXE from crashing when passing bootflags
  *
  * X-1.22	Camiel Vanderhoeven				3-MAR-2007
  *	Wrote code to be executed in stead of SRM console code for writing
  *	to the serial port, and reading from IDE disks. Mechanism is based
  *	on recognition of the PC value. Should be replaced with a better
  *	mechanism in the future.
  *
  * X-1.21	Camiel Vanderhoeven				2-MAR-2007
  *	Initialize debug_string to "".
  *
  * X-1.20	Camiel Vanderhoeven				2-MAR-2007
  *	Fixed problem in Save and RestoreState; argument f conflicted with
  *	class member f.
  *
  * X-1.19	Camiel Vanderhoeven				28-FEB-2007
  *	Added support for the lockstep-mechanism.
  *
  * X-1.18	Camiel Vanderhoeven				27-FEB-2007
  *	Removed an unreachable "return 0;" line from DoClock
  *
  * X-1.17	Camiel Vanderhoeven				22-FEB-2007
  *	E_FAULT returned from translation buffer now causes DFAULT exception
  *
  * X-1.16	Camiel Vanderhoven				22-FEB-2007
  *   a)	Changed call to CTranslationBuffer::convert_address (arguments list
  *	changed)
  *   b)	Fixed HW_MTPR and HW_MFPR opcodes
  *
  * X-1.15	Camiel Vanderhoeven				19-FEB-2007
  *	Fixed preprocessor macro concatenation bug (used ## both before and
  *	after the literal; changed this to only before).
  *
  * X-1.14	Camiel Vanderhoeven				18-FEB-2007
  *	Put all actual code behind the processor opcodes in cpu_xxx.h include
  *	files, and replaced them with OP(...,...) macro's in this file.
  *
  * X-1.13       Camiel Vanderhoeven                             16-FEB-2007
  *   a) Added CAlphaCPU::listing.
  *   b) Clocking changes (due to changes in CSystem): CAlphaCPU::DoClock now
  *      returns a value, and the CPU is registered as a fast clocked device.
  *
  * X-1.12       Brian Wheeler                                   13-FEB-2007
  *      Different algorithm used for UMULH (previous algorithm suffered from
  *      portability issues).
  *
  * X-1.11       Camiel Vanderhoeven                             13-FEB-2007
  *   a) Bugfix in the UMULH instruction.
  *   b) Bugfix in the HW_MTPR VA_CTL instruction. Now updates va_ctl_va_mode
  *      instead of i_ctl_va_mode.
  *
  * X-1.10       Camiel Vanderhoeven                             12-FEB-2007
  *   a) Moved debugging macro's to cpu_debug.h
  *   b) Cleaned up SEXT and REG macro's (a lot neater now)
  *   c) Moved CAlphaCPU::get_r and CAlphaCPU::get_prbr to AlphaCPU.h as
  *      inline functions
  *   d) Use SEXT macro in a some places where exotic constructions were used
  *      previously
  *
  * X-1.9        Camiel Vanderhoeven                             12-FEB-2007
  *   a) Added X64_BYTE, X64_WORD, X64_LONG and X64_QUAD, and used these
  *      instead of the corresponding values.
  *   b) Added ier to the variables that are saved to the state file.
  *
  * X-1.8        Camiel Vanderhoeven                             9-FEB-2007
  *   a) Moved debugging flags (booleans) to CSystem.cpp.
  *   b) Removed loggin of last_write_loc and last_write_val
  *
  * X-1.7        Camiel Vanderhoeven                             7-FEB-2007
  *      Made various dubugging-related statements dependent on the
  *      definition of IDB (interactive debugger)
  *
  * X-1.6        Camiel Vanderhoeven                             3-FEB-2007
  *      Inline function printable moved to StdAfx.h
  *
  * X-1.5        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.4        Brian Wheeler                                   3-FEB-2007
  *      More scanf and printf statements made compatible with Linux/GCC/glibc.
  *
  * X-1.3        Brian Wheeler                                   3-FEB-2007
  *      Scanf and printf statements made compatible with Linux/GCC/glibc.
  *
  * X-1.2        Brian Wheeler                                   3-FEB-2007
  *      Includes are now case-correct (necessary on Linux)
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2007
  *      Initial version in CVS.
  **/
#include "StdAfx.h"
#include "AlphaCPU.h"
#include "jit/jitengine.h"
#include "chipset/AliM1543C.h"
#include "TraceEngine.h"
#include "network/lockstep.h"
#include "cpu_memory.h"
#include "cpu_control.h"
#include "cpu_arith.h"
#include "cpu_logical.h"
#include "cpu_bwx.h"
#include "cpu_fp_memory.h"
#include "cpu_fp_branch.h"
#include "cpu_fp_operate.h"
#include "cpu_misc.h"
#include "cpu_vax.h"
#include "cpu_mvi.h"
#include "cpu_pal.h"
#include "cpu_debug.h"
#include "diag_rpcc.h"
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>   // _mm_setcsr: pin host MXCSR for the JIT SSE FP path
#elif defined(_M_ARM64)
#include <intrin.h>       // _WriteStatusReg: pin host FPCR for the JIT A64 FP path
#endif
#include <thread>        // std::this_thread::sleep_for (idle_nap)

void CAlphaCPU::release_threads()
{
	try {
		mySemaphore.set();
	}
	catch (const std::overflow_error&) {
		// Already signaled, nothing to do
	}
}

void CAlphaCPU::run()
{
	try
	{
		mySemaphore.wait();
		while (state.wait_for_start)
		{
			if (StopThread)
				return;
			CThread::sleep(1);
		}
		printf("*** CPU%d *** STARTING ***\n", get_cpuid());
#if defined(JIT_REGPROF) && defined(_WIN32)
		if (get_cpuid() == 0 && getenv("ES40_JIT_SAMPLER")) jit_sampler_start();
#endif

#if defined(_M_X64) || defined(__x86_64__)
		// Pin host SSE state for the JIT FP path: round-nearest, exceptions masked,
		// FTZ/DAZ off (denormal results must materialize to hit the interp-bail).
		_mm_setcsr(0x1F80);
#elif defined(_M_ARM64)
		// Same pin for the A64 JIT FP path: FPCR = 0 is RN, FZ/FZ16 off, DN off, traps masked.
		_WriteStatusReg(ARM64_FPCR, 0);
#elif defined(__aarch64__)
		// Same pin for the A64 JIT FP path: FPCR = 0 is RN, FZ/FZ16 off, DN off, traps masked.
		{ const unsigned long long fpcr_default = 0; __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr_default)); }
#endif

		// Re-base the timing-calibration epoch to when execution actually begins:
		// a secondary parks (wait_for_start) before the primary releases it, so
		// leaving start_time at init time makes check_state() derive a wildly
		// wrong cc_per_instruction (huge elapsed wall-time vs ~0 instructions).
		start_time = std::chrono::steady_clock::now();
		next_timer_fire = start_time;
		tick_last_fire = start_time;
		cc_last_sync = start_time;
		cc_large = 0;
		state.instruction_count = 0;
		prev_icount = 0;
		prev_cc = 0;
		prev_time = 0;

		for (;;)
		{
			if (StopThread)
				return;
#ifdef ES40_JIT
			// One chain ceiling per batch of clock and timer work.
			jit_run((int) CJitEngine::kChainCeiling);
#else
			// execute() now owns a 512-instruction batch. Calling it 2,000 times
			// here accidentally turned one scheduler turn into ~1M instructions,
			// starving peer CPUs and device workers during native-PAL SMP handoffs.
			execute();
			CThread::yield();
#endif
			if (cSystem && cSystem->IsSystemResetRequested())
			{
				CThread::sleep(1);
				continue;
			}
		}
	}
	catch (CException& e)
	{
		printf("Exception in CPU thread: %s.\n", e.displayText().c_str());

		// Let the thread die...
	}
}

/**
 * idle_nap: sleep the cpu thread while the guest idles.  
 * HIGHLY EXPERIMENTAL
 * Should be mostly-safe on Linux, but still can oversleep in various edge cases.
 * Cchip timer is only checked before entering JIT loop, same with interpreter. 
 * so 'never sleeps past next_timer_fire' below is not guaranteed on any platform.
 * Various other guest-configurable aspects can also change these timelines
 * IPIs could be delayed in response from CPU0. This may or may not trigger
 * guest timeouts
 * FreeBSD is probably the safest host platform this code can run on. 
 * CPU0 DOES NOT IMMEDIATELY SERIVCE THE TIMER AFTER THE NAP
 *
 * Intended operation of this function: 
 * Wakes on pending interrupt/timer, StopThread, or reset (100us poll, 1ms cap); 
 * CPU0 never sleeps past next_timer_fire, since its own loop fires the interval
 * tick.  No-op unless idle_nap = true in the cpu config, and on MP configs
 * (no cross-thread IPI wakeup here -- WTINT just returns immediately).
 **/
void CAlphaCPU::idle_nap()
{
	using namespace std::chrono;

	// Either route may nap: WTINT, or host-side detection.
	if ((!idle_nap_enabled && idle_rule == RULE_NONE)
		|| (cSystem && cSystem->get_cpu_num() > 1))
		return;

	if (!idle_announced)
	{
		idle_announced = true;
		printf("*** CPU%d *** idle nap engaged ***\n", get_cpuid());
		fflush(stdout);   // stdout is block-buffered when captured by a service manager
	}

	
	auto deadline = steady_clock::now() + milliseconds(1);
	if (state.iProcNum == 0 && next_timer_fire < deadline)
		deadline = next_timer_fire;

	while (!state.check_int && !state.check_timers && !StopThread
		   && !(cSystem && cSystem->IsSystemResetRequested()))
	{
		const auto now = steady_clock::now();
		if (now >= deadline)
			break;
		auto nap = deadline - now;
		// Each wake is a syscall round trip; at a 1024 Hz tick, 100us costs
		// ~10k/sec of them. Default is the original 100us.
		if (nap > microseconds(idle_nap_poll_us))
			nap = microseconds(idle_nap_poll_us);
		std::this_thread::sleep_for(nap);
	}
}

u64 CAlphaCPU::tick_next_gap_ns(u64 period_ns)
{
	tick_fire_idx++;
	const u32 ph = tick_fire_idx % 277;
	const u32 tri = (ph <= 138) ? ph : (277 - ph);
	tick_pace_lcg = tick_pace_lcg * 1664525u + 1013904223u;
	return period_ns / 2 + period_ns * tri / 400
		+ period_ns * ((tick_pace_lcg >> 24) & 0x3f) / 1024;
}

/**
 * tick_hold: the instruction-paced envelope (timer.max_instr_per_tick) is full and the
 * wall-clock interval tick is not due yet.  Hold this CPU thread until the tick time 
 * or CPU0 must fire comes. 
 **/
CAlphaCPU::TickHold CAlphaCPU::tick_hold(u64 period_ns)
{
	using namespace std::chrono;

	const u32 seq = tick_seen_seq;
	auto deadline = steady_clock::now() + nanoseconds(2 * period_ns);   // secondaries: backstop if CPU0 is late
	if (state.iProcNum == 0)
	{
		deadline = next_timer_fire;
		if (tick_last_fire + nanoseconds(tick_gap_ns) > deadline)
			deadline = tick_last_fire + nanoseconds(tick_gap_ns);
	}

	for (;;)
	{
		if (cSystem->get_tick_seq() != seq)
			return TickHold::Ticked;
		if (state.check_int || StopThread || cSystem->IsSystemResetRequested())
			return TickHold::Doorbell;
		const auto now = steady_clock::now();
		if (now >= deadline)
			return state.iProcNum == 0 ? TickHold::Ticked : TickHold::Expired;
		auto nap = deadline - now;
		if (nap > microseconds(100))
			nap = microseconds(100);
		std::this_thread::sleep_for(nap);
	}
}

u64 CAlphaCPU::read_ipr(u32 function)
{
	return m_profile.ipr_read.read(*this, function);
}

void CAlphaCPU::write_ipr(u32 function, u64 value, AlphaIPRWriteContext context)
{
	m_profile.ipr_write.write(*this, function, value, context);
}

bool CAlphaCPU::pal_hardware_instruction_valid(u32 instruction) const
{
	const auto describe = m_profile.pal.hardware_instruction_access;
	return describe && alpha_pal_instruction_allowed(describe(instruction),
		(state.pc & 1) != 0, (u32)state.cm, state.hwe);
}

AlphaPALExceptionEntry CAlphaCPU::pal_exception_entry(AlphaPALException reason, u64 pc) const
{
	return m_profile.pal.exception_entry(reason, pc);
}

u64 CAlphaCPU::pal_exception_offset(AlphaPALException reason) const
{
	// MMU calls keep their fault-state and lock handling - family vector 
	// is made selection here. GO_PAL uses the complete entry.
	return pal_exception_entry(reason, state.current_pc).offset;
}

void CAlphaCPU::enter_pal_exception(const AlphaPALExceptionEntry& entry)
{
	state.exc_addr = entry.saved_pc;
	set_pc(state.pal_base | entry.offset | U64(1));
	if (entry.clear_lock)
		cSystem->cpu_clear_lock(state.iProcNum);
}

void CAlphaCPU::enter_native_call_pal(u32 function)
{
	const auto entry = m_profile.pal.call_pal_entry(function, state.current_pc, state.pc);
	state.exc_addr = entry.saved_pc;
	state.r[state.sde ? entry.shadow_link_register : entry.link_register] = entry.return_pc;
	set_pc(state.pal_base | entry.offset);
}

/**
 * Constructor.
 **/
CAlphaCPU::CAlphaCPU(CConfigurator* cfg, CSystem* system, const AlphaCPUProfile& profile)
	: CSystemComponent(cfg, system), m_profile(profile), mySemaphore(0, 1)
{
#ifdef ES40_JIT
	// The JIT compiles PALcode like any other guest code; the HLE can't be used
	system->request_native_pal("JIT build");
#else
	if (cfg->get_bool_value("palcode.vms.nohle", false))
	{
		char why[64];
		snprintf(why, sizeof(why), "palcode.vms.nohle set on %s", cfg->get_myName());
		system->request_native_pal(why);
	}
#endif
}

/**
 * Initialize the CPU.
 **/
void CAlphaCPU::init()
{
	memset(&state, 0, sizeof(state));
	last_dtb_virt[0] = last_dtb_virt[1] = 0;
	cc_last_read = 0;   // rpcc_read floor state tracks state.cc -- reset together
	cc_borrow = 0;
	cc_wall_remainder = 0;

	cpu_hz = myCfg->get_num_value("speed", true, 500000000);
	idle_nap_enabled = myCfg->get_bool_value("idle_nap", false);
	m_unaligned_fixup = myCfg->get_bool_value("unaligned_fixup", true);
	{
		const char* _m = myCfg->get_text_value("idle_detect", "off");
		idle_rule = idle_method_by_name(_m);
		if (idle_rule < 0)
		{
			printf("*** CPU%d *** unknown idle_detect \"%s\" -- idle detection OFF. Known:", get_cpuid(), _m);
			for (int _i = 0; idle_methods[_i].name; _i++)
				printf(" %s", idle_methods[_i].name);
			printf("\n");
			idle_rule = RULE_NONE;
		}
		idle_spin_count = (u32) myCfg->get_num_value("idle_spin_count", false, 2);
		idle_nap_poll_us = (u32) myCfg->get_num_value("idle_nap_poll_us", false, 1000);
		if (idle_spin_count < 1)
			idle_spin_count = 1;
	}
	exit_on_pal_halt = myCfg->get_myParent()->get_bool_value("exit_on_pal_halt", false);

	// Instruction-paced interval-timer cap. 
	// Default = 1.25M, the 1024Hz-tick cadence of the fastest Alpha - 1.25GHz EV68)
	m_max_instr_per_tick = myCfg->get_num_value("timer.max_instr_per_tick", false, 1250000);
	if (const char* e = getenv("ES40_MAX_INSTR_PER_TICK"))
		m_max_instr_per_tick = (u64)atoll(e);

	vmspal_lle_enabled = cSystem->native_pal_requested();

	state.iProcNum = cSystem->RegisterCPU(this);

#ifdef ES40_JIT
	if (!m_jit) m_jit = new CJitEngine(m_profile, (int)state.iProcNum);
	m_jit->set_exit_slot(&m_jit_exit);
	{
		// Tell the JIT the byte offsets (from `this`) of the fields its inline load
		// fast path reads, so compiled code can address them via [cpu + offset].
		CJitEngine::JitOffsets o;
		// Slot [0][0] of the read cache; the inline load adds dpc_index(va)*dpc_stride to reach
		// the indexed slot, so these are the base (slot 0) field offsets.
		o.dpc_valid = (uint32_t)((char*)&data_page_cache[0][0].valid - (char*)this);
		o.dpc_virt_page = (uint32_t)((char*)&data_page_cache[0][0].virt_page - (char*)this);
		o.dpc_phys_base = (uint32_t)((char*)&data_page_cache[0][0].phys_base - (char*)this);
		o.dpc_host_bias = (uint32_t)((char*)&data_page_cache[0][0].host_bias - (char*)this);
		o.dpc_cm = (uint32_t)((char*)&data_page_cache[0][0].cm - (char*)this);
		o.dpc_asn = (uint32_t)((char*)&data_page_cache[0][0].asn - (char*)this);
		o.dpc_stride = (uint32_t)sizeof(data_page_cache[0][0]);
		o.dpc_mask = (uint32_t)kDpcMask;
		o.dpc_write_row = (uint32_t)((char*)&data_page_cache[1][0] - (char*)&data_page_cache[0][0]);
		o.dpc_bank = (uint32_t)((char*)&m_dpc_bank - (char*)this);
		o.state_cm = (uint32_t)((char*)&state.cm - (char*)this);
		o.state_asn0 = (uint32_t)((char*)&state.asn0 - (char*)this);
		o.state_asn = (uint32_t)((char*)&state.asn - (char*)this);
		o.dram_ptr = (uint32_t)((char*)&dram_ptr - (char*)this);
		o.dram_size = (uint32_t)((char*)&dram_size - (char*)this);
		o.state_pc = (uint32_t)((char*)&state.pc - (char*)this);
		o.state_current_pc = (uint32_t)((char*)&state.current_pc - (char*)this);
		o.jit_budget = (uint32_t)((char*)&m_jit_budget - (char*)this);
		o.check_int = (uint32_t)((char*)&state.check_int - (char*)this);
		o.check_timers = (uint32_t)((char*)&state.check_timers - (char*)this);
		o.link_from = (uint32_t)((char*)&m_link_from - (char*)this);
		o.jit_exit = (uint32_t)((char*)&m_jit_exit - (char*)this);
		o.fpen = (uint32_t)((char*)&state.fpen - (char*)this);
		o.exc_sum = (uint32_t)((char*)&state.exc_sum - (char*)this);
		o.f_base = (uint32_t)((char*)&state.f[0] - (char*)this);
		o.fpcr = (uint32_t)((char*)&state.fpcr - (char*)this);
		o.exc_addr = (uint32_t)((char*)&state.exc_addr - (char*)this);
		o.pal_base = (uint32_t)((char*)&state.pal_base - (char*)this);
		o.sde = (uint32_t)((char*)&state.sde - (char*)this);
		o.mm_stat = (uint32_t)((char*)&state.mm_stat - (char*)this);
		o.fault_va = (uint32_t)((char*)&state.fault_va - (char*)this);
		// CPU-resident helper table:
		{
			const CJitEngine::HelperSet ht = {
				(void*)&CAlphaCPU::jit_read,        (void*)&CAlphaCPU::jit_write,
				(void*)&CAlphaCPU::jit_opcdec,      (void*)&CAlphaCPU::jit_hw_mfpr,
				(void*)&CAlphaCPU::jit_read_phys,   (void*)&CAlphaCPU::jit_hw_mtpr,
				(void*)&CAlphaCPU::jit_write_phys,  (void*)&CAlphaCPU::jit_indirect,
				(void*)&CAlphaCPU::jit_read_locked, (void*)&CAlphaCPU::jit_stc,
				(void*)&CAlphaCPU::jit_misc,        (void*)&CAlphaCPU::jit_read_vpte, (void*)&CAlphaCPU::jit_read_wchk,
				(void*)&CAlphaCPU::jit_itof,        (void*)&CAlphaCPU::jit_ftoi,
				(void*)&CAlphaCPU::jit_fltl,
				(void*)&CAlphaCPU::jit_fp_read,     (void*)&CAlphaCPU::jit_fp_write,
				(void*)&CAlphaCPU::jit_fltv };
			static_assert(sizeof(ht) == sizeof(m_jit_helper_tab),
				"m_jit_helper_tab must match HelperSet exactly (the JIT indexes it as void*[])");
			memcpy(m_jit_helper_tab, &ht, sizeof(ht));
			o.helpers = (uint32_t)((char*)&m_jit_helper_tab[0] - (char*)this);
		}
		m_jit->set_offsets(o);
	}
#endif

	state.wait_for_start = (state.iProcNum == 0) ? false : true;
	icache_enabled = true;
	flush_icache();

	tbia(ACCESS_READ);
	tbia(ACCESS_EXEC);

	//  state.fpcr = U64(0x8ff0000000000000);
	state.fpen = true;
	state.i_ctl_other = U64(0x502086);
	state.smc = 1;

	// SROM imitation...
	add_tb(0, 0, U64(0xff61), ACCESS_READ, state.asn0);

#if defined(IDB)
	bListing = false;
#endif
	myThread = 0;

	cc_large = 0;
	prev_cc = 0;
	start_cc = 0;
	prev_time = 0;
	prev_icount = 0;
	start_icount = 0;

	start_time = std::chrono::steady_clock::now();
	next_timer_fire = start_time;

#if defined(CONSTANT_TIME_FACTOR)
	cc_per_instruction = CONSTANT_TIME_FACTOR;
#else
	cc_per_instruction = 70;
#endif

	state.r[22] = state.r[22 + 32] = state.iProcNum;

	dram_ptr = cSystem->PtrToMem(0);
	dram_size = U64(1) << cSystem->get_memory_bits();

	flush_data_page_cache();

	seq_line_ptr = nullptr;
	seq_offset = 0;
	seq_remaining = 0;
	seq_next_pc = 0;

	printf("%s(%d): $Id$\n",
		devid_string, state.iProcNum);
	if (state.iProcNum == 0)
	{
		if (m_max_instr_per_tick)
			printf("%s: interval timer paced to %" PRIu64 " guest instr/tick\n",
				devid_string, m_max_instr_per_tick);
		else
			printf("%s: interval timer wall-clock only (instruction pacing disabled)\n", devid_string);
	}
}

void CAlphaCPU::ResetForSystemReset()
{
	const int savedProcNum = state.iProcNum;

	memset(&state, 0, sizeof(state));
	last_dtb_virt[0] = last_dtb_virt[1] = 0;
	cc_last_read = 0;   // rpcc_read floor state tracks state.cc -- reset together
	cc_borrow = 0;
	cc_wall_remainder = 0;
	state.iProcNum = savedProcNum;

	cpu_hz = myCfg->get_num_value("speed", true, 500000000);
	idle_nap_enabled = myCfg->get_bool_value("idle_nap", false);
	m_unaligned_fixup = myCfg->get_bool_value("unaligned_fixup", true);
	{
		const char* _m = myCfg->get_text_value("idle_detect", "off");
		idle_rule = idle_method_by_name(_m);
		if (idle_rule < 0)
		{
			printf("*** CPU%d *** unknown idle_detect \"%s\" -- idle detection OFF. Known:", get_cpuid(), _m);
			for (int _i = 0; idle_methods[_i].name; _i++)
				printf(" %s", idle_methods[_i].name);
			printf("\n");
			idle_rule = RULE_NONE;
		}
		idle_spin_count = (u32) myCfg->get_num_value("idle_spin_count", false, 2);
		idle_nap_poll_us = (u32) myCfg->get_num_value("idle_nap_poll_us", false, 1000);
		if (idle_spin_count < 1)
			idle_spin_count = 1;
	}
	exit_on_pal_halt = myCfg->get_myParent()->get_bool_value("exit_on_pal_halt", false);
	m_max_instr_per_tick = myCfg->get_num_value("timer.max_instr_per_tick", false, 1250000);
	if (const char* e = getenv("ES40_MAX_INSTR_PER_TICK"))
		m_max_instr_per_tick = (u64)atoll(e);
	tick_last_icount = 0;
	tick_seen_seq = 0;

	state.wait_for_start = (state.iProcNum == 0) ? false : true;
	icache_enabled = true;
	flush_icache();

	tbia(ACCESS_READ);
	tbia(ACCESS_EXEC);

	state.fpen = true;
	state.i_ctl_other = U64(0x502086);
	state.smc = 1;

	// SROM imitation...
	add_tb(0, 0, U64(0xff61), ACCESS_READ, state.asn0);

	myThread = 0;

	cc_large = 0;
	prev_cc = 0;
	start_cc = 0;
	prev_time = 0;
	prev_icount = 0;
	start_icount = 0;

	start_time = std::chrono::steady_clock::now();
	next_timer_fire = start_time;

#if defined(CONSTANT_TIME_FACTOR)
	cc_per_instruction = CONSTANT_TIME_FACTOR;
#else
	cc_per_instruction = 70;
#endif

	state.r[22] = state.r[22 + 32] = state.iProcNum;

	dram_ptr = cSystem->PtrToMem(0);
	dram_size = U64(1) << cSystem->get_memory_bits();

	flush_data_page_cache();

	seq_line_ptr = nullptr;
	seq_offset = 0;
	seq_remaining = 0;
	seq_next_pc = 0;
}

void CAlphaCPU::start_threads()
{
	char  buffer[5];
	mySemaphore.tryWait(1);
	if (!myThread)
	{
		sprintf(buffer, "cpu%d", state.iProcNum);
		myThread = new CThread(buffer);
		printf(" %s", myThread->getName().c_str());
		StopThread = false;
		myThread->setPriority(CThread::PRIO_HIGHEST);
		myThread->start(*this);
	}
}

void CAlphaCPU::stop_threads()
{
	StopThread = true;
	if (myThread)
	{
		try {
			mySemaphore.set();
		}
		catch (const std::overflow_error&) {
			// Already signaled
		}
		printf(" %s", myThread->getName().c_str());
		myThread->join();
		delete myThread;
		myThread = 0;
	}

	mySemaphore.tryWait(1);
}

// ============================================================================
//
// Alpha FPCR layout (all meaningful bits are in the upper 32 bits):
//
//   63    SUM      - Summary Bit (SUM).
//   62    INED     - Inexact Disable (INED). 
//   61    UNFD     - Underflow Disable (UNFD)
//   60    UNDZ     - Underflow to Zero (UNDZ)
//   59:58 DYN      -  Dynamic Rounding Mode (DYN)
//   57    IOV      - Integer Overflow (IOV)
//   56    INE      - Inexact Result (INE)
//   55    UNF      - Underflow (UNF).
//   54    OVF      - Overflow (OVF)
//   53    DZE      - Division by Zero (DZE)
//   52    INV      - Invalid Operation (INV)
//   51    OVFD     - Overflow Disable (OVFD)
//   50    DZED     - Division by Zero Disable (DZED)
//   49    INVD     - Invalid Operation Disable (INVD)
//   48    DNZ      - Denormal Operands to Zero (DNZ)
//   47    DNOD     - Denormal Operand Exception Disable (DNOD)
//   46:0  Reserved, must be read as zero
//
//   Alpha Architecture Reference Manual, 4th ed. [ARM]:
//     Section 4.7.8   - Floating-Point Control Register (FPCR)
//     Section 4.7.8.1 - Accessing the FPCR
//     Section 4.7.8.2 - Default Values of the FPCR
//     Section 4.10.4  - Move from/to Floating-Point Control Register
//   at least according to this one https://download.majix.org/dec/alpha_arch_ref.pdf
// ============================================================================

u64 CAlphaCPU::read_fpcr_arch() const {
	u64 val = state.fpcr & ~(FPCR_RAZ | U64(0x00000000FFFFFFFF));

	if (val & FPCR_ERR)
		val |= FPCR_SUM;
	else
		val &= ~FPCR_SUM;

	return val;
}

void CAlphaCPU::write_fpcr_arch(u64 arch_val) {
	u64 val = arch_val & ~(FPCR_RAZ | U64(0x00000000FFFFFFFF));

	if (val & FPCR_ERR)
		val |= FPCR_SUM;
	else
		val &= ~FPCR_SUM;

	state.fpcr = val;
}

/**
 * Destructor.
 **/
CAlphaCPU::~CAlphaCPU()
{
	stop_threads();
}

#if defined(IDB)
char    dbg_string[1000];
#if !defined(LS_MASTER) && !defined(LS_SLAVE)
char* dbg_strptr;
#endif

/**
 * \brief Do whatever needs to be done to a debug-string.
 *
 * Used in IDB-mode to handle the disassembly- string. In es40_idb, it is
 * written to the standard output.
 *
 * \param s       Pointer to the debug string.
 **/
void handle_debug_string(char* s)
{
#if defined(LS_SLAVE) || defined(LS_MASTER)

	//    lockstep_compare(s);
	*dbg_strptr++ = '\n';
	*dbg_strptr = '\0';
#else
	if (*s)
		printf("%s\n", s);
#endif
}
#endif
#if defined(MIPS_ESTIMATE)

// MIPS_INTERVAL must take longer than 1 second to execute
// or estimate will generate a divide-by-zero error
#define MIPS_INTERVAL 0xfffffff
static time_t saved = 0;
static u64    count;
static double min_mips = 999999999999999.0;
static double max_mips = 0.0;
#include <time.h>
#endif

// ASN switch: only edges out of global blocks can hold a wrong-ASN successor.
void CAlphaCPU::jit_note_asn_change()
{
#ifdef ES40_JIT
	if (m_jit)
		m_jit->note_asn_change();
#endif
}

#ifdef ES40_JIT
// wrapper: LoadROM's SRM self-decompression drives CPU 0 from the main()
// just before the CPU threads exist.
void CAlphaCPU::jit_step(int budget)
{
	jit_run(budget);
}

#if defined(JIT_REGPROF) && defined(_WIN32)
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#include <algorithm>
#include <map>
#include <string>

void CAlphaCPU::jit_sampler_start()
{
	DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &m_host_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
	m_samples = new uint64_t[5u * (1u << 18)];
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	SymInitialize(GetCurrentProcess(), nullptr, TRUE);
	HANDLE h = (HANDLE) m_host_thread;
	m_sampler = std::thread([this, h]() {
		for (;;)
		{
			Sleep(1);
			if (SuspendThread(h) == (DWORD) -1) continue;
			CONTEXT ctx; ctx.ContextFlags = CONTEXT_CONTROL;
			if (GetThreadContext(h, &ctx))
			{
				const uint32_t n = m_nsamples.load(std::memory_order_relaxed);
				if (n < (1u << 18))
				{
					uint64_t* rec = &m_samples[5u * n];
					rec[0] = ctx.Rip;
					SIZE_T got = 0;
					if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID) ctx.Rsp, &rec[1], 4 * sizeof(uint64_t), &got) || got < 4 * sizeof(uint64_t))
						rec[1] = rec[2] = rec[3] = rec[4] = 0;
					m_nsamples.store(n + 1, std::memory_order_relaxed);
				}
			}
			ResumeThread(h);
		}
	});
	m_sampler.detach();
	printf("[JIT][SAMPLER][CPU%d] host RIP sampler on (1 kHz)\n", get_cpuid());
}

void CAlphaCPU::jit_sampler_report()
{
	const uint32_t n = m_nsamples.load(std::memory_order_relaxed);
	if (n < 100) return;
	std::vector<CJitEngine::HostRange> tab;
	m_jit->host_code_table(tab);
	std::sort(tab.begin(), tab.end(), [](const CJitEngine::HostRange& a, const CJitEngine::HostRange& b) { return a.start < b.start; });
	std::map<uint32_t, uint32_t> blk;                 // block idx -> samples
	std::map<uint64_t, uint32_t> blk_off;             // (idx<<20 | offset) -> samples
	std::map<std::string, uint32_t> sym;              // symbol -> samples
	std::map<std::string, uint32_t> symc;             // symbol <- caller -> samples
	uint32_t in_jit = 0, other = 0;
	auto name_of = [](uint64_t addr, char* out, size_t cap) -> bool {
		char buf[sizeof(SYMBOL_INFO) + 256]; SYMBOL_INFO* si = (SYMBOL_INFO*) buf;
		si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
		DWORD64 disp = 0;
		if (!SymFromAddr(GetCurrentProcess(), addr, &disp, si)) return false;
		snprintf(out, cap, "%s", si->Name); return true;
	};
	for (uint32_t i = 0; i < n; ++i)
	{
		const uint64_t* rec = &m_samples[5u * i];
		const uint64_t rip = rec[0];
		auto it = std::upper_bound(tab.begin(), tab.end(), rip, [](uint64_t v, const CJitEngine::HostRange& r) { return v < r.start; });
		if (it != tab.begin() && (--it)->start <= rip && rip < it->start + it->size)
		{
			in_jit++; blk[it->idx]++; blk_off[((uint64_t) it->idx << 20) | (rip - it->start)]++;
			continue;
		}
		other++;
		char nm[256];
		if (!name_of(rip, nm, sizeof(nm))) snprintf(nm, sizeof(nm), "?%llx", (unsigned long long) (rip >> 12));
		sym[nm]++;
		char caller[256] = "?";
		for (int k = 1; k <= 4; ++k)   // first stack word that names a different function
		{
			char c[256];
			if (rec[k] && name_of(rec[k], c, sizeof(c)) && strcmp(c, nm) != 0) { snprintf(caller, sizeof(caller), "%s", c); break; }
		}
		char key[520]; snprintf(key, sizeof(key), "%s <- %s", nm, caller);
		symc[key]++;
	}
	m_nsamples.store(0, std::memory_order_relaxed);
	printf("[JIT][SAMPLER][CPU%d] %u samples: JIT code %.1f%% | C++ %.1f%%\n", get_cpuid(), n, 100.0 * in_jit / n, 100.0 * other / n);
	std::vector<std::pair<uint32_t, std::string>> rows;
	for (auto& kv : sym) rows.push_back({ kv.second, kv.first });
	std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first > b.first; });
	for (size_t i = 0; i < rows.size() && i < 14; ++i)
		printf("[JIT][SAMPLER][CPU%d]   %5.1f%%  %s\n", get_cpuid(), 100.0 * rows[i].first / n, rows[i].second.c_str());
	rows.clear();
	for (auto& kv : symc) rows.push_back({ kv.second, kv.first });
	std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first > b.first; });
	for (size_t i = 0; i < rows.size() && i < 10; ++i)
		printf("[JIT][SAMPLER][CPU%d]   %5.1f%%  %s\n", get_cpuid(), 100.0 * rows[i].first / n, rows[i].second.c_str());
	printf("[JIT][SAMPLER][CPU%d] dpc: %llu flushes | helper misses empty %llu, conflict %llu, cm/asn %llu\n", get_cpuid(),
		(unsigned long long) m_dpc_flushes, (unsigned long long) m_dpc_miss[0], (unsigned long long) m_dpc_miss[1], (unsigned long long) m_dpc_miss[2]);
	m_dpc_flushes = 0; m_dpc_miss[0] = m_dpc_miss[1] = m_dpc_miss[2] = 0;
	std::vector<std::pair<uint32_t, uint32_t>> brows;
	for (auto& kv : blk) brows.push_back({ kv.second, kv.first });
	std::sort(brows.begin(), brows.end(), [](auto& a, auto& b) { return a.first > b.first; });
	for (size_t i = 0; i < brows.size() && i < 12; ++i)
	{
		const CJitEngine::JitBlock& b = m_jit->block_at(brows[i].second);
		printf("[JIT][SAMPLER][CPU%d]   %5.1f%%  block tag=%016llx prefix=%u csz=%u offs:", get_cpuid(), 100.0 * brows[i].first / n,
			(unsigned long long) b.tag, b.prefix_len, b.rp_csz);
		std::vector<std::pair<uint32_t, uint32_t>> offs;
		for (auto& kv : blk_off) if ((uint32_t) (kv.first >> 20) == brows[i].second) offs.push_back({ kv.second, (uint32_t) (kv.first & 0xfffff) });
		std::sort(offs.begin(), offs.end(), [](auto& a, auto& b) { return a.first > b.first; });
		for (size_t k = 0; k < offs.size() && k < 6; ++k) printf(" +%x(%u)", offs[k].second, offs[k].first);
		printf("\n");
	}
}
#endif

void CAlphaCPU::jit_flush_blocks()
{
	if (m_jit)
		m_jit->flush();
}

// ASM-bit-clear icache flush (process/ASN switch, ITB_IAP): keep the global PAL blocks compiled
// instead of recompiling them on every such flush (the dominant JIT cost in PAL-heavy code).
void CAlphaCPU::jit_flush_blocks_asm()
{
	if (m_jit)
		m_jit->flush_non_global();
}

void CAlphaCPU::jit_run(int budget)
{
	// Relative to the caller's budget, so no magic number here.
	if (idle_rule != RULE_NONE && idle_spin_min_instr == 0)
		idle_spin_min_instr = (u64) budget * 3 / 4;

	if (m_jit) m_jit->reclaim_if_pending();   // deferred code reclaim, here at a safe point (no compiled frame live)
	const auto now = std::chrono::steady_clock::now();
	cc_last_sync += std::chrono::nanoseconds(g_diag_excluded_ns);   // keep device-diagnostic print stalls out of the RPCC (diag_rpcc.h)
	g_diag_excluded_ns = 0;
	// Wall-clock RPCC: advance the cycle counter by real elapsed time * cpu_hz (when enabled) so
	// it tracks the configured CPU frequency no matter how fast/bursty the JIT runs. The helper
	// caps (not drops) odd deltas at 1s: dropping made the cc run slow through early-boot
	// device-init stalls, and SRM's cycles-per-tick calibration locked that in as a
	// too-low CPU speed (the 357MHz bug).
	sync_cc_wallclock();

	// Drive the Cchip interval timer once per dispatch batch (CPU0 only), not
	// once per instruction the way the in-execute() poll did.
	if (state.iProcNum == 0)
	{
		if (now >= next_timer_fire)
		{
			const u64 period_ns = theAli ? theAli->get_interval_period_ns() : 0;
			if (period_ns)
			{
				// Count-preserving, paced catch-up: the schedule advances one period per
				// fire so ticks lost to a busy/stalled CPU0 thread are repaid and the
				// guests' tick-counted clocks (VMS never resyncs) stay true to wall time.
				// Repay gaps are MODULATED so consecutive SRM cycles-per-tick windows
				// (~100 ticks) never agree during a repay stretch. Per-fire noise alone
				// averages out 1/sqrt(N) over a window; the triangle wave's ~2.8-window
				// wavelength survives the averaging, and the noise breaks symmetric-
				// alignment ties. 
				// Backlog beyond 1s (debugger pause, host sleep) is dropped.
				if (now - tick_last_fire >= std::chrono::nanoseconds(tick_gap_ns))
				{
					cSystem->interrupt(-1, true);
					tick_last_fire = now;
					tick_gap_ns = tick_next_gap_ns(period_ns);
					next_timer_fire += std::chrono::nanoseconds(period_ns);
					if (now - next_timer_fire > std::chrono::seconds(1))
						next_timer_fire = now;
				}
			}
			else
			{
				cSystem->interrupt(-1, true);
				tick_last_fire = now;
				next_timer_fire = now + std::chrono::seconds(1);
			}
		}
	}

	// Instruction-paced interval-tick envelope, for EVERY CPU.
	// Gated off for the firmware/VMS PALcode (base 0x8000, and the pre-PAL reset state):
	// SRM runs under it, and SRM's per-CPU speed calibration counts cycles
	// per tick in a tight spin. NT and OSF-style PALcodes load at other bases and are paced.
	// A CPU that retires the envelope before the wall-clock tick is due shall sleep until
	// the correct tick time, the cpu should never receive early/extra. 
	// The RPCC/cycle counter is unaffected (it stays wall-clock accurate via sync_cc_wallclock).
	const u64 pace_period_ns = (m_max_instr_per_tick && theAli && state.pal_base && state.pal_base != U64(0x8000))
		? theAli->get_interval_period_ns() : 0;   // 0 = pacing off, or the guest hasn't programmed the tick yet
	if (pace_period_ns)
	{
		const u64 ic = state.instruction_count;
		const u32 seq = cSystem->get_tick_seq();
		if (seq != tick_seen_seq)
		{
			tick_seen_seq = seq;
			tick_last_icount = ic;
		}
		else if ((ic - tick_last_icount) >= m_max_instr_per_tick)
		{
			switch (tick_hold(pace_period_ns))
			{
			case TickHold::Ticked:     // landed, or CPU0 fires it on re-entry: the window re-opens there
				return;
			case TickHold::Expired:    // secondary, CPU0 late: re-open the window rather than starve
				tick_last_icount = ic;
				break;
			case TickHold::Doorbell:   // interrupt/stop/reset: run this batch, hold again next entry
				break;
			}
		}
	}

	while (budget > 0)
	{
		const u64 start_virt = state.pc;
		const u32 start_asn = (u32)state.asn;
		const bool start_pal_shadow = (start_virt & 1) && state.sde;

		// PAL reset-vector entry
		if (start_virt == (state.pal_base | 1))
			flush_icache();

		// Host-side idle detection. If a dispatch runs its whole instruction
		// budget and the next starts at an address just seen, several times
		// over, the guest is going round in circles. This works with the JIT
		// on because a trace looping back to itself is what produces it.
		// Getting it wrong costs one sleep: 1ms at most, never past the next
		// timer tick, ended by any pending interrupt.
		if (idle_rule != RULE_NONE)
		{
			const u64 _ic = state.instruction_count;
			const int _ipl = (int) ((state.r[32 + 22] >> 8) & 0x1f);   // VMS PALcode keeps PS in p22

			if ((_ic - m_spin_ic) >= idle_spin_min_instr
				&& (idle_rule != RULE_SPIN_IPL3 || _ipl == 3))
			{
				bool seen = false;
				for (int _i = 0; _i < m_spin_set_n; _i++)
					if (m_spin_set[_i] == start_virt)
					{
						seen = true;
						break;
					}
				if (!seen)
				{
					// A new address breaks the run of consecutive repeated PCs.
					m_spin_hits = 0;
					if (m_spin_set_n < kSpinSetMax)
						m_spin_set[m_spin_set_n++] = start_virt;
					else
					{
						m_spin_set_n = 0;        // too wide to be a loop: start over
					}
				}
				else if (++m_spin_hits >= idle_spin_count)
				{
					m_spin_hits = idle_spin_count;   // saturate
					if (!m_spin_announced)
					{
						m_spin_announced = true;
						printf("*** CPU%d *** idle_detect engaged at pc=%016llx ipl=%d ***\n",
							get_cpuid(), (unsigned long long) start_virt, _ipl);
						fflush(stdout);
					}
					idle_nap();
				}
			}
			else
			{
				m_spin_hits = 0;
				m_spin_set_n = 0;
			}
			m_spin_ic = _ic;
		}

		// Resolve the block's physical start side-effect-free (FAKE = no fault, no TB fill) so
		// execute() stays the sole I-stream fetcher; covers superpage/KSEG (no TB entry). phys
		// validates a compiled block vs the live translation -- virtual+ASN keying misses remaps.
		u64  start_phys = 0;
		bool start_asm = false;
		bool have_phys = true;
		if (start_virt & 1)            // PALmode: physically addressed, always ASM
		{
			start_phys = start_virt & ~U64(1);
			start_asm = true;
		}
		else
		{
			// Fast path: icache hit-probe reads phys straight from the line, read-only (no fill,
			// no fault) -- get_icache's hit geometry minus the side effects. Covers warm code.
			const int ici = (int)(((start_virt & ~U64(3)) >> 11) & (ICACHE_ENTRIES - 1));
			if (icache_enabled && state.icache[ici].valid
				&& (state.icache[ici].asn == state.asn || state.icache[ici].asm_bit)
				&& state.icache[ici].address == (start_virt & ICACHE_MATCH_MASK))
			{
				start_phys = state.icache[ici].p_address + (start_virt & ICACHE_BYTE_MASK);
				start_asm = state.icache[ici].asm_bit;
			}
			// Slow path: superpage/KSEG + cold pages (FAKE = no side effect; miss -> interpret).
			else if (virt2phys(start_virt, &start_phys, ACCESS_EXEC | FAKE, &start_asm, 0) != 0)
				have_phys = false;
		}

		// Side-effect-free exec virt -> live physical (icache probe, else FAKE virt2phys). Re-resolves a trace
		// segment's live mapping without a TB fill / fault; mirrors the dispatch-top resolution above.
		auto live_exec_phys = [&](u64 v, u64* op) -> bool {
			v &= ~U64(3);   // strip the PALmode tag bit / align -- the physical is page-determined
			const int li = (int)((v >> 11) & (ICACHE_ENTRIES - 1));
			if (icache_enabled && state.icache[li].valid && (state.icache[li].asn == state.asn || state.icache[li].asm_bit) && state.icache[li].address == (v & ICACHE_MATCH_MASK)) { *op = state.icache[li].p_address + (v & ICACHE_BYTE_MASK); return true; }
			bool ad; return virt2phys(v, op, ACCESS_EXEC | FAKE, &ad, 0) == 0;
		};

		// Trace tier: gated lookup BEFORE the block cache. In non-debug release a hot trace runs
		// straight through and side-exits back here with state.pc set, then `continue`; the block
		// cache stays the universal fallback. Under JIT_VERIFY the production run is gated OFF,
		// traces still FORM (below), but they're exercised + compared inside the block verify (next
		// stage) rather than driving execution unchecked.
#if !defined(JIT_VERIFY) && defined(JIT_TRACES)
		// Same run-gates as the block path below: a compiled trace has no per-instruction interrupt
		// poll, so DON'T enter it while an interrupt/timer is pending (the interpreter must service it,
		// or the CPU livelocks -> OS sanity-timer bugcheck), nor a PALmode trace without SDE, nor one
		// that would overrun the budget. JIT_TRACES-off builds compile the whole hook away.
		auto trace_segs_live = [&](CJitEngine::TraceFragment* tf) -> bool {
			// The head's live phys is checked by trace_ok; re-resolve each INTERIOR segment too. An interior
			// page remapped to a different physical with IDENTICAL bytes is invisible to the source hash.
			for (uint32_t s = 1; s < tf->n_segs; ++s) { u64 sp; if (!live_exec_phys(tf->segs[s].guest_pc, &sp) || sp != tf->segs[s].phys_pc) { m_jit->note_trace_stale(); return false; } }
			return true;
		};
		if (have_phys && !(start_virt & 1) && m_jit->traces_enabled()
			&& !state.check_int && !state.check_timers)
		{
			CJitEngine::TraceFragment* t = m_jit->trace_lookup(start_virt, start_asn);
			if (t && t->underrun >= 64) { m_jit->demote_trace(t); t = nullptr; }   // R3b: stopped looping -> net tax
			if (t && (int)t->n_instr <= budget && (!(t->head_tag & 1) || state.sde)
				&& m_jit->trace_ok(t, start_phys, (const uint8_t*)dram_ptr) && trace_segs_live(t))
			{
				// chain-in: a predecessor's exit missed here and the trace is validated (vgen just
				// re-stamped) -- patch its slots with the trace's chained entry so the edge lands
				// in-frame next time. Hook-before-block ordering makes traces win the patch.
				if (m_link_from && t->chain_entry) { m_jit->note_link_bail();
					m_jit->patch_edge(m_link_from, t->head_tag, t->vgen, t->chain_entry, t->asm_global);
					m_link_from = nullptr; }
				m_jit_budget = budget;   // ceiling for the trace (its loads/bails honor it like a block)
#ifdef JIT_STATS
				const uint64_t _trace_t0 = jit_rdtsc();
#endif
				const u32 done = ((CJitEngine::JitFn)t->code)(this, &state.r[0]);
#ifdef JIT_STATS
				const uint64_t _trace_tsc = jit_rdtsc() - _trace_t0;
#endif
				state.r[31] = 0;
				break_seq_icache();      // the trace wrote state.pc natively; drop the stale cursor
				state.instruction_count += done;
				cc_large += (u64)done * cc_per_instruction;
				budget -= done;
#ifdef JIT_STATS
				cc_last_sync += std::chrono::nanoseconds(m_jit->note_exec(done, 0, _trace_tsc, 0));
				m_jit->trace_entered();
				if (done < (u32)t->n_instr) m_jit->trace_exited();   // ran fewer than the trace's first-pass span -> a side-exit/underrun
#endif
				if (done > 0) continue;  // progress: re-dispatch at the trace's exit PC
			}
		}
#endif

		// Hot path: virtual+ASN lookup, phys-validated (skipped on a translation miss).
		CJitEngine::JitBlock* b = have_phys
			? m_jit->lookup(start_virt, start_asn, start_pal_shadow) : nullptr;
		if (have_phys && !b)   // lazy-flushed survivor? hash-revalidate in place (no interpreted pass)
			b = m_jit->revalidate_flushed(start_virt, start_asn, start_pal_shadow,
				start_phys, (const uint8_t*)dram_ptr);

		// A valid block whose phys no longer matches = a page remap the virtual key can't see.
		// The cold path below re-records it; log it -- it's the smoking gun for stale-chain bugs.
		if (b && b->code && b->phys != start_phys)
		{
			static int n_stale = 0;
			if (n_stale++ < 20)
				printf("[JIT][CPU%d] DISPATCH STALE: pc=%016llx block_phys=%016llx live_phys=%016llx\n",
					(int)state.iProcNum, (unsigned long long) start_virt,
					(unsigned long long) b->phys, (unsigned long long) start_phys);
		}

		// Run the compiled safe prefix natively when available. A pending interrupt
		// is deliverable only outside PALmode (execute() applies the same rule), while
		// delayed timers still require the interpreter's per-instruction poll.
		if (b && b->code && b->phys == start_phys && (int)b->prefix_len <= budget
			&& (!state.check_int || (b->tag & 1)) && !state.check_timers
			&& (!(b->tag & 1) || b->pal_shadow == (bool)state.sde))
		{
			b->vgen = m_jit->vgen();   // phys validated + lookup proved flush-fresh: refresh the chain epoch
#ifdef JIT_TRACES
			// after a block has dispatched 8x, promote it to a single-block trace (a
			// future trace head). Dispatch-counted for now -- it undercounts chained loops, but they
			// still surface here when a chain breaks
			if (!(b->tag & 1) && m_jit->traces_enabled() && ++b->hot == 8
				&& !m_jit->trace_lookup(start_virt, start_asn))
			{
				const CJitEngine::HelperSet hs = {
					(void*)&CAlphaCPU::jit_read,        (void*)&CAlphaCPU::jit_write,
					(void*)&CAlphaCPU::jit_opcdec,      (void*)&CAlphaCPU::jit_hw_mfpr,
					(void*)&CAlphaCPU::jit_read_phys,   (void*)&CAlphaCPU::jit_hw_mtpr,
					(void*)&CAlphaCPU::jit_write_phys,  (void*)&CAlphaCPU::jit_indirect,
					(void*)&CAlphaCPU::jit_read_locked, (void*)&CAlphaCPU::jit_stc,
					(void*)&CAlphaCPU::jit_misc,        (void*)&CAlphaCPU::jit_read_vpte, (void*)&CAlphaCPU::jit_read_wchk,
					(void*)&CAlphaCPU::jit_itof,        (void*)&CAlphaCPU::jit_ftoi,
					(void*)&CAlphaCPU::jit_fltl,
					(void*)&CAlphaCPU::jit_fp_read,     (void*)&CAlphaCPU::jit_fp_write,
					(void*)&CAlphaCPU::jit_fltv };
				// Loop/superblock FUSION: follow each block's STATIC taken branch target, growing the trace,
				// until the chain branches back to the head (compile_trace then closes the loop) or hits a
				// non-branch / non-live target / a non-head cycle / the segment cap. Works under verify too
				// (b->link is only set by production chaining).
				// 
				// form LOOP traces only -- a closed back-edge is the one shape with an advantage
				// over block chains (in-trace iteration + the spike's per-trace pins).
				// 
				// Linear traces duplicated block work (I-cache) and varying-successor heads (DCL
				// dispatch) formed pure side-exit taxes. Non-closing heads pay this scan once (hot
				// fires at exactly 8) and never re-form.
				static constexpr bool TraceLoopsOnly = true;
				CJitEngine::JitBlock* blist[CJitEngine::kMaxTraceSegs] = { b };
				u32 nb = 1;
				bool closes = false;
				// link-guided fusion: computed jumps (0x1a JMP/JSR/RET only -- no mode/state
				// change) follow the block's own hottest OBSERVED successor; the inter-block guard
				// (R10 == fused tag) is exactly the return-address speculation check. Lets loops
				// containing calls close (the common real-world hot-loop shape).
				static constexpr bool TraceLinkFusion = true;
				for (CJitEngine::JitBlock* cur = b; nb < CJitEngine::kMaxTraceSegs; ) {
				  const u32* aw = (const u32*)((const u8*)dram_ptr + cur->phys);
				  const u32 lop = aw[cur->prefix_len - 1]; const u32 lopc = lop >> 26;
				  u64 spc = 0;
				  if (lopc >= 0x38 && lopc <= 0x3f) {
				    // A conditional's static target is often the loop exit.
					// Prefer the successor this block exit took most recently
					// fall back to the static edge before linking.
				    for (int li = 0; li < CJitEngine::kLinkSlots; ++li)
				      if (cur->link[li].body) { spc = cur->link[li].tag; break; }
				    if (!spc) {
				      const int64_t disp = (int64_t)((uint64_t)(lop & 0x1FFFFF) << 43) >> 43;
				      spc = (((cur->tag & ~U64(1)) + 4 * (u64)(cur->prefix_len - 1)) + 4 + (u64)(disp * 4)) | (cur->tag & 1);
				    }
				  } else if (lopc == 0x30 || lopc == 0x34) {   // unconditional PC-relative target
				    const int64_t disp = (int64_t)((uint64_t)(lop & 0x1FFFFF) << 43) >> 43;
				    spc = (((cur->tag & ~U64(1)) + 4 * (u64)(cur->prefix_len - 1)) + 4 + (u64)(disp * 4)) | (cur->tag & 1);
				  } else if (TraceLinkFusion && lopc == 0x1a) {
				    for (int li = 0; li < CJitEngine::kLinkSlots; ++li)
				      if (cur->link[li].body) { spc = cur->link[li].tag; break; }
				    if (!spc || (spc & 1) != (cur->tag & 1)) break;   // no observed successor / mode change
				  } else break;                                       // breaker terminator: don't cross
				  if (spc == b->tag) { closes = true; break; }   // back-edge to the head -> compile_trace closes the loop
				  CJitEngine::JitBlock* succ = m_jit->lookup(spc, start_asn, false);
				  if (!succ || succ == b || !succ->code || succ->prefix_len == 0) break;
				  u64 sp; if (!live_exec_phys(spc, &sp) || sp != succ->phys) break;   // successor remapped since compile -> stale, don't fuse
				  bool dup = false; for (u32 j = 0; j < nb; ++j) if (blist[j] == succ) { dup = true; break; }
				  if (dup) break;
				  blist[nb++] = succ; cur = succ;
				}
				if (!TraceLoopsOnly || closes)
					m_jit->compile_trace(m_jit->trace_slot(start_virt), blist, nb, (const uint8_t*)dram_ptr, dram_size, hs);
			}
#endif   // JIT_TRACES
#ifdef JIT_VERIFY
			// Interpret the prefix (authoritative), recording each loaded value so the
			// compiled pass can replay it instead of re-reading memory. Skip the compare
			// if an interrupt/trap diverts us mid-prefix.
			u64 snap[64];   // 64: capture the PALshadow bank (r32..r63) for PALmode blocks
			memcpy(snap, state.r, sizeof(snap));
			u64 f_pre[64];  // FP file before the interp pass -- restored before the compiled pass below
			memcpy(f_pre, state.f, sizeof(f_pre));   // so the compiled FP ops read the same f[] the interp did
			// I_CTL/CM/SIRR + PCTX fields (HW_MFPR 0x40-7f reads astrr/aster/fpen/ppcen) are read-modify-written; a HW_MFPR reads, a later HW_MTPR writes,
			// so the interp pass writes before the compiled pass's HW_MFPR reads. Snapshot the read-back
			// fields here and restore them before b->code() below -- like snap/f_pre do for GPRs/FP.
			const bool ictl_sde_pre = state.sde, ictl_hwe_pre = state.hwe;
			const int  ictl_spe_pre = state.i_ctl_spe, ictl_vam_pre = state.i_ctl_va_mode;
			const u64  ictl_vptb_pre = state.i_ctl_vptb, ictl_other_pre = state.i_ctl_other;
			const int  cm_pre = state.cm, sir_pre = state.sir;   // CM/SIRR read-back (HW_MFPR CM/SIRR)
			const int  aster_pre = state.aster, astrr_pre = state.astrr;   // AST/FPEN/PPCEN read-back via the
			const int  fpen_pre = state.fpen, ppcen_pre = state.ppcen;     // PCTX group (HW_MFPR 0x40-7f)
			// EXC_SUM REG[12:8] (HRM 5.2.13) reports the trapping op's dest register -- report state,
			// not execution state, and which pass recorded it differs benignly. Compare the exception
			// bits strictly, mask the REG field.
			auto ipr_ne = [](int ii, u64 x, u64 y) {
				if (ii == 13) { x &= ~U64(0x1f00); y &= ~U64(0x1f00); }
				return x != y;
			};
			const u64  exc_sum_pre = state.exc_sum, fpcr_pre = state.fpcr; // restored before the TRACE pass:
			                                                               // boundary compares need the pre-span
			                                                               // value, not the interp's inherited final
			// HW_MTPR verify: a compiled block writes IPR fields directly in LIVE state (its GPR
			// writes go to jr scratch). cap_iprs snapshots the writable IPR set (the per-boundary
			// snapshots below and the post-pass compares both use it); put_iprs restores it. Mostly
			// pure stores; I_CTL is read-modify-written, so it's also reset pre-pass (ictl_*_pre,
			// above). Keep this list in sync with jit_hw_mtpr.
			auto cap_iprs = [&](u64* d) {
				d[0] = state.last_tb_virt; d[1] = last_dtb_virt[0]; d[2] = last_dtb_virt[1];
				d[3] = state.pctr_ctl; d[4] = state.dc_ctl; d[5] = (u64)state.cc_offset; d[6] = (u64)(u32)state.alt_cm;
				// IER enables; check_int not snapshotted (rolling it back could suppress a poll)
				d[7] = (u64)(u32)state.asten; d[8] = (u64)(u32)state.sien; d[9] = (u64)(u32)state.pcen;
				d[10] = (u64)(u32)state.cren; d[11] = (u64)(u32)state.slen; d[12] = (u64)(u32)state.eien;
				d[13] = state.exc_sum;   // FPSTART clears it (compiled ITOFx/FTOIx/FLTL)
				d[14] = state.fpcr;      // MT_FPCR (compiled FLTL) writes it
				d[15] = (u64)state.sde; d[16] = (u64)state.hwe;            // I_CTL (compiled as terminator)
				d[17] = (u64)(u32)state.i_ctl_spe; d[18] = (u64)(u32)state.i_ctl_va_mode;
				d[19] = state.i_ctl_vptb; d[20] = state.i_ctl_other;
				d[21] = (u64)(u32)state.cm;    d[22] = (u64)(u32)state.sir;     // CM, SIRR
				d[23] = (u64)(u32)state.aster; d[24] = (u64)(u32)state.astrr;   // 0x40-group ASTER/ASTRR
				d[25] = (u64)(u32)state.fpen;  d[26] = (u64)(u32)state.ppcen;   // 0x40-group FPEN/PPCEN
				};
			auto put_iprs = [&](const u64* s) {
				state.last_tb_virt = s[0]; last_dtb_virt[0] = s[1]; last_dtb_virt[1] = s[2];
				state.pctr_ctl = s[3]; state.dc_ctl = s[4]; state.cc_offset = (u32)s[5]; state.alt_cm = (int)s[6];
				state.asten = (int)s[7]; state.sien = (int)s[8]; state.pcen = (int)s[9];
				state.cren = (int)s[10]; state.slen = (int)s[11]; state.eien = (int)s[12];
				state.exc_sum = s[13];
				state.fpcr = s[14];
				state.sde = (bool)s[15]; state.hwe = (bool)s[16];
				state.i_ctl_spe = (int)s[17]; state.i_ctl_va_mode = (int)s[18];
				state.i_ctl_vptb = s[19]; state.i_ctl_other = s[20];
				state.cm = (int)s[21]; state.sir = (int)s[22];
				flush_data_page_cache();
				state.aster = (int)s[23]; state.astrr = (int)s[24];
				state.fpen = (int)s[25]; state.ppcen = (int)s[26];
				};
			// Side-exit-shaped verify: snapshot the interp reference at EVERY fused-block boundary it 
			// crosses, so a compiled trace/region that legitimately exits early is compared AT ITS EXIT
			// instead of being flagged as mismatch. Today's verify traces have no interior gates, so the
			// early-exit compare is dormant scaffolding the region will light up.
			u32 n_bnd = 0;
			u32 bnd_cnt[CJitEngine::kMaxTraceSegs];       // cumulative interp instr count at boundary
			u32 bnd_vn[CJitEngine::kMaxTraceSegs];        // interp load-log cursor at boundary
			u32 bnd_sn[CJitEngine::kMaxTraceSegs];        // interp store-log cursor at boundary
			u64 bnd_pc[CJitEngine::kMaxTraceSegs];        // interp pc at boundary (the resume PC)
			u64 bnd_r[CJitEngine::kMaxTraceSegs][64];     // GPRs incl. PALshadow bank
			u64 bnd_f[CJitEngine::kMaxTraceSegs][64];     // FP file
			u64 bnd_ipr[CJitEngine::kMaxTraceSegs][27];   // cap_iprs set
			const u32* vw = (const u32*)((const u8*)dram_ptr + b->phys);
			u32 verify_words[16] = {};   // actual reference instructions for mismatch diagnostics
			u32 vn = 0;   // loads recorded for replay
			u32 sn = 0;   // stores recorded for the compiled-pass compare
			u64 vpc = start_virt;
			u32 cur_len = b->prefix_len;   // current interpreted block's length + start tag; step 3
			u64 cur_tag = b->tag;          // advances these per segment across a fused trace
				u32 n_interp = 0;              // ops the interp ran over the trace's fused span (vs t->code's done)
			bool clean = true;
			CJitEngine::TraceFragment* vtr = m_jit->traces_enabled() ? m_jit->trace_lookup(start_virt, start_asn) : nullptr;
				if (vtr && !m_jit->trace_ok(vtr, start_phys, (const uint8_t*)dram_ptr)) vtr = nullptr;   // stale -> verify as a plain block
				const u32 nseg = vtr ? vtr->n_segs : 1;   // interp the trace's full fused span; else just block b
				for (u32 seg = 0; seg < nseg && clean; ++seg) {
				if (vtr) { cur_len = vtr->segs[seg].n_instr; cur_tag = vtr->segs[seg].guest_pc;
				           vw = (const u32*)((const u8*)dram_ptr + vtr->segs[seg].phys_pc); vpc = cur_tag; }
				for (u32 k = 0; k < cur_len; ++k)
			{
				// Compute the load's effective address from the live registers BEFORE
				// executing it (Rb may be the load's own dest), to compare against the JIT.
				// SRM copies over this code while its original instructions remain in the
				// icache until IMB. Classify the word execute() will fetch, not the changed
				// DRAM word, or a cached HW_ST can disappear from the reference store log.
				// This is a read-only peek: a miss is still fetched/translated by execute().
				u32 ins = vw[k];
				if (state.pc == seq_next_pc && seq_remaining > 0)
					ins = endian_32(seq_line_ptr[seq_offset]);
				else if (icache_enabled)
				{
					const int li = (int)((state.pc >> 11) & (ICACHE_ENTRIES - 1));
					const auto& line = state.icache[li];
					if (line.valid && (line.asn == state.asn || line.asm_bit)
						&& line.address == (state.pc & ICACHE_MATCH_MASK))
						ins = endian_32(line.data[(state.pc >> 2) & ICACHE_INDEX_MASK]);
				}
				if (n_interp + k < 16) verify_words[n_interp + k] = ins;
				const u32 opc = ins >> 26;
				const int lra = (ins >> 21) & 0x1F;
				// HW_LD physical (0x1b, func 0/1) is a load too: the compiled form replays through
				// this same vlog, but its address is physical (untranslated) with a 12-bit disp.
				// Virtual forms too, logged va is jit_read_vpte's replay key.
				const u32 hwld_func = (ins >> 12) & 0xf;
				const bool is_hwld = (opc == 0x1b) &&
					(hwld_func <= 1 || hwld_func == 4 || hwld_func == 5 || hwld_func >= 8);
				// RPCC/RC/RS (MISC 0x18) and family-marked IPRs read CPU state the verify can't
				// re-derive; the compiled forms pull their value from this same load log (jit_misc /
				// jit_hw_mfpr replay it), so log them like loads.
				const u32  miscfn = (ins & 0xFFFF);
				// Compiled misc reads: RC/RS (0xE000/0xF000) for all Ra incl. 31 (the flag-only side-effect
				// forms compile too); RPCC (0xC000) only when it has a GPR dest (Ra!=31).
				const bool is_miscrd = (opc == 0x18) && (miscfn == 0xE000 || miscfn == 0xF000 || (miscfn == 0xC000 && lra != 31));
				const auto ipr_read = (opc == 0x19)
					? m_profile.ipr_read.describe((ins >> 8) & 0xff) : AlphaIPRReadDescriptor{};
				const bool is_ipr_replay = (opc == 0x19) && ipr_read.replay
					&& (lra != 31 || !ipr_read.discard_safe);
				const bool is_fpld = (opc == 0x22 || opc == 0x23 || opc == 0x20 || opc == 0x21);   // LDS/LDT/LDF/LDG: dest is f[lra]
				// Ordinary loads log only with a destination. Stateful misc/IPR reads log
				// even to R31 when the compiled helper must consume a replay slot.
				const bool isld = ((opc == 0x28 || opc == 0x29 || opc == 0x0a || opc == 0x0c
					|| opc == 0x2a || opc == 0x2b || opc == 0x0b || is_hwld || is_fpld) && lra != 31)
					|| is_miscrd || is_ipr_replay;  // +LDBU/LDWU +LDx_L +LDQ_U +RPCC/RC/RS +ISUM +LDS/LDT
				u64 eva = 0;
				if (isld && !is_miscrd && !is_ipr_replay)   // misc/ISUM reads have no effective address -- only a logged value
				{
					const int lrb = (ins >> 16) & 0x1F;
					const int ldisp = is_hwld ? (int)((int32_t)(ins << 20) >> 20)   // HW_LD: 12-bit
						: (int)(int16_t)(ins & 0xFFFF);        // LDx:   16-bit
					eva = (lrb == 31 ? (u64)0 : state.r[RREG(lrb)]) + (u64)ldisp;
					if (opc == 0x0b) eva &= ~U64(7);   // LDQ_U: address forced to 8-byte alignment
				}
				// Stores touch memory, not GPRs, so record (addr,value) for the compiled-pass
				// compare. Ra (lra) is the value source; Rb is the base. HW_ST physical (0x1f func
				// 0/1) stores too, with a physical (untranslated) address and a 12-bit disp.
				const bool is_sc = (opc == 0x2e || opc == 0x2f);   // STL_C/STQ_C: store-conditional (success in Ra)
				const bool is_hwst = (opc == 0x1f) && (((ins >> 12) & 0xf) <= 1);
				const bool is_fpst = (opc == 0x26 || opc == 0x27 || opc == 0x24 || opc == 0x25);   // STS/STT/STF/STG: value source is f[lra]
				const bool isst = (opc == 0x2c || opc == 0x2d || opc == 0x0d || opc == 0x0e || opc == 0x0f || is_sc || is_hwst || is_fpst);  // +STx_C +STQ_U +STS/STT
				u64 sva = 0, sval = 0;
				if (isst)
				{
					const int srb = (ins >> 16) & 0x1F;
					const int sdisp = is_hwst ? (int)((int32_t)(ins << 20) >> 20)   // HW_ST: 12-bit
						: (int)(int16_t)(ins & 0xFFFF);        // STx:   16-bit
					sva = (srb == 31 ? (u64)0 : state.r[RREG(srb)]) + (u64)sdisp;
					if (opc == 0x0f) sva &= ~U64(7);   // STQ_U: address forced to 8-byte alignment
					if (is_fpst) sval = (opc == 0x27) ? state.f[lra] : (opc == 0x26) ? (u64)ieee_sts(state.f[lra])
						: (opc == 0x24) ? (u64)vax_stf(state.f[lra]) : vax_stg(state.f[lra]);   // STT/STS/STF/STG
					else         sval = (lra == 31 ? (u64)0 : state.r[RREG(lra)]);
				}
				// Computed jump (JMP/JSR/RET): target = Rb & ~3, taken before execute() (the
				// jump's target uses the old Rb, even if Ra==Rb gets the return address after).
				u64 jtgt = 0;
				if (opc == 0x1a || opc == 0x1e)   // JMP/JSR/RET (Rb & ~3) or HW_RET/HWREI (Rb & ~2)
				{
					const int jrb = (ins >> 16) & 0x1F;
					const u64 jmask = (opc == 0x1e) ? ~U64(2) : ~U64(3);
					jtgt = (jrb == 31 ? (u64)0 : state.r[RREG(jrb)]) & jmask;
					if (opc == 0x1a)
						jtgt |= cur_tag & 3;   // DO_JMP: mode bits come from the current pc
				}
				execute();
				--budget;
				vpc += 4;
				if (state.pc != vpc) {
					// The terminator (a compiled branch at the last index) diverges to its
					// target -- expected. But execute() services an async interrupt/trap at the
					// TOP, before the instruction runs, so the interpreter can divert to a PAL
					// handler at the terminator WITHOUT executing the branch. Accept the
					// divergence only when state.pc is the branch's actual target; otherwise it's
					// an interrupt/trap and the compiled block (which doesn't model interrupts --
					// the dispatcher's !check_int guard handles that) legitimately differs, so
					// skip the compare instead of flagging a false mismatch.
					bool ok_branch = false;
					if (k == cur_len - 1 &&
						(opc == 0x30 || opc == 0x34 || (opc >= 0x38 && opc <= 0x3f)))
					{
						const int64_t bdisp = (int64_t)((uint64_t)(ins & 0x1FFFFF) << 43) >> 43;
						const u64 tgt = vpc + (u64)(bdisp * 4);   // vpc == branch_pc + 4 (fall-through)
						ok_branch = (state.pc == tgt);
					}
					else if (k == cur_len - 1 && (opc == 0x1a || opc == 0x1e))
					{
						ok_branch = (state.pc == jtgt);   // computed jump (JMP/HW_RET) reached its register target
					}
					else if (k == cur_len - 1 && opc == 0x00)
					{
						// CALL_PAL vectored to its PALcode entry (pal_base | offset); the kernel-mode
						// path never traps, but accept the OPCDEC vector too.
						const u32 func = ins & 0x1FFFFFFF;
						const u64 voff = m_profile.pal.call_pal_entry(func, vpc - 4, vpc).offset;
						ok_branch = (state.pc == (state.pal_base | voff)) || (state.pc == (state.pal_base | pal_exception_offset(AlphaPALException::OpcodeDecode) | U64(1)));
					}
					if (!ok_branch)
						clean = false;
					break;
				}
				if (isld && vn < kJitVlogCap)
				{
					m_jit_vaddr[vn] = eva;
					m_jit_vlog[vn] = is_fpld ? state.f[lra] : state.r[RREG(lra)];   // FP dest -> f[]; shadow-aware GPR otherwise
					vn++;
				}
				if (isst && sn < kJitVlogCap)
				{
					m_jit_slog_addr[sn] = sva;
					m_jit_slog_val[sn] = sval;
					m_jit_slog_success[sn] = is_sc ? state.r[RREG(lra)] : (u64)1;   // STx_C result (post-execute); ordinary store = 1
					sn++;
				}
			}
			if (!clean) break;          // fault/divergence in this segment -> stop (compare skipped)
				n_interp += cur_len;        // this segment ran fully
				if (vtr && n_bnd < CJitEngine::kMaxTraceSegs) {
					// Side-exit-shaped verify: capture the reference AT this fused-block boundary.
					bnd_cnt[n_bnd] = n_interp; bnd_vn[n_bnd] = vn; bnd_sn[n_bnd] = sn; bnd_pc[n_bnd] = state.pc;
					memcpy(bnd_r[n_bnd], state.r, sizeof(bnd_r[0]));
					memcpy(bnd_f[n_bnd], state.f, sizeof(bnd_f[0]));
					cap_iprs(bnd_ipr[n_bnd]);
					n_bnd++;
				}
				if (vtr && seg + 1 < nseg && state.pc != vtr->segs[seg + 1].guest_pc) break;   // path left the fused trace -> side-exit
				}   // end per-segment interp loop
				if (clean)
			{
				// (cap_iprs/put_iprs are defined above the interp reference pass -- the per-boundary
				// side-exit snapshots need them there.)
				u64 ipr_interp[27];
				cap_iprs(ipr_interp);
				// FP file: compiled ITOFx writes state.f live (like the IPR writes); snapshot the
				// interp pass's result, compare after the compiled pass, then restore.
				u64 f_interp[64];
				memcpy(f_interp, state.f, sizeof(f_interp));
				u64 jr[64];   // 64: a compiled PALmode block may touch the shadow bank
				memcpy(jr, snap, sizeof(jr));
				memcpy(state.f, f_pre, sizeof(f_pre));   // restore the FP file like the GPRs: the compiled pass reads pre-interp f
				state.sde = ictl_sde_pre; state.hwe = ictl_hwe_pre;   // restore I_CTL too: a compiled HW_MFPR must read the pre-interp value
				state.i_ctl_spe = ictl_spe_pre; state.i_ctl_va_mode = ictl_vam_pre;
				state.i_ctl_vptb = ictl_vptb_pre; state.i_ctl_other = ictl_other_pre;
				state.cm = cm_pre; state.sir = sir_pre;                      // ...and CM/SIRR
				flush_data_page_cache();
				state.aster = aster_pre; state.astrr = astrr_pre;            // ...and the PCTX read-backs (a
				state.fpen = fpen_pre; state.ppcen = ppcen_pre;              // HW_MFPR PCTX reads these live)
				const u64 interp_pc = state.pc;   // interpreter is authoritative for the PC
					const u32 n_stores_interp = sn;   // interp's RECORDED store count (sn), NOT the replay cursor m_jit_slog_i; a compiled pass must consume exactly this many (catches a missing/extra store)
				m_jit_vreplay = true;
				m_jit_vmismatch = false;
				m_jit_vlog_i = 0;
				m_jit_vlog_n = vn;
				m_jit_slog_i = 0;
				m_jit_slog_n = sn;
				dpc_sync_bank();
				const u32 done = m_jit->run_block(this, jr, b);   // also writes state.pc (the JIT's next PC)
				m_jit_vreplay = false;
				if (!vtr && done == b->prefix_len)
				{
					if (state.pc != interp_pc) {
						m_jit_vmismatch = true;
						// Dump the JIT's source (DRAM at b->phys, vw[]) vs the icache (what the
						// interpreter actually fetches), word by word. If the middle words differ
						// the icache holds a stale/different version the JIT never compiled.
						const int icl = (int)((start_virt >> 11) & (ICACHE_ENTRIES - 1));
						const u32 wb = (u32)((start_virt >> 2) & ICACHE_INDEX_MASK);
						printf("[JIT][VERIFY] PC MISMATCH at %016llx: interp=%016llx jit=%016llx plen=%u\n",
							(unsigned long long) start_virt, (unsigned long long) interp_pc,
							(unsigned long long) state.pc, b->prefix_len);
						printf("   dram:");
						for (u32 w = 0; w < b->prefix_len && w < 16; ++w) printf(" %08x", vw[w]);
						printf("\n   icch:");
						for (u32 w = 0; w < b->prefix_len && w < 16; ++w)
							printf(" %08x", endian_32(state.icache[icl].data[(wb + w) & ICACHE_INDEX_MASK]));
						printf("\n   r1 i=%016llx j=%016llx  r2 i=%016llx j=%016llx  r5 snap=%016llx\n",
							(unsigned long long) state.r[1], (unsigned long long) jr[1],
							(unsigned long long) state.r[2], (unsigned long long) jr[2],
							(unsigned long long) snap[5]);
					}
					u64 ipr_jit[27];
					cap_iprs(ipr_jit);   // compiled pass wrote IPRs into live state; check vs interp
					for (int ii = 0; ii < 27; ii++) if (ipr_ne(ii, ipr_jit[ii], ipr_interp[ii]))
					{
						m_jit_vmismatch = true;
						printf("[JIT][VERIFY] IPR MISMATCH at %016llx slot %d: interp=%016llx jit=%016llx\n",
							(unsigned long long) start_virt, ii,
							(unsigned long long) ipr_interp[ii], (unsigned long long) ipr_jit[ii]);
					}
					for (int fi = 0; fi < 64; fi++) if (state.f[fi] != f_interp[fi])
					{
						m_jit_vmismatch = true;
						printf("[JIT][VERIFY] FP MISMATCH at %016llx f%d: interp=%016llx jit=%016llx\n",
							(unsigned long long) start_virt, fi,
							(unsigned long long) f_interp[fi], (unsigned long long) state.f[fi]);
					}
					if (m_jit_vlog_i != vn) {
						m_jit_vmismatch = true;
						printf("[JIT][VERIFY] LOAD COUNT MISMATCH at %016llx: interp=%u jit=%u\n", (unsigned long long) start_virt, vn, m_jit_vlog_i);
					}
					if (m_jit_slog_i != n_stores_interp) {
						m_jit_vmismatch = true;
						printf("[JIT][VERIFY] STORE COUNT MISMATCH at %016llx: interp=%u jit=%u\n", (unsigned long long) start_virt, n_stores_interp, m_jit_slog_i);
					}
						cc_last_sync += std::chrono::nanoseconds(m_jit->verify_compare(start_virt, state.r, jr, verify_words, b->prefix_len, m_jit_vmismatch) + g_diag_excluded_ns);   // don't bill the verify progress-print OR the PCI decode-off diag stall to the RPCC
						g_diag_excluded_ns = 0;   // consumed here in the verify path so jit_run's later sync doesn't double-count it
				}
				else if (m_jit_vmismatch)
				{
					// A deferred op (or a longer trace reference) prevents a GPR compare,
					// but any earlier replay mismatch still belongs to this compiled pass.
					cc_last_sync += std::chrono::nanoseconds(m_jit->verify_failed());
				}
				// verify extension: the production trace-run hook is gated off under JIT_VERIFY, so
				// exercise the trace HERE; re-restore pre-interp state, run t->code on a 2nd scratch
				// (replaying the same logged loads), then compare to the authoritative interp. This makes
				// the trace's own state.pc write + frame visible to the differential harness.
				if (m_jit->traces_enabled()) {
					CJitEngine::TraceFragment* tr = m_jit->trace_lookup(start_virt, start_asn);
					if (tr && m_jit->trace_ok(tr, start_phys, (const uint8_t*)dram_ptr)
							&& [&]{ for (uint32_t s = 1; s < tr->n_segs; ++s) { u64 sp; if (!live_exec_phys(tr->segs[s].guest_pc, &sp) || sp != tr->segs[s].phys_pc) return false; } return true; }()) {   // verify now mirrors the production hook's interior live-phys check
						memcpy(state.f, f_pre, sizeof(f_pre));
						state.sde = ictl_sde_pre; state.hwe = ictl_hwe_pre;
						state.i_ctl_spe = ictl_spe_pre; state.i_ctl_va_mode = ictl_vam_pre;
						state.i_ctl_vptb = ictl_vptb_pre; state.i_ctl_other = ictl_other_pre;
						state.cm = cm_pre; state.sir = sir_pre;
						flush_data_page_cache();
						state.aster = aster_pre; state.astrr = astrr_pre;
						state.fpen = fpen_pre; state.ppcen = ppcen_pre;
						state.exc_sum = exc_sum_pre; state.fpcr = fpcr_pre;   // else the trace inherits the
						                                                      // interp's FINAL values -> false
						                                                      // mismatch at interior boundaries
						u64 jr_t[64];
						memcpy(jr_t, snap, sizeof(jr_t));
						m_jit_vreplay = true; m_jit_vmismatch = false; m_jit_vlog_i = 0; m_jit_slog_i = 0;
						m_jit_vlog_n = vn; m_jit_slog_n = sn;
						const u32 done_t = ((CJitEngine::JitFn)tr->code)(this, jr_t);
						m_jit_vreplay = false;
							if (done_t == n_interp) {
							if (state.pc != interp_pc)
							{
								m_jit_vmismatch = true;
								printf("[JIT][VERIFY] TRACE PC MISMATCH at %016llx: interp=%016llx trace=%016llx (n=%u)\n",
									(unsigned long long) start_virt, (unsigned long long) interp_pc,
									(unsigned long long) state.pc, n_interp);
							}
							u64 ipr_jit_t[27]; cap_iprs(ipr_jit_t);   // trace wrote IPRs into live state; check vs interp (parity with the block path)
							for (int ii = 0; ii < 27; ii++) if (ipr_ne(ii, ipr_jit_t[ii], ipr_interp[ii])) {
								m_jit_vmismatch = true;
								printf("[JIT][VERIFY] TRACE IPR MISMATCH at %016llx slot %d: interp=%016llx trace=%016llx\n", (unsigned long long) start_virt, ii, (unsigned long long) ipr_interp[ii], (unsigned long long) ipr_jit_t[ii]);
							}
							for (int fi = 0; fi < 64; fi++) if (state.f[fi] != f_interp[fi]) {
								m_jit_vmismatch = true;
								printf("[JIT][VERIFY] TRACE FP MISMATCH at %016llx f%d: interp=%016llx trace=%016llx\n", (unsigned long long) start_virt, fi, (unsigned long long) f_interp[fi], (unsigned long long) state.f[fi]);
							}
							if (m_jit_vlog_i != vn) {
								m_jit_vmismatch = true;
								printf("[JIT][VERIFY] TRACE LOAD COUNT MISMATCH at %016llx: interp=%u trace=%u\n", (unsigned long long) start_virt, vn, m_jit_vlog_i);
							}
							if (m_jit_slog_i != n_stores_interp) {
								m_jit_vmismatch = true;
								printf("[JIT][VERIFY] TRACE STORE COUNT MISMATCH at %016llx: interp=%u trace=%u\n", (unsigned long long) start_virt, n_stores_interp, m_jit_slog_i);
							}
							cc_last_sync += std::chrono::nanoseconds(m_jit->verify_compare(start_virt, state.r, jr_t, verify_words, n_interp, m_jit_vmismatch));
							m_jit->note_trace_verify(0);   // full-span compare
						} else {
							// Early exit: legitimate if the trace stopped exactly at a fused-block
							// boundary the reference pass also crossed
							int bi = -1;
							for (u32 q = 0; q + 1 < n_bnd; ++q) if (bnd_cnt[q] == done_t) { bi = (int)q; break; }
							if (bi < 0) {
								// Mid-block early bail: a deferred op  stops compiled execution AT that op 
								// the BLOCK path silently skips its compare in exactly this case (done < prefix_len). 
								// Identify it by the bail PC: the fault/bail path wrote state.pc = the op's own 
								// address along the fused path. 
								u64 bail_pc = 0; u32 cum = 0;
								for (u32 s2 = 0; s2 < tr->n_segs; ++s2) {
									if (done_t < cum + tr->segs[s2].n_instr) { bail_pc = tr->segs[s2].guest_pc + 4 * (u64)(done_t - cum); break; }
									cum += tr->segs[s2].n_instr;
								}
								if (bail_pc && state.pc == bail_pc) {
									if (m_jit_vmismatch)
										cc_last_sync += std::chrono::nanoseconds(m_jit->verify_failed());
									m_jit->note_trace_verify(3);   // deferred-op bail: compare skipped (block-path parity)
								} else {
									m_jit_vmismatch = true;
									static int n_tcm = 0;   // rate-limited: a real count divergence repeats -- 25 samples suffice
									if (n_tcm++ < 25)
										printf("[JIT][VERIFY] TRACE COUNT MISMATCH at %016llx: interp_span=%u trace_done=%u pc=%016llx\n",
											(unsigned long long) start_virt, n_interp, done_t, (unsigned long long) state.pc);
									m_jit->note_trace_verify(2);   // true divergence: no boundary or bail-pc matches
									cc_last_sync += std::chrono::nanoseconds(m_jit->verify_failed());
								}
							} else {
								if (state.pc != bnd_pc[bi])
								{
									m_jit_vmismatch = true;
									printf("[JIT][VERIFY] TRACE SIDE-EXIT PC MISMATCH at %016llx: boundary=%016llx trace=%016llx (n=%u)\n",
										(unsigned long long) start_virt, (unsigned long long) bnd_pc[bi],
										(unsigned long long) state.pc, done_t);
								}
								u64 ipr_jit_t[27]; cap_iprs(ipr_jit_t);
								// Slots 0-2 (last TB fills) are replay-invisible (verify loads skip translation),
								// so they're only sound at full-span compares (by inheritance) -- skip here.
								for (int ii = 3; ii < 27; ii++)
									if (ipr_ne(ii, ipr_jit_t[ii], bnd_ipr[bi][ii])) {
										m_jit_vmismatch = true;
										printf("[JIT][VERIFY] TRACE SIDE-EXIT IPR MISMATCH at %016llx slot %d: interp=%016llx trace=%016llx\n", (unsigned long long) start_virt, ii, (unsigned long long) bnd_ipr[bi][ii], (unsigned long long) ipr_jit_t[ii]);
									}
								for (int fi = 0; fi < 64; fi++) if (state.f[fi] != bnd_f[bi][fi]) {
									m_jit_vmismatch = true;
									printf("[JIT][VERIFY] TRACE SIDE-EXIT FP MISMATCH at %016llx f%d: interp=%016llx trace=%016llx\n", (unsigned long long) start_virt, fi, (unsigned long long) bnd_f[bi][fi], (unsigned long long) state.f[fi]);
								}
								if (m_jit_vlog_i != bnd_vn[bi]) {
									m_jit_vmismatch = true;
									printf("[JIT][VERIFY] TRACE SIDE-EXIT LOAD COUNT MISMATCH at %016llx: interp=%u trace=%u\n", (unsigned long long) start_virt, bnd_vn[bi], m_jit_vlog_i);
								}
								if (m_jit_slog_i != bnd_sn[bi]) {
									m_jit_vmismatch = true;
									printf("[JIT][VERIFY] TRACE SIDE-EXIT STORE COUNT MISMATCH at %016llx: interp=%u trace=%u\n", (unsigned long long) start_virt, bnd_sn[bi], m_jit_slog_i);
								}
								cc_last_sync += std::chrono::nanoseconds(m_jit->verify_compare(start_virt, bnd_r[bi], jr_t, verify_words, done_t, m_jit_vmismatch));
								m_jit->note_trace_verify(1);   // legitimate boundary side-exit
							}
						}
					}
				}
				state.pc = interp_pc;   // restore; the block's PC write was only for the check
				put_iprs(ipr_interp);   // roll back the compiled pass's live IPR writes (verify-only)
				memcpy(state.f, f_interp, sizeof(f_interp));   // ...and its FP writes
				break_seq_icache();     // compiled pass + raw pc restore bypassed set_pc
			}
			continue;
#else
			// A predecessor block's epilogue cache-missed and asked to be linked here. Now 
			// that we know this block is live and compiled (and its vgen was just refreshed above), 
			// snapshot it into the source's successor slots so its exit jumps straight in instead of 
			// returning.
			// 
			// Tag-keyed: an existing entry for this target MUST be refreshed in place.
			if (m_link_from) { m_jit->note_link_bail();
				m_jit->patch_edge(m_link_from, b->tag,
					b->vgen | (b->pal_shadow ? (U64(1) << 63) : 0), b->jit_body, b->asm_global);
				m_link_from = nullptr; }
			m_jit_budget = budget;   // ceiling for compiled chains (epilogue stops at it)
#ifdef JIT_STATS
			const uint64_t _comp_t0 = jit_rdtsc();
#endif
			dpc_sync_bank();
			const u32 done = m_jit->run_block(this, &state.r[0], b);
#ifdef JIT_STATS
			const uint64_t _comp_tsc = jit_rdtsc() - _comp_t0;   // host cycles in this compiled chain
#endif
			state.r[31] = 0;
			break_seq_icache();   // compiled block wrote pc natively (no set_pc); drop the stale cursor
			// state.pc is written by the compiled block itself (next PC, or the bail PC).
			// Account for the compiled ops: instruction count (+ cc_large for the legacy speed
			// calibration). state.cc (RPCC) is no longer advanced per-instruction.
			// wall-clock * cpu_hz at the jit_run boundary above, so a hot compiled loop can't
			// run the cycle counter ahead of real time
			state.instruction_count += done;
			cc_large += (u64)done * cc_per_instruction;
			budget -= done;
#ifdef JIT_STATS
			{
				const uint64_t _ns = m_jit->note_exec(done, 0, _comp_tsc, 0);
				cc_last_sync += std::chrono::nanoseconds(_ns);   // don't bill the stats-print stall to the wall-clock RPCC
				if (_ns)
				{
					printf("[JIT][STATS][CPU%d] int-kicks: eir %llu | sir %llu | ast %llu | irq_h %llu | timer %llu | delivered %llu | masked-at-poll %llu\n",
						(int)state.iProcNum, (unsigned long long)m_stat_kick[0], (unsigned long long)m_stat_kick[1], (unsigned long long)m_stat_kick[2],
						(unsigned long long)m_stat_kick[3], (unsigned long long)m_stat_kick[4], (unsigned long long)m_stat_kick[5], (unsigned long long)m_stat_kick[6]);
					memset(m_stat_kick, 0, sizeof(m_stat_kick));
					printf("[JIT][STATS][CPU%d] tb: dtbm-single %llu | double %llu | itb-miss %llu | dfault %llu | tbia %llu | tbiap %llu | tbis_d %llu | fills %llu | hint-invalid %llu | hint-other %llu\n",
						(int)state.iProcNum, (unsigned long long)m_stat_tb[0], (unsigned long long)m_stat_tb[1], (unsigned long long)m_stat_tb[2], (unsigned long long)m_stat_tb[3],
						(unsigned long long)m_stat_tb[4], (unsigned long long)m_stat_tb[5], (unsigned long long)m_stat_tb[6], (unsigned long long)m_stat_tb[7],
						(unsigned long long)m_stat_tb[8], (unsigned long long)m_stat_tb[9]);
					for (int k = 0; k < 6; k++)
					{
						int best = -1;
						for (int sl = 0; sl < 128; sl++) if (m_stat_va[sl][1] && (best < 0 || m_stat_va[sl][1] > m_stat_va[best][1])) best = sl;
						if (best < 0) break;
						printf("[JIT][STATS][CPU%d]   miss-page %016llx x%llu\n", (int)state.iProcNum, (unsigned long long)(m_stat_va[best][0] << 13), (unsigned long long)m_stat_va[best][1]);
						m_stat_va[best][1] = 0;
					}
					memset(m_stat_va, 0, sizeof(m_stat_va)); memset(m_stat_tb, 0, sizeof(m_stat_tb));
				}
#if defined(JIT_REGPROF) && defined(_WIN32)
				if (_ns && m_samples) jit_sampler_report();
#endif
			}
#endif
			if (done > 0) continue;   // progress made; done==0 (faulting first insn) falls through
#endif
		}

		// Miss path (cold): the up-front translation gave start_phys/start_asm (when have_phys).
		// We're not running a compiled block here, so drop any pending link request, interpret
		// the block, record it, and compile its prefix. Count the link-miss bail before dropping it.
		if (m_link_from) m_jit->note_link_bail();
		m_link_from = nullptr;
#ifdef JIT_STATS
		// Compiled with no code: the first op is a runtime breaker.
		if (have_phys && b && b->compiled && !b->code && start_phys + 4 <= dram_size)
			m_jit->note_interp_breaker(*(const u32*)((const u8*)dram_ptr + start_phys));
#endif
		u32 n = 0;
		u64 expected = start_virt;
#ifdef JIT_STATS
		const uint64_t _interp_t0 = jit_rdtsc();
#endif
		while (budget > 0)
		{
			execute();
			--budget;
			++n;
			expected += 4;
			if (state.pc != expected)
				break;
		}
#ifdef JIT_STATS
		cc_last_sync += std::chrono::nanoseconds(m_jit->note_exec(0, n, 0, jit_rdtsc() - _interp_t0));   // don't bill the stats-print stall to the wall-clock RPCC
#endif
		// Stale icache line for this span
		bool src_stale = false;
		if (icache_enabled && have_phys)
		{
			const u64 vs = start_virt & ~U64(3);
			const u64 ve = vs + 4 * (u64)n;
			for (u64 v = vs & ~U64(0x7ff); v < ve; v += 0x800)
			{
				const int li = (int)((v >> 11) & (ICACHE_ENTRIES - 1));
				const auto& L = state.icache[li];
				if (L.valid && (L.asn == state.asn || L.asm_bit)
					&& L.address == ((v | (start_virt & 1)) & ICACHE_MATCH_MASK))
				{
					const u64 lo = (v > vs) ? v : vs;
					const u64 hi = (v + 0x800 < ve) ? v + 0x800 : ve;
					const u64 poff = L.p_address + (lo - v);
					if (poff + (hi - lo) <= dram_size
						&& memcmp((const uint8_t*)dram_ptr + poff,
							(const uint8_t*)L.data + (lo - v), (size_t)(hi - lo)))
					{
						src_stale = true;
						break;
					}
				}
			}
		}

		// Record only translatable block starts (a translation miss left have_phys false).
		if (have_phys && !src_stale && state.pc != expected)
		{
			CJitEngine::JitBlock* nb = m_jit->record(start_virt, start_phys, start_asn,
				start_pal_shadow, start_asm, n, (const uint8_t*)dram_ptr);
			if (!nb->compiled)
				m_jit->compile_block(nb, (const uint8_t*)dram_ptr, dram_size,
					(void*)&CAlphaCPU::jit_read, (void*)&CAlphaCPU::jit_write,
					(void*)&CAlphaCPU::jit_opcdec, (void*)&CAlphaCPU::jit_hw_mfpr,
					(void*)&CAlphaCPU::jit_read_phys, (void*)&CAlphaCPU::jit_hw_mtpr,
					(void*)&CAlphaCPU::jit_write_phys, (void*)&CAlphaCPU::jit_indirect,
					(void*)&CAlphaCPU::jit_read_locked, (void*)&CAlphaCPU::jit_stc,
					(void*)&CAlphaCPU::jit_misc, (void*)&CAlphaCPU::jit_read_vpte, (void*)&CAlphaCPU::jit_read_wchk,
					(void*)&CAlphaCPU::jit_itof, (void*)&CAlphaCPU::jit_ftoi,
					(void*)&CAlphaCPU::jit_fltl,
					(void*)&CAlphaCPU::jit_fp_read, (void*)&CAlphaCPU::jit_fp_write,
					(void*)&CAlphaCPU::jit_fltv);
		}
	}
}

// A diverging replay may issue more memory operations than the reference. Keep
// counting them, but never read stale or out-of-bounds log slots. The pass-level
// count comparison supplies the diagnostic and aggregates this failure once.
bool CAlphaCPU::jit_verify_load_slot(u32& slot)
{
	slot = m_jit_vlog_i++;
	if (slot < m_jit_vlog_n && slot < kJitVlogCap) return true;
	m_jit_vmismatch = true;
	return false;
}

bool CAlphaCPU::jit_verify_store_slot(u32& slot)
{
	slot = m_jit_slog_i++;
	if (slot < m_jit_slog_n && slot < kJitVlogCap) return true;
	m_jit_vmismatch = true;
	return false;
}

// The emitted slow path installs the faulting current_pc; its exit commits earlier
// guest registers before PAL dispatch. Match DATA_PHYS before any memory side effect.
int CAlphaCPU::jit_unalign(u64 va, u32 ins, int flags, int align)
{
	if (!ins || m_jit_vreplay) return 1;
	state.fault_va = va;
	state.va_form_va = va;
	state.exc_sum = (u64)I_GETRA(ins) << 8;
	state.mm_stat = (I_GETOP(ins) << 4) | ((flags & ACCESS_WRITE) ? 1 : 0);
	TRACE_UNALIGN(flags, align);
	GO_PAL(AlphaPALException::Unaligned);
	return 2;
}

// unaligned_fixup: perform a misaligned load/store in place. Every page the access touches is
// translated (and may fault) before any byte moves; MMIO bails to the interpreter.
int CAlphaCPU::jit_unaligned_rw(CAlphaCPU* cpu, u64 va, int size_bits, u32 ins, int rw, u64* data)
{
	if (!ins || cpu->m_jit_vreplay) return 1;
	const int n = size_bits / 8;
	const u64 va_last = va + n - 1;
	const int n1 = (int)(0x2000 - (va & U64(0x1FFF)));   // bytes on the first page
	u64 phys[2];
	for (int k = 0; k < 2; ++k)
	{
		const u64 a = k ? va_last : va;
		if (k && ((va ^ va_last) >> 13) == 0) { phys[1] = phys[0] + n - 1; break; }
		const u64 vp = a & ~U64(0x1FFF);
		SDataPageCache& dpc = cpu->dpc_slot(rw, a);
		if (dpc.virt_page == vp && dpc.valid && dpc.cm == cpu->state.cm && dpc.asn == cpu->state.asn0)
			phys[k] = dpc.phys_base | (a & U64(0x1FFF));
		else
		{
			if (cpu->virt2phys(a, &phys[k], rw ? ACCESS_WRITE : ACCESS_READ, nullptr, ins)) return 2;
			dpc.phys_base = phys[k] & ~U64(0x1FFF);
			dpc.host_bias = ((phys[k] | U64(0x1FFF)) < cpu->dram_size)
				? ((u64)cpu->dram_ptr + (phys[k] & ~U64(0x1FFF)) - vp) : 0;
			dpc.virt_page = dpc.host_bias ? vp : ~U64(0);
			dpc.cm = (u8)cpu->state.cm;
			dpc.asn = (u8)cpu->state.asn0;
			dpc.valid = dpc.host_bias != 0;
		}
	}
	if (phys[0] >= cpu->dram_size || phys[1] >= cpu->dram_size) return 1;   // MMIO: interpreter
	u8* p = (u8*)cpu->dram_ptr;
	u64 v = rw ? *data : 0;
	for (int k = 0; k < n; ++k)
	{
		const u64 pa = (k < n1) ? phys[0] + k : phys[1] - (n - 1 - k);
		if (rw) p[pa] = (u8)(v >> (k * 8));
		else    v |= (u64)p[pa] << (k * 8);
	}
	if (!rw) *data = v;
	return 0;
}

// JIT load helper (static). descr[7:0] is size_bits; production loads also
// carry the instruction in descr[63:32]. That lets alignment/translation faults enter PAL
// exactly once instead of returning to execute() to repeat the faulting instruction.
int CAlphaCPU::jit_read(CAlphaCPU* cpu, u64 va, u64 descr, u64* out)
{
	const int size_bits = (int)(descr & 0xff);
	const u32 ins = (u32)(descr >> 32);
	const u64 amask = (u64)(size_bits / 8) - 1;
	if (va & amask)
		return cpu->m_unaligned_fixup ? jit_unaligned_rw(cpu, va, size_bits, ins, 0, out)
		                              : cpu->jit_unalign(va, ins, ACCESS_READ, (int)amask);

	u64 phys;
	const u64 vp = va & ~U64(0x1FFF);
	SDataPageCache& dpc = cpu->dpc_slot(0, va);
	if (dpc.virt_page == vp && dpc.valid && dpc.cm == cpu->state.cm && dpc.asn == cpu->state.asn0)
	{
		phys = dpc.phys_base | (va & U64(0x1FFF));
	}
	else
	{
#if defined(JIT_REGPROF) && defined(_WIN32)
		cpu->m_dpc_miss[dpc.virt_page == ~U64(0) ? 0 : dpc.virt_page != vp ? 1 : 2]++;
#endif
		if (ins && !cpu->m_jit_vreplay)
		{
			// The emitted cold stub installed state.current_pc before this call. 
			// A nonzero result means virt2phys has already selected native-PAL fault.
			if (cpu->virt2phys(va, &phys, ACCESS_READ, nullptr, ins)) return 2;
		}
		else
		{
			// Verify/legacy callers do not carry an instruction word and retain the old
			// side-effect-free retry contract.
			const int i = cpu->FindTBEntry(va, ACCESS_READ);
			if (i < 0) return 1;
			const auto& e = cpu->state.tb[TB_INDEX_DATA][i];
			if (!e.access[0][cpu->state.cm] || e.fault[0]) return 1;
			phys = e.phys | (va & e.keep_mask);
		}
		dpc.phys_base = phys & ~U64(0x1FFF);
		dpc.host_bias = ((phys | U64(0x1FFF)) < cpu->dram_size)
			? ((u64)cpu->dram_ptr + (phys & ~U64(0x1FFF)) - vp) : 0;
		dpc.virt_page = dpc.host_bias ? vp : ~U64(0);
		dpc.cm = (u8)cpu->state.cm;
		dpc.asn = (u8)cpu->state.asn0;
		dpc.valid = dpc.host_bias != 0;
	}

	// DRAM only: bail on MMIO so the interpreter does device reads (side effects + ordering). This
	// MUST precede the verify replay: production bails here, and a device byte/word read is NOT
	// size-truncated (LDBU/LDWU use READ_VIRT, no sext func), so replaying an MMIO value and then
	// re-truncating it with movzx would falsely mismatch the interpreter. Bailing -> the compiled
	// block stops at the load (done < prefix_len) and the verify skips the compare, matching prod.
	if (phys >= cpu->dram_size)
		return 1;

	// Verify replay: return the value the interpreter pass loaded here, rather than
	// re-reading (another CPU may have written it)
	if (cpu->m_jit_vreplay)
	{
		u32 i;
		if (!cpu->jit_verify_load_slot(i)) { *out = 0; return 0; }
		if (va != cpu->m_jit_vaddr[i])
		{
			cpu->m_jit_vmismatch = true;
			static int n = 0;
			if (n++ < 50)
				printf("[JIT] LOAD ADDR MISMATCH: compiled va=%016llx interp va=%016llx\n",
					(unsigned long long) va, (unsigned long long) cpu->m_jit_vaddr[i]);
		}
		*out = cpu->m_jit_vlog[i];
		return 0;
	}

	*out = dram_read(cpu->dram_ptr, phys, size_bits);
	return 0;
}

// JIT FP load helper (static). descr packs size[7:0], format[17:16], instruction[63:32].
// FPSTART (fpen -> FEN bail; exc_sum=0) precedes the discarded-f31 and alignment checks.
// Production reuses jit_read's cache read/direct PAL faults; verify replays the interp's CONVERTED
// f-value (FP loads join the load log), so it consumes m_jit_vlog like an integer load.
int CAlphaCPU::jit_fp_read(CAlphaCPU* cpu, u64 va, u32 fa, u64 descr)
{
	if (cpu->state.fpen == 0) return 1;       // FEN trap (FPSTART)
	cpu->state.exc_sum = 0;
	if (fa == 31) return 0;                    // f31 dest: interp skips the read
	const u64 amask = ((descr & 0xff) / 8) - 1;
	if ((va & amask) && !cpu->m_unaligned_fixup)   // fixup: jit_read below splits the access
		return cpu->jit_unalign(va, (u32)(descr >> 32), ACCESS_READ, (int)amask);
	if (cpu->m_jit_vreplay)
	{
		u32 i;
		if (!cpu->jit_verify_load_slot(i)) { cpu->state.f[fa] = 0; return 0; }
		if (va != cpu->m_jit_vaddr[i])
		{
			cpu->m_jit_vmismatch = true;
			static int n = 0;
			if (n++ < 50)
				printf("[JIT] FP LOAD ADDR MISMATCH: compiled va=%016llx interp va=%016llx\n",
					(unsigned long long) va, (unsigned long long) cpu->m_jit_vaddr[i]);
		}
		cpu->state.f[fa] = cpu->m_jit_vlog[i];   // logged converted value (no re-convert)
		return 0;
	}
	u64 raw;
	const int result = jit_read(cpu, va, descr, &raw);
	if (result) return result;   // retain the distinction between retry and entered PAL
	switch ((descr >> 16) & 3)   // fmt: 0=T raw, 1=S ieee, 2=F vax, 3=G vax
	{
	case 1:  cpu->state.f[fa] = cpu->ieee_lds((u32)raw); break;   // LDS
	case 2:  cpu->state.f[fa] = cpu->vax_ldf((u32)raw); break;    // LDF
	case 3:  cpu->state.f[fa] = cpu->vax_ldg(raw);       break;    // LDG
	default: cpu->state.f[fa] = raw;                     break;    // LDT raw
	}
	return 0;
}

// JIT FP store helper (static). STS/STT: MEM[va] = convert(f[fa]) per DO_STS/DO_STT. Routes through
// jit_write so verify compares vs the store log (FP stores join it) and production writes.
int CAlphaCPU::jit_fp_write(CAlphaCPU* cpu, u64 va, u32 fa, u64 descr)
{
	if (cpu->state.fpen == 0) return 1;       // FEN trap (FPSTART)
	cpu->state.exc_sum = 0;
	u64 value;
	switch ((descr >> 16) & 3)   // fmt: 0=T raw, 1=S ieee, 2=F vax, 3=G vax
	{
	case 1:  value = (u64)cpu->ieee_sts(cpu->state.f[fa]); break;   // STS
	case 2:  value = (u64)cpu->vax_stf(cpu->state.f[fa]); break;    // STF
	case 3:  value = cpu->vax_stg(cpu->state.f[fa]);       break;    // STG
	default: value = cpu->state.f[fa];                     break;    // STT raw
	}
	return jit_write(cpu, va, descr, value);
}

// JIT MISC read helper (static). RPCC (sel 0): the wall-clock-pinned cycle counter (DO_RPCC, JIT
// lane), synced to now at each read. RC (sel 1) / RS (sel 2): read the interrupt flag, then clear /
// set it. All three read state the differential verify can't re-derive (cc is time-varying; the
// flag is consumed by the read), so in verify we replay the interp pass's value -- like a load.
u64 CAlphaCPU::jit_misc(CAlphaCPU* cpu, u32 sel)
{
	if (cpu->m_jit_vreplay)
	{
		u32 i;
		return cpu->jit_verify_load_slot(i) ? cpu->m_jit_vlog[i] : 0; // no double side effect
	}

	switch (sel)
	{
	case 0:                                            // RPCC: Ra = cc_offset : cc[31:0]
		return cpu->rpcc_read();                       // live read (NetBSD PCC timecounter) + collision progress
	case 1:                                            // RC: Ra = bIntrFlag; bIntrFlag = false
	{
		u64 v = cpu->state.bIntrFlag ? 1 : 0;
		cpu->state.bIntrFlag = false;
		return v;
	}
	default:                                           // RS (sel 2): Ra = bIntrFlag; bIntrFlag = true
	{
		u64 v = cpu->state.bIntrFlag ? 1 : 0;
		cpu->state.bIntrFlag = true;
		return v;
	}
	}
}

// JIT int->FP move helper (static). ITOFx: f[fc] = fmt(value). Mirrors DO_ITOFx incl. FPSTART
// (fpen==0 -> return 1, the FEN-trap bail; exc_sum cleared). fmt: 0=T raw, 1=S, 2=F.
int CAlphaCPU::jit_itof(CAlphaCPU* cpu, u32 fc, u64 value, u32 fmt)
{
	if (cpu->state.fpen == 0) return 1;       // FEN trap: the interpreter vectors it
	cpu->state.exc_sum = 0;                   // FPSTART
	if (fmt == 1)      cpu->state.f[fc] = cpu->ieee_lds((u32)value);
	else if (fmt == 2) cpu->state.f[fc] = cpu->vax_ldf(SWAP_VAXF((u32)value));
	else               cpu->state.f[fc] = value;
	return 0;
}

// JIT FP->int move helper (static). FTOIx: *out = fmt(f[fa]). Same FPSTART bail. fmt: 0=T, 1=S.
int CAlphaCPU::jit_ftoi(CAlphaCPU* cpu, u32 fa, u32 fmt, u64* out)
{
	if (cpu->state.fpen == 0) return 1;       // FEN trap: the interpreter vectors it
	cpu->state.exc_sum = 0;                   // FPSTART
	*out = (fmt == 1) ? sext_u64_32(cpu->ieee_sts(cpu->state.f[fa])) : cpu->state.f[fa];
	return 0;
}

// JIT FLTL non-arithmetic helper (static). FPCR moves, sign-copies, FP conditional moves and the
// CVTLQ/CVTQL bit rearrangements -- mirrored verbatim from cpu_fp_operate.h; classify only routes
// these funcs here (no FP math, no /V trap forms). Returns 1 = FEN-trap bail.
int CAlphaCPU::jit_fltl(CAlphaCPU* cpu, u32 ins)
{
	if (cpu->state.fpen == 0) return 1;       // FEN trap: the interpreter vectors it
	cpu->state.exc_sum = 0;                   // FPSTART
	u64* f = cpu->state.f;
	const u32 fa = (ins >> 21) & 0x1f, fb = (ins >> 16) & 0x1f, fc = ins & 0x1f;
	switch ((ins >> 5) & 0x7ff)
	{
	case 0x010:                                                                  // CVTLQ
		f[fc] = sext_u64_32(((f[fb] >> 32) & 0xC0000000) | ((f[fb] >> 29) & 0x3FFFFFFF));
		break;
	case 0x020: f[fc] = (f[fa] & FPR_SIGN) | (f[fb] & ~FPR_SIGN); break;          // CPYS
	case 0x021: f[fc] = ((f[fa] & FPR_SIGN) ^ FPR_SIGN) | (f[fb] & ~FPR_SIGN); break;   // CPYSN
	case 0x022:                                                                  // CPYSE
		f[fc] = (f[fa] & (FPR_SIGN | FPR_EXP)) | (f[fb] & ~(FPR_SIGN | FPR_EXP));
		break;
	case 0x024: cpu->write_fpcr_arch(f[fa]); break;                               // MT_FPCR
	case 0x025: f[fa] = cpu->read_fpcr_arch(); break;                             // MF_FPCR (dest = Fa)
	case 0x02a: if ((f[fa] & ~FPR_SIGN) == 0) f[fc] = f[fb]; break;               // FCMOVEQ
	case 0x02b: if ((f[fa] & ~FPR_SIGN) != 0) f[fc] = f[fb]; break;               // FCMOVNE
	case 0x02c: if ((f[fa] & FPR_SIGN) && (f[fa] & ~FPR_SIGN) != 0) f[fc] = f[fb]; break;   // FCMOVLT
	case 0x02d: if (!(f[fa] & FPR_SIGN) || (f[fa] & ~FPR_SIGN) == 0) f[fc] = f[fb]; break;  // FCMOVGE
	case 0x02e: if ((f[fa] & FPR_SIGN) || (f[fa] & ~FPR_SIGN) == 0) f[fc] = f[fb]; break;   // FCMOVLE
	case 0x02f: if (!(f[fa] & FPR_SIGN) && (f[fa] & ~FPR_SIGN) != 0) f[fc] = f[fb]; break;  // FCMOVGT
	case 0x030: {                                                                // CVTQL (no /V form)
		u64 cvtql_src = f[fb];
		f[fc] = ((cvtql_src & U64(0xC0000000)) << 32) | ((cvtql_src & U64(0x3FFFFFFF)) << 29);
		if (FPR_GETSIGN(cvtql_src) ? (cvtql_src < U64(0xFFFFFFFF80000000)) :
			(cvtql_src > U64(0x000000007FFFFFFF)))
			cpu->write_fpcr_arch(cpu->state.fpcr | FPCR_IOV);
		break;
	}
	}
	return 0;
}

// JIT VAX-FP arithmetic helper (static). FLTV (0x15): mirrors the interpreter's vax_* dispatch into
// f[fc]. FPSTART bail (return 1) if FP disabled. Returns 2 when the op delivered an arith trap: vax_trap
// GO_PALs (sets state.pc + exc_sum), so exc_sum != 0 detects it and the caller honors state.pc -- the op
// runs exactly once (no re-execute). Returns 0 on the common no-trap path. Fc==31 is gated out in classify.
int CAlphaCPU::jit_fltv(CAlphaCPU* cpu, u32 ins)
{
	if (cpu->state.fpen == 0) return 1;       // FEN trap (FPSTART): op not run
	cpu->state.exc_sum = 0;
	u64* f = cpu->state.f;
	const u32 fa = (ins >> 21) & 0x1f, fb = (ins >> 16) & 0x1f, fc = ins & 0x1f;
	const u32 fn = (ins >> 5) & 0x7ff;
	UFP u;
	switch (fn)
	{
	case 0x0a5: case 0x4a5: f[fc] = (cpu->vax_fcmp(f[fa], f[fb], ins) == 0) ? U64(0x4000000000000000) : 0; break;   // CMPGEQ
	case 0x0a6: case 0x4a6: f[fc] = (cpu->vax_fcmp(f[fa], f[fb], ins) < 0) ? U64(0x4000000000000000) : 0; break;   // CMPGLT
	case 0x0a7: case 0x4a7: f[fc] = (cpu->vax_fcmp(f[fa], f[fb], ins) <= 0) ? U64(0x4000000000000000) : 0; break;   // CMPGLE
	case 0x03c: case 0x0bc: f[fc] = cpu->vax_cvtif(f[fb], ins, DT_F); break;   // CVTQF
	case 0x03e: case 0x0be: f[fc] = cpu->vax_cvtif(f[fb], ins, DT_G); break;   // CVTQG
	default:
		switch (fn & 0x7f)
		{
		case 0x000: f[fc] = cpu->vax_fadd(f[fa], f[fb], ins, DT_F, false); break;   // ADDF
		case 0x001: f[fc] = cpu->vax_fadd(f[fa], f[fb], ins, DT_F, true);  break;   // SUBF
		case 0x002: f[fc] = cpu->vax_fmul(f[fa], f[fb], ins, DT_F);        break;   // MULF
		case 0x003: f[fc] = cpu->vax_fdiv(f[fa], f[fb], ins, DT_F);        break;   // DIVF
		case 0x01e: cpu->vax_unpack_d(f[fb], &u, ins); f[fc] = cpu->vax_rpack(&u, ins, DT_G); break;   // CVTDG
		case 0x020: f[fc] = cpu->vax_fadd(f[fa], f[fb], ins, DT_G, false); break;   // ADDG
		case 0x021: f[fc] = cpu->vax_fadd(f[fa], f[fb], ins, DT_G, true);  break;   // SUBG
		case 0x022: f[fc] = cpu->vax_fmul(f[fa], f[fb], ins, DT_G);        break;   // MULG
		case 0x023: f[fc] = cpu->vax_fdiv(f[fa], f[fb], ins, DT_G);        break;   // DIVG
		case 0x02c: cpu->vax_unpack(f[fb], &u, ins); f[fc] = cpu->vax_rpack(&u, ins, DT_F); break;   // CVTGF
		case 0x02d: cpu->vax_unpack(f[fb], &u, ins); f[fc] = cpu->vax_rpack_d(&u, ins);     break;   // CVTGD
		case 0x02f: f[fc] = cpu->vax_cvtfi(f[fb], ins); break;   // CVTGQ
		}
	}
	return cpu->state.exc_sum ? 2 : 0;   // exc_sum != 0 => vax_trap GO_PAL'd -> caller honors state.pc
}

// JIT load-locked helper (static). LDx_L: the verify-checked load PLUS cpu_lock -- the LL/SC
// exclusive monitor. cpu_lock is per-CPU + atomic + idempotent.
int CAlphaCPU::jit_read_locked(CAlphaCPU* cpu, u64 va, u64 descr, u64* out)
{
	const int size_bits = (int)(descr & 0xff);
	const u32 ins = (u32)(descr >> 32);
	const u64 amask = (u64)(size_bits / 8) - 1;
	if (va & amask) return cpu->jit_unalign(va, ins, ACCESS_READ, (int)amask);

	u64 phys;
	const u64 vp = va & ~U64(0x1FFF);
	SDataPageCache& dpc = cpu->dpc_slot(0, va);
	if (dpc.virt_page == vp && dpc.valid && dpc.cm == cpu->state.cm && dpc.asn == cpu->state.asn0)
	{
		phys = dpc.phys_base | (va & U64(0x1FFF));
	}
	else
	{
#if defined(JIT_REGPROF) && defined(_WIN32)
		cpu->m_dpc_miss[dpc.virt_page == ~U64(0) ? 0 : dpc.virt_page != vp ? 1 : 2]++;
#endif
		if (ins && !cpu->m_jit_vreplay)
		{
			// The emitter installed current_pc. Translation faults enter native PAL here.
			if (cpu->virt2phys(va, &phys, ACCESS_READ, nullptr, ins)) return 2;
		}
		else
		{
			// Differential replay retains the side-effect-free lookup used by the
			// interpreter.
			const int i = cpu->FindTBEntry(va, ACCESS_READ);
			if (i < 0) return 1;
			const auto& e = cpu->state.tb[TB_INDEX_DATA][i];
			if (!e.access[0][cpu->state.cm] || e.fault[0]) return 1;
			phys = e.phys | (va & e.keep_mask);
		}
		dpc.phys_base = phys & ~U64(0x1FFF);
		dpc.host_bias = ((phys | U64(0x1FFF)) < cpu->dram_size)
			? ((u64)cpu->dram_ptr + (phys & ~U64(0x1FFF)) - vp) : 0;
		dpc.virt_page = dpc.host_bias ? vp : ~U64(0);
		dpc.cm = (u8)cpu->state.cm;
		dpc.asn = (u8)cpu->state.asn0;
		dpc.valid = dpc.host_bias != 0;
	}

	if (phys >= cpu->dram_size)                // MMIO: interpreter handles the I/O-space locked path
		return 1;

	if (cpu->m_jit_vreplay)
	{
		u32 i;
		if (!cpu->jit_verify_load_slot(i)) { *out = 0; return 0; }
		if (va != cpu->m_jit_vaddr[i])
		{
			cpu->m_jit_vmismatch = true;
			static int n = 0;
			if (n++ < 50)
				printf("[JIT] LDx_L ADDR MISMATCH: compiled va=%016llx interp va=%016llx\n",
					(unsigned long long) va, (unsigned long long) cpu->m_jit_vaddr[i]);
		}
		*out = cpu->m_jit_vlog[i];   // the interp pass's (already sign-extended) value
	}
	else
	{
		const u64 raw = dram_read(cpu->dram_ptr, phys, size_bits);
		*out = (size_bits == 32) ? sext_u64_32(raw) : raw;   // LDL_L sign-extends; LDQ_L is the full quad
	}

	// Establish the LL exclusive monitor (DO_LDx_L's READ_VIRT_LOCK_F makes the same cpu_lock call).
	cpu->cSystem->cpu_lock(cpu->state.iProcNum, phys, *out);
	return 0;
}

// Virtual HW_LD helper. "descr" carries the instruction plus transfer size/VPTE/ALT/WrChk flags.
// normal build will deliver translation faults directly; verify retains a side-effect-free replay probe.
int CAlphaCPU::jit_read_vpte(CAlphaCPU* cpu, u64 va, u64 descr, u64* out)
{
	const int size_bits = (int)(descr & 0xff);
	const bool vpte = (descr & 0x100) != 0;
	const bool alt = (descr & 0x200) != 0;
	const bool wchk = (descr & 0x400) != 0;
	const u32 ins = (u32)(descr >> 32);
	const u64 amask = (u64)(size_bits / 8) - 1;
	// HW_LD translates the original VA, then its no-trap physical access rounds the
	// resulting physical address down.

	u64 phys;
	const int flags = ACCESS_READ | (vpte ? VPTE : 0) | (alt ? ALT : 0) | (wchk ? WRCHK : 0);
	if (ins && !cpu->m_jit_vreplay)
	{
		// The emitter installed current_pc. A nonzero result has already selected the
		// native-PAL DTB/access-fault entry.
		if (cpu->virt2phys(va, &phys, flags, nullptr, ins)) return 2;
	}
	else
	{
		// The reference interpreter already completed this instruction before verify replay.
		const int i = cpu->FindTBEntry(va, ACCESS_READ);
		if (i < 0) return 1;
		const auto& e = cpu->state.tb[TB_INDEX_DATA][i];
		const int cm = vpte ? 0 : alt ? cpu->state.alt_cm : cpu->state.cm;
		if (!e.access[0][cm] || e.fault[0]) return 1;
		if (wchk && (!e.access[1][cm] || e.fault[1])) return 1;
		phys = e.phys | (va & e.keep_mask);
	}

	phys &= ~amask;

	if (phys >= cpu->dram_size)               // MMIO: bail before the replay (mirrors jit_read)
		return 1;

	if (cpu->m_jit_vreplay)
	{
		// Virtual HW_LD to R31 is a fault/access probe: Translation and MMIO checks 
		// still matter, but its discarded value has no slot.
		if (((ins >> 21) & 0x1f) == 31)
		{
			*out = 0;
			return 0;
		}
		u32 i;
		if (!cpu->jit_verify_load_slot(i)) { *out = 0; return 0; }
		if (va != cpu->m_jit_vaddr[i])
		{
			cpu->m_jit_vmismatch = true;
			static int n = 0;
			if (n++ < 50)
				printf("[JIT] HW_LD VIRT ADDR MISMATCH: compiled va=%016llx interp va=%016llx\n",
					(unsigned long long) va, (unsigned long long) cpu->m_jit_vaddr[i]);
		}
		*out = cpu->m_jit_vlog[i];
		return 0;
	}

	*out = dram_read(cpu->dram_ptr, phys, size_bits);
	return 0;
}

// JIT HW_LD WrChk helper (static). HW_LD func 0xa (DO_HW_LDL case 10, HRM TYPE 1012 WrChk): a
// longword VIRTUAL read that ALSO requires WRITE access -- the interpreter ACVs if read OR write
// protection is clear (virt2phys WRCHK) and faults FOR/FOW. Side-effect-free TB probe mirroring
// jit_read_vpte but checked vs CURRENT mode (not kernel); bails (interp re-runs + vectors the
// fault) on miss/protection/fault/MMIO. Verify replays the value like any load.
int CAlphaCPU::jit_read_wchk(CAlphaCPU* cpu, u64 va, int size_bits, u64* out)
{
	const u64 amask = (u64)(size_bits / 8) - 1;
	if (va & amask) return 1;                 // unaligned: let the interpreter handle it

	const int i = cpu->FindTBEntry(va, ACCESS_READ);
	if (i < 0) return 1;                       // TB miss
	const auto& e = cpu->state.tb[TB_INDEX_DATA][i];
	const int cm = cpu->state.cm;
	if (!e.access[0][cm]) return 1;            // no read access (ACV)
	if (!e.access[1][cm]) return 1;            // no write access -- WrChk fails (ACV)
	if (e.fault[0] || e.fault[1]) return 1;    // FOR/FOW: bail so the interpreter vectors the fault
	const u64 phys = e.phys | (va & e.keep_mask);

	if (phys >= cpu->dram_size)               // MMIO: bail before the replay (mirrors jit_read_vpte)
		return 1;

	if (cpu->m_jit_vreplay)
	{
		u32 i;
		if (!cpu->jit_verify_load_slot(i)) { *out = 0; return 0; }
		if (va != cpu->m_jit_vaddr[i])
		{
			cpu->m_jit_vmismatch = true;
			static int n = 0;
			if (n++ < 50)
				printf("[JIT] WCHK ADDR MISMATCH: compiled va=%016llx interp va=%016llx\n",
					(unsigned long long) va, (unsigned long long) cpu->m_jit_vaddr[i]);
		}
		*out = cpu->m_jit_vlog[i];
		return 0;
	}

	*out = dram_read(cpu->dram_ptr, phys, size_bits);
	return 0;
}

// JIT HW_LD helper (static). Physical read of size_bits at phys -> *out with NO translation -
// the PALmode HW_LD physical longword/quadword forms (func 0/1). Aligns like READ_PHYS_NT.
// Verify replays the interpreter's value (race-free); MMIO bails so the interpreter does the
// ordered device read. Returns 0 on success, 1 on a bail.
int CAlphaCPU::jit_read_phys(CAlphaCPU* cpu, u64 phys, int size_bits, u64* out)
{
	// MMIO: bail before the replay so verify models production (which bails here). A device read
	// isn't size-truncated, so a replayed+re-truncated value would falsely mismatch. dram_size is
	// page-aligned and the align below only rounds within 8 bytes, so this raw check is exact.
	if (phys >= cpu->dram_size)
		return 1;

	if (cpu->m_jit_vreplay)
	{
		u32 i;
		if (!cpu->jit_verify_load_slot(i)) { *out = 0; return 0; }
		if (phys != cpu->m_jit_vaddr[i])
		{
			cpu->m_jit_vmismatch = true;
			static int n = 0;
			if (n++ < 50)
				printf("[JIT] HW_LD ADDR MISMATCH: compiled pa=%016llx interp pa=%016llx\n",
					(unsigned long long) phys, (unsigned long long) cpu->m_jit_vaddr[i]);
		}
		*out = cpu->m_jit_vlog[i];
		return 0;
	}

	phys &= ~((u64)(size_bits / 8) - 1);     // align like READ_PHYS_NT (ALIGN_PHYS)
	*out = dram_read(cpu->dram_ptr, phys, size_bits);
	return 0;
}

// JIT store helper: the descriptor and three-way return contract match jit_read.
int CAlphaCPU::jit_write(CAlphaCPU* cpu, u64 va, u64 descr, u64 value)
{
	const int size_bits = (int)(descr & 0xff);
	const u32 ins = (u32)(descr >> 32);
	const u64 amask = (u64)(size_bits / 8) - 1;
	if (va & amask)
		return cpu->m_unaligned_fixup ? jit_unaligned_rw(cpu, va, size_bits, ins, 1, &value)
		                              : cpu->jit_unalign(va, ins, ACCESS_WRITE, (int)amask);

	// Verify: the interpreter pass already performed (and recorded) this store. Compare
	// rather than write -- stores change memory, not GPRs, so the differential GPR check
	// can't see them; this is how compiled stores get validated.
	if (cpu->m_jit_vreplay)
	{
		u32 i;
		if (!cpu->jit_verify_store_slot(i)) return 0;
		if (va != cpu->m_jit_slog_addr[i] || value != cpu->m_jit_slog_val[i])
		{
			cpu->m_jit_vmismatch = true;
			static int n = 0;
			if (n++ < 50)
				printf("[JIT] STORE MISMATCH: compiled va=%016llx val=%016llx  interp va=%016llx val=%016llx\n",
					(unsigned long long) va, (unsigned long long) value,
					(unsigned long long) cpu->m_jit_slog_addr[i], (unsigned long long) cpu->m_jit_slog_val[i]);
		}
		return 0;
	}

	u64 phys;
	const u64 vp = va & ~U64(0x1FFF);
	SDataPageCache& dpc = cpu->dpc_slot(1, va);
	if (dpc.virt_page == vp && dpc.valid && dpc.cm == cpu->state.cm && dpc.asn == cpu->state.asn0)
	{
		phys = dpc.phys_base | (va & U64(0x1FFF));
	}
	else
	{
#if defined(JIT_REGPROF) && defined(_WIN32)
		cpu->m_dpc_miss[dpc.virt_page == ~U64(0) ? 0 : dpc.virt_page != vp ? 1 : 2]++;
#endif
		if (ins && !cpu->m_jit_vreplay)
		{
			if (cpu->virt2phys(va, &phys, ACCESS_WRITE, nullptr, ins)) return 2;
		}
		else
		{
			const int i = cpu->FindTBEntry(va, ACCESS_WRITE);
			if (i < 0) return 1;
			const auto& e = cpu->state.tb[TB_INDEX_DATA][i];
			if (!e.access[1][cpu->state.cm] || e.fault[1]) return 1;
			phys = e.phys | (va & e.keep_mask);
		}
		dpc.phys_base = phys & ~U64(0x1FFF);
		dpc.host_bias = ((phys | U64(0x1FFF)) < cpu->dram_size)
			? ((u64)cpu->dram_ptr + (phys & ~U64(0x1FFF)) - vp) : 0;
		dpc.virt_page = dpc.host_bias ? vp : ~U64(0);
		dpc.cm = (u8)cpu->state.cm;
		dpc.asn = (u8)cpu->state.asn0;
		dpc.valid = dpc.host_bias != 0;
	}

	if (phys < cpu->dram_size)
		dram_write(cpu->dram_ptr, phys, size_bits, value);
	else
		cpu->cSystem->WriteMem(phys, size_bits, value, cpu);
	return 0;
}

// JIT HW_ST helper (static). Physical write of size_bits = value at phys with NO translation -
// the PALmode HW_ST physical longword/quadword forms (func 0/1). Aligns like WRITE_PHYS_NT.
// Verify compares against the interpreter's recorded store (stores change memory, not GPRs);
// MMIO bails so the interpreter does the ordered device write. Returns 0 on success, 1 on a bail.
int CAlphaCPU::jit_write_phys(CAlphaCPU* cpu, u64 phys, int size_bits, u64 value)
{
	if (cpu->m_jit_vreplay)
	{
		u32 i;
		if (!cpu->jit_verify_store_slot(i)) return 0;
		if (phys != cpu->m_jit_slog_addr[i] || value != cpu->m_jit_slog_val[i])
		{
			cpu->m_jit_vmismatch = true;
			static int n = 0;
			if (n++ < 50)
				printf("[JIT] HW_ST STORE MISMATCH: compiled pa=%016llx val=%016llx  interp pa=%016llx val=%016llx\n",
					(unsigned long long) phys, (unsigned long long) value,
					(unsigned long long) cpu->m_jit_slog_addr[i], (unsigned long long) cpu->m_jit_slog_val[i]);
		}
		return 0;
	}

	phys &= ~((u64)(size_bits / 8) - 1);     // align like WRITE_PHYS_NT (ALIGN_PHYS)
	if (phys >= cpu->dram_size)
		return 1;                              // MMIO: let the interpreter do the ordered write
	dram_write(cpu->dram_ptr, phys, size_bits, value);
	return 0;
}

// JIT store-conditional helper (static). Same-address STx_C uses the emulator's
// CAS-backed MP model; different-address same-line STx_C stores without comparing
// against the LDx_L datum.
u64 CAlphaCPU::jit_stc(CAlphaCPU* cpu, u64 va, u64 descr, u64 value)
{
	const int size_bits = (int)(descr & 0xff);
	const u64 amask = (u64)(size_bits / 8) - 1;
	if (va & amask)
		return (u64)cpu->jit_unalign(va, (u32)(descr >> 32), ACCESS_WRITE, (int)amask) << 8;
	if (cpu->m_jit_vreplay)
	{
		u32 i;
		if (!cpu->jit_verify_store_slot(i)) return 0;
		const u64 success = cpu->m_jit_slog_success[i];
		if (va != cpu->m_jit_slog_addr[i] || (success && value != cpu->m_jit_slog_val[i]))
		{
			cpu->m_jit_vmismatch = true;
			static int n = 0;
			if (n++ < 50)
				printf("[JIT] STx_C MISMATCH: compiled va=%016llx val=%016llx ok=%llu  interp va=%016llx val=%016llx\n",
					(unsigned long long) va, (unsigned long long) value, (unsigned long long) success,
					(unsigned long long) cpu->m_jit_slog_addr[i], (unsigned long long) cpu->m_jit_slog_val[i]);
		}
		return success;
	}

	// Side-effect-free write-path translation (mirror jit_write); bail to the interpreter on a TB
	// miss / protection / fault-on-write so it does the side-effecting translation.
	u64 phys;
	const u64 vp = va & ~U64(0x1FFF);
	SDataPageCache& dpc = cpu->dpc_slot(1, va);
	if (dpc.virt_page == vp && dpc.valid && dpc.cm == cpu->state.cm && dpc.asn == cpu->state.asn0)
	{
		phys = dpc.phys_base | (va & U64(0x1FFF));
	}
	else
	{
#if defined(JIT_REGPROF) && defined(_WIN32)
		cpu->m_dpc_miss[dpc.virt_page == ~U64(0) ? 0 : dpc.virt_page != vp ? 1 : 2]++;
#endif
		const int i = cpu->FindTBEntry(va, ACCESS_WRITE);
		if (i < 0) return U64(0x100);                                        // TB miss
		const auto& e = cpu->state.tb[TB_INDEX_DATA][i];
		if (!e.access[1][cpu->state.cm]) return U64(0x100);                  // protection (ACV)
		if (e.fault[1]) return U64(0x100);                                   // fault-on-write (FOW)
		phys = e.phys | (va & e.keep_mask);
		dpc.phys_base = phys & ~U64(0x1FFF);
		dpc.host_bias = ((phys | U64(0x1FFF)) < cpu->dram_size)
			? ((u64)cpu->dram_ptr + (phys & ~U64(0x1FFF)) - vp) : 0;
		dpc.virt_page = dpc.host_bias ? vp : ~U64(0);
		dpc.cm = (u8)cpu->state.cm;
		dpc.asn = (u8)cpu->state.asn0;
		dpc.valid = dpc.host_bias != 0;
	}

	// Shared LL/SC path: consumes the monitor, applies the ABA sequence guard,
	// CASes RAM or performs the MMIO conditional store.
	return cpu->cSystem->cpu_stx_c(cpu->state.iProcNum, phys, size_bits, value,
		cpu->dram_ptr, cpu->dram_size, cpu);
}

/* CALL_PAL OPCDEC trap: a privileged function (< 0x40) attempted in user mode. Mirrors
   GO_PAL(AlphaPALException::OpcodeDecode) -- save the faulting PC in EXC_ADDR, vector to the PALcode OPCDEC entry,
   and clear the load-lock flag */
void CAlphaCPU::jit_opcdec(CAlphaCPU* cpu, u64 cpc)
{
	cpu->enter_pal_exception(cpu->pal_exception_entry(AlphaPALException::OpcodeDecode, cpc));
}

/* HW_MFPR (PALmode): return the IPR selected by (ins>>8)&0xff. */
u64 CAlphaCPU::jit_hw_mfpr(CAlphaCPU* cpu, u32 ins, u64 cur)
{
	const u32 function = (ins >> 8) & 0xff;
	// Replay reads of asynchronous state from the interpreter reference pass.
	if (cpu->m_jit_vreplay && cpu->m_profile.ipr_read.describe(function).replay)
	{
		u32 i;
		return cpu->jit_verify_load_slot(i) ? cpu->m_jit_vlog[i] : 0;
	}
	(void)cur;
	return cpu->read_ipr(function);
}

// The family policy owns the write; its descriptor controls compiled eligibility.
void CAlphaCPU::jit_hw_mtpr(CAlphaCPU* cpu, u32 function, u64 value)
{
	cpu->write_ipr(function, value, AlphaIPRWriteContext::Compiled);
}

// JIT indirect-jump chaining helper (static). For a compiled JMP/HW_RET, look up the block at the
// runtime target in this CPU's block cache (the dispatcher's own lookup -- validates valid + tag +
// asn/asm_global) and return its chained re-entry point if it's compiled and runnable in the current
// context; else null, so the compiled jump bails to the dispatcher. Keying on the actual target lets
// any number of distinct targets chain without the old single-slot link thrashing on varying jumps.
void* CAlphaCPU::jit_indirect(CAlphaCPU* cpu, u64 target, void* link_cache)
{
#if defined(JIT_STATS) && defined(JIT_STATS_RESOLVER_TSC)   // two RDTSCs per call: perturbs throughput
	const uint64_t t0 = jit_rdtsc();
	void* const r = jit_indirect_impl(cpu, target, link_cache);
	cpu->m_jit->note_resolver_tsc(jit_rdtsc() - t0);
	return r;
#else
	return jit_indirect_impl(cpu, target, link_cache);
#endif
}

void* CAlphaCPU::jit_indirect_impl(CAlphaCPU* cpu, u64 target, void* link_cache)
{
	cpu->m_jit->note_jmp_attempt();
	// PAL reset entry: never chain in - match dispatch/interp
	if (target == (cpu->state.pal_base | 1))
		return nullptr;
	CJitEngine::JitBlock* b = cpu->m_jit->lookup(target, (u32)cpu->state.asn,
		(target & 1) && cpu->state.sde);
	if (!b || !b->jit_body) return nullptr;
	auto hit = [&](void* body) -> void* {
		if (link_cache)
			cpu->m_jit->patch_edge(link_cache, target,
				cpu->m_jit->vgen() | (b->pal_shadow ? (U64(1) << 63) : 0), body, b->asm_global);
		cpu->m_jit->jcache_fill(target, b->pal_shadow, body, (u32)cpu->state.asn);
		cpu->m_jit->note_jmp_hit();
		return body;
	};
	if (target & 1)
	{
		// PALmode target: the I-stream is physically addressed (not paged), so no remap / no stale
		// risk. lookup selected the body whose register-bank mapping matches the live SDE state.
		return hit(b->jit_body);
	}
	// Native target: validate the live physical every time (a single-page ITB invalidate no
	// longer bumps the epoch). Icache probe first, else a side-effect-free translation that
	// also covers superpage targets.
	const u64 gen = cpu->m_jit->vgen();
	const u64 v = target & ~U64(3);
	const int li = (int)((v >> 11) & (ICACHE_ENTRIES - 1));
	u64 live;
	if (cpu->icache_enabled && cpu->state.icache[li].valid
		&& (cpu->state.icache[li].asn == cpu->state.asn || cpu->state.icache[li].asm_bit)
		&& cpu->state.icache[li].address == (v & ICACHE_MATCH_MASK))
		live = cpu->state.icache[li].p_address + (v & ICACHE_BYTE_MASK);
	else
	{
		bool ad;
		if (cpu->virt2phys(v, &live, ACCESS_EXEC | FAKE, &ad, 0) != 0)
			return nullptr;                                     // ITB miss: let the dispatcher fault it in
	}
	if (live != b->phys)
	{
		// Caught a stale block: the page was remapped without flushing the JIT cache. Bail so the
		// dispatcher re-records/recompiles. Rate-limited log -- this firing confirms the stale chain.
		static int n = 0;
		if (n++ < 50)
			printf("[JIT][CPU%d] INDIRECT STALE: target=%016llx block_phys=%016llx -- recompiling\n",
				(int)cpu->state.iProcNum, (unsigned long long) target, (unsigned long long) b->phys);
		return nullptr;
	}
	b->vgen = gen;                                             // re-validated -> fast path henceforth
	return hit(b->jit_body);
}
#endif

/**
 * Check if threads are still running.
 *
 * Calibrate the CPU timing loop.
 **/
void CAlphaCPU::check_state()
{
	if (myThread && !myThread->isRunning())
		FAILURE(Thread, "CPU thread has died");

#if !defined(CONSTANT_TIME_FACTOR)
	if (state.instruction_count > 0)
	{
		// correct CPU timing loop...
		u64 icount = state.instruction_count;
		u64 cc = cc_large;
		u64 time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
		s64 ce = cc_per_instruction;

		u64 cc_aim = time * cpu_hz / 1000000; // microsecond resolution
		u64 ce_aim = cc_aim / icount;

		s64 icount_lapse = icount - prev_icount;
		s64 cc_diff = cc_aim - cc;
		s64 ce_diff = (s64)((float)cc_diff / (float)icount_lapse);

		s64 ce_new = ce_aim + ce_diff;
		if (ce_new < 1)
			ce_new = 1;
		if (ce_new > 200)
			ce_new = 200;

		if (ce_new != ce)
		{

			//    printf("                                     time %12" PRId64 " | prev %12" PRId64 "  \n",time,prev_time);
			//    printf("          count lapse %12" PRId64 " | curr %12" PRId64 " | prev %12" PRId64 "  \n",icount_lapse,icount,prev_icount);
			//    printf("cc %12" PRId64 " | aim %12" PRId64 " | diff %12" PRId64 " | prev %12" PRId64 "  \n",cc,cc_aim,cc_diff,prev_cc);
			//    printf("ce %12" PRId64 " | aim %12" PRId64 " | diff %12" PRId64 " | new  %12" PRId64 "  \n",ce,ce_aim,ce_diff,ce_new);
			//    printf("==========================================================================  \n");
			cc_per_instruction = ce_new;
			//    printf("cpu %d speed factor: %d\n",get_cpuid(),ce_new);
		}

		prev_cc = cc;
		prev_icount = icount;
		prev_time = time;
	}
#endif
	return;
}

/**
 * \brief Called each clock-cycle.
 *
 * This is where the actual CPU emulation takes place. Each clocktick, one instruction
 * is processed by the processor. The instruction pipeline is not emulated, things are
 * complicated enough as it is. The one exception is the instruction cache, which is
 * implemented, to accomodate self-modifying code. The instruction cache can be disabled
 * if self-modifying code is not expected.
 **/
void CAlphaCPU::execute()
{
	u32 ins;
	int i = 0;
	u64 phys_address;
	u64 temp_64;
	u64 temp_64_1;
	u64 temp_64_2;
	UFP ufp1;
	UFP ufp2;

	bool pbc = false;

	int opcode;
	int function;

#ifndef ES40_JIT
	// ---- Batch loop: execute up to 512 instructions before returning ----
	int _batch_budget = 512;
	u64 _cc_accum = 0;      // accumulated cycle counts (flushed every 32 insns)
	int _icount_accum = 0;  // accumulated instruction count
	const u64 _cc_per_ins = cc_per_instruction;  // cache in register
#else
	const u64 _cc_per_ins = cc_per_instruction;
#endif

#ifndef ES40_JIT
	{
		const auto now = std::chrono::steady_clock::now();

		// Wall-clock RPCC at batch granularity - same semantics as the JIT build. The
		// old per-instruction advance ran at cc_per_instruction rate, which lags real
		// time while the check_state feedback converges; SRM's cycles-per-tick speed
		// calibration measured that lag consistently and locked in a low CPU speed.
		sync_cc_wallclock();

		// Poll the wall-clock Cchip interval timer once per execute() batch
		// (~512 instructions) rather than every 32;
		if (state.iProcNum == 0)
		{
			if (now >= next_timer_fire)
			{
				const u64 period_ns = theAli ? theAli->get_interval_period_ns() : 0;
				if (period_ns)
				{
					// Count-preserving, paced catch-up: the schedule advances one period per
					// fire so ticks lost to a busy/stalled CPU0 thread are repaid and the
					// guests' tick-counted clocks (VMS never resyncs) stay true to wall time.
					// Repay gaps are MODULATED so consecutive SRM cycles-per-tick windows
					// (~100 ticks) never agree during a repay stretch. Per-fire noise alone
					// averages out 1/sqrt(N) over a window; the triangle wave's ~2.8-window
					// wavelength survives the averaging, and the noise breaks symmetric-
					// alignment ties. 
					// Backlog beyond 1s (debugger pause, host sleep) is dropped.
					if (now - tick_last_fire >= std::chrono::nanoseconds(tick_gap_ns))
					{
						cSystem->interrupt(-1, true);
						tick_last_fire = now;
						tick_gap_ns = tick_next_gap_ns(period_ns);
						next_timer_fire += std::chrono::nanoseconds(period_ns);
						if (now - next_timer_fire > std::chrono::seconds(1))
							next_timer_fire = now;
					}
				}
				else
				{
					cSystem->interrupt(-1, true);
					tick_last_fire = now;
					next_timer_fire = now + std::chrono::seconds(1);
				}
			}
		}
	}

_next_instruction:
	if (--_batch_budget <= 0)
	{
		// Flush remaining accumulated counters before returning. state.cc is wall-clock
		// (advanced at batch top); _cc_accum only feeds cc_large for the legacy
		// check_state speed-factor feedback.
		state.instruction_count += _icount_accum;
		cc_large += _cc_accum;
		return;
	}
#endif

#if defined(MIPS_ESTIMATE)

	// Calculate simulated performance statistics
	if (++count >= MIPS_INTERVAL)
	{
		clock_t current = clock();

		if (saved > 0)
		{
			double  secs = (current - saved) / (double)CLOCKS_PER_SEC;
			double  ips = MIPS_INTERVAL / secs;
			double  mips = ips / 1000000.0;
			if (max_mips < mips)
				max_mips = mips;
			if (min_mips > mips)
				min_mips = mips;
			printf("ES40 MIPS (%3.1f sec):: current: %5.3f, min: %5.3f, max: %5.3f\n",
				secs, mips, min_mips, max_mips);
		}

		saved = current;
		count = 0;
	}
#endif
#if defined(IDB)
	char* funcname = 0;
	dbg_string[0] = '\0';
#if !defined(LS_MASTER) && !defined(LS_SLAVE)
	dbg_strptr = dbg_string;
#endif
#endif
	state.current_pc = state.pc;

	//--------------------------------------------------------------------------------
	// This section skips the memory check in SRM. Set the define in config_debug  
	// for the memory check to run.
	//--------------------------------------------------------------------------------
#if defined(SKIP_SRM_MEMTEST) && !defined(ES40_JIT)
	// All five SRM mem-test patch points are in page 0x8b000 and only fire
	// during early SRM boot. Gate on the page so every other instruction pays
	// one compare instead of five.
	// JIT builds must NOT use these hooks - a block-cache miss interprets a few 
	// iterations, thus only one hook like 0x8bb90 fires alone, and aborts the
	// fill while a still-compiled verify loop then runs, warm inits show mem errors
	// JIT is fast enough, even 16GB configs don't take long. 
	if ((state.current_pc & ~U64(0xFFF)) == U64(0x8b000))
	{
		if (state.current_pc == U64(0x8bb90))
		{
			if (state.r[5] != U64(0xaaaaaaaaaaaaaaaa))
			{
				printf("wrong memory check skip!\n");
			}
			else
			{
				state.r[0] = state.r[4];
			}
		}

		if (state.current_pc == U64(0x8bbe0))
		{
			if (state.r[5] != U64(0xaaaaaaaaaaaaaaaa))
			{
				printf("wrong memory check skip!\n");
			}
			else
			{
				state.r[16] = 0;
			}
		}

		if (state.current_pc == U64(0x8bc28))
		{
			if (state.r[5] != U64(0xaaaaaaaaaaaaaaaa))
			{
				printf("wrong memory check skip!\n");
			}
			else
			{
				state.r[8] = state.r[4];
			}
		}

		if (state.current_pc == U64(0x8bc70))
		{
			if (state.r[7] != U64(0x5555555555555555))
			{
				printf("wrong memory check skip1!\n");
			}
			else
			{
				state.r[0] = 0;
			}
		}

		if (state.current_pc == U64(0x8bcb0))
		{
			if (state.r[7] != U64(0x5555555555555555))
			{
				printf("wrong memory check skip2!\n");
			}
			else
			{
				state.r[3] = state.r[4];
			}
		}
	}
#endif
	//--------------------------------------------------------------------------------
	// end of skip memory test section
	//--------------------------------------------------------------------------------

	// Service interrupts
	if (DO_ACTION)
	{
#ifndef ES40_JIT
		// We're actually executing code. Cycle counter should be updated, interrupt and interrupt
		// timer status needs to be checked, and the next instruction should be fetched from the
		// instruction cache.
		// Increase the cycle counter if it is currently enabled.
		_icount_accum++;
		_cc_accum += _cc_per_ins;

		if ((_batch_budget & 31) == 0)
		{
			// Flush accumulated counters to state. state.cc is wall-clock (batch top);
			// _cc_accum only feeds cc_large for the check_state speed-factor feedback.
			state.instruction_count += _icount_accum;
			_icount_accum = 0;
			cc_large += _cc_accum;
			_cc_accum = 0;

			// There are one or more active delayed irq_h interrupts. Go through the 6
			// irq_h timers, decrease them as needed, and set the interrupt if the timer
			// reaches 0. Batch to reduce memory ops.
			if (state.check_timers)
			{
				state.check_timers = false;
				for (int j = 0; j < 6; j++)
				{
					if (state.irq_h_timer[j])
					{
						if (state.irq_h_timer[j] <= 32)
						{
							state.irq_h_timer[j] = 0;
							state.eir |= (U64(0x1) << j);
#ifdef JIT_STATS
							m_stat_kick[4]++;
#endif
							// The timer hasn't reached 0 yet; check on the timers again next clock tick.
							state.check_int = true;
						}
						else
						{
							// The timer has reached 0. Set the interrupt status, and set the flag that we
							// need to check the interrupt status
							state.irq_h_timer[j] -= 32;
							state.check_timers = true;
						}
					}
				}
			}
		}
#else
		// New unwound interpreter / future JIT path

		state.instruction_count++;
		cc_large += _cc_per_ins;
		// state.cc (RPCC) is pinned to wall-clock * cpu_hz at the jit_run boundary, not advanced
		// per-instruction here - interpreter can't run RPCC ahead of wall time.

		// Process delayed irq_h timers one instruction at a time.
		if (state.check_timers)
		{
			state.check_timers = false;
			for (int ti = 0; ti < 6; ti++)
			{
				if (state.irq_h_timer[ti])
				{
					if (state.irq_h_timer[ti] <= 1)
					{
						state.irq_h_timer[ti] = 0;
						state.eir |= (U64(0x1) << ti);
#ifdef JIT_STATS
						m_stat_kick[4]++;
#endif
						state.check_int = true;
					}
					else
					{
						state.irq_h_timer[ti]--;
						state.check_timers = true;
					}
				}
			}
		}
#endif

		if (state.check_int && !(state.pc & 1))
		{
			// Clear the interrupt doorbell up front. A remote irq_h() (e.g. an IPI from
			// another CPU) sets eir then re-raises check_int; clearing first guarantees a
			// cross-thread re-raise is re-checked next tick rather than lost by a late clear.
			state.check_int = false;

			// One or more of the variables that affect interrupt status have changed, and we are not
			// currently inside PALmode. It is not certain that this means we hava an interrupt to
			// service, but we might have. This needs to be checked.


			if (state.pal_vms) {
				// PALcode base is set to 0x8000; meaning OpenVMS PALcode is currently active. In this
				// case, our VMS PALcode replacement routines are valid, and should be used as it is
				// faster than using the original PALcode.

				// irq<4> = halt / MP work request (TIG ev6_halt)
				if (state.eir & state.eien & 0x10)
				{
					GO_PAL(AlphaPALException::Interrupt);
					seq_remaining = 0;
#ifndef ES40_JIT
					goto _next_instruction;
#else
					return;
#endif
				}

				// irq<1>=device, irq<2>=timer, irq<3>=IPI. (Was 0x6 = device+timer only,
				// which stranded incoming IPIs under VMS PALcode -> CPUSPINWAIT.)
				if (state.eir & state.eien & 0xe)
					if (vmspal_ent_ext_int(state.eir & state.eien & 0xe))
						return;

				if (state.sir & state.sien & 0xfffc)
					if (vmspal_ent_sw_int(state.sir & state.sien))
						return;

				if (state.asten && (state.aster & state.astrr & ((1 << (state.cm + 1)) - 1)))
					if (vmspal_ent_ast_int(state.aster & state.astrr & ((1 << (state.cm + 1)) - 1)))
						return;

				if (state.sir & state.sien)
					if (vmspal_ent_sw_int(state.sir & state.sien))
						return;
			}
			else

			{

				// PALcode base is set to an unsupported value. We have no choice but to transfer control
				// to PALmode at the PALcode interrupt entry point.
				//        if (state.eir & 8)
				//        {
				//          printf("%s: IP interrupt received%s...\n",devid_string, (state.eien&8)?"(enabled)":"(masked)");
				//        }
				if ((state.eien & state.eir) || (state.sien & state.sir) || (state.asten
					&& (state.aster & state.astrr & ((1 << (state.cm + 1)) - 1))))
				{
#ifdef JIT_STATS
					m_stat_kick[5]++;
#endif
					GO_PAL(AlphaPALException::Interrupt);
					seq_remaining = 0;
#ifndef ES40_JIT
					goto _next_instruction;
#else
					return;
#endif
				}
#ifdef JIT_STATS
				else m_stat_kick[6]++;   // flag was set but nothing deliverable at the poll
#endif
			}

		}

		// If profiling is enabled, increase the profiling counter for the current block of addresses.
#if defined(PROFILE)
		PROFILE_DO(state.pc);
#endif

#ifndef ES40_JIT
		// ---- Fast sequential icache path ----
				// If PC matches expected sequential address and we have words remaining
				// in the current icache line, read directly without any lookup.
		if (state.pc == seq_next_pc && seq_remaining > 0)
		{
			ins = endian_32(seq_line_ptr[seq_offset]);
#if defined(DEBUG_ARC)
			if (state.current_pc < 0x10000) {
				printf("PC=%016" PRIx64 " ins=%08x\n", state.current_pc, ins);
			}
#endif
			seq_offset++;
			seq_remaining--;
			seq_next_pc += 4;
			//state.pc_phys += 4;
#if defined(IDB)
			current_pc_physical = state.pc_phys;
#endif
		}
		else
		{
			// PAL reset-vector entry: drop stale icache lines (in-place image rewrite)
			if (state.pc == (state.pal_base | 1))
				flush_icache();

			// Full icache lookup
			if (get_icache(state.pc, &ins))
				goto _next_instruction;

			// Set up sequential tracking from the cache hit
			if (icache_enabled && !(state.pc & 1))
			{
				int _siq_line = state.last_found_icache;
				seq_line_ptr = state.icache[_siq_line].data;
				int _siq_word = (int)((state.pc >> 2) & ICACHE_INDEX_MASK);
				seq_offset = _siq_word + 1;
				seq_remaining = ICACHE_LINE_SIZE - seq_offset;
				seq_next_pc = (state.pc & ~U64(0x3)) + 4;
			}
			else
			{
				seq_remaining = 0;
			}

#if defined(IDB)
			current_pc_physical = state.pc_phys;
#endif
		}
#else
		// Fast sequential icache path -- the same fast path the batched interpreter uses above.
		// The JIT lane calls execute() once per instruction, so this cursor persists across calls
		// within an interpreted run. A compiled block can't remap (it runs no PAL/TB ops), and any
		// flush or IMB resets the cursor through break_seq_icache(), so stale lines can't be read.
		// A TB-miss returns to the dispatcher (which re-dispatches at the fault handler).
		if (state.pc == seq_next_pc && seq_remaining > 0)
		{
			ins = endian_32(seq_line_ptr[seq_offset]);
			seq_offset++;
			seq_remaining--;
			seq_next_pc += 4;
		}
		else
		{
			// PAL reset-vector entry: drop stale icache lines (in-place image rewrite)
			if (state.pc == (state.pal_base | 1))
				flush_icache();

			if (get_icache(state.pc, &ins))
				return;

			if (icache_enabled && !(state.pc & 1))
			{
				int _siq_line = state.last_found_icache;
				seq_line_ptr = state.icache[_siq_line].data;
				int _siq_word = (int)((state.pc >> 2) & ICACHE_INDEX_MASK);
				seq_offset = _siq_word + 1;
				seq_remaining = ICACHE_LINE_SIZE - seq_offset;
				seq_next_pc = (state.pc & ~U64(0x3)) + 4;
			}
			else
			{
				seq_remaining = 0;
			}
		}

#if defined(IDB)
		current_pc_physical = state.pc_phys;
#endif

#endif
	}           // if (DO_ACTION)
	else
	{

		// We're not really executing any code (DO_ACTION is false); that means that we're
		// in a debugging session, and just listing instructions at a particular address.
		// In this case, we treat the program counter as a physical address.
		ins = (u32)(cSystem->ReadMem(state.pc, 32, this));
#if defined(DEBUG_ARC)
		if (state.current_pc < 0x10000) {
			printf("PC=%016" PRIx64 " ins=%08x\n", state.current_pc, ins);
		}
#endif
	}

	// Increase the program counter. The current value is retained in state.current_pc.
#if defined(IDB)
	next_pc();
#else
	state.pc += 4;
#endif

	// Clear "always zero" registers. The last instruction might have written something to
	// one of these registers.
	state.r[31] = 0;
	state.f[31] = 0;

	// Decode and dispatch opcode. This is kept very compact using the OP-macro defined in
	// cpu_debug.h. For the normal emulator, this simply calls the DO_<mnemonic> macro defined
	// in one of the other cpu_*.h files; but for the interactive debugger, it will also do
	// disassembly, where the second parameter to the macro (e.g. R12_R3) determines the
	// formatting applied to the operands. The macro ends with "return 0;".
#if defined(IDB)
	last_instruction = ins;
#endif
	opcode = ins >> 26;

	// EV6 retires FP operates (ITFP/FLTV/FLTI/FLTL) into F31 at decode: no FEN
	// or arith trap (21264 HRM 2.5, Table 2-6). MT_FPCR (no dest) and MF_FPCR
	// (dest is Fa) are excluded.
	if (opcode >= 0x14 && opcode <= 0x17 && (ins & 0x1f) == 31
		&& !(opcode == 0x17 && (((ins >> 5) & 0x7fe) == 0x024))
#if defined(IDB)
		&& !bListing
#endif
		)
		ES40_EXECUTE_END();

	switch (opcode)
	{
	case 0x00:  // CALL_PAL
		function = ins & 0x1fffffff;
		OP(CALL_PAL, PAL);

		//    switch (function)
		//    {
		//      case 0x123401: OP_FNC(vmspal_int_read_ide, NOP);
		//      default: OP(CALL_PAL,PAL);
		//    }
	case 0x08:
		OP(LDA, MEM);

	case 0x09:
		OP(LDAH, MEM);

	case 0x0a:
		OP(LDBU, MEM);

	case 0x0b:
		OP(LDQ_U, MEM);

	case 0x0c:
		OP(LDWU, MEM);

	case 0x0d:
		OP(STW, MEM);

	case 0x0e:
		OP(STB, MEM);

	case 0x0f:
		OP(STQ_U, MEM);

	case 0x10:  // INTA* instructions
		function = (ins >> 5) & 0x7f;
		switch (function)
		{
		case 0x40:  OP(ADDL_V, R12_R3);
		case 0x00:  OP(ADDL, R12_R3);
		case 0x02:  OP(S4ADDL, R12_R3);
		case 0x49:  OP(SUBL_V, R12_R3);
		case 0x09:  OP(SUBL, R12_R3);
		case 0x0b:  OP(S4SUBL, R12_R3);
		case 0x0f:  OP(CMPBGE, R12_R3);
		case 0x12:  OP(S8ADDL, R12_R3);
		case 0x1b:  OP(S8SUBL, R12_R3);
		case 0x1d:  OP(CMPULT, R12_R3);
		case 0x60:  OP(ADDQ_V, R12_R3);
		case 0x20:  OP(ADDQ, R12_R3);
		case 0x22:  OP(S4ADDQ, R12_R3);
		case 0x69:  OP(SUBQ_V, R12_R3);
		case 0x29:  OP(SUBQ, R12_R3);
		case 0x2b:  OP(S4SUBQ, R12_R3);
		case 0x2d:  OP(CMPEQ, R12_R3);
		case 0x32:  OP(S8ADDQ, R12_R3);
		case 0x3b:  OP(S8SUBQ, R12_R3);
		case 0x3d:  OP(CMPULE, R12_R3);
		case 0x4d:  OP(CMPLT, R12_R3);
		case 0x6d:  OP(CMPLE, R12_R3);
		default:    UNKNOWN2;
		}
		break;

	case 0x11:  // INTL* instructions
		function = (ins >> 5) & 0x7f;
		switch (function)
		{
		case 0x00:  OP(AND, R12_R3);
		case 0x08:  OP(BIC, R12_R3);
		case 0x14:  OP(CMOVLBS, R12_R3);
		case 0x16:  OP(CMOVLBC, R12_R3);
		case 0x20:  OP(BIS, R12_R3);
		case 0x24:  OP(CMOVEQ, R12_R3);
		case 0x26:  OP(CMOVNE, R12_R3);
		case 0x28:  OP(ORNOT, R12_R3);
		case 0x40:  OP(XOR, R12_R3);
		case 0x44:  OP(CMOVLT, R12_R3);
		case 0x46:  OP(CMOVGE, R12_R3);
		case 0x48:  OP(EQV, R12_R3);
		case 0x61:  OP(AMASK, R2_R3);
		case 0x64:  OP(CMOVLE, R12_R3);
		case 0x66:  OP(CMOVGT, R12_R3);
		case 0x6c:  OP(IMPLVER, X_R3);
		default:    UNKNOWN2;
		}
		break;

	case 0x12:  // INTS* instructions
		function = (ins >> 5) & 0x7f;
		switch (function)
		{
		case 0x02:  OP(MSKBL, R12_R3);
		case 0x06:  OP(EXTBL, R12_R3);
		case 0x0b:  OP(INSBL, R12_R3);
		case 0x12:  OP(MSKWL, R12_R3);
		case 0x16:  OP(EXTWL, R12_R3);
		case 0x1b:  OP(INSWL, R12_R3);
		case 0x22:  OP(MSKLL, R12_R3);
		case 0x26:  OP(EXTLL, R12_R3);
		case 0x2b:  OP(INSLL, R12_R3);
		case 0x30:  OP(ZAP, R12_R3);
		case 0x31:  OP(ZAPNOT, R12_R3);
		case 0x32:  OP(MSKQL, R12_R3);
		case 0x34:  OP(SRL, R12_R3);
		case 0x36:  OP(EXTQL, R12_R3);
		case 0x39:  OP(SLL, R12_R3);
		case 0x3b:  OP(INSQL, R12_R3);
		case 0x3c:  OP(SRA, R12_R3);
		case 0x52:  OP(MSKWH, R12_R3);
		case 0x57:  OP(INSWH, R12_R3);
		case 0x5a:  OP(EXTWH, R12_R3);
		case 0x62:  OP(MSKLH, R12_R3);
		case 0x67:  OP(INSLH, R12_R3);
		case 0x6a:  OP(EXTLH, R12_R3);
		case 0x72:  OP(MSKQH, R12_R3);
		case 0x77:  OP(INSQH, R12_R3);
		case 0x7a:  OP(EXTQH, R12_R3);
		default:    UNKNOWN2;
		}
		break;

	case 0x13:  // INTM* instructions
		function = (ins >> 5) & 0x7f;
		switch (function)  // ignore /V for now
		{
		case 0x40:  OP(MULL_V, R12_R3);
		case 0x00:  OP(MULL, R12_R3);
		case 0x60:  OP(MULQ_V, R12_R3);
		case 0x20:  OP(MULQ, R12_R3);
		case 0x30:  OP(UMULH, R12_R3);
		default:    UNKNOWN2;
		}
		break;

	case 0x14:          // ITFP* instructions
		function = (ins >> 5) & 0x7ff;
		switch (function)
		{
		case 0x004:
			OP(ITOFS, R1_F3);

		case 0x00a:
		case 0x08a:
		case 0x10a:
		case 0x18a:
		case 0x40a:
		case 0x48a:
		case 0x50a:
		case 0x58a:
			OP(SQRTF, F2_F3);

		case 0x00b:
		case 0x04b:
		case 0x08b:
		case 0x0cb:
		case 0x10b:
		case 0x14b:
		case 0x18b:
		case 0x1cb:
		case 0x50b:
		case 0x54b:
		case 0x58b:
		case 0x5cb:
		case 0x70b:
		case 0x74b:
		case 0x78b:
		case 0x7cb:
			OP(SQRTS, F2_F3);

		case 0x014:
			OP(ITOFF, R1_F3);

		case 0x024:
			OP(ITOFT, R1_F3);

		case 0x02a:
		case 0x0aa:
		case 0x12a:
		case 0x1aa:
		case 0x42a:
		case 0x4aa:
		case 0x52a:
		case 0x5aa:
			OP(SQRTG, F2_F3);

		case 0x02b:
		case 0x06b:
		case 0x0ab:
		case 0x0eb:
		case 0x12b:
		case 0x16b:
		case 0x1ab:
		case 0x1eb:
		case 0x52b:
		case 0x56b:
		case 0x5ab:
		case 0x5eb:
		case 0x72b:
		case 0x76b:
		case 0x7ab:
		case 0x7eb:
			OP(SQRTT, F2_F3);

		default:
			UNKNOWN2;
		}
		break;

	case 0x15:          // FLTV* instructions
		function = (ins >> 5) & 0x7ff;
		switch (function)
		{
		case 0x0a5:
		case 0x4a5:
			OP(CMPGEQ, F12_F3);

		case 0x0a6:
		case 0x4a6:
			OP(CMPGLT, F12_F3);

		case 0x0a7:
		case 0x4a7:
			OP(CMPGLE, F12_F3);

		case 0x03c:
		case 0x0bc:
			OP(CVTQF, F2_F3);

		case 0x03e:
		case 0x0be:
			OP(CVTQG, F2_F3);

		default:
			if (function & 0x200)
			{
				UNKNOWN2;
			}

			switch (function & 0x7f)
			{
			case 0x000: OP(ADDF, F12_F3);
			case 0x001: OP(SUBF, F12_F3);
			case 0x002: OP(MULF, F12_F3);
			case 0x003: OP(DIVF, F12_F3);
			case 0x01e: OP(CVTDG, F2_F3);
			case 0x020: OP(ADDG, F12_F3);
			case 0x021: OP(SUBG, F12_F3);
			case 0x022: OP(MULG, F12_F3);
			case 0x023: OP(DIVG, F12_F3);
			case 0x02c: OP(CVTGF, F12_F3);
			case 0x02d: OP(CVTGD, F2_F3);
			case 0x02f: OP(CVTGQ, F2_F3);
			default:    UNKNOWN2;
			}
			break;
		}
		break;

	case 0x16:          // FLTI* instructions
		function = (ins >> 5) & 0x7ff;
		switch (function)
		{
		case 0x0a4:
		case 0x5a4:
			OP(CMPTUN, F12_F3);

		case 0x0a5:
		case 0x5a5:
			OP(CMPTEQ, F12_F3);

		case 0x0a6:
		case 0x5a6:
			OP(CMPTLT, F12_F3);

		case 0x0a7:
		case 0x5a7:
			OP(CMPTLE, F12_F3);

		case 0x2ac:
		case 0x6ac:
			OP(CVTST, F2_F3);

		default:
			if (((function & 0x600) == 0x200) || ((function & 0x500) == 0x400))
			{
				UNKNOWN2;
			}

			switch (function & 0x3f)
			{
			case 0x00:  OP(ADDS, F12_F3);
			case 0x01:  OP(SUBS, F12_F3);
			case 0x02:  OP(MULS, F12_F3);
			case 0x03:  OP(DIVS, F12_F3);
			case 0x20:  OP(ADDT, F12_F3);
			case 0x21:  OP(SUBT, F12_F3);
			case 0x22:  OP(MULT, F12_F3);
			case 0x23:  OP(DIVT, F12_F3);
			case 0x2c:  OP(CVTTS, F2_F3);
			case 0x2f:  OP(CVTTQ, F2_F3);
			case 0x3c:  if ((function & 0x300) == 0x100) { UNKNOWN2; }OP(CVTQS, F2_F3);
			case 0x3e:  if ((function & 0x300) == 0x100) { UNKNOWN2; }OP(CVTQT, F2_F3);
			default:    UNKNOWN2;
			}
			break;
		}
		break;

	case 0x17:          // FLTL* instructions
		function = (ins >> 5) & 0x7ff;
		switch (function)
		{
		case 0x010:
			OP(CVTLQ, F2_F3);

		case 0x020:
			OP(CPYS, F12_F3);

		case 0x021:
			OP(CPYSN, F12_F3);

		case 0x022:
			OP(CPYSE, F12_F3);

		case 0x024:
			OP(MT_FPCR, X_F1);

		case 0x025:
			OP(MF_FPCR, X_F1);

		case 0x02a:
			OP(FCMOVEQ, F12_F3);

		case 0x02b:
			OP(FCMOVNE, F12_F3);

		case 0x02c:
			OP(FCMOVLT, F12_F3);

		case 0x02d:
			OP(FCMOVGE, F12_F3);

		case 0x02e:
			OP(FCMOVLE, F12_F3);

		case 0x02f:
			OP(FCMOVGT, F12_F3);

		case 0x030:
		case 0x130:
		case 0x530:
			OP(CVTQL, F12_F3);

		default:
			UNKNOWN2;
		}
		break;

	case 0x18:          // MISC* instructions
		function = (ins & 0xffff);
		switch (function)
		{
		case 0x0000:  OP(TRAPB, NOP);
		case 0x0400:  OP(EXCB, NOP);
		case 0x4000:  OP(MB, NOP);
		case 0x4400:  OP(WMB, NOP);
		case 0x4800:  OP(IMB, NOP);
		case 0x8000:  OP(FETCH, NOP);
		case 0xA000:  OP(FETCH_M, NOP);
		case 0xC000:  OP(RPCC, X_R1);
		case 0xE000:  OP(RC, X_R1);
		case 0xE800:  OP(ECB, NOP);
		case 0xF000:  OP(RS, X_R1);
		case 0xF800:  OP(WH64, NOP);
		case 0xFC00:  OP(WH64EN, NOP);
		default:      UNKNOWN2;
		}
		break;

	case 0x19:          // HW_MFPR (PALRES)
		// The family decides access; rejection uses the shared OPCDEC entry path.
		if (!pal_hardware_instruction_valid(ins)) {
			GO_PAL(AlphaPALException::OpcodeDecode);
			ES40_EXECUTE_END();
		}
		function = (ins >> 8) & 0xff;
		OP(HW_MFPR, MFPR);

	case 0x1a:          // JSR* instructions
		OP(JMP, JMP);

	case 0x1b:          // PAL reserved - HW_LD (PALRES)
		if (!pal_hardware_instruction_valid(ins)) {
			GO_PAL(AlphaPALException::OpcodeDecode);
			ES40_EXECUTE_END();
		}
		function = (ins >> 12) & 0xf;
		if (function & 1)
		{
			OP(HW_LDQ, HW_LD);
		}
		else
		{
			OP(HW_LDL, HW_LD);
		}

	case 0x1c:          // FPTI* instructions
		function = (ins >> 5) & 0x7f;
		switch (function)
		{
		case 0x00:  OP(SEXTB, R2_R3);
		case 0x01:  OP(SEXTW, R2_R3);
		case 0x30:  OP(CTPOP, R2_R3);
		case 0x31:  OP(PERR, R2_R3);
		case 0x32:  OP(CTLZ, R2_R3);
		case 0x33:  OP(CTTZ, R2_R3);
		case 0x34:  OP(UNPKBW, R2_R3);
		case 0x35:  OP(UNPKBL, R2_R3);
		case 0x36:  OP(PKWB, R2_R3);
		case 0x37:  OP(PKLB, R2_R3);
		case 0x38:  OP(MINSB8, R12_R3);
		case 0x39:  OP(MINSW4, R12_R3);
		case 0x3a:  OP(MINUB8, R12_R3);
		case 0x3b:  OP(MINUW4, R12_R3);
		case 0x3c:  OP(MAXUB8, R12_R3);
		case 0x3d:  OP(MAXUW4, R12_R3);
		case 0x3e:  OP(MAXSB8, R12_R3);
		case 0x3f:  OP(MAXSW4, R12_R3);
		case 0x70:  OP(FTOIT, F1_R3);
		case 0x78:  OP(FTOIS, F1_R3);
		default:    UNKNOWN2;
		}
		break;

	case 0x1d:          // HW_MTPR (PALRES)
		if (!pal_hardware_instruction_valid(ins)) {
			GO_PAL(AlphaPALException::OpcodeDecode);
			ES40_EXECUTE_END();
		}
		function = (ins >> 8) & 0xff;
		OP(HW_MTPR, MTPR);

	case 0x1e:          // HW_RET (PALRES)
		if (!pal_hardware_instruction_valid(ins)) {
			GO_PAL(AlphaPALException::OpcodeDecode);
			ES40_EXECUTE_END();
		}
		OP(HW_RET, RET);

	case 0x1f:          // HW_ST (PALRES)
		if (!pal_hardware_instruction_valid(ins)) {
			GO_PAL(AlphaPALException::OpcodeDecode);
			ES40_EXECUTE_END();
		}
		function = (ins >> 12) & 0xf;
		if (function & 1)
		{
			OP(HW_STQ, HW_ST);
		}
		else
		{
			OP(HW_STL, HW_ST);
		}

	case 0x20:
		OP(LDF, FMEM);

	case 0x21:
		OP(LDG, FMEM);

	case 0x22:
		OP(LDS, FMEM);

	case 0x23:
		OP(LDT, FMEM);

	case 0x24:
		OP(STF, FMEM);

	case 0x25:
		OP(STG, FMEM);

	case 0x26:
		OP(STS, FMEM);

	case 0x27:
		OP(STT, FMEM);

	case 0x28:
		OP(LDL, MEM);

	case 0x29:
		OP(LDQ, MEM);

	case 0x2a:
		OP(LDL_L, MEM);

	case 0x2b:
		OP(LDQ_L, MEM);

	case 0x2c:
		OP(STL, MEM);

	case 0x2d:
		OP(STQ, MEM);

	case 0x2e:
		OP(STL_C, MEM);

	case 0x2f:
		OP(STQ_C, MEM);

	case 0x30:
		OP(BR, BR);

	case 0x31:
		OP(FBEQ, FCOND);

	case 0x32:
		OP(FBLT, FCOND);

	case 0x33:
		OP(FBLE, FCOND);

	case 0x34:
		OP(BSR, BSR);

	case 0x35:
		OP(FBNE, FCOND);

	case 0x36:
		OP(FBGE, FCOND);

	case 0x37:
		OP(FBGT, FCOND);

	case 0x38:
		OP(BLBC, COND);

	case 0x39:
		OP(BEQ, COND);

	case 0x3a:
		OP(BLT, COND);

	case 0x3b:
		OP(BLE, COND);

	case 0x3c:
		OP(BLBS, COND);

	case 0x3d:
		OP(BNE, COND);

	case 0x3e:
		OP(BGE, COND);

	case 0x3f:
		OP(BGT, COND);

	default:
		UNKNOWN1;
	}

#ifndef ES40_JIT
	goto _next_instruction;
#else
	return;
#endif
}

#if defined(IDB)

/**
 * \brief Produce disassembly-listing without marker
 *
 * \param from    Address of first instruction to be disassembled.
 * \param to      Address of instruction following the last instruction to
 *                be disassembled.
 **/
void CAlphaCPU::listing(u64 from, u64 to)
{
	listing(from, to, 0);
}

/**
 * \brief Produce disassembly-listing with marker
 *
 * \param from    Address of first instruction to be disassembled.
 * \param to      Address of instruction following the last instruction to
 *                be disassembled.
 * \param mark    Address of instruction to be underlined with a marker line.
 **/
void CAlphaCPU::listing(u64 from, u64 to, u64 mark)
{
	printf("%%CPU-I-LISTNG: Listing from %016" PRIx64 " to %016" PRIx64 "\n", from, to);

	u64   iSavedPC;
	bool  bSavedDebug;
	iSavedPC = state.pc;
	bSavedDebug = bDisassemble;
	bDisassemble = true;
	bListing = true;
	for (state.pc = from; state.pc <= to;)
	{
		execute();
		if (state.pc == mark)
			printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
	}

	bListing = false;
	state.pc = iSavedPC;
	bDisassemble = bSavedDebug;
}

u64 CAlphaCPU::get_instruction_count()
{
	return state.instruction_count;
}
#endif
static u32  cpu_magic1 = 0x2126468C;
static u32  cpu_magic2 = 0xC8646212;

/**
 * Save state to a Virtual Machine State file.
 **/
int CAlphaCPU::SaveState(FILE* f)
{
	long  ss = sizeof(state);

	fwrite(&cpu_magic1, sizeof(u32), 1, f);
	fwrite(&ss, sizeof(long), 1, f);
	fwrite(&state, sizeof(state), 1, f);
	fwrite(&cpu_magic2, sizeof(u32), 1, f);
	printf("%s: %d bytes saved.\n", devid_string, (int)ss);
	return 0;
}

/**
 * Restore state from a Virtual Machine State file.
 **/
int CAlphaCPU::RestoreState(FILE* f)
{
	long    ss;
	u32     m1;
	u32     m2;
	size_t  r;

	r = fread(&m1, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m1 != cpu_magic1)
	{
		printf("%s: MAGIC 1 does not match!\n", devid_string);
		return -1;
	}

	r = fread(&ss, sizeof(long), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (ss != sizeof(state))
	{
		printf("%s: STRUCT SIZE does not match!\n", devid_string);
		return -1;
	}

	r = fread(&state, sizeof(state), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	r = fread(&m2, sizeof(u32), 1, f);
	if (r != 1)
	{
		printf("%s: unexpected end of file!\n", devid_string);
		return -1;
	}

	if (m2 != cpu_magic2)
	{
		printf("%s: MAGIC 1 does not match!\n", devid_string);
		return -1;
	}

	printf("%s: %d bytes restored.\n", devid_string, (int)ss);
	last_dtb_virt[0] = last_dtb_virt[1] = 0;
	// RAM and TB state now belong to the restored state. Restored icache stays. Rest gets chucked.
	flush_data_page_cache();
	break_seq_icache();
#ifdef ES40_JIT
	m_link_from = nullptr;      // pending link-patch request into pre-restore code
	if (m_jit) m_jit->flush();  // epoch bump: blocks/traces re-hash against restored RAM before running
#endif
	// The floor/debt state is intentionally not part of the legacy save-state
	// format.  Rebase it to the restored architectural counter and a fresh host
	// epoch so a restore cannot create a false jump or bill paused wall time.
	cc_last_read = state.cc;
	cc_borrow = 0;
	cc_wall_remainder = 0;
	cc_last_sync = std::chrono::steady_clock::now();

	return 0;
}

/***************************************************************************/

/**
 * \name TB
 * Translation Buffer related functions
 ******************************************************************************/

 //\{

 /**
  * \brief Find translation-buffer entry
  *
  * Try to find a translation-buffer entry that maps the page inside which
  * the specified virtual address lies.
  *
  * \param virt    Virtual address to find in translation buffer.
  * \param flags   ACCESS_EXEC determines which translation buffer to use.
  * \return        Number of matching entry, or -1 if no match found.
  **/
int CAlphaCPU::FindTBEntry(u64 virt, int flags)
{

	// Use ITB (tb[1]) if ACCESS_EXEC is set, otherwise the unified Dstream TB (tb[0]).
	int t = (flags & ACCESS_EXEC) ? TB_INDEX_ITB : TB_INDEX_DATA;
	int asn = state.asn;

	int rw = (flags & ACCESS_WRITE) ? 1 : 0;

#define TB_ASN_MATCH(entry) ((entry).asm_bit || \
	((entry).asn == ((t == TB_INDEX_ITB) ? asn : state.asn0)))

	// Page-indexed hint first, then the last match, then the linear scan (which refills the hint).
	const u32 h = (u32)(virt >> 13) & 255;
	int i = (int)m_tb_idx[t][h] - 1;
	if (i >= 0 && state.tb[t][i].valid
		&& !((state.tb[t][i].virt ^ virt) & state.tb[t][i].match_mask)
		&& TB_ASN_MATCH(state.tb[t][i]))	return i;

	i = state.last_found_tb[t][rw];
	if (state.tb[t][i].valid
		&& !((state.tb[t][i].virt ^ virt) & state.tb[t][i].match_mask)
		&& TB_ASN_MATCH(state.tb[t][i]))	{ m_tb_idx[t][h] = (u16)(i + 1); return i; }

	for (i = 0; i < TB_ENTRIES; i++)
	{
		if (state.tb[t][i].valid
			&& !((state.tb[t][i].virt ^ virt) & state.tb[t][i].match_mask)
			&& TB_ASN_MATCH(state.tb[t][i]))
		{
			state.last_found_tb[t][rw] = i;
			m_tb_idx[t][h] = (u16)(i + 1);
			return i;
		}
	}

#undef TB_ASN_MATCH
	return -1;
}

static inline u64 alpha_sext_u64_43(u64 a)
{
	return (a & U64(0x0000040000000000)) ? (a | U64(0xfffff80000000000))
		: (a & U64(0x000007ffffffffff));
}

static inline bool alpha_valid_va_form(u64 virt, bool va48)
{
	return (va48 ? sext_u64_48(virt) : alpha_sext_u64_43(virt)) == virt;
}

int CAlphaCPU::initiate_acv_fault(u64 virt, int flags, u32 ins)
{
	int res;

	state.exc_addr = state.current_pc;

	if (flags & ACCESS_EXEC)
	{
		state.exc_sum = 0;
		if (state.pal_vms)
		{
			res = vmspal_ent_iacv(flags);
			return res ? res : -1;
		}

		set_pc(state.pal_base + pal_exception_offset(AlphaPALException::InstructionAccessViolation) + 1);
		return -1;
	}

	state.fault_va = virt;
	state.va_form_va = virt;
	state.exc_sum = ((u64)REG_1 & 0x1f) << 8;  /* HRM 5.2.13: EXC_SUM REG[12:8] is 5 bits */

	u32 opcode = I_GETOP(ins);
	state.mm_stat =
		(
			(opcode == 0x1b || opcode == 0x1f) ? opcode -
			0x18 : opcode
			) <<
		4 |
		(flags & ACCESS_WRITE) |
		2;
	if (state.pal_vms)
	{
		res = vmspal_ent_dfault(flags);
		return res ? res : -1;
	}

	set_pc(state.pal_base + pal_exception_offset(AlphaPALException::DataFault) + 1);
	return -1;
}

/**
 * \brief Translate a virtual address to a physical address.
 *
 * Translate a 64-bit virtual address into a 64-bit physical address, using
 * the page table buffers.
 *
 * The following steps are taken to resolve the address:
 *  - See if the address can be found in the translation buffer.
 *  - If not, try to load the right page table entry into the translation
 *    buffer, if this is not possible, trap to the OS.
 *  - Check access privileges.
 *  - Check fault bits.
 *  .
 *
 * \param virt    Virtual address to be translated.
 * \param phys    Pointer to where the physical address is to be returned.
 * \param flags   Set of flags that determine the exact functioning of the
 *                function. A combination of the following flags:
 *                  - ACCESS_READ   Data-read-access.
 *                  - ACCESS_WRITE  Data-write-access.
 *                  - ACCESS_EXEC   Code-read-access.
 *                  - NO_CHECK      Do not perform access checks.
 *                  - VPTE          VPTE access; if this misses, it's a double miss.
 *                  - FAKE          Access is not initiated by executing code, but by
 *                                  the debugger. If a translation can't be found
 *                                  through the translation buffer, don't bother.
 *                  - ALT           Use alt_cm for access checks instead of cm.
 *                  - RECUR         Recursive try. We tried to find this address
 *                                  before, added a TB entry, and now it should sail
 *                                  through.
 *                  - PROBE         Access is for a PROBER or PROBEW access; Don't
 *                                  swap in the page if it is outswapped.
 *                  - PROBEW        Access is for a PROBEW access.
 *                  .
 * \param asm_bit Status of the ASM (address space match) bit in the page-table-entry.
 * \param ins     Instruction currently being executed. Important for the correct
 *                handling of traps.
 *
 * \return        0 on success, -1 if address could not be converted without
 *                help (in this case state.pc contains the address of the
 *                next instruction to execute (PALcode or OS entry point).
 **/
int CAlphaCPU::virt2phys(u64 virt, u64* phys, int flags, bool* asm_bit, u32 ins)
{
	int   t = (flags & ACCESS_EXEC) ? TB_INDEX_ITB : TB_INDEX_DATA;
	int   i;
	int   res;

	int   spe = (flags & ACCESS_EXEC) ? state.i_ctl_spe : state.m_ctl_spe;
	/*
	 * Access-check current mode selection.
	 *  - VPTE (HRM 6.4.1 Table 6-3): Virtual/VPTE accesses are LD_VPTE
	 *    page-table fetches and use *kernel mode* for permission checks
	 *    regardless of executing CM.
	 *  - ALT (HRM 6.4.1 Table 6-3 row 1102, Table 6-4 row 1102): the
	 *    /alt variants use DTB_ALT_MODE for permission checks.
	 *  - Otherwise: current mode.
	 * Order matters: VPTE wins over ALT (VPTE is never combined with ALT in
	 * any valid HW_LD encoding, but the precedence is clear).
	 */
	int   cm = (flags & VPTE) ? 0 :
		(flags & ALT) ? state.alt_cm :
		state.cm;
	bool  forreal = !(flags & FAKE);
	bool  va48 = (flags & ACCESS_EXEC) ? (state.i_ctl_va_mode & 1)
		: (state.va_ctl_va_mode & 1);

#if defined IDB
	if (bListing)
	{
		*phys = virt;
		return 0;
	}
#endif
#if defined(DEBUG_TB)
	if (forreal)
#if defined(IDB)
		if (bTB_Debug)
#endif
			printf("TB %" PRIx64 ",%x: ", virt, flags);
#endif

	if (!alpha_valid_va_form(virt, va48))
	{
#if defined(DEBUG_TB)
		if (forreal)
#if defined(IDB)
			if (bTB_Debug)
#endif
				printf("acv-va-form\n");
#endif
		if (!forreal)
			return -1;
		return initiate_acv_fault(virt, flags, ins);
	}

	// try superpage first.
	if (spe)
	{
		bool spe_hit = false;
		u64 spe_phys = 0;

#if defined(DEBUG_TB)
		if (forreal)
#if defined(IDB)
			if (bTB_Debug)
#endif
				printf("try spe...");
#endif

		// HRM 5.3.9: SPE[2], when set, enables superpage mapping when VA[47:46] = 2.
		// In this mode, VA[43:13] are mapped directly to PA[43:13] and VA[45:44] are
		// ignored.
		if (((virt & SPE_2_MASK) == SPE_2_MATCH) && (spe & 4))
		{
			spe_hit = true;
			spe_phys = virt & SPE_2_MAP;
		}

		// SPE[1], when set, enables superpage mapping when VA[47:41] = 7E. In
		// this mode, VA[40:13] are mapped directly to PA[40:13] and PA[43:41] are
		// copies of PA[40] (sign extension).
		else if (((virt & SPE_1_MASK) == SPE_1_MATCH) && (spe & 2))
		{
			spe_hit = true;
			spe_phys = (virt & SPE_1_MAP) | ((virt & SPE_1_TEST) ? SPE_1_ADD : 0);
		}

		// SPE[0], when set, enables superpage mapping when VA[47:30] = 3FFFE.
		// In this mode, VA[29:13] are mapped directly to PA[29:13] and PA[43:30] are
		// cleared.
		else if (((virt & SPE_0_MASK) == SPE_0_MATCH) && (spe & 1))
		{
			spe_hit = true;
			spe_phys = virt & SPE_0_MAP;
		}

		if (spe_hit)
		{
			if (cm)
			{
#if defined(DEBUG_TB)
				if (forreal)
#if defined(IDB)
					if (bTB_Debug)
#endif
						printf("SPE-ACV\n");
#endif
				if (!forreal)
					return -1;
				return initiate_acv_fault(virt, flags, ins);
			}

			*phys = spe_phys;
			// Superpage translations are ASN-independent; treat them as ASM.
			if (asm_bit)
				*asm_bit = true;
#if defined(DEBUG_TB)
			if (forreal)
#if defined(IDB)
				if (bTB_Debug)
#endif
					printf("SPE\n");
#endif
			return 0;
		}
	}

	// try to find it in the translation buffer
	i = FindTBEntry(virt, flags);

	if (i < 0)       // not found, either trap to PALcode, or try to load the TB entry and try again.
	{
		if (!forreal)  // debugger-lookup of the address
			return -1;  // report failure, and don't look any further
		if (!state.pal_vms)  // unknown PALcode
		{

			// transfer execution to PALcode
			state.exc_addr = state.current_pc;
			if (flags & VPTE)
			{
				// HRM 5.1.3: VA is NOT written for LD_VPTE misses
				state.va_form_va = virt;
				state.exc_sum = ((u64)REG_1 & 0x1f) << 8;  /* HRM 5.2.13: EXC_SUM REG[12:8] is 5 bits */
				/*
				 * I_CTL[VA_48] selects the DTB double-miss PAL entry.
				 * state.i_ctl_va_mode packs bits [16:15] of I_CTL, so bit 0
				 * here is the architectural VA_48 bit.
				 */
#ifdef JIT_STATS
				m_stat_tb[1]++;
#endif
				const auto reason = (state.i_ctl_va_mode & 1)
					? AlphaPALException::DataTlbDoubleMiss48 : AlphaPALException::DataTlbDoubleMiss43;
				set_pc(state.pal_base + pal_exception_offset(reason) + 1);
			}
			else if (flags & ACCESS_EXEC)
			{
#ifdef JIT_STATS
				m_stat_tb[2]++;
#endif
				set_pc(state.pal_base + pal_exception_offset(AlphaPALException::InstructionTlbMiss) + 1);
			}
			else
			{
				state.fault_va = virt;
				state.va_form_va = virt;
				state.exc_sum = ((u64)REG_1 & 0x1f) << 8;  /* HRM 5.2.13: EXC_SUM REG[12:8] is 5 bits */

				u32 opcode = I_GETOP(ins);
				state.mm_stat =
					(
						(opcode == 0x1b || opcode == 0x1f) ? opcode -
						0x18 : opcode
						) <<
					4 |
					(flags & ACCESS_WRITE);
#ifdef JIT_STATS
				m_stat_tb[0]++;
				{
					const int h = (int)m_tb_idx[t][(virt >> 13) & 255] - 1;
					if (h >= 0) m_stat_tb[state.tb[t][h].valid ? 9 : 8]++;
					const u64 pg = virt >> 13; const u32 sl = (u32)(pg * 0x9E3779B1u) >> 25;
					if (m_stat_va[sl][0] != pg) { m_stat_va[sl][0] = pg; m_stat_va[sl][1] = 0; }
					m_stat_va[sl][1]++;
				}
#endif
				set_pc(state.pal_base + pal_exception_offset(AlphaPALException::DataTlbMiss) + 1);
			}

			return -1;
		}
		else  // VMS PALcode
		{
			if (flags & RECUR) // we already tried this
			{
				printf("Translationbuffer RECUR lookup failed!\n");
				return -1;
			}

			state.exc_addr = state.current_pc;
			if (flags & VPTE)
			{

				// try to handle the double miss. If this needs to transfer control
				// to the OS, it will return non-zero value.
				if ((res = vmspal_ent_dtbm_double_3(flags)))
					return res;

				// Double miss succesfully handled. Try to get the physical address again.
				return virt2phys(virt, phys, flags | RECUR, asm_bit, ins);
			}
			else if (flags & ACCESS_EXEC)
			{

				// try to handle the ITB miss. If this needs to transfer control
				// to the OS, it will return non-zero value.
				if ((res = vmspal_ent_itbm(flags)))
					return res;

				// ITB miss succesfully handled. Try to get the physical address again.
				return virt2phys(virt, phys, flags | RECUR, asm_bit, ins);
			}
			else
			{
				state.fault_va = virt;
				state.exc_sum = ((u64)REG_1 & 0x1f) << 8;  /* HRM 5.2.13: EXC_SUM REG[12:8] is 5 bits */

				u32 opcode = I_GETOP(ins);
				state.mm_stat =
					(
						(opcode == 0x1b || opcode == 0x1f) ? opcode -
						0x18 : opcode
						) <<
					4 |
					(flags & ACCESS_WRITE);

				// try to handle the single miss. If this needs to transfer control
				// to the OS, it will return non-zero value.
				if ((res = vmspal_ent_dtbm_single(flags)))
					return res;

				// Single miss succesfully handled. Try to get the physical address again.
				return virt2phys(virt, phys, flags | RECUR, asm_bit, ins);
			}
		}
	}

	// If we get here, the number of the matching TB entry is in i.
#if defined(DEBUG_TB)
	else
	{
		if (forreal)
#if defined(IDB)
			if (bTB_Debug)
#endif
				printf("entry %d - ", i);
	}
#endif
	if (!(flags & NO_CHECK))
	{

		// check if requested access is allowed.
		// WRCHK (HRM 6.4.1: HW_LD type 1012/1112 WrChk variants) requires that
		// BOTH read and write protection pass for the access mode -- fail if
		// the natural-direction access bit is clear OR (when WRCHK is set on a
		// read) the write access bit is also clear.
		if (!state.tb[t][i].access[flags & ACCESS_WRITE][cm]
			|| ((flags & WRCHK) && !state.tb[t][i].access[1][cm]))
		{
#if defined(DEBUG_TB)
			if (forreal)
#if defined(IDB)
				if (bTB_Debug)
#endif
					printf("acv\n");
#endif
			if (!forreal) // FAKE probe: report failure, don't vector the fault — caller re-runs it under the interpreter
				return -1;
			if (flags & ACCESS_EXEC)
			{

				// handle I-stream access violation
				state.exc_addr = state.current_pc;
				state.exc_sum = 0;
				if (state.pal_vms)
				{
					if ((res = vmspal_ent_iacv(flags)))
						return res;
				}
				else
				{
					set_pc(state.pal_base + pal_exception_offset(AlphaPALException::InstructionAccessViolation) + 1);
					return -1;
				}
			}
			else
			{

				// Handle D-stream access violation
				state.exc_addr = state.current_pc;
				state.fault_va = virt;
				/* HRM 5.1.5/D.24: VA_FORM is derived from VA, which is written
				   on every D-stream fault (incl. DFAULT), not just TB miss.
				   Keep the VA_FORM snapshot current so the OS fault handler
				   computes the right PTE. */
				state.va_form_va = virt;
				state.exc_sum = ((u64)REG_1 & 0x1f) << 8;  /* HRM 5.2.13: EXC_SUM REG[12:8] is 5 bits */

				u32 opcode = I_GETOP(ins);
				state.mm_stat =
					(
						(opcode == 0x1b || opcode == 0x1f) ? opcode -
						0x18 : opcode
						) <<
					4 |
					(flags & ACCESS_WRITE) |
					2;
				if (state.pal_vms)
				{
					if ((res = vmspal_ent_dfault(flags)))
						return res;
				}
				else
				{
#ifdef JIT_STATS
					m_stat_tb[3]++;
#endif
					set_pc(state.pal_base + pal_exception_offset(AlphaPALException::DataFault) + 1);
					return -1;
				}
			}
		}

		// check if requested access doesn't fault.
		// WRCHK additionally requires that the FOW bit be clear -- HRM 6.4.1
		// WrChk variants check both FOR and FOW.
		if (state.tb[t][i].fault[flags & ACCESS_MODE]
			|| ((flags & WRCHK) && state.tb[t][i].fault[1]))
		{
#if defined(DEBUG_TB)
			if (forreal)
#if defined(IDB)
				if (bTB_Debug)
#endif
					printf("fault\n");
#endif
			if (!forreal) // FAKE probe: report failure, don't vector the fault — caller re-runs it under the interpreter
				return -1;
			if (flags & ACCESS_EXEC)
			{

				// handle I-stream access fault
				state.exc_addr = state.current_pc;
				state.exc_sum = 0;
				if (state.pal_vms)
				{
					if ((res = vmspal_ent_iacv(flags)))
						return res;
				}
				else
				{
					set_pc(state.pal_base + pal_exception_offset(AlphaPALException::InstructionAccessViolation) + 1);
					return -1;
				}
			}
			else
			{

				// handle D-stream access fault
				state.exc_addr = state.current_pc;
				state.fault_va = virt;
				/* HRM 5.1.5/D.24: VA_FORM is derived from VA, which is written
				   on every D-stream fault (including FOR/FOW DFAULT), not just TB
				   miss. Keep the VA_FORM snapshot current so the OS fault
				   handler computes the right PTE address. */
				state.va_form_va = virt;
				state.exc_sum = ((u64)REG_1 & 0x1f) << 8;  /* HRM 5.2.13: EXC_SUM REG[12:8] is 5 bits */

				/* HRM 5.3.8 MM_STAT: FOR [bit 2] is set when a fault-on-read
				 * error occurs during a read transaction with PTE[FOR] set.
				 * FOW [bit 3] is set when a fault-on-write error occurs.
				 * For HW_LD WrChk variants the chip is performing both a
				 * read AND a write-protection check, so PTE[FOW] (if set)
				 * is reportable too
				 */
				u32 opcode = I_GETOP(ins);
				int for_bit =
					((flags & ACCESS_WRITE) == 0)
					&& state.tb[t][i].fault[0] ? 4 : 0;
				int fow_bit =
					(((flags & ACCESS_WRITE) || (flags & WRCHK))
						&& state.tb[t][i].fault[1]) ? 8 : 0;
				state.mm_stat =
					(
						(opcode == 0x1b || opcode == 0x1f) ? opcode -
						0x18 : opcode
						) <<
					4 |
					(flags & ACCESS_WRITE) |
					for_bit | fow_bit;
				if (state.pal_vms)
				{
					if ((res = vmspal_ent_dfault(flags)))
						return res;
				}
				else
				{
#ifdef JIT_STATS
					m_stat_tb[3]++;
#endif
					set_pc(state.pal_base + pal_exception_offset(AlphaPALException::DataFault) + 1);
					return -1;
				}
			}
		}
	}

	// No access violations or faults
	// Return the converted address
	*phys = state.tb[t][i].phys | (virt & state.tb[t][i].keep_mask);
	if (asm_bit)
		*asm_bit = state.tb[t][i].asm_bit ? true : false;

#if defined(DEBUG_TB)
	if (forreal)
#if defined(IDB)
		if (bTB_Debug)
#endif
			printf("phys: %" PRIx64 " - OK\n", *phys);
#endif
	return 0;
}

 /**
  * \brief Add translation-buffer entry
  *
  * Add a translation-buffer entry to one of the translation buffers.
  *
  * \param virt    Virtual address.
  * \param pte_phys Physical-address input supplied by the family fill caller.
  * \param pte_flags Family-specific fill attributes.
  * \param flags   ACCESS_EXEC determines which translation buffer to use.
  * \param asn     Address space number latched by the PAL fill port.
  **/
void CAlphaCPU::add_tb(u64 virt, u64 pte_phys, u64 pte_flags, int flags, int asn)
{
	int t = (flags & ACCESS_EXEC) ? TB_INDEX_ITB : TB_INDEX_DATA;
	int rw = (flags & ACCESS_WRITE) ? 1 : 0;
	const auto fill = m_profile.tb_fill.decode(*this, virt, pte_phys, pte_flags, flags);
	int i;

	// Refill of a mapped page: the page-indexed hint finds it; otherwise take a fresh entry. A
	// stale duplicate left behind by a hint collision is harmless: lookups prefer the hint, and
	// every invalidate scans the whole buffer.
	i = -1;
	{
		const int h = (int)m_tb_idx[t][(fill.virt >> 13) & 255] - 1;
		if (h >= 0 && state.tb[t][h].valid
			&& !((state.tb[t][h].virt ^ fill.virt) & state.tb[t][h].match_mask)
			&& (state.tb[t][h].asm_bit || state.tb[t][h].asn == asn))
			i = h;
	}

#ifdef ES40_JIT
	// A same-(virt,asn) ITB overwrite with a DIFFERENT physical is a code-page remap performed
	// WITHOUT a separate TBIS; chained JIT blocks compiled from the old mapping must re-validate, 
	// so bump the generation. A next_tb eviction (i reassigned below) replaces a DIFFERENT vpage 
	// (not a remap of this one) and must NOT bump.
	const bool itb_remap = (t == TB_INDEX_ITB) && (i >= 0)
		&& (state.tb[t][i].phys != fill.phys);
#endif

	if (i < 0)
	{
		i = state.next_tb[t];
		state.next_tb[t]++;
		if (state.next_tb[t] == TB_ENTRIES)
			state.next_tb[t] = 0;
	}

	if (t == TB_INDEX_DATA)
	{
		// Invalidate both the entry being replaced and the mapping being installed. 
		if (state.tb[t][i].valid)
			invalidate_data_page_cache(state.tb[t][i].virt, state.tb[t][i].match_mask);
		invalidate_data_page_cache(fill.virt, fill.match_mask);
	}

	state.tb[t][i].match_mask = fill.match_mask;
	state.tb[t][i].keep_mask = fill.keep_mask;
	state.tb[t][i].virt = fill.virt & fill.match_mask;
	state.tb[t][i].phys = fill.phys;
	memcpy(state.tb[t][i].fault, fill.fault, sizeof(fill.fault));
	memcpy(state.tb[t][i].access, fill.access, sizeof(fill.access));
	state.tb[t][i].asm_bit = fill.asm_bit;
	state.tb[t][i].asn = asn;
	state.tb[t][i].valid = true;
#ifdef JIT_STATS
	if (t == TB_INDEX_DATA) m_stat_tb[7]++;
#endif
	state.last_found_tb[t][rw] = i;
	m_tb_idx[t][(fill.virt >> 13) & 255] = (u16)(i + 1);

#ifdef ES40_JIT
	if (itb_remap && m_jit) m_jit->note_itb_invalidate(CJitEngine::kBumpRemap);   // code page remapped in place -> chains re-validate
#endif

#if defined(DEBUG_TB_)
#if defined(IDB)
	if (bTB_Debug)
#endif
	{
		printf("Add TB---------------------------------------\n");
		printf("Map VIRT    %016" PRIx64 "\n", state.tb[t][i].virt);
		printf("Matching    %016" PRIx64 "\n", state.tb[t][i].match_mask);
		printf("And keeping %016" PRIx64 "\n", state.tb[t][i].keep_mask);
		printf("To PHYS     %016" PRIx64 "\n", state.tb[t][i].phys);
		printf("Read : %c%c%c%c %c\n", state.tb[t][i].access[0][0] ? 'K' : '-',
			state.tb[t][i].access[0][1] ? 'E' : '-',
			state.tb[t][i].access[0][2] ? 'S' : '-',
			state.tb[t][i].access[0][3] ? 'U' : '-', state.tb[t][i].fault[0] ? 'F' : '-');
		printf("Write: %c%c%c%c %c\n", state.tb[t][i].access[1][0] ? 'K' : '-',
			state.tb[t][i].access[1][1] ? 'E' : '-',
			state.tb[t][i].access[1][2] ? 'S' : '-',
			state.tb[t][i].access[1][3] ? 'U' : '-', state.tb[t][i].fault[1] ? 'F' : '-');
		printf("Exec : %c%c%c%c %c\n", state.tb[t][i].access[1][0] ? 'K' : '-',
			state.tb[t][i].access[1][1] ? 'E' : '-',
			state.tb[t][i].access[1][2] ? 'S' : '-',
			state.tb[t][i].access[1][3] ? 'U' : '-', state.tb[t][i].fault[1] ? 'F' : '-');
	}
#endif
}

/**
 * \brief Add translation-buffer entry to the DTB
 *
 * The format of the PTE field is:
 * \code
 *   63 62           32 31     16  15  14  13  12  11  10  9   8  7 6  5  4  3  2   1  0
 *  +--+---------------+---------+---+---+---+---+---+---+---+---+-+----+---+-+---+---+-+
 *  |  |  PA <43:13>   |         |UWE|SWE|EWE|KWE|URE|SRE|ERE|KRE| | GH |ASM| |FOW|FOR| |
 *  +--+---------------+---------+---+---+---+---+---+---+---+---+-+----+---+-+---+---+-+
 *                               +-------------------------------+    |   |   +-------+
 *                                                           |        |   |       |
 *  (user,supervisor,executive,kernel)(read,write)enable ----+        |   |       |
 *                                              granularity hint -----+   |       |
 *                                               address space match -----+       |
 *                                                      fault-on-(read,write) ----+
 * \endcode
 *
 * \param virt    Virtual address.
 * \param pte     Translation in DTB_PTE format.
 * \param dtb     DTB fill port number (0 = DTB_PTE0, 1 = DTB_PTE1).
 **/
void CAlphaCPU::add_tb_d(u64 virt, u64 pte, int dtb)
{
	add_tb(virt, pte >> (32 - 13), pte, ACCESS_READ, dtb ? state.asn1 : state.asn0);
}

/**
 * \brief Add translation-buffer entry to the ITB
 *
 * The format of the PTE field is:
 * \code
 *   63              44 43           13 12  11  10  9   8  7 6  5  4  3   0
 *  +------------------+---------------+--+---+---+---+---+-+----+---+-----+
 *  |                  |  PA <43:13>   |  |URE|SRE|ERE|KRE| | GH |ASM|     |
 *  +------------------+---------------+--+---+---+---+---+-+----+---+-----+
 *                                        +---------------+    |   |
 *                                                    |        |   |
 *  (user,supervisor,executive,kernel)read enable ----+        |   |
 *                                       granularity hint -----+   |
 *                                        address space match -----+
 *
 * \endcode
 *
 * \param virt    Virtual address.
 * \param pte     Translation in ITB_PTE format.
 **/
void CAlphaCPU::add_tb_i(u64 virt, u64 pte)
{
	add_tb(virt, pte, pte & 0xf70, ACCESS_EXEC, state.asn);
}

/**
 * \brief Invalidate all translation-buffer entries
 *
 * Invalidate all translation-buffer entries in one of the translation buffers.
 *
 * \param flags   ACCESS_EXEC determines which translation buffer to use.
 **/
void CAlphaCPU::tbia(int flags)
{
	int t = (flags & ACCESS_EXEC) ? TB_INDEX_ITB : TB_INDEX_DATA;
	int i;
	for (i = 0; i < TB_ENTRIES; i++)
		state.tb[t][i].valid = false;
#ifdef JIT_STATS
	if (t == TB_INDEX_DATA) m_stat_tb[4]++;
#endif
	state.last_found_tb[t][0] = 0;
	state.last_found_tb[t][1] = 0;
	state.next_tb[t] = 0;
	if (t == TB_INDEX_DATA) flush_data_page_cache();
#ifdef ES40_JIT
	else if (m_jit) m_jit->note_itb_invalidate(CJitEngine::kBumpTbia);   // whole ITB cleared -> indirect chains re-validate phys
#endif
}

/**
 * \brief Invalidate all process-specific translation-buffer entries
 *
 * Invalidate all translation-buffer entries that do not have the ASM bit
 * set in one of the translation buffers.
 *
 * \param flags   ACCESS_EXEC determines which translation buffer to use.
 **/
void CAlphaCPU::tbiap(int flags)
{
	int t = (flags & ACCESS_EXEC) ? TB_INDEX_ITB : TB_INDEX_DATA;
	int i;
	for (i = 0; i < TB_ENTRIES; i++)
		if (!state.tb[t][i].asm_bit)
			state.tb[t][i].valid = false;
#ifdef JIT_STATS
	if (t == TB_INDEX_DATA) m_stat_tb[5]++;
#endif

	if (t == TB_INDEX_DATA) flush_data_page_cache();
#ifdef ES40_JIT
	else if (m_jit) m_jit->note_itb_invalidate(CJitEngine::kBumpTbiap);   // process ITB entries cleared -> chains re-validate phys
#endif
}

/**
 * \brief Invalidate single translation-buffer entry
 *
 * \param virt    Virtual address for which the entry should be invalidated.
 * \param flags   ACCESS_EXEC determines which translation buffer to use.
 **/
void CAlphaCPU::tbis(u64 virt, int flags)
{
	int t = (flags & ACCESS_EXEC) ? TB_INDEX_ITB : TB_INDEX_DATA;

	if (t == TB_INDEX_DATA)
	{
		tbis_d(virt, state.asn0);
		if (state.asn1 != state.asn0)
			tbis_d(virt, state.asn1);
		return;
	}

	int i = FindTBEntry(virt, flags);
	if (i >= 0)
		state.tb[t][i].valid = false;
#ifdef ES40_JIT
	// Chains into this page must re-validate their physical before running again, cached
	// entry or not. One 8 KB page clears only the edges targeting it; a granularity-hint
	// entry spans several pages, so that case keeps the epoch bump.
	if (m_jit)
	{
		if (i >= 0 && state.tb[t][i].match_mask != U64(0x0000ffffffffe000))
			m_jit->note_itb_invalidate(CJitEngine::kBumpTbisGh);
		else
			m_jit->invalidate_links_page(virt);
	}
#endif
}

void CAlphaCPU::tbis_d(u64 virt, int asn)
{
	bool found = false;
#ifdef JIT_STATS
	m_stat_tb[6]++;
#endif

	for (int i = 0; i < TB_ENTRIES; i++)
	{
		if (state.tb[TB_INDEX_DATA][i].valid
			&& !((state.tb[TB_INDEX_DATA][i].virt ^ virt) & state.tb[TB_INDEX_DATA][i].match_mask)
			&& (state.tb[TB_INDEX_DATA][i].asm_bit || state.tb[TB_INDEX_DATA][i].asn == asn))
		{
			invalidate_data_page_cache(state.tb[TB_INDEX_DATA][i].virt,
				state.tb[TB_INDEX_DATA][i].match_mask);
			state.tb[TB_INDEX_DATA][i].valid = false;
			found = true;
		}
	}

	// If the TB entry was already evicted, an old base-page DPC line can still exist.
	// Retire its direct slot using the mask recognized by the cache's fast path.
	if (!found)
		invalidate_data_page_cache(virt, U64(0x0000ffffffffe000));
}

//\}

/**
 * \brief Enable i-cache regardles of config file.
 *
 * Required for SRM-ROM decompression.
 **/
void CAlphaCPU::enable_icache()
{
	icache_enabled = true;
}

/**
 * \brief Restore i-cache after temporary ROM decompression setup.
 **/
void CAlphaCPU::restore_icache()
{
	icache_enabled = true;
}

#if defined(IDB)
const char* PAL_NAME[] = {
  "HALT", "CFLUSH", "DRAINA", "LDQP", "STQP", "SWPCTX", "MFPR_ASN",
  "MTPR_ASTEN",
  "MTPR_ASTSR", "CSERVE", "SWPPAL", "MFPR_FEN", "MTPR_FEN", "MTPR_IPIR",
  "MFPR_IPL", "MTPR_IPL",
  "MFPR_MCES", "MTPR_MCES", "MFPR_PCBB", "MFPR_PRBR", "MTPR_PRBR",
  "MFPR_PTBR", "MFPR_SCBB", "MTPR_SCBB",
  "MTPR_SIRR", "MFPR_SISR", "MFPR_TBCHK", "MTPR_TBIA", "MTPR_TBIAP",
  "MTPR_TBIS", "MFPR_ESP", "MTPR_ESP",
  "MFPR_SSP", "MTPR_SSP", "MFPR_USP", "MTPR_USP", "MTPR_TBISD", "MTPR_TBISI",
  "MFPR_ASTEN", "MFPR_ASTSR",
  "28", "MFPR_VPTB", "MTPR_VPTB", "MTPR_PERFMON", "2C", "2D", "MTPR_DATFX",
  "2F",
  "30", "31", "32", "33", "34", "35", "36", "37",
  "38", "39", "3A", "3B", "3C", "3D", "WTINT", "MFPR_WHAMI",
  "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-",
  "-",
  "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-",
  "-",
  "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-",
  "-",
  "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-",
  "-",
  "BPT", "BUGCHK", "CHME", "CHMK", "CHMS", "CHMU", "IMB", "INSQHIL",
  "INSQTIL", "INSQHIQ", "INSQTIQ", "INSQUEL", "INSQUEQ", "INSQUEL/D",
  "INSQUEQ/D", "PROBER",
  "PROBEW", "RD_PS", "REI", "REMQHIL", "REMQTIL", "REMQHIQ", "REMQTIQ",
  "REMQUEL",
  "REMQUEQ", "REMQUEL/D", "REMQUEQ/D", "SWASTEN", "WR_PS_SW", "RSCC",
  "READ_UNQ", "WRITE_UNQ",
  "AMOVRR", "AMOVRM", "INSQHILR", "INSQTILR", "INSQHIQR", "INSQTIQR",
  "REMQHILR", "REMQTILR",
  "REMQHIQR", "REMQTIQR", "GENTRAP", "AB", "AC", "AD", "CLRFEN", "AF",
  "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "BA", "BB",
  "BC", "BD", "BE", "BF"
};

const char* IPR_NAME[] = {
  "ITB_TAG", "ITB_PTE", "ITB_IAP", "ITB_IA", "ITB_IS", "PMPC", "EXC_ADDR",
  "IVA_FORM",
  "IER_CM", "CM", "IER", "IER_CM", "SIRR", "ISUM", "HW_INT_CLR", "EXC_SUM",
  "PAL_BASE", "I_CTL", "IC_FLUSH_ASM", "IC_FLUSH", "PCTR_CTL", "CLR_MAP",
  "I_STAT", "SLEEP",
  "?0001.1000?", "?0001.1001?", "?0001.1010?", "?0001.1011?", "?0001.1100?",
  "?0001.1101?", "?0001.1110?", "?0001.1111?",
  "DTB_TAG0", "DTB_PTE0", "?0010.0010?", "?0010.0011?", "DTB_IS0", "DTB_ASN0",
  "DTB_ALTMODE", "MM_STAT",
  "M_CTL", "DC_CTL", "DC_STAT", "C_DATA", "C_SHFT", "M_FIX", "?0010.1110?",
  "?0010.1111?",
  "?0011.0000?", "?0011.0001?", "?0011.0010?", "?0011.0011?", "?0011.0100?",
  "?0010.0101?", "?0010.0110?", "?0010.0111?",
  "?0011.1000?", "?0011.1001?", "?0011.1010?", "?0011.1011?", "?0011.1100?",
  "?0010.1101?", "?0010.1110?", "?0010.1111?",
  "PCTX.00000", "PCTX.00001", "PCTX.00010", "PCTX.00011", "PCTX.00100",
  "PCTX.00101", "PCTX.00110", "PCTX.00111",
  "PCTX.01000", "PCTX.01001", "PCTX.01010", "PCTX.01011", "PCTX.01100",
  "PCTX.01101", "PCTX.01110", "PCTX.01111",
  "PCTX.10000", "PCTX.10001", "PCTX.10010", "PCTX.10011", "PCTX.10100",
  "PCTX.10101", "PCTX.10110", "PCTX.10111",
  "PCTX.11000", "PCTX.11001", "PCTX.11010", "PCTX.11011", "PCTX.11100",
  "PCTX.11101", "PCTX.11110", "PCTX.11111",
  "PCTX.00000", "PCTX.00001", "PCTX.00010", "PCTX.00011", "PCTX.00100",
  "PCTX.00101", "PCTX.00110", "PCTX.00111",
  "PCTX.01000", "PCTX.01001", "PCTX.01010", "PCTX.01011", "PCTX.01100",
  "PCTX.01101", "PCTX.01110", "PCTX.01111",
  "PCTX.10000", "PCTX.10001", "PCTX.10010", "PCTX.10011", "PCTX.10100",
  "PCTX.10101", "PCTX.10110", "PCTX.10111",
  "PCTX.11000", "PCTX.11001", "PCTX.11010", "PCTX.11011", "PCTX.11100",
  "PCTX.11101", "PCTX.11110", "PCTX.11111",
  "?1000.0000?", "?1000.0001?", "?1000.0010?", "?1000.0011?", "?1000.0100?",
  "?1000.0101?", "?1000.0110?", "?1000.0111?",
  "?1000.1000?", "?1000.1001?", "?1000.1010?", "?1000.1011?", "?1000.1100?",
  "?1000.1101?", "?1000.1110?", "?1000.1111?",
  "?1001.0000?", "?1001.0001?", "?1001.0010?", "?1001.0011?", "?1001.0100?",
  "?1001.0101?", "?1001.0110?", "?1001.0111?",
  "?1001.1000?", "?1001.1001?", "?1001.1010?", "?1001.1011?", "?1001.1100?",
  "?1001.1101?", "?1001.1110?", "?1001.1111?",
  "DTB_TAG1", "DTB_PTE1", "DTB_IAP", "DTB_IA", "DTB_IS1", "DTB_ASN1",
  "?1010.0110?", "?1010.0111?",
  "?1010.1000?", "?1010.1001?", "?1010.1010?", "?1010.1011?", "?1010.1100?",
  "?1010.1101?", "?1010.1110?", "?1010.1111?",
  "?1011.0000?", "?1011.0001?", "?1011.0010?", "?1011.0011?", "?1011.0100?",
  "?1011.0101?", "?1011.0110?", "?1011.0111?",
  "?1011.1000?", "?1011.1001?", "?1011.1010?", "?1011.1011?", "?1011.1100?",
  "?1011.1101?", "?1011.1110?", "?1011.1111?",
  "CC", "CC_CTL", "VA", "VA_FORM", "VA_CTL", "?1100.0101?", "?1100.0110?",
  "?1100.0111?",
  "?1100.1000?", "?1100.1001?", "?1100.1010?", "?1100.1011?", "?1100.1100?",
  "?1100.1101?", "?1100.1110?", "?1100.1111?",
  "?1101.0000?", "?1101.0001?", "?1101.0010?", "?1101.0011?", "?1101.0100?",
  "?1101.0101?", "?1101.0110?", "?1101.0111?",
  "?1101.1000?", "?1101.1001?", "?1101.1010?", "?1101.1011?", "?1101.1100?",
  "?1101.1101?", "?1101.1110?", "?1101.1111?",
  "?1110.0000?", "?1110.0001?", "?1110.0010?", "?1110.0011?", "?1110.0100?",
  "?1110.0101?", "?1110.0110?", "?1110.0111?",
  "?1110.1000?", "?1110.1001?", "?1110.1010?", "?1110.1011?", "?1110.1100?",
  "?1110.1101?", "?1110.1110?", "?1110.1111?",
  "?1111.0000?", "?1111.0001?", "?1111.0010?", "?1111.0011?", "?1111.0100?",
  "?1111.0101?", "?1111.0110?", "?1111.0111?",
  "?1111.1000?", "?1111.1001?", "?1111.1010?", "?1111.1011?", "?1111.1100?",
  "?1111.1101?", "?1111.1110?", "?1111.1111?",
};
#endif
