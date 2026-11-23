/* ES40 emulator -- Jensen flash socket
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
#include "JensenFlash.h"
#include "SnapshotFile.h"
#include "config/Configurator.h"
#include "system/System.h"
#include <filesystem>
#include <fstream>

namespace {
std::vector<u8> read_image(const std::string& path, unsigned bank)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    FAILURE(Configuration, "Cannot open Jensen FEPROM image: " + path);
  const std::streamoff size = file.tellg();
  if (size != Intel28F008SA::capacity && !(bank == 0 && size == 256 * 1024))
    FAILURE(Configuration, std::string("Jensen FEPROM image must be ") +
            (bank == 0 ? "256 KiB or 1 MiB: " : "1 MiB: ") + path);
  std::vector<u8> image(static_cast<size_t>(size));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(image.data()), size))
    FAILURE(IO, "Error reading Jensen FEPROM image: " + path);
  return image;
}

bool same_file(const std::string& a, const std::string& b) try
{
  if (a.empty() || b.empty())
    return false;
  namespace fs = std::filesystem;
  if (fs::exists(a) && fs::exists(b))
    return fs::equivalent(a, b);
  const auto left = fs::weakly_canonical(fs::absolute(a));
  const auto right = fs::weakly_canonical(fs::absolute(b));
#if defined(_WIN32)
  return _wcsicmp(left.c_str(), right.c_str()) == 0;
#else
  return left == right;
#endif
}
catch (const std::filesystem::filesystem_error& error) {
  FAILURE(Configuration, std::string("Cannot compare Jensen FEPROM paths: ") + error.what());
}
}

JensenFlash::JensenFlash(CConfigurator* cfg, CSystem& sys, unsigned bank) : system(sys)
{
  const std::string key = bank == 0 ? "rom.flash0" : "rom.flash1";
  seed_path = cfg->get_text_value(key.c_str(), "");
  backing_path = cfg->get_text_value((key + ".nvram").c_str(), "");
  if (!seed_path.empty())
    seed = read_image(seed_path, bank);
  if (seed.size() != Intel28F008SA::capacity) {
    if (!backing_path.empty())
      FAILURE(Configuration, key + ".nvram requires a 1 MiB flash device");
    return;
  }
  if (backing_path.empty())
    backing_path = seed_path + ".feprom" + std::to_string(bank) + ".nvram";
  if (same_file(seed_path, backing_path))
    FAILURE(Configuration, key + ".nvram must be separate from its firmware image");
  vpp_high = cfg->get_bool_value((key + ".vpp").c_str(), true);
  std::error_code error;
  const bool saved = std::filesystem::exists(backing_path, error);
  if (error)
    FAILURE(Configuration, "Cannot check Jensen FEPROM image: " + backing_path + ": " + error.message());
  auto contents = saved ? read_image(backing_path, 1) : seed;
  chip.emplace(std::move(contents));
}

void JensenFlash::check_paths(const JensenFlash& other) const
{
  if (same_file(backing_path, other.backing_path) || same_file(backing_path, other.seed_path) ||
      same_file(other.backing_path, seed_path))
    FAILURE(Configuration, "Jensen FEPROM sockets require distinct writable images");
}

void JensenFlash::advance(Clock::time_point now)
{
  if (now <= sampled)
    return;
  if (chip && chip->state().remaining_ns && sampled != Clock::time_point{}) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - sampled);
    if (chip->advance(elapsed.count()))
      dirty = true;
  }
  sampled = now;
}

u8 JensenFlash::read(u32 address)
{
  if (!chip)
    return seed[address];
  if (chip->state().remaining_ns)
    advance(system.device_access_time());
  return chip->read(address);
}

void JensenFlash::write(u32 address, u8 data)
{
  if (chip) {
    advance(system.device_access_time());
    chip->write(address, data, vpp_high);
  }
}

void JensenFlash::reset()
{
  if (chip) {
    advance(system.device_access_time());
    chip->reset();
  }
}

void JensenFlash::flush()
{
  if (!dirty || restoring)
    return;
  CSnapshotFile file(backing_path.c_str());
  const auto& bytes = chip->bytes();
  if (fwrite(bytes.data(), 1, bytes.size(), file.get()) != bytes.size())
    FAILURE(IO, "Could not save Jensen FEPROM: " + backing_path);
  file.publish();
  dirty = false;
}

void JensenFlash::service(bool force_flush)
{
  if (!chip || (!dirty && !chip->state().remaining_ns))
    return;
  const auto now = system.device_access_time();
  if (sampled != Clock::time_point{})
    advance(now);
  if (force_flush || now >= next_flush) {
    flush();
    next_flush = now + std::chrono::seconds(1);
  }
}

void JensenFlash::pause()
{
  service(true);
  sampled = {};
}

void JensenFlash::resume()
{
  sampled = system.device_access_time();
}

bool JensenFlash::save(FILE* file)
{
  if (sampled != Clock::time_point{})
    advance(system.device_access_time());
  const u32 size = static_cast<u32>(seed.size());
  if (fwrite(&size, sizeof(size), 1, file) != 1)
    return false;
  if (!chip)
    return true;
  const auto& s = chip->state();
  const u8 registers[] = {s.mode, s.setup, s.operation, s.errors, s.data};
  return fwrite(registers, sizeof(registers), 1, file) == 1 &&
    fwrite(&s.address, sizeof(s.address), 1, file) == 1 &&
    fwrite(&s.remaining_ns, sizeof(s.remaining_ns), 1, file) == 1 &&
    fwrite(chip->bytes().data(), size, 1, file) == 1;
}

bool JensenFlash::restore(FILE* file, bool legacy)
{
  restoring = true;
  if (!legacy) {
    u32 size;
    if (fread(&size, sizeof(size), 1, file) != 1 || size != seed.size())
      return false;
  }
  if (!chip)
    return true;
  Intel28F008SA restored(seed);
  if (!legacy) {
    auto& s = restored.state();
    u8 registers[5];
    if (fread(registers, sizeof(registers), 1, file) != 1 ||
        fread(&s.address, sizeof(s.address), 1, file) != 1 ||
        fread(&s.remaining_ns, sizeof(s.remaining_ns), 1, file) != 1 ||
        fread(restored.bytes().data(), restored.bytes().size(), 1, file) != 1)
      return false;
    s.mode = registers[0];
    s.setup = registers[1];
    s.operation = registers[2];
    s.errors = registers[3];
    s.data = registers[4];
    if (!restored.valid_state())
      return false;
    restored.set_vpp(vpp_high);
  }
  dirty = dirty || restored.bytes() != chip->bytes();
  chip = std::move(restored);
  sampled = {};
  return true;
}

void JensenFlash::finalize_restore()
{
  restoring = false;
}

