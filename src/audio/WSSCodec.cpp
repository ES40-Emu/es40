/* ES40 AD1848 codec port from 86Box src/sound/snd_ad1848.c, d4f9a0432fb8.
 * Copyright 2008-2020 Sarah Walker, 2018-2020 TheCollector1995, 2021-2025 RichardG.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "WSSCodec.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
using std::int16_t;
using std::uint8_t;
using std::uint16_t;

enum SampleFormat { Unsigned8, MuLaw, Signed16, ALaw };

int16_t decode_sample(unsigned format, const uint8_t* data)
{
    switch (format) {
    case Unsigned8:
        return (int(data[0]) - 128) * 256;
    case Signed16:
        return static_cast<int16_t>(data[0] | (uint16_t(data[1]) << 8));
    case MuLaw: {
        const uint8_t code = ~data[0];
        const int magnitude = (((code & 15) * 8 + 0x84) << ((code >> 4) & 7)) - 0x84;
        return static_cast<int16_t>((code & 0x80) ? -magnitude : magnitude);
    }
    default: {
        const uint8_t code = data[0] ^ 0x55;
        const unsigned segment = (code >> 4) & 7;
        int magnitude = (code & 15) * 16 + 8;
        if (segment)
            magnitude = (magnitude + 256) << (segment - 1);
        return static_cast<int16_t>((code & 0x80) ? magnitude : -magnitude);
    }
    }
}

void encode_sample(unsigned format, int16_t sample, uint8_t* data)
{
    switch (format) {
    case Unsigned8:
        data[0] = (int(sample) + 32768) >> 8;
        return;
    case Signed16:
        data[0] = static_cast<uint8_t>(sample);
        data[1] = static_cast<uint16_t>(sample) >> 8;
        return;
    default:
        break;
    }

    const bool negative = sample < 0;
    int magnitude = negative ? -int(sample) : int(sample);
    if (format == MuLaw)
        magnitude = (std::min)(magnitude + 0x84, 32767);
    else if (negative)
        --magnitude;

    unsigned segment = 0;
    for (int threshold = 256; magnitude >= threshold; threshold *= 2)
        ++segment;
    const unsigned shift = (format == ALaw && !segment) ? 4 : segment + 3;
    const uint8_t code = static_cast<uint8_t>((segment << 4) | ((magnitude >> shift) & 15));
    if (format == MuLaw)
        data[0] = code ^ (negative ? 0x7f : 0xff);
    else
        data[0] = code ^ (negative ? 0x55 : 0xd5);
}

double attenuation(unsigned steps)
{
    static const auto gains = [] {
        std::array<double, 64> table{};
        for (unsigned i = 0; i < table.size(); ++i)
            table[i] = std::pow(10.0, -1.5 * i / 20.0);
        return table;
    }();
    return gains[steps];
}

int16_t dac_sample(int16_t sample, uint8_t control)
{
    return (control & 0x80) ? 0 : static_cast<int16_t>(sample * attenuation(control & 0x3f));
}

int16_t adc_sample(int16_t sample, uint8_t control)
{
    // The host input feeds line and mic. AUX1 and the post-mix input are silent.
    if (control & 0x40)
        return 0;
    double level = sample / attenuation(control & 15);
    if ((control & 0xa0) == 0xa0)
        level *= 10.0;
    return static_cast<int16_t>((std::clamp)(level, -32768.0, 32767.0));
}
}

void WSSCodec::reset()
{
    *this = {};
    index = 0x40;
    status = 0xcc;
    for (unsigned reg = 2; reg <= 7; ++reg)
        regs[reg] = 0x80;
    regs[9] = 0x08;
    regs[12] = 0x0a;
}

uint8_t WSSCodec::read(unsigned port)
{
    switch (port) {
    case 0:
        return index;
    case 1: {
        const unsigned reg = index & 15;
        const uint8_t value = regs[reg];
        if (reg == 11)
            regs[11] &= ~0x20;
        return value;
    }
    case 2:
        return status;
    default:
        return 0xff;
    }
}

void WSSCodec::write(unsigned port, uint8_t value)
{
    switch (port) {
    case 0:
        // Let polling guests observe calibration once on leaving mode change.
        if ((index & 0x40) && !(value & 0x40))
            regs[11] |= 0x20;
        index = value & 0x6f;
        break;
    case 1: {
        const unsigned reg = index & 15;
        if (reg == 11 || reg == 12)
            break;
        if (!(index & 0x40)) {
            if (reg == 8)
                break;
            if (reg == 9)
                value = (regs[9] & ~3) | (value & 3);
        }
        regs[reg] = value;
        if (reg == 14)
            count = (uint16_t(value) << 8) | regs[15];
        break;
    }
    case 2:
        status &= ~1;
        break;
    }
}

bool WSSCodec::playback_enabled() const
{
    return !(index & 0x40) && !((index & 0x20) && (status & 1)) &&
        (regs[9] & 0x41) == 1;
}

bool WSSCodec::capture_enabled() const
{
    return !(index & 0x40) && !((index & 0x20) && (status & 1)) &&
        (regs[9] & 0x83) == 2;
}

bool WSSCodec::irq_asserted() const
{
    return (status & 1) && (regs[10] & 2);
}

unsigned WSSCodec::sample_rate() const
{
    static const unsigned divisors[] = {3072, 1536, 896, 768, 448, 384, 512, 2560};
    const unsigned clock = (regs[8] & 1) ? 16934400 : 24576000;
    return clock / divisors[(regs[8] >> 1) & 7];
}

unsigned WSSCodec::frame_bytes() const
{
    const unsigned width = (regs[8] & 0x60) == 0x40 ? 2 : 1;
    return (regs[8] & 0x10) ? 2 * width : width;
}

void WSSCodec::decode_frame(const uint8_t* guest, int16_t& left, int16_t& right) const
{
    const unsigned format = (regs[8] >> 5) & 3;
    left = decode_sample(format, guest);
    right = (regs[8] & 0x10) ? decode_sample(format, guest + frame_bytes() / 2) : left;
    left = dac_sample(left, regs[6]);
    right = dac_sample(right, regs[7]);
}

void WSSCodec::encode_frame(int16_t left, int16_t right, uint8_t* guest) const
{
    const unsigned format = (regs[8] >> 5) & 3;
    encode_sample(format, adc_sample(left, regs[0]), guest);
    if (regs[8] & 0x10)
        encode_sample(format, adc_sample(right, regs[1]), guest + frame_bytes() / 2);
}

void WSSCodec::advance_frame()
{
    if (count) {
        --count;
    } else {
        status |= 1;
        count = (uint16_t(regs[14]) << 8) | regs[15];
    }
}
