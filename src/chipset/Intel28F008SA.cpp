/* ES40 emulator -- Intel 28F008SA FlashFile memory
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

#include "Intel28F008SA.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
enum ReadMode : u8 { ReadArray, ReadIdentifier, ReadStatus };
enum Setup : u8 { NoSetup, ByteWriteSetup, BlockEraseSetup };
enum Operation : u8 { Idle, ByteWriting, BlockErasing, EraseSuspended };

// Write state machine status bits (Intel 290429-008, Table 4).
constexpr u8 ready = 0x80;
constexpr u8 suspended = 0x40;
constexpr u8 sequence_error = 0x30;
constexpr u8 vpp_low = 0x08;

// Typical byte write and 64 KB block erase times (Intel 290429-008, §9.10).
constexpr u64 byte_write_ns = 8000;
constexpr u64 block_erase_ns = 1600000000;

u32 block_base(u32 address)
{
  return address & ~(Intel28F008SA::block_size - 1);
}
}

Intel28F008SA::Intel28F008SA(std::vector<u8> image)
  : array(std::move(image))
{
  if (array.size() != capacity)
    throw std::invalid_argument("Intel 28F008SA requires a 1 MiB image");
}

u8 Intel28F008SA::read(u32 address) const
{
  if (address >= capacity)
    return 0xff;

  switch (chip.mode) {
  case ReadIdentifier:
    // Intel specifies identifier byte addresses 00000H and 00001H (§4.2).
    if (address == 0)
      return 0x89;
    if (address == 1)
      return 0xa2;
    return 0xff;
  case ReadStatus:
    if (chip.operation == EraseSuspended)
      return chip.errors | ready | suspended;
    return chip.operation == Idle ? chip.errors | ready : chip.errors;
  default:
    // Array reads during erase suspend are defined only in other blocks (§4.6).
    if (chip.operation == EraseSuspended && block_base(address) == chip.address)
      return 0xff;
    return array[address];
  }
}

void Intel28F008SA::write(u32 address, u8 data, bool vpp_high)
{
  set_vpp(vpp_high);
  if (address >= capacity)
    return;

  if (chip.operation != Idle) {
    // Only status, erase suspend, and suspended reads/resume are valid (§§4.5–4.7).
    if (data == 0x70)
      chip.mode = ReadStatus;
    else if (data == 0xb0 && chip.operation == BlockErasing)
      chip.operation = EraseSuspended;
    else if (chip.operation == EraseSuspended) {
      if (data == 0xff)
        chip.mode = ReadArray;
      else if (data == 0xd0) {
        chip.operation = BlockErasing;
        chip.mode = ReadStatus;
      }
    }
    return;
  }

  if (chip.setup != NoSetup) {
    const bool erase = chip.setup == BlockEraseSetup;
    const u32 setup_address = chip.address;
    chip.setup = NoSetup;
    chip.mode = ReadStatus;

    // An invalid erase confirm sets both command sequence error bits (§7.0).
    if (erase && data != 0xd0) {
      chip.errors |= sequence_error;
      return;
    }
    // Both writes must select the same erase block or program byte (Table 3).
    if (erase ? block_base(address) != block_base(setup_address) : address != setup_address)
      return;
    // A low VPP error must be cleared before retrying (§4.4, Table 4).
    if (!vpp_high || (chip.errors & vpp_low)) {
      chip.errors |= vpp_low;
      return;
    }
    chip.address = erase ? block_base(address) : address;
    chip.data = erase ? 0 : data;
    chip.operation = erase ? BlockErasing : ByteWriting;
    chip.remaining_ns = erase ? block_erase_ns : byte_write_ns;
    return;
  }

  // Command user interface (Intel 290429-008, Table 3).
  switch (data) {
  case 0xff:
    chip.mode = ReadArray;
    break;
  case 0x90:
    chip.mode = ReadIdentifier;
    break;
  case 0x70:
    chip.mode = ReadStatus;
    break;
  case 0x50:
    chip.errors = 0;
    break;
  case 0x40:
  case 0x10:
    chip.setup = ByteWriteSetup;
    chip.address = address;
    break;
  case 0x20:
    chip.setup = BlockEraseSetup;
    chip.address = address;
    break;
  }
}

bool Intel28F008SA::advance(u64 elapsed_ns)
{
  if (chip.operation != ByteWriting && chip.operation != BlockErasing)
    return false;

  if (elapsed_ns < chip.remaining_ns) {
    chip.remaining_ns -= elapsed_ns;
    return false;
  }

  bool changed;
  if (chip.operation == ByteWriting) {
    // Internal verify checks only programmed 1-to-0 transitions (§4.7).
    const u8 programmed = array[chip.address] & chip.data;
    changed = array[chip.address] != programmed;
    array[chip.address] = programmed;
  } else {
    auto first = array.begin() + chip.address;
    auto last = first + block_size;
    changed = std::any_of(first, last, [](u8 value) { return value != 0xff; });
    std::fill(first, last, u8{0xff});
  }
  chip.remaining_ns = 0;
  chip.operation = Idle;
  return changed;
}

void Intel28F008SA::set_vpp(bool high)
{
  // Removing programming voltage aborts the active write or erase (§§6.0–7.0).
  if (!high && chip.operation != Idle) {
    chip.operation = Idle;
    chip.remaining_ns = 0;
    chip.mode = ReadStatus;
    chip.errors |= vpp_low;
  }
}

void Intel28F008SA::reset()
{
  // RP# resets the CUI to read array and the status register to 80H (§3.4).
  chip = State{};
}

bool Intel28F008SA::valid_state() const
{
  if (array.size() != capacity || chip.mode > ReadStatus || chip.setup > BlockEraseSetup ||
      chip.operation > EraseSuspended || (chip.errors & ~(sequence_error | vpp_low)) ||
      chip.address >= capacity)
    return false;

  if (chip.operation == Idle)
    return chip.remaining_ns == 0;

  if (chip.setup != NoSetup || chip.remaining_ns == 0 || (chip.errors & vpp_low))
    return false;

  if (chip.operation == ByteWriting)
    return chip.mode == ReadStatus && chip.remaining_ns <= byte_write_ns;

  return chip.address == block_base(chip.address) && chip.remaining_ns <= block_erase_ns &&
         (chip.mode == ReadStatus || (chip.operation == EraseSuspended && chip.mode == ReadArray));
}
