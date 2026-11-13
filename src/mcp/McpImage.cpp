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
 * Screen images for the MCP server (see McpImage.h).
 **/

#include "McpImage.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <zlib.h>

namespace es40mcp
{

Image crop(const Image& src, Rect r)
{
  Image out;
  int x0 = std::max(r.x, 0);
  int y0 = std::max(r.y, 0);
  int x1 = std::min(r.x + r.w, (int)src.w);
  int y1 = std::min(r.y + r.h, (int)src.h);

  if (x1 <= x0 || y1 <= y0)
    return out;
  out.w = (unsigned)(x1 - x0);
  out.h = (unsigned)(y1 - y0);
  out.px.resize((size_t)out.w * out.h);
  for (unsigned y = 0; y < out.h; y++)
  {
    const uint32_t* s = &src.px[(size_t)(y0 + y) * src.w + x0];
    std::copy(s, s + out.w, &out.px[(size_t)y * out.w]);
  }
  return out;
}

Image scale(const Image& src, double factor)
{
  Image out;

  if (src.empty())
    return out;
  out.w = std::max(1u, (unsigned)std::lround(src.w * factor));
  out.h = std::max(1u, (unsigned)std::lround(src.h * factor));
  if (out.w == src.w && out.h == src.h)
    return src;
  out.px.resize((size_t)out.w * out.h);

  for (unsigned oy = 0; oy < out.h; oy++)
  {
    unsigned sy0 = (unsigned)((uint64_t)oy * src.h / out.h);
    unsigned sy1 = (unsigned)((uint64_t)(oy + 1) * src.h / out.h);

    if (sy1 <= sy0)
      sy1 = sy0 + 1;
    for (unsigned ox = 0; ox < out.w; ox++)
    {
      unsigned sx0 = (unsigned)((uint64_t)ox * src.w / out.w);
      unsigned sx1 = (unsigned)((uint64_t)(ox + 1) * src.w / out.w);
      uint64_t r = 0, g = 0, b = 0, n = 0;

      if (sx1 <= sx0)
        sx1 = sx0 + 1;
      for (unsigned sy = sy0; sy < sy1 && sy < src.h; sy++)
      {
        for (unsigned sx = sx0; sx < sx1 && sx < src.w; sx++)
        {
          uint32_t p = src.at(sx, sy);
          r += (p >> 16) & 0xff;
          g += (p >> 8) & 0xff;
          b += p & 0xff;
          n++;
        }
      }
      if (!n)
        n = 1;
      out.px[(size_t)oy * out.w + ox] = 0xff000000u | (uint32_t)((r / n) << 16) |
        (uint32_t)((g / n) << 8) | (uint32_t)(b / n);
    }
  }
  return out;
}

Image mark(const Image& src, int x, int y)
{
  Image out = src;
  auto put = [&](int px, int py, uint32_t c) {
    if (px >= 0 && py >= 0 && px < (int)out.w && py < (int)out.h)
      out.px[(size_t)py * out.w + px] = c;
  };

  for (int d = -7; d <= 7; d++)
  {
    // a red cross with a dark edge, readable on any background
    put(x + d, y - 1, 0xff000000u);
    put(x + d, y + 1, 0xff000000u);
    put(x - 1, y + d, 0xff000000u);
    put(x + 1, y + d, 0xff000000u);
  }
  for (int d = -7; d <= 7; d++)
  {
    put(x + d, y, 0xffff2020u);
    put(x, y + d, 0xffff2020u);
  }
  return out;
}

static void put_be32(std::string& s, uint32_t v)
{
  s += (char)(v >> 24);
  s += (char)(v >> 16);
  s += (char)(v >> 8);
  s += (char)v;
}

static void chunk(std::string& out, const char* type, const std::string& data)
{
  std::string body(type, 4);

  body += data;
  put_be32(out, (uint32_t)data.size());
  out += body;
  put_be32(out, (uint32_t)crc32(0, (const Bytef*)body.data(), (uInt)body.size()));
}

static int paeth(int a, int b, int c)
{
  int p = a + b - c;
  int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);

  if (pa <= pb && pa <= pc)
    return a;
  return pb <= pc ? b : c;
}

std::string png(const Image& img)
{
  static const char signature[8] = { '\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n' };
  const size_t row = (size_t)img.w * 3;
  std::string out(signature, 8);
  std::string header;
  std::vector<uint8_t> raw;
  std::vector<uint8_t> cur(row), prev(row, 0), trial(row), best(row);

  put_be32(header, img.w);
  put_be32(header, img.h);
  header += (char)8;  // bits per sample
  header += (char)2;  // RGB
  header += (char)0;  // deflate
  header += (char)0;  // adaptive filters
  header += (char)0;  // no interlace
  chunk(out, "IHDR", header);

  raw.reserve((row + 1) * img.h);
  for (unsigned y = 0; y < img.h; y++)
  {
    const uint32_t* p = &img.px[(size_t)y * img.w];
    long best_cost = -1;
    uint8_t best_filter = 0;

    for (unsigned x = 0; x < img.w; x++)
    {
      cur[x * 3 + 0] = (uint8_t)(p[x] >> 16);
      cur[x * 3 + 1] = (uint8_t)(p[x] >> 8);
      cur[x * 3 + 2] = (uint8_t)p[x];
    }
    // pick the filter whose output looks smallest (sum of |signed byte|)
    for (uint8_t f = 0; f <= 4; f++)
    {
      long cost = 0;
      for (size_t i = 0; i < row; i++)
      {
        int a = i >= 3 ? cur[i - 3] : 0;
        int b = prev[i];
        int c = i >= 3 ? prev[i - 3] : 0;
        int pred = 0;
        switch (f)
        {
        case 1: pred = a; break;
        case 2: pred = b; break;
        case 3: pred = (a + b) / 2; break;
        case 4: pred = paeth(a, b, c); break;
        default: break;
        }
        trial[i] = (uint8_t)(cur[i] - pred);
        cost += std::abs((int)(int8_t)trial[i]);
      }
      if (best_cost < 0 || cost < best_cost)
      {
        best_cost = cost;
        best_filter = f;
        best.swap(trial);
      }
    }
    raw.push_back(best_filter);
    raw.insert(raw.end(), best.begin(), best.end());
    prev.swap(cur);
  }

  uLongf zlen = compressBound((uLong)raw.size());
  std::string z(zlen, '\0');
  if (compress2((Bytef*)&z[0], &zlen, raw.data(), (uLong)raw.size(), 6) != Z_OK)
    return std::string();
  z.resize(zlen);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", std::string());
  return out;
}

std::string base64(const std::string& data)
{
  static const char table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;

  out.reserve((data.size() + 2) / 3 * 4);
  while (i + 2 < data.size())
  {
    uint32_t v = ((uint8_t)data[i] << 16) | ((uint8_t)data[i + 1] << 8) | (uint8_t)data[i + 2];
    out += table[(v >> 18) & 63];
    out += table[(v >> 12) & 63];
    out += table[(v >> 6) & 63];
    out += table[v & 63];
    i += 3;
  }
  if (i + 1 == data.size())
  {
    uint32_t v = (uint8_t)data[i] << 16;
    out += table[(v >> 18) & 63];
    out += table[(v >> 12) & 63];
    out += "==";
  }
  else if (i + 2 == data.size())
  {
    uint32_t v = ((uint8_t)data[i] << 16) | ((uint8_t)data[i + 1] << 8);
    out += table[(v >> 18) & 63];
    out += table[(v >> 12) & 63];
    out += table[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

} // namespace es40mcp
