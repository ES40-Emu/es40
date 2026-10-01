/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2023, 2026 by gdwnldsKSC
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
  * Contains the definitions for the emulated Serial Port devices.
  *
  * $Id$
  *
  * X-1.21       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.20       Camiel Vanderhoeven                             26-MAR-2008
  *      Fix compiler warnings.
  *
  * X-1.19       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.18       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.17       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.15       Brian Wheeler                                   29-FEB-2008
  *      Restart serial port connection if lost.
  *
  * X-1.14       Camiel Vanderhoeven                             06-JAN-2008
  *      Proper interrupt handling.
  *
  * X-1.13       Camiel Vanderhoeven                             02-JAN-2008
  *      Comments.
  *
  * X-1.12       Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.11       Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.10       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator.
  *
  * X-1.9        Camiel Vanderhoeven                             30-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.8        Camiel Vanderhoeven                             28-FEB-2007
  *      Added throughSocket to support lockstep debugging.
  *
  * X-1.7        Camiel Vanderhoeven                             16-FEB-2007
  *   a) Changed header guards.
  *   b) DoClock is also used in Win32 environments.
  *
  * X-1.6        Camiel Vanderhoeven                             12-FEB-2007
  *      Added comments.
  *
  * X-1.5        Camiel Vanderhoeven                             9-FEB-2007
  *      Added comments.
  *
  * X-1.4        Camiel Vanderhoeven                             3-FEB-2007
  *      Added bInitialized to support eating the initialization bytes from
  *      the Telnet client.
  *
  * X-1.3        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.2        Brian Wheeler                                   3-FEB-2007
  *      Added Linux support.
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2007
  *      Initial version in CVS.
  *
  * \author Camiel Vanderhoeven
  **/
#if !defined(INCLUDED_SERIAL_H)
#define INCLUDED_SERIAL_H

#include "SystemComponent.h"
#include "network/telnet.h"
#include <atomic>
#include <string>
#include <vector>

class CAliM1543C;

#define STAGE_SIZE 8192

  /**
   * \brief Emulated serial port.
   *
   * The serial port is translated to a telnet port.
   **/
class CSerial : public CSystemComponent, public CRunnable
{
public:
  void          write(const char* s, int dsize);
  void          write_cstr(const char* s);
  virtual void  WriteMem(int index, u64 address, int dsize, u64 data);
  virtual u64   ReadMem(int index, u64 address, int dsize);
  CSerial(CConfigurator* cfg, CSystem* c, u16 number);
  virtual       ~CSerial();

  void bind_isa_bridge(CAliM1543C* bridge) override { isa_bridge = bridge; }
  const CSystemComponent* memory_decode_owner() const noexcept override;
  bool decodes_memory_access(int index, u64 address, int dsize,
    bool write) const noexcept override;
  bool uses_subtractive_decode(int index, u64 address, int dsize,
    bool write) const noexcept override;

  int           receive(const char* data, int dsize);
  virtual void  check_state();
  virtual int   SaveState(FILE* f);
  virtual int   RestoreState(FILE* f);
  void          eval_interrupts();
  void          WaitForConnection();
  virtual void  run();
  void          execute();

  virtual void  init();
  virtual void  start_threads();
  virtual void  stop_threads();
private:
  // Borrowed routing owner; not part of the saved device state.
  const CAliM1543C* isa_bridge = nullptr;

  void          serial_menu();
  void          drain_staging();
  void          close_connection();
  void          close_sockets();

  CThread* myThread = nullptr;
  std::atomic<bool> StopThread{false};
  bool  breakHit = false;

  unsigned char  iac_carry[8];          // Partial telnet sequence carried across recv() calls
  int            iac_carry_len;         // Number of valid bytes in iac_carry
  bool           in_subneg;             // Currently inside IAC SB ... IAC SE subnegotiation

  char           stageBuf[STAGE_SIZE];  // Cooked data waiting for baud-rate delivery
  int            stageLen;              // Number of valid bytes in stageBuf

  /// The state structure contains all elements that need to be saved to the statefile.
  struct SSrl_state
  {
    u8    bTHR; /**< Transmit Hold Register */
    u8    bRDR; /**< Received Data Register */
    u8    bBRB_LSB;
    u8    bBRB_MSB;
    u8    bIER; /**< Interrupt Enable Register */
    u8    bIIR; /**< Interrupt Identification Register */
    u8    bFCR;
    u8    bLCR; /**< Line Control Register (Data Format Register) */
    u8    bMCR; /**< Modem Control Register */
    u8    bLSR; /**< Line Status Register */
    u8    bMSR; /**< Modem Status Register */
    u8    bSPR; /**< Scratch Pad Register */
    int   serial_cycles;
    char  rcvBuffer[1024];
    int   rcvW;
    int   rcvR;
    int   iNumber;
    bool  thre_pending; /**< THRE interrupt latched (THR emptied, not yet acked by an IIR read) */
  } state;
  int listenPort = 0;
  std::vector<int64_t> listenSockets;
  std::vector<std::string> listenAddresses;
  int64_t connectSocket = INVALID_SOCKET;
#if defined(_WIN32)
  bool winsock_started = false;
#endif
  bool disabled = false;     ///< Hide the UART from the guest.
  bool raw_mode = false;     ///< Skip telnet processing and the connection banner.
  bool null_attach = false;  ///< Provide an idle UART without a listener or client.
#if defined(IDB) && defined(LS_MASTER)
  int throughSocket = INVALID_SOCKET;
#endif
};
#endif // !defined(INCLUDED_SERIAL_H)
