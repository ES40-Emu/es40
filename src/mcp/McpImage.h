/* ES40 emulator.
 * Copyright (C) 2026 by Antoni Sawicki
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

/**
 * \file
 * Screen images for the MCP server: the frames the displays show, cropped,
 * scaled and written as PNG.
 **/

#ifndef ES40_MCP_IMAGE_H
#define ES40_MCP_IMAGE_H

#include <cstdint>
#include <string>
#include <vector>

namespace es40mcp
{

/// An ARGB32 image (0xAARRGGBB), as the displays hand it over
struct Image
{
  unsigned w = 0;
  unsigned h = 0;
  std::vector<uint32_t> px;

  bool empty() const { return w == 0 || h == 0; }
  uint32_t at(unsigned x, unsigned y) const { return px[(size_t)y * w + x]; }
};

struct Rect
{
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  bool contains(int px, int py) const
  {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};

/// The part of src inside r, cut to fit
Image crop(const Image& src, Rect r);

/// src made factor times as big: averaged when smaller, nearest pixel when bigger
Image scale(const Image& src, double factor);

/// A copy with a small cross marker at x, y
Image mark(const Image& src, int x, int y);

/// The image as an RGB PNG file
std::string png(const Image& img);

std::string base64(const std::string& data);

} // namespace es40mcp

#endif
