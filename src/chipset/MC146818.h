/* ES40 emulator -- MC146818-compatible RTC conversions and divider rates.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2020 Martin Vorländer
 * Copyright (C) 2023-2026 by gdwnldsKSC
 * Copyright (C) 2026 by BlueRain-debug
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

#ifndef ES40_MC146818_H
#define ES40_MC146818_H

#include <cstdint>
#include <cstdio>
#include <ctime>

namespace mc146818 {
constexpr unsigned periodic_hz(std::uint8_t reg_a)
{
  const unsigned rate = reg_a & 15;
  if ((reg_a & 0x20) && (rate == 1 || rate == 2))
    return 512u >> rate;
  return rate ? 65536u >> rate : 0;
}

constexpr std::uint8_t to_bcd(unsigned value)
{
  return std::uint8_t(((value / 10) << 4) | (value % 10));
}

constexpr int from_bcd(std::uint8_t value)
{
  return (value >> 4) * 10 + (value & 15);
}

inline bool parse_datetime(const char* text, std::tm& date)
{
  date = {};
  if (std::sscanf(text, "%d-%d-%d %d:%d:%d", &date.tm_year, &date.tm_mon, &date.tm_mday,
                  &date.tm_hour, &date.tm_min, &date.tm_sec) < 3)
    return false;
  date.tm_year -= 1900;
  --date.tm_mon;
  return true;
}

inline std::time_t utc_time(std::tm& date)
{
#ifdef _WIN32
  return _mkgmtime(&date);
#else
  return timegm(&date);
#endif
}
}

#endif
