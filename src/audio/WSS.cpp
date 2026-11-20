/* ISA Windows Sound System, adapted from 86Box src/sound/snd_wss.c.
 * Copyright 2012-2018 Sarah Walker, 2018 TheCollector1995.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "StdAfx.h"
#include "WSS.h"
#include "chipset/AliM1543C.h"
#include "chipset/DMA.h"
#include "system/System.h"
#include <algorithm>

namespace {
constexpr u32 base = 0x530;
constexpr int dma_channels[] = {0, 0, 1, 3};
constexpr int irq_lines[] = {5, 7, 9, 10, 11, 12, 14, 15};
constexpr int batch_frames = 1024;

host_audio::Format host_format(unsigned rate)
{
    return {static_cast<int>(rate), 2, host_audio::SampleFormat::s16_le};
}
}

CWSS::CWSS(CConfigurator* cfg, CSystem* system) : CSystemComponent(cfg, system)
{
    system->RegisterMemory(this, 0, U64(0x00000801fc000000) + base, 8);
    backend = host_audio::backend_for_gui(cfg->get_gui_name());
}

CWSS::~CWSS()
{
    stop_threads();
    if (playback) host_audio::destroy_stream(playback);
    if (output) host_audio::close_device(output);
}

void CWSS::init()
{
    ResetPCI();
    if (!host_audio::initialize(backend))
        HOST_AUDIO_FAILURE(backend, "WSS: unable to initialize %s audio");
    output = host_audio::open_output_device(backend);
    if (!output)
        HOST_AUDIO_FAILURE(backend, "WSS: unable to open audio output");
    const auto format = host_format(state.codec.sample_rate());
    host_audio::create_stream(playback, &format, nullptr, backend);
    if (!playback)
        HOST_AUDIO_FAILURE(backend, "WSS: unable to create playback stream");
}

void CWSS::ResetPCI()
{
    std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
    set_irq(false);
    set_drq(false);
    state = {};
    state.config = 0x0b; // Power-on routing is IRQ 7, DMA 3.
    state.codec.reset();
    frame_credit = 0;
    last_tick = cSystem->device_access_time();
    close_capture();
    if (playback) host_audio::clear_stream(playback);
}

const CSystemComponent* CWSS::memory_decode_owner() const noexcept
{
    return bridge ? static_cast<const CSystemComponent*>(bridge) : this;
}

bool CWSS::decodes_memory_access(int, u64 address, int, bool) const noexcept
{
    return address < 8 && bridge &&
        bridge->isa_io_decode(base + (u32)address) != CAliM1543C::IsaIoDecode::None;
}

bool CWSS::uses_subtractive_decode(int, u64 address, int, bool) const noexcept
{
    return address < 8 && bridge &&
        bridge->isa_io_decode(base + (u32)address) == CAliM1543C::IsaIoDecode::Subtractive;
}

void CWSS::set_drq(bool asserted)
{
    if (dma && asserted != state.drq) {
        dma->set_drq(dma_channels[state.config & 3], asserted);
        state.drq = asserted;
    }
}

void CWSS::set_irq(bool asserted)
{
    if (bridge && asserted != state.irq) {
        const int irq = irq_lines[(state.config >> 3) & 7];
        bridge->pic_set_line(irq >> 3, irq & 7, asserted);
        state.irq = asserted;
    }
}

u64 CWSS::ReadMem(int index, u64 address, int dsize)
{
    service_audio(cSystem->device_access_time());
    u64 result = 0;
    for (int shift = 0; shift < dsize; shift += 8, ++address) {
        u8 value = 0xff;
        if (decodes_memory_access(index, address, 8, false))
            value = address < 4 ? 0x04 | (state.config & 0x40) : state.codec.read((unsigned)address - 4);
        result |= (u64)value << shift;
    }
    return result;
}

void CWSS::WriteMem(int index, u64 address, int dsize, u64 data)
{
    const auto now = cSystem->device_access_time();
    service_audio(now);
    for (int shift = 0; shift < dsize; shift += 8, ++address) {
        if (!decodes_memory_access(index, address, 8, true))
            continue;
        const u8 value = (u8)(data >> shift);
        if (address < 4) {
            if (dma_channels[value & 3] != dma_channels[state.config & 3]) {
                set_drq(false);
                state.position = 0;
            }
            if ((value ^ state.config) & 0x38)
                set_irq(false);
            state.config = value;
        } else {
            const bool playing = state.codec.playback_enabled();
            const bool recording = state.codec.capture_enabled();
            const bool format_write = address == 5 && (state.codec.read(0) & 0x4f) == 0x48;
            state.codec.write((unsigned)address - 4, value);
            if (format_write || playing != state.codec.playback_enabled() ||
                recording != state.codec.capture_enabled()) {
                set_drq(false);
                state.position = 0;
                frame_credit = 0;
                last_tick = now;
                if (running.load()) update_streams();
            }
        }
        set_irq(state.codec.irq_asserted());
    }
}

void CWSS::close_capture()
{
    if (capture) host_audio::destroy_stream(capture);
    if (input) host_audio::close_device(input);
    capture = {};
    input = {};
    capture_requested = false;
}

void CWSS::update_streams()
{
    const unsigned rate = state.codec.sample_rate();
    const auto format = host_format(rate);
    if (host_rate != rate) {
        if (!host_audio::set_format(playback, &format, nullptr))
            HOST_AUDIO_FAILURE(backend, "WSS: unable to set playback rate");
        if (capture && !host_audio::set_format(capture, nullptr, &format))
            HOST_AUDIO_FAILURE(backend, "WSS: unable to set capture rate");
        host_rate = rate;
    }
    if (!state.codec.capture_enabled()) {
        close_capture();
    } else if (!capture_requested) {
        input = host_audio::open_input_device(backend);
        if (input) {
            host_audio::create_stream(capture, nullptr, &format, backend);
            if (!capture || !host_audio::bind_stream(input, capture))
                close_capture();
        }
        if (!capture)
            printf("%s: capture input unavailable, recording silence.\n", devid_string);
        capture_requested = true;
    }
}

void CWSS::start_threads()
{
    std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
    if (running.load()) return;
    if (!host_audio::bind_stream(output, playback))
        HOST_AUDIO_FAILURE(backend, "WSS: unable to bind playback stream");
    try {
        update_streams();
        frame_credit = 0;
        last_tick = cSystem->device_access_time();
        failed.store(false);
        running.store(true);
        worker.start(*this);
    } catch (...) {
        stop_threads();
        throw;
    }
}

void CWSS::stop_threads()
{
    running.store(false);
    worker.join();
    std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
    close_capture();
    if (playback) {
        host_audio::unbind_stream(playback);
        host_audio::clear_stream(playback);
    }
}

void CWSS::check_state()
{
    if (failed.load()) FAILURE(Thread, "WSS audio worker failed");
}

void CWSS::run()
{
    try {
        while (running.load()) {
            {
                std::unique_lock<std::recursive_mutex> lock(cSystem->get_device_bus_mutex(), std::try_to_lock);
                if (lock.owns_lock())
                    service_audio(std::chrono::steady_clock::now());
            }
            CThread::sleep(2);
        }
    } catch (...) {
        failed.store(true);
        throw;
    }
}

void CWSS::service_audio(std::chrono::steady_clock::time_point now)
{
    if (!running.load() || now <= last_tick) return;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_tick).count();
    last_tick = now;
    const bool playing = state.codec.playback_enabled();
    if (!dma || (!playing && !state.codec.capture_enabled())) return;
    // Limit catch-up after host stalls while retaining the fractional sample clock.
    constexpr s64 second = 1000000000;
    frame_credit += (std::min<s64>)(ns, second / 50) * state.codec.sample_rate();
    const int frames = int((std::min<s64>)(frame_credit / second, batch_frames));
    if (!frames) return;
    frame_credit %= second;
    set_drq(true);

    u8 pcm[batch_frames * 4];
    int received = 0;
    if (!playing && capture) {
        received = host_audio::read(capture, pcm, frames * 4);
        if (received < 0)
            HOST_AUDIO_FAILURE(backend, "WSS: unable to read capture stream");
    }
    const int channel = dma_channels[state.config & 3];
    const unsigned frame_bytes = state.codec.frame_bytes();
    int complete = 0;
    while (complete < frames) {
        u8* sample = &pcm[complete * 4];
        if (!playing && !state.position) {
            s16 left = 0, right = 0;
            if (complete * 4 + 4 <= received) {
                left = (s16)(sample[0] | (sample[1] << 8));
                right = (s16)(sample[2] | (sample[3] << 8));
            }
            state.codec.encode_frame(left, right, state.frame);
        }
        while (state.position < frame_bytes) {
            u16 unit = state.frame[state.position];
            const auto result = playing ? dma->service_recv_unit(channel, unit)
                                        : dma->service_send_unit(channel, unit);
            if (!result.transferred) break;
            state.frame[state.position++] = (u8)unit;
        }
        if (state.position < frame_bytes) break;
        state.position = 0;
        if (playing) {
            s16 left, right;
            state.codec.decode_frame(state.frame, left, right);
            sample[0] = (u8)left;
            sample[1] = (u8)((u16)left >> 8);
            sample[2] = (u8)right;
            sample[3] = (u8)((u16)right >> 8);
        }
        ++complete;
        state.codec.advance_frame();
        set_irq(state.codec.irq_asserted());
        if (!state.codec.playback_enabled() && !state.codec.capture_enabled()) break;
    }
    const bool held = !state.codec.playback_enabled() && !state.codec.capture_enabled();
    if (complete == frames || held)
        set_drq(false);
    if (!playing && held) close_capture();
    if (playing && complete) {
        const int queued = host_audio::queued(playback);
        // Keep at most a quarter-second of stereo 16-bit samples ahead of the host.
        if (queued < 0 || (queued < (int)host_rate && !host_audio::write(playback, pcm, complete * 4)))
            HOST_AUDIO_FAILURE(backend, "WSS: unable to queue playback");
    }
}

int CWSS::SaveState(FILE* file)
{
    std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
    static_assert(std::is_trivially_copyable_v<decltype(state)>);
    return file && fwrite(&state, sizeof(state), 1, file) == 1 ? 0 : -1;
}

int CWSS::RestoreState(FILE* file)
{
    std::lock_guard<std::recursive_mutex> lock(cSystem->get_device_bus_mutex());
    decltype(state) saved{};
    if (!file || fread(&saved, sizeof(saved), 1, file) != 1 ||
        saved.position >= saved.codec.frame_bytes() || saved.drq > 1 || saved.irq > 1)
        return -1;
    state = saved;
    frame_credit = 0;
    last_tick = cSystem->device_access_time();
    return 0;
}
