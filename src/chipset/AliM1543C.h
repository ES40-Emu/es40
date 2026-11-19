/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by gdwnldsKSC
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
  * Contains the definitions for the ISA part of the emulated Ali M1543C chipset.
  *
  * $Id$
  *
  * X-1.34       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.33       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.32       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.31       Camiel Vanderhoeven                             11-MAR-2008
  *      Named, debuggable mutexes.
  *
  * X-1.30       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.29       Camiel Vanderhoeven                             26-FEB-2008
  *      Moved DMA code into it's own class (CDMA)
  *
  * X-1.28       Camiel Vanderhoeven                             12-FEB-2008
  *      Moved keyboard code into it's own class (CKeyboard)
  *
  * X-1.27       Camiel Vanderhoeven                             07-FEB-2008
  *      Comments.
  *
  * X-1.26       Brian Wheeler                                   02-FEB-2008
  *      Completed LPT support so it works with FreeBSD as a guest OS.
  *
  * X-1.25       Camiel Vanderhoeven                             08-JAN-2008
  *      Comments.
  *
  * X-1.24       Camiel Vanderhoeven                             02-JAN-2008
  *      Comments; moved keyboard status register bits to "status" struct.
  *
  * X-1.23       Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.22       Brian wheeler                                   17-DEC-2007
  *      Better DMA support.
  *
  * X-1.21       Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.20       Brian Wheeler                                   11-DEC-2007
  *      Improved timer logic (again).
  *
  * X-1.19       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator; move IDE and USB to their own classes.
  *
  * X-1.18       Camiel Vanderhoeven                             7-DEC-2007
  *      Add busmaster_status; add pic_edge_level.
  *
  * X-1.17       Camiel Vanderhoeven                             7-DEC-2007
  *      Generate keyboard interrupts when needed.
  *
  * X-1.16       Camiel Vanderhoeven                             6-DEC-2007
  *      Changed keyboard implementation (with thanks to the Bochs project!!)
  *
  * X-1.15       Brian Wheeler                                   1-DEC-2007
  *      Added console support (using SDL library), corrected timer
  *      behavior for Linux/BSD as a guest OS.
  *
  * X-1.14       Camiel Vanderhoeven                             16-APR-2007
  *      Added ResetPCI()
  *
  * X-1.13       Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.12       Camiel Vanderhoeven                             31-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.11	Camiel Vanderhoeven				3-MAR-2007
  *	Added inline function get_ide_disk, which returns a file handle.
  *
  * X-1.10	Camiel Vanderhoeven				20-FEB-2007
  *	Added member variable to keep error status.
  *
  * X-1.9	Brian Wheeler					20-FEB-2007
  *	Information about IDE disks is now kept in the ide_info structure.
  *
  * X-1.8	Camiel Vanderhoeven				16-FEB-2007
  *	DoClock now returns int.
  *
  * X-1.7	Camiel Vanderhoeven				12-FEB-2007
  *	Formatting.
  *
  * X-1.6	Camiel Vanderhoeven				12-FEB-2007
  *	Added comments.
  *
  * X-1.5        Camiel Vanderhoeven                             9-FEB-2007
  *      Replaced f_ variables with ide_ members.
  *
  * X-1.4        Camiel Vanderhoeven                             9-FEB-2007
  *      Added comments.
  *
  * X-1.3        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.2        Brian Wheeler                                   3-FEB-2007
  *      Includes are now case-correct (necessary on Linux)
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2007
  *      Initial version in CVS.
  *
  * \author Camiel Vanderhoeven
  **/
#if !defined(INCLUDED_ALIM1543C_H_)
#define INCLUDED_ALIM1543C_H_

#include "emu/PCIDevice.h"

#include <chrono>

class CAliM1543C_pmu;
class CDMA;

  /**
   * \brief Emulated ISA part of the ALi M1543C chipset.
   *
   * The ALi M1543C device provides i/o and glue logic support to the system:
   * ISA, DMA, Interrupt, Timer, TOY Clock.
   *
   * Documentation consulted:
   *  - Ali M1543C B1 South Bridge Version 1.20 (http://mds.gotdns.com/sensors/docs/ali/1543dScb1-120.pdf)
   *  - Keyboard Scancodes, by Andries Brouwer (http://www.win.tue.nl/~aeb/linux/kbd/scancodes.html)
   *  .
   **/
class CAliM1543C : public CPCIDevice, public CRunnable
{
public:
  virtual int   SaveState(FILE* f);
  virtual int   RestoreState(FILE* f);

  //    void instant_tick();
  //    void interrupt(int number);
  virtual void  run();
  virtual void  check_state();
  virtual void  WriteMem_Legacy(int index, u32 address, int dsize, u32 data);
  virtual u32   ReadMem_Legacy(int index, u32 address, int dsize);
  bool uses_subtractive_decode(int index, u64 address, int dsize,
    bool write) const noexcept override;
  bool decodes_memory_access(int index, u64 address, int dsize,
    bool write) const noexcept override;
  bool memory_decode_fallback(int index) const noexcept override;

  // Read existing register state under the system device-bus lock.
  enum class IsaIoDecode { None, Positive, Subtractive };
  IsaIoDecode isa_io_decode(u32 port) const noexcept;
  bool is_docking_mode() const noexcept;
  bool is_pmu_hidden() const noexcept;
  bool programmable_io_is_subtractive() const noexcept;
  bool superio_claims_port(u32 port, bool write) const noexcept;
  void bind_pmu(CAliM1543C_pmu* pmu) { docking_pmu = pmu; }
  void bind_dma(const CDMA& dma) { isa_dma = &dma; }
  u32 config_read_custom(int func, u32 address, int dsize, u32 data) override;
  void config_write_custom(int func, u32 address, int dsize,
    u32 old_data, u32 new_data, u32 data) override;

  void          do_pit_clock();

  CAliM1543C(CConfigurator* cfg, class CSystem* c, int pcibus, int pcidev);
  virtual       ~CAliM1543C();
  void          pic_interrupt(int index, int intno);
  void          pic_deassert(int index, int intno);
  void          pic_set_line(int index, int intno, bool active);

  void          set_floppy_presence(bool driveA, bool driveB);

  void          init();
  void          start_threads();
  void          stop_threads();

  // Battery-backed CMOS/TOY NVRAM file persistence (sys0 rom.toy)
  void          save_toy_nvram(bool verbose = false);
  void          restore_toy_nvram();
private:
  // Host wiring; the PMU's selector bytes already live in saved PCI state.
  CAliM1543C_pmu* docking_pmu = nullptr;
  const CDMA* isa_dma = nullptr;
  CThread* myThread = nullptr;
  CMutex* myRegLock;
  bool      StopThread;

  CFastMutex picLock{ "pic" };
  // Unlocked inner helpers — called only while picLock is held.
  void          pic_interrupt_inner(int index, int intno);
  void          pic_deassert_inner(int index, int intno);
  void          pic_set_line_inner(int index, int intno, bool active);

  // REGISTER 61 (NMI)
  u8        reg_61_read();
  void      reg_61_write(u8 data);

  // REGISTERS 70 - 73: TOY
  u8        toy_read(u32 address);
  void      toy_write(u32 address, u8 data);

  // Timer/Counter
  u8        pit_read(u32 address);
  void      pit_write(u32 address, u8 data);
  void      pit_clock();
  bool      pit_out(int c);

  // Wall-clock pacing for the 8254: elapsed host time is converted to
  // 1.193182 MHz input clocks; m_pit_acc carries the sub-clock remainder
  // (in ns * PIT_CLOCK_HZ units). m_pit_epoch marks each channel's last
  // counter load so pit_out() can derive the output pin analytically
  // (jitter-free) instead of from thread-tick decrements.
  // Not saved state - re-primed on restore.
  std::chrono::steady_clock::time_point m_pit_last;
  std::chrono::steady_clock::time_point m_pit_epoch[3];
  u64       m_pit_acc;

  // Wall-clock RTC periodic-flag (reg C PF) pacing, re-primed on rate change 
  std::chrono::steady_clock::time_point m_toy_pf_epoch;
  u64       m_toy_pf_count = 0;
  u64       m_toy_pf_freq = 0;

public:
  // Period in ns of the MC146818 SQW output (rate from TOY reg A).
  // CPU thread reads this every batch boundary to pace b_irq<2>.
  u64       get_interval_period_ns() const;
private:

  // interrupt controller
  u8        pic_read(int index, u32 address);
  void      pic_write(int index, u32 address, u8 data);
  u8        pic_read_vector();
  u8        pic_read_edge_level(int index);
  void      pic_write_edge_level(int index, u8 data);
  u8        pic_control_read(u32 address);
  void      pic_control_write(u32 address, u8 data);
  int       pic_get_irq(int index);
  void      pic_update_output(int index);
  void      pic_intack(int index, int irq);
  void      pic_init_reset(int index);

  // LPT controller
  u8        lpt_read(u32 address);
  void      lpt_write(u32 address, u8 data);
  void      lpt_reset();

  // Built-in Super I/O configuration interface
  void      superio_reset();
  u8        superio_read(u32 address);
  void      superio_write(u32 address, u8 data);
  u8        superio_current_reg() const;
  void      superio_apply_ldn(int ldn);

  // ISA Plug-and-Play protocol (ports 0x279 ADDRESS, 0xA79 WRITE_DATA,
  // and an OS-selectable READ_DATA port in 0x203-0x3FF).  We expose no
  // PnP cards, so the implementation only tracks enough protocol state
  // to swallow the OS's enumeration cleanly and answer "no cards".
  enum {
    PNP_WAIT_FOR_KEY = 0,
    PNP_SLEEP        = 1,
    PNP_ISOLATION    = 2,
    PNP_CONFIG       = 3
  };
  void      isapnp_addr_write(u8 data);
  void      isapnp_data_write(u8 data);
  u8        isapnp_data_read(u32 address);
  static const u8 isapnp_init_key[32];

  /// The state structure contains all elements that need to be saved to the statefile.
  struct SAli_state
  {

    // REGISTER 61 (NMI)
    u8    reg_61;

    // REGISTERS 70 - 73: TOY
    u8    toy_stored_data[256];
    u8    toy_access_ports[4];
    long  toy_offset;               // seconds: (user-set time) - (host time)

    // Timer/Counter
    u32   pit_counter[9];
#define PIT_OFFSET_LATCH  3
#define PIT_OFFSET_MAX    6
    u8    pit_status[4];
    u8    pit_mode[4];

    // interrupt controller  IRR (request) and ISR (in-service) are kept 
    // separate so an edge that arrives while a higher-priority IRQ is in 
    // service is still latched and re-fires after EOI. 
    u8    pic_irr[2];               // raw interrupt request register
    u8    pic_imr[2];               // interrupt mask register (1 = masked)
    u8    pic_isr[2];               // in-service register
    u8    pic_last_irr[2];          // line history for edge detection
    u8    pic_irq_base[2];          // ICW2 vector base (was pic_intvec)
    u8    pic_priority_add[2];      // priority rotation offset
    u8    pic_read_reg_select[2];   // OCW3 bit 0: 0=IRR, 1=ISR readback
    u8    pic_poll[2];              // OCW3 poll-mode pending
    u8    pic_special_mask[2];      // OCW3 special mask mode
    u8    pic_init_state[2];        // 0=normal, 1=ICW2, 2=ICW3, 3=ICW4
    u8    pic_auto_eoi[2];          // ICW4 AEOI bit
    u8    pic_rotate_on_aeoi[2];    // OCW2 rotate-on-AEOI
    u8    pic_special_fnm[2];       // ICW4 special fully nested mode
    u8    pic_init4[2];             // ICW1 bit 0: ICW4 needed
    u8    pic_single_mode[2];       // ICW1 bit 1: single PIC, no slave
    u8    pic_elcr[2];              // edge/level control register (ELCR)
    u8    pic_ltim[2];              // ICW1 bit 3: level-trigger global mode
    u8    pic_control_index;        // latched index for 0x22/0x23 backdoor
    u8    pic_control_regs[6]{};    // currently unused

    u8    lpt_data;
    u8    lpt_control;
    u8    lpt_status;
    bool  lpt_init;

    // SuperIO
    bool      superio_config_mode = false;
    u8        superio_unlock_state = 0;
    u8        superio_index = 0;
    u8        superio_ldn = 0;
    u8        superio_chip_regs[256]{};
    u8        superio_ldn_regs[16][256]{};

    // ISA Plug-and-Play
    int       isapnp_state    = 0;        // PNP_WAIT_FOR_KEY at boot
    int       isapnp_key_pos  = 0;
    u8        isapnp_reg      = 0;        // last register selected via 0x279
    u16       isapnp_rd_port  = 0;        // OS-set RD_DATA port in 0x203-0x3FF
    u8        isapnp_wake_csn = 0;
  } state;

  FILE* lpt = nullptr;

  // sys0 "arc_year_compat" config: when true, encode TOY year as offset
  // from 1980 so the ARC console displays the correct year. Off by default
  // because OpenVMS writes the BBW year in its own (year - 2000) format on
  // SET TIME / shutdown, which would shift internal time forward 20 years
  // here. VMS users wanting ARC to also display correctly should leave
  // this off and accept ARC's year-20 display, or set it true and answer
  // the boot time prompts
  bool arc_year_compat;
};

extern CAliM1543C* theAli;
#endif // !defined(INCLUDED_ALIM1543C_H_)
