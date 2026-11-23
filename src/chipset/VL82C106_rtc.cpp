/* ES40 emulator -- VL82C106 real-time clock and standby RAM
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

#include "StdAfx.h"
#include "VL82C106.h"
#include "MC146818.h"
#include "SnapshotFile.h"
#include "config/Configurator.h"
#include "system/System.h"
#include <algorithm>
#include <cerrno>
#include <ctime>
#include <memory>

namespace {
constexpr u32 second_ns = 1000000000;
// The divider starts its first update after 500 ms (PB22H-KB, pp. 22-9, 22-17–18).
constexpr u32 update_begin = 500000000;
constexpr u32 uip_begin = update_begin - 244000;
constexpr u32 update_end = update_begin + 1984000;

enum : u8 { reg_a = 10, reg_b, reg_c, reg_d };
enum : u8 { day_saving = 1, hours_24 = 2, binary = 4, uf = 0x10, af = 0x20, pf = 0x40, set = 0x80 };

unsigned number(u8 value, u8 mode)
{
  return (mode & binary) ? value : mc146818::from_bcd(value);
}

u8 encoded(unsigned value, u8 mode)
{
  return (mode & binary) ? u8(value) : mc146818::to_bcd(value);
}

u8 encoded_hour(unsigned hour, u8 mode)
{
  if (mode & hours_24)
    return encoded(hour, mode);
  return encoded((hour + 11) % 12 + 1, mode) | (hour >= 12 ? 0x80 : 0);
}

unsigned hours(u8 value, u8 mode)
{
  if (mode & hours_24)
    return number(value, mode);
  return number(value & 0x7f, mode) % 12 + ((value & 0x80) ? 12 : 0);
}

unsigned month_length(unsigned month, unsigned year)
{
  static constexpr u8 days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12)
    return 0;
  return days[month - 1] + (month == 2 && year % 4 == 0);
}

void next_date(u8* regs)
{
  const u8 mode = regs[reg_b];
  const unsigned weekday = number(regs[6], mode);
  if (weekday >= 1 && weekday <= 7)
    regs[6] = encoded(weekday % 7 + 1, mode);
  const unsigned day = number(regs[7], mode);
  const unsigned month = number(regs[8], mode);
  const unsigned year = number(regs[9], mode);
  const unsigned last = month_length(month, year);
  if (!day || day > last || year > 99)
    return;
  regs[7] = encoded(day == last ? 1 : day + 1, mode);
  if (day == last) {
    regs[8] = encoded(month % 12 + 1, mode);
    if (month == 12)
      regs[9] = encoded((year + 1) % 100, mode);
  }
}

bool alarm_matches(u8 alarm, u8 value)
{
  return alarm >= 0xc0 || alarm == value; // Wildcards are 11xxxxxx (PB22H-KB, p. 22-16).
}

void check_alarm(u8* regs, unsigned first, unsigned last)
{
  if ((regs[reg_c] & af) || first > last)
    return;
  const u8 mode = regs[reg_b];
  const unsigned sec = number(regs[1], mode);
  if (regs[1] < 0xc0 && (sec >= 60 || encoded(sec, mode) != regs[1]))
    return;
  for (unsigned minute = first / 60; minute <= last / 60; ++minute) {
    if (!alarm_matches(regs[5], encoded_hour(minute / 60, mode)) ||
        !alarm_matches(regs[3], encoded(minute % 60, mode)))
      continue;
    const unsigned when = minute * 60 + sec;
    if (regs[1] >= 0xc0 || (when >= first && when <= last)) {
      regs[reg_c] |= af;
      return;
    }
  }
}

void advance_calendar(u8* regs, u64 elapsed, bool& fall_back)
{
  const u8 mode = regs[reg_b];
  const unsigned hour = hours(regs[4], mode);
  const unsigned min = number(regs[2], mode);
  const unsigned sec = number(regs[0], mode);
  if (hour >= 24 || min >= 60 || sec >= 60 || encoded_hour(hour, mode) != regs[4])
    return;
  unsigned now = hour * 3600 + min * 60 + sec;
  while (elapsed) {
    const unsigned month = number(regs[8], mode);
    const unsigned day = number(regs[7], mode);
    // DSE changes the clock on the last Sundays of April and October (PB22H-KB, p. 22-10).
    const bool dst = (mode & day_saving) && regs[6] == 1 && now < 7200 &&
      ((month == 4 && day >= 24 && day <= 30) || (month == 10 && day >= 25 && day <= 31 && !fall_back));
    const unsigned boundary = dst ? 7200 : 86400;
    const unsigned count = unsigned((std::min)(elapsed, u64(boundary - now)));
    const unsigned end = now + count;
    check_alarm(regs, now + 1, end == boundary ? end - 1 : end);
    elapsed -= count;
    now = end;
    if (end == boundary) {
      if (dst) {
        fall_back = month == 10;
        now = fall_back ? 3600 : 10800;
      } else {
        next_date(regs);
        now = 0;
        fall_back = false;
      }
      check_alarm(regs, now, now);
    }
  }
  regs[0] = encoded(now % 60, mode);
  regs[2] = encoded(now / 60 % 60, mode);
  regs[4] = encoded_hour(now / 3600, mode);
}
}

void VL82C106::rtc_seed(const std::tm& now)
{
  std::fill_n(rtc_bytes, sizeof(rtc_bytes), u8{0});
  rtc_bytes[reg_a] = 0x20;
  rtc_set_calendar(now);
  rtc_phase_ns = update_end;
  rtc_skip_update = false;
}

void VL82C106::rtc_set_calendar(const std::tm& now)
{
  const u8 mode = rtc_bytes[reg_b];
  rtc_bytes[0] = encoded(now.tm_sec, mode);
  rtc_bytes[2] = encoded(now.tm_min, mode);
  rtc_bytes[4] = encoded_hour(now.tm_hour, mode);
  rtc_bytes[6] = encoded(now.tm_wday + 1, mode);
  rtc_bytes[7] = encoded(now.tm_mday, mode);
  rtc_bytes[8] = encoded(now.tm_mon + 1, mode);
  rtc_bytes[9] = encoded((now.tm_year % 100 + 100) % 100, mode);
  rtc_fall_back = false;
}

void VL82C106::rtc_reset()
{
  // RESET clears interrupt enables and flags (PB22H-KB, pp. 22-10–14).
  rtc_bytes[reg_b] &= 0x87;
  rtc_refresh(cSystem->device_access_time());
  rtc_bytes[reg_c] = 0;
}

void VL82C106::rtc_sync_irq()
{
  // IRQF is the OR of each enabled, pending source (PB22H-KB, p. 22-13).
  const bool level = (rtc_bytes[reg_b] & rtc_bytes[reg_c] & (pf | af | uf)) != 0;
  if (!rtc_irq_level || *rtc_irq_level != level) {
    rtc_irq_level = level;
    rtc_irq_output(level);
  }
}

void VL82C106::rtc_advance(u64 elapsed_ns)
{
  if (rtc_bytes[reg_a] & 0x40)
    return;
  const u32 fraction = u32(elapsed_ns % second_ns);
  const unsigned hz = mc146818::periodic_hz(rtc_bytes[reg_a]);
  if (hz && (elapsed_ns >= second_ns || u64(rtc_phase_ns + fraction) * hz / second_ns !=
                                       u64(rtc_phase_ns) * hz / second_ns))
    rtc_bytes[reg_c] |= pf;
  const u32 since_update = (rtc_phase_ns + second_ns - update_end) % second_ns;
  u64 updates = elapsed_ns / second_ns + (since_update + fraction) / second_ns;
  rtc_phase_ns = (rtc_phase_ns + fraction) % second_ns;
  if (updates && rtc_skip_update) {
    --updates;
    rtc_skip_update = false;
  }
  if (updates && !(rtc_bytes[reg_b] & set)) {
    rtc_bytes[reg_c] |= uf;
    advance_calendar(rtc_bytes, updates, rtc_fall_back);
  }
}

void VL82C106::rtc_refresh(std::chrono::steady_clock::time_point now)
{
  if (rtc_sample != std::chrono::steady_clock::time_point{} && now > rtc_sample) {
    rtc_advance(std::chrono::duration_cast<std::chrono::nanoseconds>(now - rtc_sample).count());
    rtc_sample = now;
  }
  rtc_sync_irq();
}

u64 VL82C106::rtc_next_irq_ns() const
{
  const u8 mode = rtc_bytes[reg_b];
  if ((rtc_bytes[reg_a] & 0x40) || (mode & rtc_bytes[reg_c] & (pf | af | uf)))
    return 0;
  u64 delay = 0;
  if (!(mode & set) && (mode & (uf | af))) {
    delay = rtc_phase_ns < update_end ? update_end - rtc_phase_ns : second_ns - rtc_phase_ns + update_end;
    if (rtc_skip_update)
      delay += second_ns;
  }
  const unsigned hz = mc146818::periodic_hz(rtc_bytes[reg_a]);
  if (hz && (mode & pf)) {
    const u64 next = u64(rtc_phase_ns) * hz / second_ns + 1;
    const u64 periodic = (next * second_ns + hz - 1) / hz - rtc_phase_ns;
    if (!delay || periodic < delay)
      delay = periodic;
  }
  return delay;
}

u8 VL82C106::rtc_read(u8 index)
{
  rtc_refresh(cSystem->device_access_time());
  if (index >= sizeof(rtc_bytes))
    return 0xff;
  const bool updating = !(rtc_bytes[reg_b] & set) && !(rtc_bytes[reg_a] & 0x40) &&
    !rtc_skip_update && rtc_phase_ns < update_end;
  if (index < reg_a && updating && rtc_phase_ns >= update_begin)
    return 0xff; // Time and alarm RAM are off the bus during update (PB22H-KB, p. 22-17).
  if (index == reg_a)
    return rtc_bytes[index] | ((updating && rtc_phase_ns >= uip_begin) ? 0x80 : 0);
  u8 value = rtc_bytes[index];
  if (index == reg_c) {
    if (value & rtc_bytes[reg_b] & (pf | af | uf)) {
      value |= 0x80;
      rtc_wakeup.notify_one();
    }
    rtc_bytes[index] = 0;
    rtc_sync_irq();
  } else if (index == reg_d) {
    if (!(value & 0x80))
      battery_dirty = true;
    rtc_bytes[index] = 0x80; // Reading D sets VRT (PB22H-KB, p. 22-14).
  }
  return value;
}

void VL82C106::rtc_write(u8 index, u8 value)
{
  rtc_refresh(cSystem->device_access_time());
  if (index >= sizeof(rtc_bytes) || index == reg_c || index == reg_d)
    return;
  const bool active_update = !(rtc_bytes[reg_a] & 0x40) &&
    rtc_phase_ns >= update_begin && rtc_phase_ns < update_end;
  if (index == reg_a) {
    if ((rtc_bytes[index] ^ value) & 0x40) {
      rtc_phase_ns = 0;
      rtc_skip_update = false;
    }
    rtc_bytes[index] = 0x20 | (value & 0x4f); // DV1:0 are fixed at 10b (PB22H-KB, Table 22-6).
  } else if (index == reg_b) {
    if (active_update && (rtc_bytes[index] & set) && !(value & set))
      rtc_skip_update = true;
    // Entering SET clears UIE; bit 3 is unused (PB22H-KB, p. 22-11).
    if ((value & set) && !(rtc_bytes[index] & set))
      value &= ~uf;
    rtc_bytes[index] = value & ~0x08;
  } else {
    if (index < reg_a && active_update && !(rtc_bytes[reg_b] & set) && !rtc_skip_update)
      return;
    if (index > reg_d && rtc_bytes[index] == value)
      return;
    rtc_bytes[index] = value;
    if (index < reg_a && (index >= 6 || !(index & 1)))
      rtc_fall_back = false;
  }
  battery_dirty = true;
  if (index == reg_a || index == reg_b)
    rtc_wakeup.notify_one();
  rtc_sync_irq();
}

namespace {
u64 wall_time_ns()
{
  const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
}

std::tm initial_date(const char* configured)
{
  std::tm date{};
  if (configured) {
    if (!mc146818::parse_datetime(configured, date) || mc146818::utc_time(date) == std::time_t(-1))
      FAILURE_1(Configuration, "Invalid Jensen RTC time: %s", configured);
  } else {
    std::time_t now = std::time(nullptr);
    gmtime_s(&date, &now);
  }
  return date;
}
}

void VL82C106::seed_clock()
{
  rtc_seed(initial_date(myCfg->get_text_value("time")));
}

bool VL82C106::save_rtc(FILE* file) const
{
  const u8 flags = u8(rtc_fall_back) | (u8(rtc_skip_update) << 1);
  return fwrite(rtc_bytes, sizeof(rtc_bytes), 1, file) == 1 &&
    fwrite(&rtc_phase_ns, sizeof(rtc_phase_ns), 1, file) == 1 && fwrite(&flags, 1, 1, file) == 1;
}

bool VL82C106::restore_rtc(FILE* file)
{
  u8 flags;
  if (fread(rtc_bytes, sizeof(rtc_bytes), 1, file) != 1 ||
      fread(&rtc_phase_ns, sizeof(rtc_phase_ns), 1, file) != 1 || fread(&flags, 1, 1, file) != 1 ||
      flags > 3 || rtc_phase_ns >= second_ns || (rtc_bytes[reg_a] & 0xb0) != 0x20 ||
      (rtc_bytes[reg_b] & 8) || (rtc_bytes[reg_c] & 0x8f) || (rtc_bytes[reg_d] & 0x7f))
    return false;
  rtc_fall_back = (flags & 1) != 0;
  rtc_skip_update = (flags & 2) != 0;
  return true;
}

void VL82C106::load_battery()
{
  battery_path = myCfg->get_text_value("rom.toy", default_battery_path);
  std::unique_ptr<FILE, decltype(&fclose)> file(fopen(battery_path.c_str(), "rb"), &fclose);
  if (!file) {
    if (errno != ENOENT)
      FAILURE(IO, "Cannot open Jensen RTC battery image: " + battery_path);
    seed_clock();
  } else {
    char tag[4];
    u64 saved;
    if (fread(tag, sizeof(tag), 1, file.get()) != 1 || memcmp(tag, "VLB1", sizeof(tag)) ||
        fread(&saved, sizeof(saved), 1, file.get()) != 1 || !restore_rtc(file.get()) ||
        fread(serial_cs, sizeof(serial_cs), 1, file.get()) != 1 ||
        fread(serial_cs_written, sizeof(serial_cs_written), 1, file.get()) != 1 ||
        serial_cs_written[0] > 7 || serial_cs_written[1] > 7 || fgetc(file.get()) != EOF || ferror(file.get()))
      FAILURE(Configuration, "Invalid Jensen RTC battery image: " + battery_path);
    // VBAT powers the clock and standby RAM while the system is off (PB22H-KB, pp. 1-10, 22-4).
    const u64 now = wall_time_ns();
    if (now > saved)
      rtc_advance(now - saved);
    if (const char* configured = myCfg->get_text_value("time"))
      rtc_set_calendar(initial_date(configured));
  }
  battery_ready = true;
}

void VL82C106::flush_storage()
{
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  if (!battery_ready || !battery_dirty)
    return;
  if (rtc_sample != Clock::time_point{})
    rtc_refresh(cSystem->device_access_time());
  CSnapshotFile file(battery_path.c_str());
  const u64 stamp = wall_time_ns();
  if (fwrite("VLB1", 4, 1, file.get()) != 1 || fwrite(&stamp, sizeof(stamp), 1, file.get()) != 1 ||
      !save_rtc(file.get()) || fwrite(serial_cs, sizeof(serial_cs), 1, file.get()) != 1 ||
      fwrite(serial_cs_written, sizeof(serial_cs_written), 1, file.get()) != 1)
    FAILURE(IO, "Cannot save Jensen RTC battery image: " + battery_path);
  file.publish();
  battery_dirty = false;
}

void VL82C106::check_state()
{
  CKeyboard::check_state();
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  if (battery_dirty) {
    const auto now = cSystem->device_access_time();
    if (now >= next_flush) {
      flush_storage();
      next_flush = now + std::chrono::seconds(1);
    }
  }
}

void VL82C106::run_rtc()
{
  std::unique_lock<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  while (rtc_sample != Clock::time_point{}) {
    rtc_refresh(Clock::now());
    const u64 delay = rtc_next_irq_ns();
    if (delay)
      rtc_wakeup.wait_until(lock, rtc_sample + std::chrono::nanoseconds(delay));
    else
      rtc_wakeup.wait(lock);
  }
}

void VL82C106::start_threads()
{
  CKeyboard::start_threads();
  std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
  if (!rtc_worker.joinable()) {
    rtc_sample = cSystem->device_access_time();
    try {
      rtc_worker = std::thread(&VL82C106::run_rtc, this);
    } catch (const std::system_error& error) {
      rtc_sample = {};
      FAILURE(Thread, std::string("Cannot start Jensen RTC: ") + error.what());
    }
  }
}

void VL82C106::stop_threads()
{
  {
    std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
    if (rtc_sample != Clock::time_point{}) {
      rtc_refresh(cSystem->device_access_time());
      rtc_sample = {};
      battery_dirty = true;
    }
    rtc_wakeup.notify_one();
  }
  if (rtc_worker.joinable())
    rtc_worker.join();
  CKeyboard::stop_threads();
}
