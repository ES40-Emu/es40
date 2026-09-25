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
  * Contains the definitions for the emulated Typhoon Chipset devices.
  *
  * $Id$
  *
  * X-1.34       Camiel Vanderhoeven                             26-MAR-2008
  *      Fix compiler warnings.
  *
  * X-1.33       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.32       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.31       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.30       Camiel Vanderhoeven                             11-MAR-2008
  *      Named, debuggable mutexes.
  *
  * X-1.29       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.28       Camiel Vanderhoeven                             04-MAR-2008
  *      Support some basic MP features. (CPUID read from C-Chip MISC
  *      register, inter-processor interrupts)
  *
  * X-1.27       Camiel Vanderhoeven                             26-FEB-2008
  *      Added CF8/CFC method for getting at PCI config space.
  *
  * X-1.26       Camiel Vanderhoeven                             08-FEB-2008
  *      Show originating device name on memory errors.
  *
  * X-1.25       Camiel Vanderhoeven                             04-FEB-2008
  *      Corrected a typo in the definition of PTE_SHIFT (2 should be 12)
  *      that caused an OpenVMS procgone bugcheck.
  *
  * X-1.24       Camiel Vanderhoeven                             01-FEB-2008
  *      Avoid unnecessary shift-operations to calculate constant values.
  *
  * X-1.23       Camiel Vanderhoeven                             08-JAN-2008
  *      Layout of comments.
  *
  * X-1.22       Camiel Vanderhoeven                             08-JAN-2008
  *      Split out chipset registers.
  *
  * X-1.21       Camiel Vanderhoeven                             07-JAN-2008
  *      DMA scatter/gather access. Split out some things.
  *
  * X-1.20       Camiel Vanderhoeven                             02-JAN-2008
  *      Comments.
  *
  * X-1.19       Camiel Vanderhoeven                             10-DEC-2007
  *      Added get_cpu
  *
  * X-1.18       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator.
  *
  * X-1.17       Camiel Vanderhoeven                             2-DEC-2007
  *      Added support for code profiling, and for direct operations on the
  *      Tsunami/Typhoon's interrupt registers.
  *
  * X-1.16       Brian Wheeler                                   1-DEC-2007
  *      Added panic.
  *
  * X-1.15       Camiel Vanderhoeven                             16-NOV-2007
  *      Replaced PCI_ReadMem and PCI_WriteMem with PCI_Phys.
  *
  * X-1.14       Camiel Vanderhoeven                             18-APR-2007
  *      Faster lockstep mechanism (send info 50 cpu cycles at a time)
  *
  * X-1.13       Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.12       Camiel Vanderhoeven                             10-APR-2007
  *      Replaced LoadROM(...) and SelectROM() calls with a single LoadROM()
  *      call. (See System.cpp, X-1.23).
  *
  * X-1.11       Camiel Vanderhoeven                             10-APR-2007
  *      Removed obsolete ROM-handling code.
  *
  * X-1.10       Camiel Vanderhoeven                             30-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.9        Camiel Vanderhoeven                             1-MAR-2007
  *      Removes $-sign from variable names. The Sun C-compiler can't handle
  *      these.
  *
  * X-1.8        Camiel Vanderhoeven                             18-FEB-2007
  *      Added iSSCycles variable to handle cycle-counting/slow-clocking in
  *      single-step mode.
  *
  * X-1.7        Camiel Vanderhoeven                             16-FEB-2007
  *   a) Replaced DoClock with run and single_step.
  *   b) Added support for slow-clocked devices.
  *
  * X-1.6        Camiel Vanderhoeven                             12-FEB-2007
  *      Added comments.
  *
  * X-1.5        Brian Wheeler                                   7-FEB-2007
  *      CSystem constructor takes filename of configuration file as an
  *      argument.
  *
  * X-1.4        Camiel Vanderhoeven                             7-FEB-2007
  *      Added trace (pointer to CTraceEngine)
  *
  * X-1.3        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.2        Brian Wheeler                                   3-FEB-2007
  *      Add support for configuration file.
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2007
  *      Initial version in CVS.
  *
  * \author Camiel Vanderhoeven (camiel@camicom.com / http://www.camicom.com)
  **/
#include "SystemComponent.h"
#include "TraceEngine.h"
#include "i2c_spd.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if !defined(INCLUDED_SYSTEM_H)
#define INCLUDED_SYSTEM_H

class CDisplayOutput;
class CVGA;

#if defined(PROFILE)
#define PROFILE_FROM      U64(0x8000)
#define PROFILE_TO        U64(0x1a81c0)
#define PROFILE_AFTER     U64(0x200000)
#define PROFILE_BUCKSIZE  16
#define PROFILE_LENGTH    (PROFILE_TO - PROFILE_FROM)
#define PROFILE_INSTS     (PROFILE_LENGTH / 4)
#define PROFILE_BUCKETS   (PROFILE_INSTS / PROFILE_BUCKSIZE)
#define PROFILE_YN(a) \
    ((a >= PROFILE_FROM) && (a < PROFILE_TO) && profile_started)
#define PROFILE_BUCKET(a) \
  profile_buckets[(a - PROFILE_FROM) / 4 / PROFILE_BUCKSIZE]
#define PROFILE_DO(a)                    \
  if((a & (~U64(0x3))) >= PROFILE_AFTER) \
    profile_started = true;              \
  if(PROFILE_YN(a))                      \
  {                                      \
    PROFILE_BUCKET(a)++;                 \
    profiled_insts++;                    \
  }

extern u64    profile_buckets[PROFILE_BUCKETS];
extern u64    profiled_insts;
extern bool   profile_started;
#endif
#if defined(LS_MASTER) || defined(LS_SLAVE)
extern char* dbg_strptr;
#endif

struct MPDState {
  // Host open-drain drivers (1 = released high, 0 = pulling low)
  bool cks_out = true;   // SCL
  bool ds_out = true;   // SDA
};

/// Structure used for mapping memory ranges to devices.
struct SMemoryUser
{
  CSystemComponent* component;  /**< Device that occupies this range. */
  int                 index;      /**< Index within the device. Used by devices that occupy more than one range. */
  u64                 base;       /**< Address of first byte. */
  u64                 length;     /**< Number of bytes in range. */
  // Geometry only: another range's span intersects this one. Recomputed on
  // every registration or remap; eligibility is always queried live.
  bool                may_overlap = true;
};

/// Structure used for configuration values.
struct SConfig
{
  char* key;    /**< Name of the value. */
  char* value;  /**< Value of the value. */
};

/**
 * \brief Emulated Typhoon 21272 chipset.
 *
 * Documentation consulted:
 *  - Tsunami/Typhoon 21272 Chipset Hardware Reference Manual  [HRM] (http://download.majix.org/dec/tsunami_typhoon_21272_hrm.pdf)
 *  - AlphaServer ES40 and AlphaStation ES40 Service Guide [SG] (http://www.dec-store.com/PD_00158.aspx)
 *  - Tru64 include file dc104x.h [T64] (http://samy.pl/packet/MISC/tru64/usr/include/alpha/dc104x.h)
 *  .
 *
 * The ES40 emulator has the following chipset configuration:
 *   - 1 x 21274-C1 Cchip (controller chip) - The Cchip controls the other chips in the
 *     chipset, as well as the DRAM memory array in a system. The Cchip interfaces with
 *     the CPU’s command and address buses.
 *   - 8 x 21274-D1 Dchip (data slice chip) - The Dchips interface with the system data
 *     bus and provide the data path between the CPU, DRAM memory, and the Pchip(s).
 *   - 2 x 21272-P1 Pchip (peripheral interface chip) - The interface to the PCI bus.
 *   .
 **/
class CSystem
{
public:
  void          DumpMemory(unsigned int filenum);
  char* PtrToMem(u64 address);
  unsigned int  get_memory_bits();
  bool          RestoreState(const char* fn);
  void          SaveState(const char* fn);
  void          flush_storage();
  u64           PCI_Phys(int pcibus, u32 address);
  u64           PCI_Phys_direct_mapped(u32 address, u64 wsm, u64 tba);
  u64           PCI_Phys_scatter_gather(u32 address, u64 wsm, u64 tba);
  void          interrupt(int number, bool assert);
  u32           get_tick_seq() { return m_tick_seq.load(std::memory_order_relaxed); }
  int           LoadROM();
  void          PrepareX86OptionROMMemory();
  u64           ReadMem(u64 address, int dsize, CSystemComponent* source);
  void          WriteMem(u64 address, int dsize, u64 data,
    CSystemComponent* source);
  // Take before device-private locks when it can perform DMA or mapped I/O. 
  // Recursive for nested device dispatch; never hold across thread waits/joins. 
  // Host callbacks already holding a host lock must not wait here.
  std::recursive_mutex& get_device_bus_mutex() { return device_bus_mutex; }
  void          Run();
  int           SingleStep();

  void          init();
  void          start_threads();
  void          stop_threads();

  // True while we are performing an in-process reset (stop/reset/start).
  // Devices (S3/SDL) use this to PAUSE instead of destroying the window.
  void          SetResetInProgress(bool v) { m_reset_in_progress.store(v, std::memory_order_release); }
  bool          IsResetInProgress() const { return m_reset_in_progress.load(std::memory_order_acquire); }

  int           RegisterMemory(CSystemComponent* component, int index,
    u64 base, u64 length);
  int           RegisterComponent(CSystemComponent* component);
  void          UnregisterComponent(CSystemComponent* component);
  int           RegisterCPU(class CAlphaCPU* cpu);

  bool has_vga_device(const CVGA* exclude = nullptr) const noexcept;
  // Unambiguous fallback for missing firmware metadata; null for zero/many.
  const CVGA* get_sole_vga_device() const noexcept;

  struct SDisplayOutput
  {
    const CSystemComponent* component;
    const CDisplayOutput* output;
  };
  // Query completed devices for outputs
  std::vector<SDisplayOutput> get_display_outputs() const;

  CSystem(CConfigurator* cfg);
  void          ResetMem(unsigned int membits);

  CAlphaCPU* get_cpu(int cpunum) { return acCPUs[cpunum]; };
  int           get_cpu_num() { return iNumCPUs; };

  // We determine the PALcode (native or optimized C++ replacements) during config ingestion
  // if any CPU has nohle = true set. If any single CPU has it set, we apply to all
  void          request_native_pal(const char* why)
  {
    if (!bNativePal)
      printf("%%SYS-I-NATIVEPAL: %s: running native PALcode on all CPUs, vmspal replacement routines disabled.\n", why);
    bNativePal = true;
  };
  bool          native_pal_requested() { return bNativePal; };

  // DIMM topology: 4 MMBs of 8 slots; array a lives on MMB a as sets of 4
  // identical DIMMs (J1-4 lower set, J5-8 upper set). Max config = 4 arrays
  // x 8 x 1GB DIMMs = 32GB (all 32 slots).
  struct SDimmLayout
  {
    int      n_arrays;        // populated arrays == populated MMBs (1, 2 or 4)
    int      dimms_per_array; // 8 (twice-split) or 4 (lower set only)
    uint32_t dimm_mb;         // capacity of each (identical) DIMM
  };
  const SDimmLayout& get_dimm_layout() { return m_dimm_layout; };
  const std::vector<uint8_t>& get_dimm_spd() { return m_dimm_spd; };

  virtual       ~CSystem();
  unsigned int  iNumMemoryBits;

  void          panic(char* message, int flags);

#define PANIC_NOSHUTDOWN  0
#define PANIC_SHUTDOWN    1
#define PANIC_ASKSHUTDOWN 2
#define PANIC_LISTING     4
  void          clear_clock_int(int ProcNum);
  void          clear_ipi(int ProcNum);   // ack interprocessor interrupt: clear MISC<IPINTR>, drop b_irq<3>
  u64           get_c_misc();
  u64           get_c_dir(int ProcNum);
  u64           get_c_dim(int ProcNum);
  void          set_c_dim(int ProcNum, u64 value);

  void          cpu_lock(int cpuid, u64 address, u64 value);   // LDx_L: record locked range + loaded value
  bool          cpu_take_lock(int cpuid, u64 address, u64* expected, bool* same_address);
  u64           cpu_stx_c(int cpuid, u64 phys, int size_bits, u64 value,
                          char* dram, u64 dram_sz, CSystemComponent* source);  // STx_C: 1 = success
  void          cpu_clear_lock(int cpuid);                     // exception/interrupt: drop the lock
  void          RequestSystemReset();
  bool          IsSystemResetRequested() const;
  bool          ProcessPendingReset();   // Returns true if a reset was performed.
  void          RequestShutdown() noexcept;
  bool          IsShutdownRequested() const noexcept;

private:
  void bind_isa_devices();
  // Recipients and coordinates captured before handlers; nested accesses may
  // remap registry entries without changing the outer transaction.
  struct SDecodeClaims
  {
    static constexpr int kMax = 16;
    int count = 0;
    SMemoryUser range[kMax];
    bool subtractive = false;
  };
  void recompute_range_overlaps();
  void collect_decode_claims(u64 address, int dsize, bool write,
    SDecodeClaims& claims, const CSystemComponent* source) const;
  void prepare_shared_access(u64 address, int dsize, bool write,
    const SDecodeClaims& claims, const CSystemComponent* source);
  u64 resolve_shared_read(u64 address, int dsize, const SDecodeClaims& claims,
    const CSystemComponent* source);
  [[noreturn]] void report_decode_overlap(u64 address, int dsize, bool write,
    const SDecodeClaims& claims, const CSystemComponent* source,
    const char* reason, const std::string& detail = std::string()) const;
  std::string describe_claimant(const SMemoryUser& range, u64 address) const;
  void log_shared_event(const std::string& key, const std::string& line);
  void note_pio_access(u64 address, int dsize, bool write, u64 data, int claims);
  void dispatch_pci_io_write(u64 address, int dsize, u64 data,
    const SDecodeClaims& claims);
  void          CheckForShutdown() const;
  void          ResetChipsetState();
  void          UpdateX86BIOSClock();
  std::atomic<bool> m_reset_requested{ false };
  // Host control request, deliberately outside serialized guest state.
  std::atomic<bool> m_shutdown_requested{ false };
  std::atomic<bool> m_reset_in_progress{ false };
  std::atomic<bool> m_reported_3c509_probe{ false };
  std::atomic<bool> m_x86_bios_clock_active{ false };
  u32           m_x86_bios_clock_initial_ticks{ 0 };
  u64           m_x86_bios_clock_elapsed_ticks{ 0 };
  std::chrono::steady_clock::time_point m_x86_bios_clock_epoch;
  // Chipset PIO decode for addresses above main memory (under device_bus_mutex).
  u64           pio_read(u64 address, int dsize, CSystemComponent* source);
  void          pio_write(u64 address, int dsize, u64 data, CSystemComponent* source);
  u64           cchip_csr_read(u32 address, CSystemComponent* source);
  void          cchip_csr_write(u32 address, u64 data, CSystemComponent* source);
  u64           pchip_csr_read(int num, u32 address);
  void          pchip_csr_write(int num, u32 address, u64 data);
  void          latch_pchip_error(int num, u64 data);
  void          pchip_config_write_abort(int num, u32 address);
  u8            dchip_csr_read(u32 address);
  void          dchip_csr_write(u32 address, u8 data);
  u8            tig_read(u32 address);
  void          tig_write(u32 address, u8 data);
  void          tig_update_halt_lines();
  void          update_pchip_error_irqs();
  void          recompute_device_irqs();

  // --- MPD / SPD wiring ---
  MPDState       m_mpd;
  I2CBus         m_mpd_bus;

  // Build SPD images that match configured memory.
  void init_spd_from_config_mb(uint32_t total_mb);
  SDimmLayout    m_dimm_layout;
  std::vector<uint8_t> m_dimm_spd; // shared SPD image (all DIMMs identical)

  int           iNumCPUs;
  bool          bNativePal = false;
  u64           cpu_lock_value[4]; // per-CPU LDx_L value, for same-address STx_C compare-and-swap

  // LL/SC ABA guard. 
  static const u32 kLLBuckets = 65536;   // 64B-line hash; power of two
  std::atomic<u32> m_ll_seq[kLLBuckets];
  std::atomic<u8>  m_ll_lk[kLLBuckets];  // serializes seq-check+CAS+bump per bucket
  u32              m_ll_seq_snap[4];     // per-CPU bucket sequence at LDx_L time

  // Serializes drir RMW + delivery in interrupt() across device threads. On
  // CSystem (not in saved 'state'), so SaveState is unaffected.
  std::mutex    drir_lock;
  // Shared by mapped-device dispatch and DMA; not part of saved guest state.
  std::recursive_mutex device_bus_mutex;
  std::atomic<u32> m_tick_seq{ 0 };  // interval-tick sequence; CPU instruction pacing

  /// The state structure contains all elements that need to be saved to the statefile.
  struct SSys_state
  {
    std::atomic<int> cpu_lock_flags;
    u64 cpu_lock_address[4];

    /**
   * TIGbus state data
   *
   * More details in: HRM, 6.3; T64. Detailed information is hard to find...
   *
   * The TIGbus (TTL Integrated Glue Logic) is the interface between the chipset and
   * the interrupt controller, flash ROM, and possibly some other system components.
   **/
    struct SSys_tig
    {
      u8  FwWrite;
      u8  HaltA;
      u8  HaltB;
      u8  ModInfo;
      u8  ipcr[5];  /* ipcr0-4 @ a00 thru b00: PAL MP-restart park/release */
    } tig;

    /**
   * CCHIP state data
   *
   * More details in: HRM, 1.2.1.
   *
   * The 21274-C1 Cchip (controller chip) is the heart of the ES40's Typhoon chipset. It
   * interfaces directly with the CPU's through the System address ports, it issues
   * controls to the Dchips (data slice chips) and Pchips (peripheral interface chips)
   * using the Dchip control ports, and the CAPbus (C-And-P-chip bus). It controls memory
   * using the DRAM command and address ports. It also controls the TIGbus.
   **/
    struct SSys_cchip
    {

      /**
     * DIM: Device Interrupt Mask Registers.
     *
     * These mask registers control which interrupts are allowed to go through to the
     * CPUs. No interrupt in DRIR will get through to the masked interrupt registers (and on
     * to interrupt the CPUs) unless the corresponding mask bit is set in DIMn. All bits are initialized
     * to 0 at reset.
     **/
      u64 dim[4];

      /**
     * DRIR: Device Raw Interrupt Request Register.
     *
     * DRIR indicates which of the 64 possible device interrupts is asserted.
     *
     * \code
     * +---------+---------+---------+------+-------------------------------------+
     * | Field   | Bits    | Type    | Init | Description                         |
     * +---------+---------+---------+------+-------------------------------------+
     * | ERR     | <63:58> | RO      | 0    | IRQ0 error interrupts               |
     * |         |         |         |      |    <63> Chip detected MISC<NXM>     |
     * |         |         |         |      |    <62> hookup to Pchip0 error      |
     * |         |         |         |      |    <61> hookup to Pchip1 errror     |
     * +---------+---------+---------+------+-------------------------------------+
     * | RES     | <57:56> | RO      | 0    | Reserved                            |
     * +---------+---------+---------+------+-------------------------------------+
     * | DEV     | <55:0>  | RO      | 0    | PCI interrupts pending to the CPU   |
     * +---------+---------+---------+------+-------------------------------------+
     * \endcode
     *
     * Combined with DIM[n] to form DIR[n]:
     *
     * DIR: Device Interrupt Request Registers.
     *
     * These registers indicate which interrupts are pending to the CPUs. If a raw request
     * bit is set and the corresponding mask bit is set, then the corresponding bit in this register
     * will be set and the appropriate CPU will be interrupted.
     **/
      u64 drir;

      /**
     * Miscellaneous Register (MISC - RW).
     *
     * +---------+---------+---------+------+-------------------------------------+
     * | Field   | Bits    | Type    | Init | Description                         |
     * +---------+---------+---------+------+-------------------------------------+
     * | RES     | <63:44> | MBZ,RAZ | 0    | Reserved.                           |
     * | DEVSUP  | <43:40> | WO      | 0    | Suppress IRQ1 interrupts to the CPU |
     * |         |         |         |      | corresponding to a 1 in this field  |
     * |         |         |         |      | until the interrupt polling machine |
     * |         |         |         |      | has completed a poll of all PCI     |
     * |         |         |         |      | devices.                            |
     * +---------+---------+---------+------+-------------------------------------+
     * | REV     | <39:32> | RO      | 8    | Latest revision of Cchip            |
     * +---------+---------+---------+------+-------------------------------------+
     * | NXS     | <31:29> | RO      | 0    | NXM source - Device that caused NXM |
     * |         |         |         |      | - UNPREDICTABLE if NXM is not set.  |
     * |         |         |         |      |   Value Source                      |
     * |         |         |         |      |   0..3  CPU 0..3                    |
     * |         |         |         |      |   4..5  Pchip 0..1                  |
     * +---------+---------+---------+------+-------------------------------------+
     * | NXM     | <28>    | R,W1C   | 0    | Nonexistent memory address detected.|
     * |         |         |         |      | Sets DRIR<63> and locks the NXS     |
     * |         |         |         |      | field until it is cleared.          |
     * +---------+---------+---------+------+-------------------------------------+
     * | RES     | <27:25> | MBZ,RAZ | 0    | Reserved.                           |
     * +---------+---------+---------+------+-------------------------------------+
     * | ACL     | <24>    | WO      | 0    | Arbitration clear - writing a 1 to  |
     * |         |         |         |      | this bit clears ABT and ABW fields. |
     * +---------+---------+---------+------+-------------------------------------+
     * | ABT     | <23:20> | R,W1S   | 0    | Arbitration try - writing a 1 to    |
     * |         |         |         |      | these bits sets them.               |
     * +---------+---------+---------+------+-------------------------------------+
     * | ABW     | <19:16> | R,W1S   | 0    | Arbitration won - writing a 1 to    |
     * |         |         |         |      | these bits sets them unless one is  |
     * |         |         |         |      | already set, in which case the      |
     * |         |         |         |      | write is ignored.                   |
     * +---------+---------+---------+------+-------------------------------------+
     * | IPREQ   | <15:12> | WO      | 0    | Interprocessor interrupt request -  |
     * |         |         |         |      | write a 1 to the bit corresponding  |
     * |         |         |         |      | to the CPU you want to interrupt.   |
     * |         |         |         |      | Writing a 1 here sets the corres-   |
     * |         |         |         |      | ponding bit in IPINTR.              |
     * +---------+---------+---------+------+-------------------------------------+
     * | IPINTR  | <11:8>  | R,W1C   | 0    | Interprocessor interrupt pending -  |
     * |         |         |         |      | one bit per CPU. Pin irq<3> is      |
     * |         |         |         |      | asserted to the CPU corresponding   |
     * |         |         |         |      | to a 1 in this field.               |
     * +---------+---------+---------+------+-------------------------------------+
     * | ITINTR  | <7:4>   | R,W1C   | 0    | Interval timer interrupt pending -  |
     * |         |         |         |      | one bit per CPU. Pin irq<2> is      |
     * |         |         |         |      | asserted to the CPU corresponding   |
     * |         |         |         |      | to a 1 in this field.               |
     * +---------+---------+---------+------+-------------------------------------+
     * | RES     | <3:2>   | MBZ,RAZ | 0    | Reserved.                           |
     * +---------+---------+---------+------+-------------------------------------+
     * | CPUID   | <1:0>   | RO      |      | ID of the CPU performing the read.  |
     * +---------+---------+---------+------+-------------------------------------+
     * \endcode
     **/
      u64 misc;
      u64 csc;
    } cchip;

    /**
   * DCHIP state data
   *
   * More details in: HRM, 1.2.2.
   *
   * The ES40 contains eight 21274-D1 Dchips (data slice chips). Each Dchip is
   * responsible for handling 8 bits of the 64-bit data bus (in the ES40, other configurations
   * using less Dchips are possible). Each Dchip interfaces with the Cchip for control,
   * with each of the Pchips, with each of the CPU's and with each of the DRAM arrays.
   **/
    struct SSys_dchip
    {
      u8  drev;
      u8  dsc;
      u8  dsc2;
      u8  str;
    } dchip;

    /**
   * PCHIP state data
   *
   * More details in: HRM, 1.2.3.
   *
   * The ES40 contains two 21272-P1 Pchips (peripheral interface chips). Each Pchip
   * controls one 64-bit PCI bus, and interfaces it to the Cchip and the Dchips.
   *
   * On PIO transfers from the CPU's (or PTP transfers from the other PCI bus), the Pchip
   * acts as bus master on the PCI bus.
   *
   * On DMA or PTP transfers from a PCI device, the Pchip acts as target on the PCI bus.
   * To determine on which addresses to respond, each Pchip contains 4 DMA/PTP windows,
   * that support both direct mapped and scatter-gather DMA/PTP memory access.
   **/
    struct SSys_pchip
    {
      u64 plat;
      u64 perr;
      u64 perrmask;
      u64 pctl;
      u64 wsba[4];
      u64 wsm[4];
      u64 tba[4];
    } pchip[2];

  } state;
  void* memory;

  //    void * memmap;
  int                   iNumComponents;
  std::vector<CSystemComponent*> acComponents;
  int                   iNumMemories;
  std::vector<std::unique_ptr<SMemoryUser>> asMemories;
  // Decode diagnostic policy
  bool stop_on_decode_conflict = false;
  // Emulator continuation for a shared read whose claimants disagree.
  // Not PCI spec, it's undefined there. 
  enum class SharedReadPolicy { Stop, Claimant, And, Or };
  SharedReadPolicy shared_read_policy = SharedReadPolicy::Stop;
  int shared_read_hose = -1;
  int shared_read_device = -1;
  std::unordered_map<std::string, u64> shared_event_counts;
  // Recent mapped PIO accesses, printed when an overlap stops or disagrees.
  struct SPioTrace { u64 address; u64 data; int dsize; int claims; bool write; };
  static constexpr int kPioTraceSize = 64;
  SPioTrace pio_trace[kPioTraceSize] = {};
  unsigned pio_trace_next = 0;
  void print_pio_trace() const;

  class CAlphaCPU* acCPUs[4];

  CConfigurator* myCfg;

  int                   iSingleStep;

#if defined(IDB)
  int                   iSSCycles;
#endif
};

inline u64 CSystem::get_c_misc()
{
  return state.cchip.misc;
}

inline u64 CSystem::get_c_dir(int ProcNum)
{
  return state.cchip.drir & state.cchip.dim[ProcNum];
}

inline u64 CSystem::get_c_dim(int ProcNum)
{
  return state.cchip.dim[ProcNum];
}

inline void CSystem::set_c_dim(int ProcNum, u64 value)
{
  state.cchip.dim[ProcNum] = value;
}

extern CSystem* theSystem;

/* constants for P-Chip CSR's */
#define PCI_PCTL_HOLE       U64(0x0000000000000020) /* <5>     */
#define PCI_PCTL_HOLE_START 0x00080000
#define PCI_PCTL_HOLE_END   0x000fffff

/* constants for pci-to-phys-address-mapping */
#define PCI_WSM_MASK      U64(0x00000000fff00000)   /* <31:20> */
#define PCI_ADD_MASK      U64(0x00000000000fffff)   /* <19:0>  */
#define PCI_TBA_MASK      U64(0x00000007fff00000)   /* <34:20> */
#define PCI_PTE_ADD_MASK  U64(0x00000000000fe000)   /* <19:13> */
#define PCI_PTE_ADD_SHIFT 10
#define PCI_PTE_TBA_MASK  U64(0x00000007fffffc00)   /* <34:10> */
#define PCI_PTE_MASK      U64(0x00000007ffffe000)   /* <34:13> */
#define PCI_PTE_SHIFT     12
#define PCI_PTE_ADD2_MASK U64(0x0000000000001fff)   /* <12:0>  */
#define PCI_PTE_PEER_BIT  U64(0x0000000090000000)   /* <31,28> */

#define PHYS_PIO_ACCESS   U64(0x0000080000000000)   /* <43>    */
#endif // !defined(INCLUDED_SYSTEM_H)
