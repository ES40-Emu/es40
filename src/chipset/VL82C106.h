/* ES40 emulator -- VL82C106 keyboard, mouse, serial, parallel and real-time clock controller
 * Copyright (C) 2026 by gdwnldsKSC
 * All rights reserved.
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ES40_VL82C106_H
#define ES40_VL82C106_H

#include "io/Keyboard.h"
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <thread>

class CSerial;

class VL82C106 final : public CKeyboard
{
public:
  static constexpr const char* default_battery_path = "jensen-toy.rom";
  VL82C106(CConfigurator* cfg, CSystem* system, u64 local_base,
    std::function<void(bool)> irq_output, std::function<void(unsigned, bool)> serial_irq_output,
    std::function<void(bool)> rtc_irq_output, std::function<void(bool)> lpt_irq_output);
  ~VL82C106() override;
  void init() override;
  void start_threads() override;
  void stop_threads() override;
  void check_state() override;
  void flush_storage() override;
  CSerial& serial_port(unsigned number) const { return *serial[number]; }
  // Connector inputs are PD[7:0] and BUSY, ACK_L, PE, SLCT, ERR_L in bits 7:3 (PB22H-KB, Table 21-2).
  void set_parallel_inputs(u8 data, u8 status_pins);
  struct ParallelOutputs {
    std::optional<u8> data;
    u8 control; // SLIN_L, INIT_L, AFD_L and STB_L in bits 3:0 (PB22H-KB, Table 21-3).
  };
  ParallelOutputs parallel_outputs() const;
  bool decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept override;
  u64 ReadMem(int index, u64 address, int dsize) override;
  void WriteMem(int index, u64 address, int dsize, u64 data) override;
  void ResetPCI() override;
  int SaveState(FILE* f) override;
  int RestoreState(FILE* f) override;
  void finalize_restore() noexcept override;

private:
  void map_ports() override;
  int decode_port(u16 port, bool write) const noexcept;
  bool selects_serial(unsigned channel, u16 port) const noexcept;
  bool selects_lpt(u16 port) const noexcept;
  u8 read_lpt_status();
  void sync_lpt_irq();
  bool at_mode() const { return (control1 & 2) != 0; }
  void set_irq_lines(bool keyboard, bool mouse) override;
  void reset_controller_state() override;
  u8 default_scancode_set() const override { return 2; }
  bool handle_controller_command(u8 command) override;
  bool handle_controller_parameter(u8 command, u8 data) override;
  u8 status_byte(u8 normal) const override;
  bool input_locked() const override { return locked; }
  void password_scancodes(const u8* bytes, bool released) override;
  u8 output_port() const;

  using Clock = std::chrono::steady_clock;
  void rtc_seed(const std::tm& now);
  void rtc_set_calendar(const std::tm& now);
  void rtc_reset();
  void rtc_advance(u64 elapsed_ns);
  void rtc_refresh(Clock::time_point now);
  void rtc_sync_irq();
  u64 rtc_next_irq_ns() const;
  u8 rtc_read(u8 index);
  void rtc_write(u8 index, u8 value);
  void run_rtc();
  void load_battery();
  bool save_rtc(FILE* file) const;
  bool restore_rtc(FILE* file);
  bool save_lpt_cs(FILE* file) const;
  bool restore_lpt_cs(FILE* file);
  void seed_clock();

  const u64 local_base;
  const std::function<void(bool)> irq_output;
  CSerial* serial[2];
  std::optional<bool> irq_level;
  u8 rtc_index = 0;
  const std::function<void(bool)> rtc_irq_output;
  u8 rtc_bytes[0x50]{};
  u32 rtc_phase_ns = 0;
  bool rtc_fall_back = false;
  bool rtc_skip_update = false;
  Clock::time_point rtc_sample{}, next_flush{};
  std::optional<bool> rtc_irq_level;
  std::condition_variable_any rtc_wakeup;
  std::thread rtc_worker;
  std::string battery_path;
  bool battery_dirty = false;
  bool battery_ready = false;
  u8 control0 = 0;
  u8 control1 = 0;
  u8 serial_cs[2][3]{};
  u8 serial_cs_written[2]{};
  const std::function<void(bool)> lpt_irq_output;
  std::optional<bool> lpt_irq_level;
  u8 lpt_cs[3]{}, lpt_cs_written = 0;
  u8 lpt_data = 0, lpt_control = 0;
  u8 lpt_input_data = 0xff, lpt_status_pins = 0xf8; // Unplugged status inputs have pull-ups (VL82C106, p. 6-191).
  bool lpt_irq_pending = false;
  u8 at_inhibit_override = 0;
  u8 ram[31]{};
  u8 output_latch = 1;
  u8 pulse_mask = 0;
  std::chrono::steady_clock::time_point pulse_end{};
  u64 pulse_remaining_ns = 0;
  u8 password[8]{};
  u8 password_length = 0;
  u8 recent_keys[8]{};
  unsigned recent_count = 0;
  bool locked = false;
};

#endif
