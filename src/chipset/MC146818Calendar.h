/* ES40 emulator -- MC146818-family calendar and alarm counter
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
#ifndef ES40_MC146818CALENDAR_H
#define ES40_MC146818CALENDAR_H

#include <cstdint>
#include "datatypes.h"
#include "MC146818.h"
#include <algorithm>

class MC146818Calendar
{
  enum : u8 { reg_b = 11, reg_c = 12, day_saving = 1, hours_24 = 2, binary = 4, af = 0x20 };

public:
  enum class AprilSunday { First, Last };

  static unsigned number(u8 value, u8 mode)
  {
    return (mode & binary) ? value : mc146818::from_bcd(value);
  }

  static u8 encoded(unsigned value, u8 mode)
  {
    return (mode & binary) ? u8(value) : mc146818::to_bcd(value);
  }

  static u8 encoded_hour(unsigned hour, u8 mode)
  {
    if (mode & hours_24)
      return encoded(hour, mode);
    return encoded((hour + 11) % 12 + 1, mode) | (hour >= 12 ? 0x80 : 0);
  }

  static unsigned hours(u8 value, u8 mode)
  {
    if (mode & hours_24)
      return number(value, mode);
    return number(value & 0x7f, mode) % 12 + ((value & 0x80) ? 12 : 0);
  }

  static unsigned month_length(unsigned month, unsigned year)
  {
    static constexpr u8 days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
      return 0;
    return days[month - 1] + (month == 2 && year % 4 == 0);
  }

private:
  static void next_date(u8* regs)
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

  static bool alarm_matches(u8 alarm, u8 value)
  {
    return alarm >= 0xc0 || alarm == value; // Wildcards are 11xxxxxx (PB22H-KB, p. 22-16).
  }

  static void check_alarm(u8* regs, unsigned first, unsigned last)
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

public:
  static void advance_calendar(u8* regs, u64 elapsed, bool& fall_back, AprilSunday spring_sunday)
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
      // DSE uses the first or last Sunday of April and the last Sunday of October (DS1287 p. 349, PB22H-KB p. 22-10).
      const bool spring = month == 4 && (spring_sunday == AprilSunday::First ? day >= 1 && day <= 7 : day >= 24 && day <= 30);
      const bool dst = (mode & day_saving) && regs[6] == 1 && now < 7200 &&
        (spring || (month == 10 && day >= 25 && day <= 31 && !fall_back));
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

  static void set_calendar(u8* regs, const std::tm& now)
  {
    const u8 mode = regs[reg_b];
    regs[0] = encoded(now.tm_sec, mode);
    regs[2] = encoded(now.tm_min, mode);
    regs[4] = encoded_hour(now.tm_hour, mode);
    regs[6] = encoded(now.tm_wday + 1, mode);
    regs[7] = encoded(now.tm_mday, mode);
    regs[8] = encoded(now.tm_mon + 1, mode);
    regs[9] = encoded((now.tm_year % 100 + 100) % 100, mode);
  }
};

#endif
