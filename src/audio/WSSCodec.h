/* AD1848 codec adapted from 86Box src/sound/snd_ad1848.c.
 * Copyright 2008-2020 Sarah Walker, 2018-2020 TheCollector1995, 2021-2025 RichardG.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ES40_WSS_CODEC_H
#define ES40_WSS_CODEC_H

#include <cstdint>

class WSSCodec
{
public:
    void reset();
    std::uint8_t read(unsigned port);
    void write(unsigned port, std::uint8_t value);

    bool playback_enabled() const;
    bool capture_enabled() const;
    bool irq_asserted() const;
    unsigned sample_rate() const;
    unsigned frame_bytes() const;
    void decode_frame(const std::uint8_t* guest, std::int16_t& left, std::int16_t& right) const;
    void encode_frame(std::int16_t left, std::int16_t right, std::uint8_t* guest) const;
    void advance_frame();

private:
    std::uint8_t regs[16];
    std::uint8_t index;
    std::uint8_t status;
    std::uint16_t count;
};

#endif
