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
  * Contains the definitions for the emulated DMA controller.
  *
  * $Id$
  *
  * X-1.5        Camiel Vanderhoeven                             29-APR-2008
  *      Removed unused reference to floppy disk image.
  *
  * X-1.4        Brian Wheeler                                   29-APR-2008
  *      Fixed floppy disk implementation.
  *
  * X-1.3        Brian Wheeler                                   18-APR-2008
  *      Rewrote DMA code to make it ready for floppy support.
  *
  * X-1.2        Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.1        Camiel Vanderhoeven                             26-FEB-2008
  *      Created. Contains code previously found in AliM1543C.h
  *
  * \author Camiel Vanderhoeven
  **/
#if !defined(INCLUDED_DMA_H)
#define INCLUDED_DMA_H

#include "emu/SystemComponent.h"

class CDMAHost
{
public:
  virtual ~CDMAHost() = default;
  // A bridge that maps DMA ports supplies their decode policy.
  virtual const CSystemComponent* isa_decode_owner() const noexcept { return nullptr; }
  virtual bool dma_transfer_enabled() = 0;
  // Called under the device-bus lock with controller-generated addresses and byte counts.
  virtual void dma_read_memory(u32 address, void* dest, size_t bytes) = 0;
  virtual void dma_write_memory(u32 address, const void* source, size_t bytes) = 0;
};

  /**
   * \brief Emulated DMA controller.
   **/


class CDMA : public CSystemComponent
{
public:
  enum class Model { AliM1543C, Intel82357 };
  CDMA(CConfigurator* cfg, CSystem* c, CDMAHost& host, Model model = Model::AliM1543C);
  virtual       ~CDMA();

  virtual void  ResetPCI() override;

  bool uses_subtractive_decode(int index, u64 address, int dsize,
    bool write) const noexcept override;
  const CSystemComponent* memory_decode_owner() const noexcept override;

  virtual int   DoClock();
  virtual void  WriteMem(int index, u64 address, int dsize, u64 data);
  virtual u64   ReadMem(int index, u64 address, int dsize);
  // Logical byte ports for the two 8237 banks and the ISA low-page registers.
  u8            read_legacy_port(u16 port);
  void          write_legacy_port(u16 port, u8 data);
  // High-page byte ports for bridges that map the 0x481-0x48b bank.
  u8            read_high_page_port(u16 port) const;
  void          write_high_page_port(u16 port, u8 data);
  // The 82357 exposes count bits 23:16 at a separate byte port per channel.
  u8            read_high_count_port(u16 port) const;
  void          write_high_count_port(u16 port, u8 data);
  u8            read_stop_port(u16 port) const;
  void          write_stop_port(u16 port, u8 data);
  u8            read_extended_mode_port(u16 port) const;
  void          write_extended_mode_port(u16 port, u8 data);
  u8            read_ali_shadow(u8 selector) const;
  virtual int   SaveState(FILE* f);
  virtual int   RestoreState(FILE* f);

  // State accesses use the system's shared device-bus lock. 
  // Device callers must take that lock before their own locks. 
  // Even across multi-call sequences such as reading the count and transferring a buffer.

  // Software request register: controller 0-1, local channel 0-3.
  void          set_request(int index, int channel, int data);
  // Device request line: global channel 0-3 or 5-7.
  // asserted is the logical request state, not the electrical pin level.
  // Outside system reset, only the device lowers this line;
  // TC, EOP and master clear do not lower it.
  void          set_drq(int channel, bool asserted);

  struct SDMA_result
  {
    size_t transferred;    // Bytes moved; zero for verify or standalone EOP.
    bool blocked;          // No DMA service; registers and buffers are unchanged.
    bool terminal_count;   // This call exhausted the count, even with auto-init.
    bool external_eop;     // This call honored the device's EOP indication.
  };

  // Buffers and lengths are in bytes.
  // length 0 uses the current count.
  // Shifted word transfers must cover whole words.
  // send: device to memory; recv: memory to device.
  // Calls remain device-paced and do not require an explicit set_drq().
  // A software request or retained block bypasses only that channel's mask;
  // incoming hardware DRQ does not. Enable and cascade requirements still apply.
  // Channels 0-3 also require controller 1 enabled and channel 4 unmasked
  // and programmed for cascade. Channel 4's address/count are not serviced.
  // Cascade mode, a wrong direction or an illegal transfer type is blocked.
  // Verify services the count without accessing memory or the buffer:
  // blocked is false, transferred is zero, and terminal_count reports exhaustion.
  // The 82357 accepts device EOP only when extended-mode bit 6 selects input.
  // eop ends the transfer after the last unit serviced by this call, including
  // when length is capped by the current count. It is not a latched pin level.
  // Blocked calls do not apply EOP, including calls on cascade-mode channels.
  // EOP sets completion status and masks or auto-initializes the channel;
  // otherwise the remaining address/count are retained. DRQ is unchanged.
  // terminal_count and external_eop can both be true; completion occurs once.
  // Results do not clear the guest-visible terminal-count status.
  SDMA_result   send_data(int channel, void* data, size_t length = 0,
                  bool eop = false);
  SDMA_result   recv_data(int channel, void* data, size_t length = 0,
                  bool eop = false);
  // One bus cycle. Active bytes are packed into the low bits in little-endian order.
  // A 16-bit destination blocks a cycle that needs more than two bytes.
  // A blocked or verify receive leaves data unchanged.
  SDMA_result   send_unit(int channel, u32 data, bool eop = false);
  SDMA_result   recv_unit(int channel, u32& data, bool eop = false);
  SDMA_result   recv_unit(int channel, u16& data, bool eop = false);
  // Explicit-request service, at most one bus cycle per call.
  // Demand requires unmasked hardware DRQ; single also accepts a software request.
  // Block starts from either request and, after successful service, retains it
  // until TC/EOP even if DRQ or the software request drops. Auto-init releases it.
  // Between calls, own mask-set/disable/reset or channel reprogramming drops
  // retention without generating TC/EOP. Pending requests can start a new service.
  // A held DRQ can service successive calls; these calls never lower DRQ.
  // Cascade returns blocked. Existing enable/cascade and completion rules apply.
  // Blocked or verify receives leave data unchanged; blocked calls ignore EOP.
  // Command bit 4 selects rotating priority; otherwise local order is 0-3.
  // Rotation makes the released channel lowest priority, including channel 4
  // for lower-controller service. Ongoing demand/block service is not preempted.
  // Demand releases on DRQ withdrawal; single re-arbitrates for every unit.
  // Failed calls and idle register writes do not rotate. Bus timing is not modeled.
  SDMA_result   service_send_unit(int channel, u32 data, bool eop = false);
  SDMA_result   service_recv_unit(int channel, u32& data, bool eop = false);
  SDMA_result   service_recv_unit(int channel, u16& data, bool eop = false);
  // End an accepted demand/block service without consuming another unit.
  SDMA_result   service_eop(int channel);
  // Device queries use global channels 0-3 or 5-7; invalid channels throw.
  // Raw programmed 8237 mode byte, including the local channel-select bits.
  u8            get_mode(int channel);
  // Readiness for service_send_unit/service_recv_unit, including explicit
  // requests and arbitration. 
  bool          can_service_send(int channel);
  bool          can_service_recv(int channel);
  // Raw byte/word count register (number of DMA units minus one).
  int           get_count(int channel);
  // Current count plus one in bytes; independent of mask/enable state.
  size_t        get_transfer_size(int channel);

private:
  CDMAHost& dma_host;
  const Model model;

  // Derived transfer geometry; not guest registers or saved state.
  struct SDMA_transfer_properties
  {
    size_t data_width;     // Maximum bytes per device transfer.
    size_t count_unit;     // Bytes represented by one count-register unit.
    u32 address_step;     // Byte address increment per device transfer.
    unsigned address_shift; // Shift from programmed address to byte address.
    u32 address_mask;     // Byte address bits that can carry or borrow.
  };

  static void   validate_device_channel(int channel);
  void          select_compatible_address(int channel);
  u32           count_mask() const;
  bool          accepts_external_eop(int channel) const;
  SDMA_transfer_properties transfer_properties(int channel) const;
  static u32    dma_address(u16 pagebase, u32 current,
                  const SDMA_transfer_properties& properties);
  static u32    advance_address(u32 address, size_t bytes, bool decrement,
                  bool finish_cycle, const SDMA_transfer_properties& properties);
  static size_t increment_chunk_size(u32 address, size_t length,
                  const SDMA_transfer_properties& properties);
  size_t        unit_size(int channel) const;
  template<typename T>
  SDMA_result   receive_unit(int channel, T& data, bool eop, bool arbitrate);
  void          transfer_memory(u32 address, u8* buffer, size_t length, bool decrement,
                  bool write, const SDMA_transfer_properties& properties);
  u8            get_requests(int ctrlr);
  bool          cascade_enabled() const;
  bool          service_requested(int channel) const;
  int           select_service_channel(int ctrlr) const;
  bool          service_selected(int channel) const;
  bool          service_ready(int channel, u8 transfer_type) const;
  void          rotate_priority(int channel);
  void          release_service(int channel);
  void          finish_service(int channel, const SDMA_result& result);
  void          do_dma();
  bool          advance_transfer(int channel, size_t length,
                  const SDMA_transfer_properties& properties, bool eop);
  void          complete_transfer(int channel);

  /// The state structure contains all elements that need to be saved to the statefile.
  struct SDMA_state
  {
    /// DMA channel state
    struct SDMA_chan
    {
      u16   current;
      u16   base;
      u16   pagebase;
      u16   current_page;
      u32   count;
      u32   base_count;
      u8    mode;
      bool  extended_address; // 82357 high page was written last.
    } channel[8];

    /// DMA controller state
    struct SDMA_ctrl
    {
      u8  status;
      u8  command;
      u8  request; // software request bits
      u8  drq;     // device-driven request levels, independent of mask/command
      u8  block_active; // accepted request-aware block services, not DRQ levels
      u8  demand_active; // held demand services, released when DRQ falls
      u8  next_priority; // first local channel considered in rotating mode
      u8  mask;
      bool lobyte; // low byte is next for address or count access
    } controller[2];
    bool cascade_active; // accepted upper channel-4 grant, not lower HRQ

    /// ALi M1543C extensions, separate from the standard 8237 state.
    struct SDMA_ali
    {
      // Raw bits 5:4: 0x00/0x10/0x20 compatible, 0x30 Type F.
      // System reset clears these; guest master clear preserves them.
      u8 cycle_timing[8];
    } ali;
    struct SDMA_82357
    {
      u8 extended_mode[8]; // Bits 7:2 of each channel's extended-mode register.
      u32 stop_address[8]; // Address bits 23:2 for channels 0-3 and 5-7.
    } isp;
  }
  state;
};

#define DMA_IO_BASE 0x1000
#define DMA0_IO_MAIN DMA_IO_BASE + 0
#define DMA1_IO_MAIN DMA_IO_BASE + 1
#define DMA_IO_LPAGE DMA_IO_BASE + 2
#define DMA_IO_HPAGE DMA_IO_BASE + 3
#define DMA0_IO_CHANNEL DMA_IO_BASE + 4
#define DMA1_IO_CHANNEL DMA_IO_BASE + 5
#define DMA0_IO_EXT DMA_IO_BASE + 6
#define DMA1_IO_EXT DMA_IO_BASE + 7

#endif // !defined(INCLUDED_DMA_H)
