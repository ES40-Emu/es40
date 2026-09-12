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
  * \author Camiel Vanderhoeven (camiel@camicom.com / http://www.camicom.com)
  **/
#if !defined(INCLUDED_DMA_H)
#define INCLUDED_DMA_H

#include "SystemComponent.h"

  /**
   * \brief Emulated DMA controller.
   **/


class CDMA : public CSystemComponent
{
public:
  CDMA(CConfigurator* cfg, CSystem* c);
  virtual       ~CDMA();

  virtual int   DoClock();
  virtual void  WriteMem(int index, u64 address, int dsize, u64 data);
  virtual u64   ReadMem(int index, u64 address, int dsize);
  virtual int   SaveState(FILE* f);
  virtual int   RestoreState(FILE* f);

  void          set_request(int index, int channel, int data);

  struct SDMA_result
  {
    size_t transferred;    // Bytes moved by this call; zero for verify.
    bool blocked;          // No DMA service; registers and buffers are unchanged.
    bool terminal_count;   // This call exhausted the count, even with auto-init.
  };

  // Buffers and lengths are in bytes.
  // length 0 uses the current count.
  // Transfers on channels 5-7 must cover whole words.
  // send: device to memory; recv: memory to device.
  // A wrong direction or illegal transfer type is blocked.
  // Verify services the count without accessing memory or the buffer:
  // blocked is false, transferred is zero, and terminal_count reports completion.
  // Results do not clear the guest-visible terminal-count status.
  SDMA_result   send_data(int channel, void* data, size_t length = 0);
  SDMA_result   recv_data(int channel, void* data, size_t length = 0);
  // One byte on channels 0-3, one little-endian word on channels 5-7.
  // Byte sends use the low 8 bits; byte receives are zero-extended.
  // A blocked or verify receive leaves data unchanged.
  SDMA_result   send_unit(int channel, u16 data);
  SDMA_result   recv_unit(int channel, u16& data);
  // Raw byte/word count register (number of DMA units minus one).
  int           get_count(int channel) { return state.channel[channel].count; };
  // Current count plus one in bytes; independent of mask/enable state.
  size_t        get_transfer_size(int channel);

private:
  void          do_dma();
  bool          advance_transfer(int channel, size_t units);

  /// The state structure contains all elements that need to be saved to the statefile.
  struct SDMA_state
  {
    /// DMA channel state
    struct SDMA_chan
    {
      u16   current;
      u16   base;
      u16   pagebase;
      u16   count;
      u16   base_count;
      u8    mode;
    } channel[8];

    /// DMA controller state
    struct SDMA_ctrl
    {
      u8  status;
      u8  command;
      u8  request;
      u8  mask;
      bool lobyte; // low byte is next for address or count access
    } controller[2];
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

extern CDMA* theDMA;

#endif // !defined(INCLUDED_DMA_H)
