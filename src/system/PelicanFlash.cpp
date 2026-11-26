/* ES40 emulator -- DEC 3000 Model 300X IOCTL flash window
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
#include "system/PelicanFlash.h"
#include "system/System.h"
#include <fstream>

enum PelicanFlashRange { DenseROM, SparseROM, DenseSSR, SparseSSR };
static constexpr size_t flash_size = 256 * 1024;
static constexpr u32 support_mask = 0x0f0002bf; // ROM controls, read mask and system-OK LED (EK-D3SYS-PM, §7.3.11).

PelicanFlash::PelicanFlash(CConfigurator* cfg, CSystem* system) : CSystemComponent(cfg, system)
{
  unsigned fitted = 0;
  for (size_t bank = 0; bank < images.size(); ++bank) {
    const std::string key = "rom.flash" + std::to_string(bank);
    const char* path = cfg->get_text_value(key.c_str(), "");
    if (!*path)
      continue;
    if (++fitted > 3)
      FAILURE(Configuration, "The 300X has three 256 KiB flash devices");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
      FAILURE(Configuration, std::string("Cannot open 300X flash image: ") + path);
    if (file.tellg() != std::streamoff(flash_size))
      FAILURE(Configuration, std::string("300X flash image must be a raw 256 KiB device: ") + path);
    images[bank].resize(flash_size);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(images[bank].data()), flash_size))
      FAILURE(IO, std::string("Cannot read 300X flash image: ") + path);
  }

  // Slot 5 maps each dense longword to an even sparse longword (EK-D3SYS-PM, Table 16 and App. A.1).
  system->RegisterMemory(this, DenseROM, U64(0x1a0000000), flash_size);
  system->RegisterMemory(this, SparseROM, U64(0x1b0000000), flash_size * 2);
  system->RegisterMemory(this, DenseSSR, U64(0x1a0040100), 4);
  system->RegisterMemory(this, SparseSSR, U64(0x1b0080200), 8);
  save_item(support, "ssr");
}

std::string PelicanFlash::snapshot_identity() const
{
  std::string id = CSystemComponent::snapshot_identity();
  for (const auto& image : images) {
    id += ':';
    if (image.empty())
      continue;
    u64 hash = U64(14695981039346656037);
    for (u8 byte : image)
      hash = (hash ^ byte) * U64(1099511628211);
    id += std::to_string(hash);
  }
  return id;
}

static u32 flash_longword(const u8* p)
{
  return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

void PelicanFlash::load_firmware(char* system_ram, char* io_ram) const
{
  constexpr size_t header = 0x40;
  auto body_length = [&](unsigned bank, const char* module, const char* object, u32 edit) {
    const auto& image = images[bank];
    // The V7 payload follows the packed TURBOchannel and ROM object headers (EK-D3SYS-PM, §13.3).
    if (image.empty() || flash_longword(&image[4]) != 0xffaa0055 ||
        memcmp(&image[8], "V7.0    DEC     ", 16) || memcmp(&image[0x18], module, 8) ||
        memcmp(&image[0x20], "ALPH", 4) || (image[0x29] & 0xf) != 1 || image[0x2b] == 0 ||
        memcmp(&image[0x30], object, 12) || flash_longword(&image[0x3c]) != edit)
      FAILURE_2(Configuration, "Pelican direct SRM requires V7.0 %s for EV4 pass 3 in rom.flash%u", module, bank);
    const size_t length = flash_longword(&image[0x2c]);
    if (length == 0 || (length & 3) || length > image.size() - header - 1)
      FAILURE_1(Configuration, "Invalid Pelican ROM object length in rom.flash%u", bank);
    // DEC's M300 V7.0 updater stores the rotate-and-add checksum in the last chip byte (EK-D3SYS-PM, §13.3).
    u8 checksum = 0;
    for (size_t i = 0; i < header + length; ++i)
      checksum = static_cast<u8>(((checksum << 1) | (checksum >> 7)) + image[i]);
    if (checksum != image.back())
      FAILURE_1(Configuration, "Invalid Pelican firmware checksum in rom.flash%u", bank);
    return length;
  };

  const size_t sys_length = body_length(2, "PELI_SYS", "SYSTEM_ROM  ", 0x889);
  const size_t io_length = body_length(0, "PELI_IO ", "IO_ROM      ", 0x21f);
  // The SROM clears low memory before loading the two flash objects (EK-D3SYS-PM, §14.1, Table 46).
  memset(system_ram + sys_length, 0, flash_size - sys_length);
  memset(io_ram + io_length, 0, flash_size - io_length);
  memcpy(system_ram, &images[2][header], sys_length);
  memcpy(io_ram, &images[0][header], io_length);
}

unsigned PelicanFlash::selected_bank() const noexcept
{
  return ((support >> 8) & 2) | ((support >> 7) & 1);
}

bool PelicanFlash::decodes_memory_access(int index, u64 address, int dsize, bool) const noexcept
{
  if (dsize != 32 && dsize != 64)
    return false;
  const bool sparse = index == SparseROM || index == SparseSSR;
  if (address & ((sparse || dsize == 64) ? 7 : 3))
    return false;
  if (index == DenseSSR || index == SparseSSR)
    return address == 0;
  return !images[selected_bank()].empty();
}

u32 PelicanFlash::read_word(size_t offset) const
{
  const auto& image = images[selected_bank()];
  // SSR<26> selects one ROM byte at SSR<25:24>, otherwise IOCTL packs four bytes (EK-D3SYS-PM, §7.2).
  if (support & (1U << 26))
    return image[offset + ((support >> 24) & 3)];
  return flash_longword(&image[offset]);
}

static u32 enabled_bytes(u32 enables)
{
  u32 result = 0;
  for (unsigned lane = 0; lane < 4; ++lane)
    if (enables & (1U << lane))
      result |= 0xffU << (lane * 8);
  return result;
}

u64 PelicanFlash::ReadMem(int index, u64 address, int dsize)
{
  const bool sparse = index == SparseROM || index == SparseSSR;
  const bool is_ssr = index == DenseSSR || index == SparseSSR;
  const size_t offset = size_t(sparse ? address >> 1 : address);
  u32 low = is_ssr ? support : read_word(offset);
  if (sparse && (support & 0x10)) // Set bits suppress their sparse read lanes (EK-D3SYS-PM, §7.3.11).
    low &= ~enabled_bytes(support);
  if (dsize == 32)
    return low;

  // Dense LDQ reads two longwords; sparse LDQ duplicates one (EK-D3SYS-PM, Tables 75–76).
  const u32 high = sparse ? low : (is_ssr ? 0xffffffffU : read_word(offset + 4));
  return (u64(high) << 32) | low;
}

void PelicanFlash::WriteMem(int index, u64, int dsize, u64 data)
{
  if (index == DenseROM || index == SparseROM)
    return; // W1 in the write-disable position inhibits FEPROM writes (EK-D3SYS-PM, §9.5.2).
  u32 mask = support_mask;
  if (index == SparseSSR && dsize == 64)
    mask &= enabled_bytes(u32(data >> 32)); // Sparse STQ carries byte enables in the high longword (EK-D3SYS-PM, App. A.1).
  support = (support & ~mask) | (u32(data) & mask);
}
