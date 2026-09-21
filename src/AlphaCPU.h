/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
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
  * Contains the definitions for the emulated DecChip 21264CB EV68 Alpha processor.
  *
  * $Id$
  *
  * X-1.59       Camiel Vanderhoeven                             12-JUN-2008
  *   a) Support to keep secondary CPUs waiting until activated from primary.
  *   b) Support for last written and last read memory locations.
  *
  * X-1.58       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.57       Camiel Vanderhoeven                             24-MAR-2008
  *      Comments.
  *
  * X-1.56       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.55       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.54       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.53       Camiel Vanderhoeven                             11-MAR-2008
  *      Named, debuggable mutexes.
  *
  * X-1.52       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.51       Brian Wheeler                                   29-FEB-2008
  *      Add BREAKPOINT INSTRUCTION command to IDB.
  *
  * X-1.50       Camiel Vanderhoeven                             08-FEB-2008
  *      Show originating device name on memory errors.
  *
  * X-1.49       Camiel Vanderhoeven                             01-FEB-2008
  *      Avoid unnecessary shift-operations to calculate constant values.
  *
  * X-1.48       Camiel Vanderhoeven                             30-JAN-2008
  *      Always use set_pc or add_pc to change the program counter.
  *
  * X-1.47       Camiel Vanderhoeven                             30-JAN-2008
  *      Remember number of instructions left in current memory page, so
  *      that the translation-buffer doens't need to be consulted on every
  *      instruction fetch when the Icache is disabled.
  *
  * X-1.46       Camiel Vanderhoeven                             29-JAN-2008
  *      Cleanup.
  *
  * X-1.45       Camiel Vanderhoeven                             29-JAN-2008
  *      Remember separate last found translation-buffer entries for read
  *      and write operations. This should help with memory copy operations.
  *
  * X-1.44       Camiel Vanderhoeven                             28-JAN-2008
  *      Better floating-point exception handling.
  *
  * X-1.43       Camiel Vanderhoeven                             27-JAN-2008
  *      Comments.
  *
  * X-1.40       Camiel Vanderhoeven                             27-JAN-2008
  *      Minor floating-point improvements.
  *
  * X-1.39       Camiel Vanderhoeven                             25-JAN-2008
  *      Added option to disable the icache.
  *
  * X-1.37       Camiel Vanderhoeven                             21-JAN-2008
  *      Moved some macro's to cpu_defs.h; implement new floating-point code.
  *
  * X-1.36       Camiel Vanderhoeven                             19-JAN-2008
  *      Run CPU in a separate thread if CPU_THREADS is defined.
  *      NOTA BENE: This is very experimental, and has several problems.
  *
  * X-1.35       Camiel Vanderhoeven                             18-JAN-2008
  *      Comments.
  *
  * X-1.34       Camiel Vanderhoeven                             18-JAN-2008
  *      Process device interrupts after a 100-cpu-cycle delay.
  *
  * X-1.33       Camiel Vanderhoeven                             08-JAN-2008
  *      Removed last references to IDE disk read SRM replacement.
  *
  * X-1.32       Camiel Vanderhoeven                             02-JAN-2008
  *      Endianess fix.
  *
  * X-1.31       Camiel Vanderhoeven                             02-JAN-2008
  *      Comments. Undid part of last change because of performance impact.
  *
  * X-1.30       Camiel Vanderhoeven                             29-DEC-2007
  *      Avoid referencing uninitialized data.
  *
  * X-1.29       Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.28       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator.
  *
  * X-1.27       Camiel Vanderhoeven                             2-DEC-2007
  *      Changed the way translation buffers work, the way interrupts work,
  *      added vmspal routines.
  *
  * X-1.26       Brian Wheeler                                   1-DEC-2007
  *      Added support for instruction counting, underlined lines in
  *      listings, corrected some unsigned/signed issues.
  *
  * X-1.25	    Brian Wheeler    				                22-NOV-2007
  *	Added set_r and set_f for LOADREG and LOADFPREG debugger commands.
  *
  * X-1.24       Camiel Vanderhoeven                             06-NOV-2007
  *      Performance improvements to ICACHE: last result is kept; cache
  *      lines are larger (512 DWORDS in stead of 16 DWORDS), cache size is
  *      configurable (both number of cache lines and size of each cache
  *      line), memcpy is used to move memory into the ICACHE.
  *      CAVEAT: ICACHE can only be filled from memory (not from I/O).
  *
  * X-1.23       Eduardo Marcelo Ferrat                          31-OCT-2007
  *      Disable SRM replacement routines.
  *
  * X-1.22       Camiel Vanderhoeven                             17-APR-2007
  *      Give ASM bit a value (true) for PALmode Icache entries.
  *
  * X-1.21       Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.20	Camiel Vanderhoeven				7-APR-2007
  *	Added get_hwpcb;
  *
  * X-1.19	Camiel Vanderhoeven				5-APR-2007
  *	Fixed X-1.14. The virtual address was returned instead of the
  *	physical one!
  *
  * X-1.18       Camiel Vanderhoeven                             31-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.17	Camiel Vanderhoeven				18-MAR-2007
  *   	Removed pointles comparison (v_prbr > 0).
  *
  * X-1.16	Camiel Vanderhoeven				14-MAR-2007
  *	bListing removed.
  *
  * X-1.15	Camiel Vanderhoeven				12-MAR-2007
  *   a)	Added possibility to retrieve physical address of current instruction.
  *   b) Added member function get_pal_base.
  *
  * X-1.14	Camiel Vanderhoeven				9-MAR-2007
  *	Try to translate a virtual PRBR value to a physical one in get_prbr.
  *
  * X-1.13	Camiel Vanderhoeven				8-MAR-2007
  *	va_form now takes a boolean argument bIBOX to determine which ASN
  *	and VPTB to use.
  *
  * X-1.12	Camiel Vanderhoeven				7-MAR-2007
  *	Added get_tb, get_asn and get_spe functions.
  *
  * X-1.11	Camiel Vanderhoeven				22-FEB-2007
  *	Add ASM bit to the instruction cache & corresponding functions.
  *
  * X-1.10	Camiel Vanderhoeven				18-FEB-2007
  *	Add get_f function.
  *
  * X-1.9        Camiel Vanderhoeven                             16-FEB-2007
  *   a) Added CAlphaCPU::listing.
  *   b) CAlphaCPU::DoClock now returns a value.
  *
  * X-1.8        Camiel Vanderhoeven                             12-FEB-2007
  *	Added get_r and get_prbr functions as inlines.
  *
  * X-1.7        Camiel Vanderhoeven                             12-FEB-2007
  *	Added inline functions to get and update the program counter (pc).
  *
  * X-1.6	Camiel Vanderhoeven				12-FEB-2007
  *	Added comments.
  *
  * X-1.5        Camiel Vanderhoeven                             9-FEB-2007
  *	Added comments.
  *
  * X-1.4        Camiel Vanderhoeven                             9-FEB-2007
  *      Moved debugging flags (booleans) to TraceEngine.
  *
  * X-1.3	Camiel Vanderhoeven				7-FEB-2007
  *	Added comments.
  *
  * X-1.2        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2007
  *      Initial version in CVS.
  **/
#if !defined(INCLUDED_ALPHACPU_H)
#define INCLUDED_ALPHACPU_H

#include <atomic>
#include "idle_detect.h"
#include <chrono>

#include "SystemComponent.h"
#include "System.h"
#include "cpu_defs.h"
class CJitEngine;   // JIT block-cache engine (ES40_JIT builds)

  /// Number of entries in the Instruction Cache
#define ICACHE_ENTRIES    1024
// Size of Instruction Cache entries in DWORDS (instructions)
#define ICACHE_LINE_SIZE  512
/** These bits should match to have an Instruction Cache hit.
    This includes bit 0, because it indicates PALmode . */
#define ICACHE_MATCH_MASK (u64) (U64(0x1) - (ICACHE_LINE_SIZE * 4))
    /// DWORD (instruction) number of an address in an ICache entry.
#define ICACHE_INDEX_MASK (u64) (ICACHE_LINE_SIZE - U64(0x1))
/// Byte numer of an address in an ICache entry.
#define ICACHE_BYTE_MASK  (u64) (ICACHE_INDEX_MASK << 2)
/// Number of entries in each Translation Buffer
#define TB_ENTRIES        16 // real EV68 has 128

/**
 * \brief Emulated CPU.
 *
 * The CPU emulated is the DECchip 21264CB Alpha Processor (EV68).
 *
 * Documentation consulted:
 *  - Alpha 21264/EV68CB and 21264/EV68DC Microprocessor Hardware Reference Manual [HRM] (http://download.majix.org/dec/21264ev68cb_ev68dc_hrm.pdf)
 *  - DS-0026A-TE: Alpha 21264B Microprocessor Hardware Reference Manual [HRM] (http://ftp.digital.com/pub/Digital/info/semiconductor/literature/21264hrm.pdf)
 *  - Alpha Architecture Reference Manual, fourth edition [ARM] (http://download.majix.org/dec/alpha_arch_ref.pdf)
 *	.
 **/
class CAlphaCPU : public CSystemComponent, public CRunnable
{
public:
  void          flush_icache_asm();
  virtual int   SaveState(FILE* f);
  virtual int   RestoreState(FILE* f);
  void          irq_h(int number, bool assert, int delay);
  void          kick_int_if_pending();
  void          idle_nap();
  enum class TickHold { Ticked, Expired, Doorbell };
  u64           tick_next_gap_ns(u64 period_ns);   // draws the modulated repay gap for the next fire
  TickHold      tick_hold(u64 period_ns);   // instruction-paced envelope full: wait for the wall-clock tick (jit_run)
  int           get_cpuid();
  void          flush_icache();

  virtual void  run();    // Poco Thread entry point
  void          execute();
  void          release_threads();

  void          set_PAL_BASE(u64 pb);
  virtual void  check_state();
  CAlphaCPU(CConfigurator* cfg, CSystem* system);
  virtual       ~CAlphaCPU();
  u64           get_r(int i, bool translate);
  u64           get_f(int i);
  void          set_r(int reg, u64 val);
  void          set_f(int reg, u64 val);
  u64           get_prbr(void);
  u64           get_hwpcb(void);
  u64           get_pc();
  u64           get_pal_base();

  void          enable_icache();
  void          restore_icache();

#ifdef ES40_JIT
  void          jit_step(int budget);   // one jit_run dispatch batch, for main-thread callers (LoadROM)
#endif

  bool          get_waiting() { return state.wait_for_start; };
  void          stop_waiting() { state.wait_for_start = false; };
  u64           get_icount() { return state.instruction_count; };
#ifdef IDB
  u64           get_current_pc_physical();
  u64           get_instruction_count();
  u32           get_last_instruction();
  u64           get_last_read_loc() { return last_read_loc; }
  u64           get_last_write_loc() { return last_write_loc; }
#endif
  u64           get_clean_pc();
  void          next_pc();
  void          set_pc(u64 p_pc);
  void          add_pc(u64 a_pc);

  u64           get_speed() { return cpu_hz; };

  u64           va_form(u64 address, bool bIBOX);

#if defined(IDB)
  void          listing(u64 from, u64 to);
  void          listing(u64 from, u64 to, u64 mark);
#endif
  int           virt2phys(u64 virt, u64* phys, int flags, bool* asm_bit, u32 instruction);

  virtual void  init();
  virtual void  start_threads();
  virtual void  stop_threads();
  void ResetForSystemReset();
private:
  CThread* myThread;
  CSemaphore mySemaphore;
  bool            StopThread;

  int             get_icache(u64 address, u32* data);
  int             FindTBEntry(u64 virt, int flags);
  int             initiate_acv_fault(u64 virt, int flags, u32 instruction);
  void            add_tb(u64 virt, u64 pte_phys, u64 pte_flags, int flags, int asn);
  void            add_tb_i(u64 virt, u64 pte);
  void            add_tb_d(u64 virt, u64 pte, int dtb);
  void            tbia(int flags);
  void            tbiap(int flags);
  void            tbis(u64 virt, int flags);
  void            tbis_d(u64 virt, int asn);

  /* Floating Point routines */
  u64             ieee_lds(u32 op);
  u32             ieee_sts(u64 op);
  u64             ieee_cvtst(u64 op, u32 ins);
  u64             ieee_cvtts(u64 op, u32 ins);
  s32             ieee_fcmp(u64 s1, u64 s2, u32 ins, u32 trap_nan);
  u64             ieee_cvtif(u64 val, u32 ins, u32 dp);
  u64             ieee_cvtfi(u64 op, u32 ins);
  u64             ieee_fadd(u64 s1, u64 s2, u32 ins, u32 dp, bool sub);
  u64             ieee_fmul(u64 s1, u64 s2, u32 ins, u32 dp);
  u64             ieee_fdiv(u64 s1, u64 s2, u32 ins, u32 dp);
  u64             ieee_sqrt(u64 op, u32 ins, u32 dp);
  int             ieee_unpack(u64 op, UFP* r, u32 ins);
  void            ieee_norm(UFP* r);
  u64             ieee_rpack(UFP* r, u32 ins, u32 dp);
  void            ieee_trap(u64 trap, u32 instenb, u64 fpcrdsb, u32 ins);
  u64             vax_ldf(u32 op);
  u64             vax_ldg(u64 op);
  u32             vax_stf(u64 op);
  u64             vax_stg(u64 op);
  void            vax_trap(u64 mask, u32 ins);
  void            vax_unpack(u64 op, UFP* r, u32 ins);
  void            vax_unpack_d(u64 op, UFP* r, u32 ins);
  void            vax_norm(UFP* r);
  u64             vax_rpack(UFP* r, u32 ins, u32 dp);
  u64             vax_rpack_d(UFP* r, u32 ins);
  int             vax_fcmp(u64 s1, u64 s2, u32 ins);
  u64             vax_cvtif(u64 val, u32 ins, u32 dp);
  u64             vax_cvtfi(u64 op, u32 ins);
  u64             vax_fadd(u64 s1, u64 s2, u32 ins, u32 dp, bool sub);
  u64             vax_fmul(u64 s1, u64 s2, u32 ins, u32 dp);
  u64             vax_fdiv(u64 s1, u64 s2, u32 ins, u32 dp);
  u64             vax_sqrt(u64 op, u32 ins, u32 dp);

  /* VMS PALcode call: */
  void            vmspal_call_cflush();
  void            vmspal_call_draina();
  void            vmspal_call_ldqp();
  void            vmspal_call_stqp();
  void            vmspal_call_swpctx();
  void            vmspal_call_mfpr_asn();
  void            vmspal_call_mtpr_asten();
  void            vmspal_call_mtpr_astsr();
  void            vmspal_call_cserve();
  void            vmspal_call_mfpr_fen();
  void            vmspal_call_mtpr_fen();
  void            vmspal_call_mfpr_ipl();
  void            vmspal_call_mtpr_ipl();
  void            vmspal_call_mfpr_mces();
  void            vmspal_call_mtpr_mces();
  void            vmspal_call_mfpr_pcbb();
  void            vmspal_call_mfpr_prbr();
  void            vmspal_call_mtpr_prbr();
  void            vmspal_call_mfpr_ptbr();
  void            vmspal_call_mfpr_scbb();
  void            vmspal_call_mtpr_scbb();
  void            vmspal_call_mtpr_sirr();
  void            vmspal_call_mfpr_sisr();
  void            vmspal_call_mfpr_tbchk();
  void            vmspal_call_mtpr_tbia();
  void            vmspal_call_mtpr_tbiap();
  void            vmspal_call_mtpr_tbis();
  void            vmspal_call_mfpr_esp();
  void            vmspal_call_mtpr_esp();
  void            vmspal_call_mfpr_ssp();
  void            vmspal_call_mtpr_ssp();
  void            vmspal_call_mfpr_usp();
  void            vmspal_call_mtpr_usp();
  void            vmspal_call_mtpr_tbisd();
  void            vmspal_call_mtpr_tbisi();
  void            vmspal_call_mfpr_asten();
  void            vmspal_call_mfpr_astsr();
  void            vmspal_call_mfpr_vptb();
  void            vmspal_call_mtpr_datfx();
  void            vmspal_call_wtint();
  void            vmspal_call_mfpr_whami();
  void            vmspal_call_imb();
  void            vmspal_call_prober();
  void            vmspal_call_probew();
  void            vmspal_call_rd_ps();
  int             vmspal_call_rei();
  void            vmspal_call_swasten();
  void            vmspal_call_wr_ps_sw();
  void            vmspal_call_rscc();
  void            vmspal_call_read_unq();
  void            vmspal_call_write_unq();

  /* VMS PALcode entry: */
  int             vmspal_ent_dtbm_double_3(int flags);
  int             vmspal_ent_dtbm_single(int flags);
  int             vmspal_ent_itbm(int flags);
  int             vmspal_ent_iacv(int flags);
  int             vmspal_ent_dfault(int flags);
  int             vmspal_ent_ext_int(int ei);
  int             vmspal_ent_sw_int(int si);
  int             vmspal_ent_ast_int(int ast);

  /* VMS PALcode internal: */
  int             vmspal_int_initiate_exception();
  int             vmspal_int_initiate_interrupt();

  // New FP helpers.....
  void write_fpcr_arch(u64 arch_val);
  u64  read_fpcr_arch() const;

  bool            icache_enabled;
  bool            vmspal_lle_enabled;
  bool            exit_on_pal_halt = false;

  // ... ... ...
  u64                                   cc_large;
  u64                                   start_icount;
  u64                                   start_cc;
  std::chrono::steady_clock::time_point start_time;
  u64                                   prev_icount;
  u64                                   prev_cc;
  u64                                   prev_time;
  u64                                   cc_per_instruction;
  u64                                   cpu_hz;

  // Wall-clock-paced Cchip interval timer (b_irq<2>).  CPU 0 fires once
  // per period as it passes batch-flush boundaries.  Avoids cross-thread
  // edge coalescing seen with AliM1543C-thread firing.  next_timer_fire is
  // the count-preserving schedule (+= period per fire); tick_last_fire paces
  // catch-up so backlog repays at no more than 2x the nominal rate.
  std::chrono::steady_clock::time_point next_timer_fire;
  std::chrono::steady_clock::time_point tick_last_fire;
  u32                                   tick_pace_lcg = 0x9e3779b9; // noise term of the catch-up gap modulation (see timer fire in jit_run/execute)
  u32                                   tick_fire_idx = 0;          // triangle-wave phase of the catch-up gap modulation
  u64                                   tick_gap_ns = 0;            // min spacing after tick_last_fire for the next fire (0 = first fire immediate)
  u64                                   tick_last_icount = 0;       // instruction_count when this CPU last observed an interval tick (instruction-paced cap)
  u32                                   tick_seen_seq = 0;          // last CSystem tick sequence this CPU observed
  u64                                   m_max_instr_per_tick = 0;   // config timer.max_instr_per_tick (0 = off); see jit_run timer

  // CALL_PAL WTINT idle nap: enabled by the cpu config, announced once
  bool                                  idle_nap_enabled = false;
  u32                                   idle_nap_poll_us = 1000; // idle_nap() wake granularity, us
  // Host-side idle detection (idle_detect cfg). See idle_detect.h.
  int                                   idle_rule = RULE_NONE;   // EIdleRule, from idle_detect
  u32                                   idle_spin_count = 2;      // consecutive hits before napping
  u64                                   idle_spin_min_instr = 0;  // a dispatch must have run at least this many
  u64                                   m_spin_ic = 0;            // instruction count at that dispatch
  u32                                   m_spin_hits = 0;
  // Working set of the current run of full-budget dispatches.
  static const int                      kSpinSetMax = 8;
  u64                                   m_spin_set[kSpinSetMax] = { 0 };
  int                                   m_spin_set_n = 0;
  bool                                  m_spin_announced = false;
  bool                                  idle_announced = false;

  // Wall-clock RPCC: state.cc advances by real elapsed time * cpu_hz so it tracks the configured
  // CPU frequency regardless of how fast/bursty the JIT runs. This is the last sync timestamp;
  // the delta since it (when cc_ena) is added to state.cc at each sync point, then it's reset to now.
  std::chrono::steady_clock::time_point cc_last_sync;
  u64                                   cc_wall_remainder = 0; // fractional cycle numerator, / 1e9

  // Advance the wall-clock cc to now. Called at batch boundaries AND on every guest RPCC read.
  void sync_cc_wallclock()
  {
    const auto now = std::chrono::steady_clock::now();
    if (cc_last_sync > now)
      cc_last_sync = now;
    auto cc_delta = now - cc_last_sync;
    cc_last_sync = now;
    if (state.cc_ena)
    {
      if (cc_delta > std::chrono::seconds(1))   // cap (not drop) odd deltas, as at batch top
        cc_delta = std::chrono::seconds(1);
      const u64 ns = (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(cc_delta).count();
      // Carry sub-cycle precision across frequent syncs.  
      const u64 scaled = ns * cpu_hz + cc_wall_remainder;
      u64 add = scaled / 1000000000ULL;
      cc_wall_remainder = scaled % 1000000000ULL;
      // Repay any rpcc_read floor-borrow by WITHHOLDING wall progress, never by subtracting
      if (cc_borrow)
      {
        const u64 repay = cc_borrow < add ? cc_borrow : add;
        add -= repay;
        cc_borrow -= repay;
      }
      state.cc += add;
    }
  }

  // RPCC read with a forward-progress guarantee. Real EV6 RPCC advances every cycle
  static constexpr u64 kRpccCollisionStep = 1;
  u64 cc_last_read = 0;
  u64 cc_borrow = 0;
  u64 rpcc_read()
  {
    sync_cc_wallclock();
    if (state.cc_ena && state.cc <= cc_last_read)
    {
      cc_borrow += cc_last_read + kRpccCollisionStep - state.cc;
      state.cc = cc_last_read + kRpccCollisionStep;
    }
    cc_last_read = state.cc;
    return ((u64) state.cc_offset) << 32 | (state.cc & U64(0xffffffff));
  }

  // DRAM fast-path cache
  char* dram_ptr;    // cSystem->PtrToMem(0) - host pointer to base es40 ram array thingy
  u64    dram_size;   // 1ULL << cSystem->get_memory_bits() — size of DRAM in bytes

  // Sequential icache fast path
  // Tracks position within current icache line for back-to-back sequential fetches.
  u32* seq_line_ptr;      // pointer to current icache line data[]
  int   seq_offset;        // next word offset within the line
  int   seq_remaining;     // words left in this line
  u64   seq_next_pc;       // expected PC for sequential hit

  inline void break_seq_icache() {
    seq_remaining = 0;
  }
  
  // Data page translation cache: direct-mapped by virtual page (kDpcEntries slots/dir) so a
  // multi-page access pattern doesn't thrash a single slot. The inline load checks one slot.
  static constexpr int kDpcBits    = 6;                  // 64 slots/dir (8KB pages -> 512KB)
  static constexpr int kDpcEntries = 1 << kDpcBits;
  static constexpr u64 kDpcMask    = (u64) kDpcEntries - 1;
  static inline u64 dpc_index(u64 va) { return (va >> 13) & kDpcMask; }
  struct SDataPageCache {
    u64  virt_page;   // va & ~0x1FFF
    u64  phys_base;   // pa & ~0x1FFF
    u64  host_bias;   // dram_ptr + phys_base - virt_page for DRAM, 0 for MMIO (host = bias + va)
    u8   cm;          // current mode (CM) at fill time
    u8   asn;         // data ASN (asn0) at fill time
    bool valid;
  } data_page_cache[2][kDpcEntries];  // [rw][dpc_index(va)]; [0]=read, [1]=write
  static_assert(sizeof(SDataPageCache) == 32, "DPC slots stay power-of-two for JIT shift indexing");

  inline void flush_data_page_cache() {
    // The generated and helper hit paths compare virt_page before using the slot. An all-ones
    // tag cannot equal an 8 KB-aligned virtual page, so a bulk fill invalidates both rows 
    // and avoids excessive stores on native-PAL translation flushes.
    memset(data_page_cache, 0xff, sizeof(data_page_cache));
  }
  inline void invalidate_data_page_cache(u64 virt, u64 match_mask) {
    // The common GH=0 mapping occupies exactly one direct-mapped slot. Clearing only its
    // read/write tags avoids a full cache sweep.
    constexpr u64 gh0_match = U64(0x0000ffffffffe000);
    if (match_mask == gh0_match) {
      const u64 i = dpc_index(virt);
      data_page_cache[0][i].virt_page = ~U64(0);
      data_page_cache[1][i].virt_page = ~U64(0);
      return;
    }
    // A granularity-hint entry can cover multiple cache indices. 
    for (int rw = 0; rw < 2; ++rw)
      for (int i = 0; i < kDpcEntries; ++i)
        if (!((data_page_cache[rw][i].virt_page ^ virt) & match_mask))
          data_page_cache[rw][i].virt_page = ~U64(0);
  }

  // ASN switch: bump the chain epoch so compiled chain edges revalidate through the asn-keyed
  // lookup paths (the chain guard checks tag+epoch only). No-op in non-JIT builds.
  void jit_note_asn_change();

#ifdef ES40_JIT
  // JIT block-discovery engine (per-CPU), allocated in init().
  CJitEngine* m_jit = nullptr;
  s64  m_jit_budget = 0;       // instruction ceiling for a compiled chain
  void* m_link_from = nullptr; // LinkSlot* array the dispatcher should patch
  void* m_jit_helper_tab[19] = {}; // helper fn table
  void jit_run(int budget);    // drives the ES40_JIT lane via the interpreter
  void jit_flush_blocks();     // invalidate all discovered JIT blocks
  void jit_flush_blocks_asm(); // invalidate only !asm_global blocks (preserve global PAL across ASN flush)
  // Compiled-block memory helpers. descr[7:0] is the transfer size; production integer
  // memops also put the instruction word in descr[63:32], allowing the helper to deliver
  // a translation fault once. Return 0 on success, 1 to retry in the interpreter, or 2
  // when the architectural fault was already delivered and state.pc is its PAL entry.
  static int jit_read(CAlphaCPU* cpu, u64 va, u64 descr, u64* out);
  static int jit_read_phys(CAlphaCPU* cpu, u64 phys, int size_bits, u64* out);  // HW_LD physical: no translation
  static int jit_read_locked(CAlphaCPU* cpu, u64 va, u64 descr, u64* out);  // LDx_L: load + establish LL/SC lock
  static int jit_read_vpte(CAlphaCPU* cpu, u64 va, u64 descr, u64* out);    // HW_LD virtual: translated read / direct native-PAL fault
  static int jit_read_wchk(CAlphaCPU* cpu, u64 va, int size_bits, u64* out);    // HW_LD func 0xa: longword virtual + WrChk
  static int jit_write(CAlphaCPU* cpu, u64 va, u64 descr, u64 value);
  static int jit_write_phys(CAlphaCPU* cpu, u64 phys, int size_bits, u64 value);  // HW_ST physical: no translation
  static int jit_fp_read(CAlphaCPU* cpu, u64 va, u32 fa, u32 descr);   // LDS/LDT: f[fa] = convert(MEM[va])
  static int jit_fp_write(CAlphaCPU* cpu, u64 va, u32 fa, u32 descr);  // STS/STT: MEM[va] = convert(f[fa])
  static u64 jit_stc(CAlphaCPU* cpu, u64 va, int size_bits, u64 value);           // STx_C: store-conditional
  // CALL_PAL OPCDEC trap (privileged func in user mode): GO_PAL(OPCDEC) incl. cpu_clear_lock.
  static void jit_opcdec(CAlphaCPU* cpu, u64 cpc);
  // HW_MFPR (PALmode): return the IPR named in ins; the caller (compiled codegen) writes Ra.
  static u64 jit_hw_mfpr(CAlphaCPU* cpu, u32 ins, u64 cur);
  // HW_MTPR (PALmode): store value (Rb) to the side-effect-free IPR named by function.
  static void jit_hw_mtpr(CAlphaCPU* cpu, u32 function, u64 value);
  // Indirect jump (JMP/HW_RET): look up the target block; return its chained re-entry or null.
  static void* jit_indirect(CAlphaCPU* cpu, u64 target, void* link_cache);
  // MISC (0x18) state reads: sel 0=RPCC (cycle counter), 1=RC, 2=RS (read interrupt flag +
  // clear/set). Value the verify can't re-derive -> replayed from the load log like a load.
  static u64 jit_misc(CAlphaCPU* cpu, u32 sel);
  // Int<->FP register moves (ITOFx / FTOIx). fmt: 0=T raw, 1=S, 2=F. Return 1 = FEN-trap bail.
  static int jit_itof(CAlphaCPU* cpu, u32 fc, u64 value, u32 fmt);
  static int jit_ftoi(CAlphaCPU* cpu, u32 fa, u32 fmt, u64* out);
  // FLTL (0x17) non-arithmetic: FPCR moves, CPYSx, FCMOVx, CVTLQ/QL. Return 1 = FEN-trap bail.
  static int jit_fltl(CAlphaCPU* cpu, u32 ins);
  // FLTV (0x15) VAX arith/convert/compare. Return 0 ok / 1 FEN-trap bail / 2 arith trap (exc_sum set).
  static int jit_fltv(CAlphaCPU* cpu, u32 ins);
  // Verify support: the interpreter pass records each value it loads, and the
  // compiled pass replays them instead of re-reading memory - false mismatch fix
  // must cover a full trace formation, not just a 64-instruction block.
  static constexpr u32 kJitVlogCap = 1024;
  bool m_jit_vreplay = false;  // compiled pass: replay recorded loads, don't re-read
  u32  m_jit_vlog_i  = 0;      // replay cursor
  u64  m_jit_vlog[kJitVlogCap];   // values the interpreter pass loaded (<= trace span)
  u64  m_jit_vaddr[kJitVlogCap];  // diagnostic: load addresses the interpreter computed
  // Store verify: the interpreter pass records each store (addr,value); the compiled
  // pass compares against it (stores touch memory, not GPRs, so the GPR check can't see them).
  u32  m_jit_slog_i  = 0;      // store-compare cursor
  u64  m_jit_slog_addr[kJitVlogCap];    // store addresses the interpreter pass wrote
  u64  m_jit_slog_val[kJitVlogCap];     // store values the interpreter pass wrote
  u64  m_jit_slog_success[kJitVlogCap]; // STx_C outcome (1/0) the interp pass got; 1 for ordinary stores
#endif

  /// The state structure contains all elements that need to be saved to the statefile
  struct SCPU_state
  {
    u64   pc;             /**< Program counter */
    u64   current_pc;   /**< Virtual address of current instruction */
    u64   pc_phys;
    u64   cc;             /**< IPR CC: Cycle counter [HRM p 5-3] */
    u64   instruction_count;  /**< Number of times doclock has been called */
    bool  cc_ena;         /**< IPR CC_CTL: Cycle counter enabled [HRM p 5-3] */
    std::atomic<bool> check_int;    /**< Interrupt maybe pending; raised cross-thread by irq_h() */
    std::atomic<bool> check_timers; /**< Delayed-irq countdown pending; set cross-thread by irq_h() */
    bool  fpen;           /**< IPR PCTX: fpe (floating point enable) [HRM p 5-21..23] */
    int   cm;           /**< IPR IER_CM: cm (current mode) [HRM p 5-9..10] */
    int   asn0;         /**< IPR DTB_ASN0: data ASN [HRM p 5-28]. Kept right after cm so the JIT dpc
                             check loads {cm,asn0} as one 8-byte compare vs the slot's {cm,asn}. */
    int   asn;          /**< IPR PCTX: asn (address space number) [HRM p 5-21..22] */
    u64   exc_addr;     /**< IPR EXC_ADDR: address of last exception [HRM p 5-8] */

    u64   r[64];          /**< Integer registers (0-31 normal, 32-63 shadow) */

    u64   f[64];    /**< Floating point registers (0-31 normal, 32-63 shadow) */

    bool  wait_for_start;
    u64   pal_base;       /**< IPR PAL_BASE [HRM: p 5-15] */
    u64   dc_stat;        /**< IPR DC_STAT: Dcache status [HRM p 5-31..32] */
    bool  ppcen;          /**< IPR PCTX: ppce (proc perf counting enable) [HRM p 5-21..23] */
    u64   i_stat;         /**< IPR I_STAT: Ibox status [HRM p 5-18..20] */
    u64   pctr_ctl;       /**< IPR PCTR_CTL [HRM p 5-23..25] */
    u32   cc_offset;      /**< IPR CC: Cycle counter offset [HRM p 5-3] */
    u64   dc_ctl;         /**< IPR DC_CTL: Dcache control [HRM p 5-30..31] */
    int   alt_cm;         /**< IPR DTB_ALTMODE: alternative cm for HW_LD/HW_ST [HRM p 5-26..27] */
    int   smc;            /**< IPR M_CTL: smc (speculative miss control) [HRM p 5-29..30] */
    bool  sde;            /**< IPR I_CTL: sde[1] (PALshadow enable) [HRM p 5-15..18] */
    u64   fault_va;       /**< IPR VA: virtual address of last Dstream miss or fault [HRM p 5-4] */
    u64   va_form_va;     /**< Address used for VA_FORM computation (may differ from VA for VPTE) */
    u64   exc_sum;        /**< IPR EXC_SUM: exception summary [HRM p 5-13..15] */
    int   i_ctl_va_mode;  /**< IPR I_CTL: (va_form_32 + va_48) [HRM p 5-15..17] */
    int   va_ctl_va_mode; /**< IPR VA_CTL: (va_form_32 + va_48) [HRM p 5-4] */
    u64   i_ctl_vptb;     /**< IPR I_CTL: vptb (virtual page table base) [HRM p 5-15..16] */
    u64   va_ctl_vptb;    /**< IPR VA_CTL: vptb (virtual page table base) [HRM p 5-4] */
    int   asn1;         /**< IPR DTB_ASN1: asn (address space number) [HRM p 5-28] */
    int   eien;         /**< IPR IER_CM: eien (external interrupt enable) [HRM p 5-9..10] */
    int   slen;         /**< IPR IER_CM: slen (serial line interrupt enable) [HRM p 5-9..10] */
    int   cren;         /**< IPR IER_CM: cren (corrected read error int enable) [HRM p 5-9..10] */
    int   pcen;         /**< IPR IER_CM: pcen (perf counter interrupt enable) [HRM p 5-9..10] */
    int   sien;         /**< IPR IER_CM: sien (software interrupt enable) [HRM p 5-9..10] */
    int   asten;        /**< IPR IER_CM: asten (AST interrupt enable) [HRM p 5-9..10] */
    int   sir;          /**< IPR SIRR: sir (software interrupt request) [HRM p 5-10..11] */
    std::atomic<int> eir;  /**< external interrupt request; raised cross-thread by irq_h() */
    int   slr;          /**< serial line interrupt request */
    int   crr;          /**< corrected read error interrupt */
    int   pcr;          /**< perf counter interrupt */
    int   astrr;        /**< IPR PCTX: astrr (AST request) [HRM p 5-21..22] */
    int   aster;        /**< IPR PCTX: aster (AST enable) [HRM p 5-21..22] */
    u64   i_ctl_other;  /**< various bits in IPR I_CTL that have no meaning to the emulator */
    u64   mm_stat;      /**< IPR MM_STAT: memory management status [HRM p 5-28..29] */
    bool  hwe;          /**< IPR I_CLT: hwe (allow palmode ins in kernel mode) [HRM p 5-15..17] */
    int   m_ctl_spe;    /**< IPR M_CTL: spe (Super Page mode enabled) [HRM p 5-29..30] */
    int   i_ctl_spe;    /**< IPR I_CTL: spe (Super Page mode enabled) [HRM p 5-15..18] */
    u64   pmpc;
    u64   fpcr;         /**< Floating-Point Control Register [HRM p 2-36] */
    bool  bIntrFlag;

    /**
     * \brief Instruction cache entry.
     *
     * An instruction cache entry contains the address and address space number
     * (ASN) + 16 32-bit instructions. [HRM 2-11]
     **/
    struct SICache
    {
      int   asn;        /**< Address Space Number */
      u32   data[ICACHE_LINE_SIZE]; /**< Actual cached instructions  */
      u64   address;          /**< Address of first instruction */
      u64   p_address;        /**< Physical address of first instruction */
      bool  asm_bit;          /**< Address Space Match bit */
      bool  valid;            /**< Valid cache entry */
    } icache[ICACHE_ENTRIES]; /**< Instruction cache entries [HRM p 2-11] */
    int next_icache;          /**< Number of next cache entry to use */
    int last_found_icache;    /**< Number of last cache entry found */

    /**
     * \brief Translation Buffer Entry.
     *
     * A translation buffer entry provides the mapping from a page of virtual memory to a page of physical memory.
     **/
    struct STBEntry
    {
      u64   virt;         /**< Virtual address of page*/
      u64   phys;         /**< Physical address of page*/
      u64   match_mask;   /**< The virtual address has to match for these bits to be a hit*/
      u64   keep_mask;    /**< This part of the virtual address is OR-ed with the phys address*/
      int   asn;          /**< Address Space Number*/
      int   asm_bit;      /**< Address Space Match bit*/
      int   access[2][4]; /**< Access permitted [read/write][current mode]*/
      int   fault[3];     /**< Fault on access [read/write/execute]*/
      bool  valid;        /**< Valid entry*/
    } tb[2][TB_ENTRIES];  /**< Unified Dstream TB model plus Istream TB entries */

    int   next_tb[2];     /**< Number of next translation buffer entry to use */
    int   last_found_tb[2][2];  /**< Number of last translation buffer entry found */
    u32   rem_ins_in_page;      /**< Number of instructions remaining in current page */
    int   iProcNum; /**< number of the current processor (0 in a 1-processor system) */
    u64   last_tb_virt;       /**< ITB_TAG staging register for ITB_PTE writes */
    bool  pal_vms;            /**< True if the PALcode base is 0x8000 (=VMS PALcode base) */
    int   irq_h_timer[6];     /**< Timers for delayed IRQ_H[0:5] assertion */
  } state;  /**< Determines CPU state that needs to be saved to the state file */

  u64 last_dtb_virt[2];   /**< DTB_TAG0/1 staging registers for DTB_PTE0/1 writes */

#ifdef IDB
  u64 current_pc_physical;  /**< Physical address of current instruction */
  u32 last_instruction;
  u64 last_read_loc;
  u64 last_write_loc;
#endif
};

/** Translate raw register (0..31) number to a number that takes PALshadow
    registers into consideration (0..63). Considers the program counter
    (to determine if we're in PALmode), and the SDE (Shadow Enable) bit. */
#define RREG(a)                                                          \
    (                                                                    \
      ((a) & 0x1f) +                                                     \
        (((state.pc & 1) && (((a) & 0xc) == 0x4) && state.sde) ? 32 : 0) \
    )

    /**
     * Empty the instruction cache.
     **/
inline void CAlphaCPU::flush_icache()
{
  if (icache_enabled)
  {
    for (int i = 0; i < ICACHE_ENTRIES; i++) {
      state.icache[i].valid = false;
    }
    state.next_icache = 0; // old version, may be relied on elsewhere
    state.last_found_icache = 0;
  }
  break_seq_icache();
#ifdef ES40_JIT
  jit_flush_blocks();
#endif
}

/**
 * Empty the instruction cache of lines with the ASM bit clear.
 **/
inline void CAlphaCPU::flush_icache_asm()
{
  if (icache_enabled)
  {
    int i;
    for (i = 0; i < ICACHE_ENTRIES; i++)
      if (!state.icache[i].asm_bit)
        state.icache[i].valid = false;
  }
  break_seq_icache();
#ifdef ES40_JIT
  jit_flush_blocks_asm();   // preserve global (ASM-bit) JIT blocks, matching the icache ASM-bit rule
#endif
}

/**
 * Set the PALcode BASE register, and determine whether we're running VMS PALcode.
 **/
inline void CAlphaCPU::set_PAL_BASE(u64 pb)
{
  u64  old_base = state.pal_base;
  state.pal_base = pb;
  bool was_vms = state.pal_vms;

  // VMS PALcode uses base 0x8000
  state.pal_vms = (pb == U64(0x8000)) && !vmspal_lle_enabled;
  //state.pal_vms = false;

#ifdef DEBUG_PAL
  if (pb != old_base || was_vms != state.pal_vms)
  {
    // Only the VMS PALcode (base 0x8000) is a type the emulator knows about.
    const char* type = (pb == U64(0x8000)) ? "VMS PALcode" : "unknown PALcode type";
    printf("%%CPU-I-PALSWITCH: cpu%d PAL_BASE %016" PRIx64 " -> %016" PRIx64 ": %s, %s\n",
      get_cpuid(), old_base, pb, type,
      state.pal_vms        ? "vmspal replacement routines engaged" :
      vmspal_lle_enabled   ? "running native (replacements disabled)" :
                             "running native (no replacements for this type)");
  }
#endif

#ifdef DEBUG_PAL
  printf("%%CPU-I-PALSWITCH: PAL=%016" PRIx64 " p21=%016" PRIx64 " p22=%016" PRIx64 " r22=%016" PRIx64 "\n", pb, state.r[53], state.r[54], state.r[22]);
  // Dump PAL scratch area contents for non-VMS PAL
  if (!state.pal_vms && state.r[53] != 0) {

    u64 scratch = U64(0x7cf420);

    printf("%%CPU-I-PALSCR: Scratch area at %016" PRIx64 ":\n", scratch);
    printf("%%CPU-I-PALSCR:   +0x00 VPTB = %016" PRIx64 "\n", cSystem->ReadMem(scratch + 0x00, 64, this));
    printf("%%CPU-I-PALSCR:   +0x08 PTBR = %016" PRIx64 "\n", cSystem->ReadMem(scratch + 0x08, 64, this));
    printf("%%CPU-I-PALSCR:   +0x10 PCBB = %016" PRIx64 "\n", cSystem->ReadMem(scratch + 0x10, 64, this));
    printf("%%CPU-I-PALSCR:   +0x18 KSP  = %016" PRIx64 "\n", cSystem->ReadMem(scratch + 0x18, 64, this));
    printf("%%CPU-I-PALSCR:   +0x98 WHAMI= %016" PRIx64 "\n", cSystem->ReadMem(scratch + 0x98, 64, this));
    printf("%%CPU-I-PALSCR:   +0x170 SCBB= %016" PRIx64 "\n", cSystem->ReadMem(scratch + 0x170, 64, this));
  }
  else if (!state.pal_vms && state.r[53] == 0) {
    printf("%%CPU-W-NOP21: PAL switched but p21=0! Scratch area not available.\n");
  }
#endif
}

/**
 * Get an instruction from the instruction cache.
 * If necessary, fill a new cache block from memory.
 *
 * get_icache checks all cache entries, to see if there is a
 * cache entry that matches the current address space number,
 * and that contains the address we're looking for. If it
 * exists, the instruction is fetched from this cache,
 * otherwise, the physical address for the instruction is
 * calculated, and the cache block is filled.
 *
 * The last cache entry that was a hit is remembered, so that
 * cache entry is checked first on the next instruction. (very
 * likely to be the same cache block)
 *
 * It would be easiest to do without the instruction cache
 * altogether, but unfortunately SRM uses self-modifying
 * code, that relies on the correct instruction stream to
 * remain in the cache.
 **/
inline int CAlphaCPU::get_icache(u64 address, u32* data)
{
  // Direct-map the icache: 2 KiB lines (ICACHE_LINE_SIZE * 4 == 2048 bytes).
  // Use VA[...:11] as the set index. The PAL bit (VA<0>) remains part of the tag.
  const u64 kLineShift = 11;
  const u64 v_aligned = address & ~U64(0x3);
  const int i = (int)((v_aligned >> kLineShift) & (ICACHE_ENTRIES - 1));
  u64   v_a;
  u64   p_a;
  int   result;
  bool  asm_bit;

  if (icache_enabled)
  {
    // ---- Fast hit probe
    if (state.icache[i].valid &&
      (state.icache[i].asn == state.asn || state.icache[i].asm_bit) &&
      state.icache[i].address == (address & ICACHE_MATCH_MASK)) {

      *data = endian_32(state.icache[i].data[(address >> 2) & ICACHE_INDEX_MASK]);

      // keep debug/pc_phys coherent even when icache is enabled
      state.pc_phys = state.icache[i].p_address + (address & ICACHE_BYTE_MASK);

#ifdef IDB
      current_pc_physical = state.pc_phys;
#endif
      state.last_found_icache = i;
      return 0;
    }

    // ---- Miss: translate + fill
    v_a = address & ICACHE_MATCH_MASK;
    if (address & 1) {
      // PALmode: VA<0> is PAL marker; physical is VA with bit0 cleared
      p_a = v_a & ~U64(0x1);
      asm_bit = true;
    }
    else {
      result = virt2phys(v_a, &p_a, ACCESS_EXEC, &asm_bit, 0);
      if (result) return result;
    }

    // Attempt to get a pointer into DRAM. If this is PIO (e.g., TIG flash),
      // PtrToMem returns null and we must *not* try to memcpy from it.
    char* mem = cSystem->PtrToMem(p_a);
    if (mem && cSystem->PtrToMem(p_a + ((ICACHE_LINE_SIZE * 4) - 1))) {
      // DRAM-backed: fill the direct-mapped icache line.
      memcpy(state.icache[i].data, mem, ICACHE_LINE_SIZE * 4);
      state.icache[i].valid = true;
      state.icache[i].asn = state.asn;
      state.icache[i].asm_bit = asm_bit;
      state.icache[i].address = address & ICACHE_MATCH_MASK;
      state.icache[i].p_address = p_a;

      *data = endian_32(state.icache[i].data[(address >> 2) & ICACHE_INDEX_MASK]);

      // same pc_phys update on fill
      state.pc_phys = p_a + (address & ICACHE_BYTE_MASK);
#ifdef IDB
      current_pc_physical = state.pc_phys;
#endif
      state.last_found_icache = i;
      return 0;
    }
    else {
      // PIO/TIG-backed: cannot fill icache lines.
        // Read exactly the requested instruction as 4 byte reads via the system bus.
      const u64 p_instr = p_a + (address & ICACHE_BYTE_MASK);
      u32 ins = 0;
      ins |= (u8)cSystem->ReadMem(p_instr + 0, 8, this);
      ins |= ((u8)cSystem->ReadMem(p_instr + 1, 8, this)) << 8;
      ins |= ((u8)cSystem->ReadMem(p_instr + 2, 8, this)) << 16;
      ins |= ((u8)cSystem->ReadMem(p_instr + 3, 8, this)) << 24;
      *data = ins; // already in target little-endian form

      state.pc_phys = p_instr;
#ifdef IDB
      current_pc_physical = state.pc_phys;
#endif
      return 0;
    }
  }

  // ---- Icache disabled (unchanged)
  if (address & 1) {
    state.pc_phys = address & ~U64(0x3);
    state.rem_ins_in_page = 1;
  }
  else {
    if (!state.rem_ins_in_page) {
      result = virt2phys(address, &state.pc_phys, ACCESS_EXEC, &asm_bit, 0);
      if (result) return result;
      state.rem_ins_in_page = 2048 - ((((u32)address) >> 2) & 2047);
    }
  }

  *data = (u32)cSystem->ReadMem(state.pc_phys, 32, this);
  return 0;
}


/**
 * Convert a virtual address to va_form format.
 * Used for IPR VA_FORM [HRM 5-5..6] and IPR IVA_FORM [HRM 5-9].
 **/
inline u64 CAlphaCPU::va_form(u64 address, bool bIBOX)
{
  switch (bIBOX ? state.i_ctl_va_mode : state.va_ctl_va_mode)
  {
  case 0:
    return((bIBOX ? state.i_ctl_vptb : state.va_ctl_vptb) & U64(0xfffffffe00000000)) | ((address >> 10) & U64(0x00000001fffffff8));

  case 1:
    return
      ((bIBOX ? state.i_ctl_vptb : state.va_ctl_vptb) & U64(0xfffff80000000000)) | ((address >> 10) & U64(0x0000003ffffffff8)) |
      (((address >> 10) & U64(0x0000002000000000)) * U64(0x3e));

  case 2:
    return((bIBOX ? state.i_ctl_vptb : state.va_ctl_vptb) & U64(0xffffffffc0000000)) | ((address >> 10) & U64(0x00000000003ffff8));
  }

  return 0;
}

/**
 * Return processor number.
 **/
inline int CAlphaCPU::get_cpuid()
{
  return state.iProcNum;
}

// CPU-local mode.
inline void CAlphaCPU::kick_int_if_pending()
{
  if ((state.eien & state.eir) || (state.sien & state.sir)
      || (state.asten
          && (state.aster & state.astrr & ((1 << (state.cm + 1)) - 1))))
    state.check_int = true;
}

/**
 * Assert or release an external interrupt line to the cpu.
 **/
inline void CAlphaCPU::irq_h(int number, bool assert, int delay)
{
  const u64 bit = U64(0x1) << number;
  bool  active = (state.eir & bit) || state.irq_h_timer[number];
  if (assert && !active)
  {
    if (delay)
    {
      state.irq_h_timer[number] = delay;
      state.check_timers = true;
    }
    else
    {
      state.eir |= bit;
      state.check_int = true;
    }

    return;
  }

  if (assert && (state.eir & bit))
  {
    // New request on an already-high line - a second device asserting while
    // the first is still being serviced. 
    // The level in eir is already correct, but check_int was consumed when the 
    // interrupt was dispatched, and after its REI nothing re-polls.
    // Re-kick so it is picked up at the next dispatch boundary, matching real hardware.
    state.check_int = true;
    return;
  }

  if (!assert && active)
  {
    state.eir &= ~bit;
    state.irq_h_timer[number] = 0;
    state.check_timers = false;
    for (int i = 0; i < 6; i++)
    {
      if (state.irq_h_timer[i])
        state.check_timers = true;
    }
  }
}

/**
 * Return program counter value.
 **/
inline u64 CAlphaCPU::get_pc()
{
  return state.pc;
}

#ifdef IDB

/**
 * Return the physical address the program counter refers to.
 **/
inline u64 CAlphaCPU::get_current_pc_physical()
{
  return state.pc_phys;
}
#endif

/**
 * Return program counter value without PALmode bit.
 **/
inline u64 CAlphaCPU::get_clean_pc()
{
  return state.pc & ~U64(0x3);
}

/**
 * Jump to next instruction
 **/
inline void CAlphaCPU::next_pc()
{
  state.pc += 4;
  state.pc_phys += 4;
  if (state.rem_ins_in_page)
    state.rem_ins_in_page--;
}

/**
 * Set program counter to a certain value.
 **/
inline void CAlphaCPU::set_pc(u64 p_pc)
{
  state.pc = p_pc;
  state.rem_ins_in_page = 0;
  seq_remaining = 0;
}

/**
 * Add  value to the program counter.
 **/
inline void CAlphaCPU::add_pc(u64 a_pc)
{
  state.pc += a_pc;
  state.rem_ins_in_page = 0;
  seq_remaining = 0;
}

/**
 * Get a register value.
 * If \a translate is true, use shadow registers if currently enabled.
 **/
inline u64 CAlphaCPU::get_r(int i, bool translate)
{
  if (translate)
    return state.r[RREG(i)];
  else
    return state.r[i];
}

/**
 * Get a fp register value.
 **/
inline u64 CAlphaCPU::get_f(int i)
{
  return state.f[i];
}

/**
 * Set a register value
 **/
inline void CAlphaCPU::set_r(int reg, u64 value)
{
  state.r[reg] = value;
}

/**
 * Set a fp register value
 **/
inline void CAlphaCPU::set_f(int reg, u64 value)
{
  state.f[reg] = value;
}

/**
 * Get the PALcode base register.
 **/
inline u64 CAlphaCPU::get_pal_base()
{
  return state.pal_base;
}

/**
 * Get the processor base register.
 * A bit fuzzy...
 **/
inline u64 CAlphaCPU::get_prbr(void)
{
  u64   v_prbr; // virtual
  u64   p_prbr; // physical
  bool  b;
  if (state.r[21 + 32] && ((u64)(state.r[21 + 32] + 0xaf) < (u64)
    ((U64(0x1) << cSystem->get_memory_bits()))))
    v_prbr = cSystem->ReadMem(state.r[21 + 32] + 0xa8, 64, this);
  else
    v_prbr = cSystem->ReadMem(0x70a8 + (0x200 * get_cpuid()), 64, this);
  if (virt2phys(v_prbr, &p_prbr, ACCESS_READ | FAKE | NO_CHECK, &b, 0))
    p_prbr = v_prbr;
  if ((u64)p_prbr > (u64)(U64(0x1) << cSystem->get_memory_bits()))
    p_prbr = 0;
  return p_prbr;
}

/**
 * Get the hardware process control block address.
 **/
inline u64 CAlphaCPU::get_hwpcb(void)
{
  u64   v_pcb;  // virtual
  u64   p_pcb;  // physical
  bool  b;
  if (state.r[21 + 32] && ((u64)(state.r[21 + 32] + 0x17) < (u64)
    ((U64(0x1) << cSystem->get_memory_bits()))))
    v_pcb = cSystem->ReadMem(state.r[21 + 32] + 0x10, 64, this);
  else
    v_pcb = cSystem->ReadMem(0x7010 + (0x200 * get_cpuid()), 64, this);
  if (virt2phys(v_pcb, &p_pcb, ACCESS_READ | NO_CHECK | FAKE, &b, 0))
    p_pcb = v_pcb;
  if (p_pcb > (u64)(U64(0x1) << cSystem->get_memory_bits()))
    p_pcb = 0;
  return p_pcb;
}

#if defined(IDB)
/**
 * Return the last instruction executed.
 **/
inline u32 CAlphaCPU::get_last_instruction(void)
{
  return last_instruction;
}
#endif
extern bool bTB_Debug;
#endif // !defined(INCLUDED_ALPHACPU_H)
