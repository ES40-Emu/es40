/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "HostAudioBackend.h"
// The Windows 8.1 SDK uses IUnknown before declaring it under C++20.
struct IUnknown;
#include <windows.h>
#include <dsound.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace host_audio
{
namespace detail
{
namespace
{
constexpr int host_rate = 48000;
constexpr int period_frames = 512;
constexpr DWORD ring_frames = host_rate / 4;
constexpr size_t queue_limit = 8 * 1024 * 1024;
using Clock = std::chrono::steady_clock;
thread_local char last_error[256] = {};

bool fail(const char* operation, HRESULT result = E_FAIL)
{
    std::snprintf(last_error, sizeof(last_error), "%s (0x%08lx)",
        operation, static_cast<unsigned long>(result));
    return false;
}

bool valid_format(const Format& format)
{
    return format.freq > 0 && format.freq <= 768000 &&
        (format.channels == 1 || format.channels == 2) &&
        (format.format == SampleFormat::u8 || format.format == SampleFormat::s16_le);
}

bool same_format(const Format& a, const Format& b)
{
    return a.freq == b.freq && a.channels == b.channels && a.format == b.format;
}

size_t frame_bytes(const Format& format)
{
    return format.channels * (format.format == SampleFormat::u8 ? 1 : 2);
}

WAVEFORMATEX wave_format(const Format& format)
{
    WAVEFORMATEX wave = {};
    wave.wFormatTag = WAVE_FORMAT_PCM;
    wave.nChannels = static_cast<WORD>(format.channels);
    wave.nSamplesPerSec = format.freq;
    wave.wBitsPerSample = format.format == SampleFormat::u8 ? 8 : 16;
    wave.nBlockAlign = static_cast<WORD>(frame_bytes(format));
    wave.nAvgBytesPerSec = wave.nSamplesPerSec * wave.nBlockAlign;
    return wave;
}

struct ByteQueue
{
    std::vector<unsigned char> data;
    size_t first = 0;

    size_t size() const { return data.size() - first; }
    const unsigned char* front() const { return data.data() + first; }
    void clear() { data.clear(); first = 0; }
    void consume(size_t bytes)
    {
        first += bytes;
        if (first == data.size()) clear();
    }
    void append(const void* source, size_t bytes)
    {
        if (!bytes) return;
        if (first && (first >= data.size() / 2 || data.size() + bytes > queue_limit)) {
            data.erase(data.begin(), data.begin() + first);
            first = 0;
        }
        const auto* begin = static_cast<const unsigned char*>(source);
        data.insert(data.end(), begin, begin + bytes);
    }
};

int sample(const unsigned char* frame, const Format& format, int channel)
{
    if (format.channels == 1) channel = 0;
    if (format.format == SampleFormat::u8) return (int(frame[channel]) - 128) * 256;
    frame += channel * 2;
    const int value = int(frame[0]) | (int(frame[1]) << 8);
    return value >= 32768 ? value - 65536 : value;
}

void put_sample(unsigned char* destination, SampleFormat format, int value)
{
    value = (std::max)(-32768, (std::min)(32767, value));
    if (format == SampleFormat::u8) {
        *destination = static_cast<unsigned char>((value + 32768) >> 8);
    } else {
        const unsigned bits = static_cast<unsigned>(value);
        destination[0] = static_cast<unsigned char>(bits);
        destination[1] = static_cast<unsigned char>(bits >> 8);
    }
}

// Rational phase preserves rate and interpolation across arbitrarily split writes.
size_t convert(ByteQueue& input, const Format& source, const Format& target,
    std::uint64_t& phase, unsigned char* output, size_t frames)
{
    const size_t source_bytes = frame_bytes(source);
    const size_t target_bytes = frame_bytes(target);
    const size_t sample_bytes = target.format == SampleFormat::u8 ? 1 : 2;
    size_t produced = 0;
    while (produced < frames) {
        const size_t index = static_cast<size_t>(phase / target.freq);
        const int fraction = static_cast<int>(phase % target.freq);
        if (index + 1 + (fraction != 0) > input.size() / source_bytes) break;
        const unsigned char* a = input.front() + index * source_bytes;
        const unsigned char* b = fraction ? a + source_bytes : a;
        int stereo[2];
        for (int channel = 0; channel < 2; ++channel) {
            const int first = sample(a, source, channel);
            const int second = sample(b, source, channel);
            stereo[channel] = first + static_cast<int>(
                (std::int64_t(second - first) * fraction) / target.freq);
        }
        if (target.channels == 1) stereo[0] = (stereo[0] + stereo[1]) / 2;
        for (int channel = 0; channel < target.channels; ++channel)
            put_sample(output + channel * sample_bytes, target.format, stereo[channel]);
        output += target_bytes;
        ++produced;
        phase += source.freq;
        const size_t consumed = (std::min)(static_cast<size_t>(phase / target.freq),
            input.size() / source_bytes);
        input.consume(consumed * source_bytes);
        phase -= std::uint64_t(consumed) * target.freq;
    }
    return produced;
}

struct Device
{
    IDirectSound8* output = nullptr;
    IDirectSoundCapture8* input = nullptr;
    ~Device()
    {
        if (input) input->Release();
        if (output) output->Release();
    }
};

struct NativeStream
{
    Stream& owner;
    std::recursive_mutex mutex;
    std::condition_variable_any wake;
    std::thread worker;
    bool quitting = false;
    bool capture;
    bool restart = false;
    bool failed = false;
    char failure[256] = {};
    std::uintptr_t device = 0;
    Format guest;
    Format host{host_rate, 2, SampleFormat::s16_le};
    IDirectSoundBuffer* output = nullptr;
    IDirectSoundCaptureBuffer* input = nullptr;
    DWORD ring_bytes = 0;
    DWORD previous_cursor = 0;
    std::uint64_t play_position = 0;
    std::uint64_t producer = 0;
    std::uint64_t phase = 0;
    Clock::time_point last_service;
    Clock::time_point retry_after;
    ByteQueue source;
    ByteQueue captured;
    std::vector<unsigned char> scratch;

    NativeStream(Stream& stream, bool recording, const Format& format)
        : owner(stream), capture(recording), guest(format) {}
    ~NativeStream() { release_buffers(); }

    bool check()
    {
        if (!failed) return true;
        std::memcpy(last_error, failure, sizeof(last_error));
        return false;
    }
    bool remember(const char* operation, HRESULT result)
    {
        fail(operation, result);
        std::memcpy(failure, last_error, sizeof(failure));
        failed = true;
        if (output) output->Stop();
        if (input) input->Stop();
        return false;
    }
    void release_buffers()
    {
        if (output) { output->Stop(); output->Release(); output = nullptr; }
        if (input) { input->Stop(); input->Release(); input = nullptr; }
        device = 0;
        restart = false;
    }
    bool retry_restore(HRESULT result)
    {
        if (result != DSERR_BUFFERLOST && result != DSERR_PRIOLEVELNEEDED &&
            result != DSERR_OTHERAPPHASPRIO) return false;
        restart = true;
        retry_after = Clock::now() + std::chrono::milliseconds(20);
        return true;
    }
    bool silence(DWORD offset, DWORD bytes)
    {
        if (!bytes) return true;
        void* first = nullptr;
        void* second = nullptr;
        DWORD first_bytes = 0, second_bytes = 0;
        const HRESULT result = output->Lock(offset, bytes, &first, &first_bytes,
            &second, &second_bytes, 0);
        if (result == DSERR_BUFFERLOST) { restart = true; return true; }
        if (FAILED(result)) return remember("DirectSound buffer lock failed", result);
        std::memset(first, 0, first_bytes);
        if (second_bytes) std::memset(second, 0, second_bytes);
        const HRESULT unlocked = output->Unlock(first, first_bytes, second, second_bytes);
        return SUCCEEDED(unlocked) || remember("DirectSound buffer unlock failed", unlocked);
    }
    bool start_output()
    {
        HRESULT result = output->Stop();
        if (FAILED(result) && result != DSERR_BUFFERLOST)
            return remember("DirectSound playback stop failed", result);
        DWORD status = 0;
        result = output->GetStatus(&status);
        if (FAILED(result)) return retry_restore(result) ||
            remember("DirectSound playback status failed", result);
        if (status & DSBSTATUS_BUFFERLOST) {
            result = output->Restore();
            if (FAILED(result)) return retry_restore(result) ||
                remember("DirectSound buffer restore failed", result);
        }
        restart = false;
        if (!silence(0, ring_bytes) || restart) return !failed;
        result = output->SetCurrentPosition(0);
        if (FAILED(result)) return remember("DirectSound playback position failed", result);
        previous_cursor = 0;
        play_position = producer = 0;
        last_service = Clock::now();
        result = output->Play(0, 0, DSBPLAY_LOOPING);
        if (retry_restore(result)) return true;
        return SUCCEEDED(result) || remember("DirectSound playback start failed", result);
    }
    bool pump_output()
    {
        const auto now = Clock::now();
        if (restart && now < retry_after) return true;
        if (now - last_service >= std::chrono::milliseconds(200)) restart = true;
        DWORD status = 0;
        HRESULT result = output->GetStatus(&status);
        if (FAILED(result)) return remember("DirectSound playback status failed", result);
        if (status & DSBSTATUS_BUFFERLOST) restart = true;
        if (restart && (!start_output() || restart)) return !failed;
        DWORD play = 0, safe = 0;
        result = output->GetCurrentPosition(&play, &safe);
        if (result == DSERR_BUFFERLOST) { restart = true; return true; }
        if (FAILED(result)) return remember("DirectSound playback cursor failed", result);
        const DWORD frame = static_cast<DWORD>(frame_bytes(host));
        play -= play % frame;
        safe = ((safe + frame - 1) / frame * frame) % ring_bytes;
        play_position += (play + ring_bytes - previous_cursor) % ring_bytes;
        previous_cursor = play;
        last_service = now;
        const std::uint64_t safe_position = play_position + (safe + ring_bytes - play) % ring_bytes;
        producer = (std::max)(producer, safe_position);
        // The safe cursor can advance by 20 ms between worker runs even though
        // we request a 2 ms wait. Keep three periods beyond it so that scheduler
        // jitter cannot overtake a single 10.7 ms period and insert silence while
        // source samples remain queued.
        const std::uint64_t end = (std::min)(safe_position + 3 * period_frames * frame,
            play_position + ring_bytes - frame);
        if (producer < end) {
            const size_t bytes = static_cast<size_t>(end - producer);
            scratch.assign(bytes, 0);
            convert(source, guest, host, phase, scratch.data(), bytes / frame);
            void* first = nullptr;
            void* second = nullptr;
            DWORD first_bytes = 0, second_bytes = 0;
            result = output->Lock(static_cast<DWORD>(producer % ring_bytes),
                static_cast<DWORD>(bytes), &first, &first_bytes, &second, &second_bytes, 0);
            if (result == DSERR_BUFFERLOST) { restart = true; return true; }
            if (FAILED(result)) return remember("DirectSound playback lock failed", result);
            std::memcpy(first, scratch.data(), first_bytes);
            if (second_bytes) std::memcpy(second, scratch.data() + first_bytes, second_bytes);
            result = output->Unlock(first, first_bytes, second, second_bytes);
            if (FAILED(result)) return remember("DirectSound playback unlock failed", result);
            producer = end;
        }
        // Clear the remaining safe region so underruns cannot replay old samples.
        const DWORD clear_bytes = static_cast<DWORD>(play_position + ring_bytes - producer);
        if (!silence(static_cast<DWORD>(producer % ring_bytes), clear_bytes)) return false;
        if (owner.callback) owner.callback(owner.userdata);
        return true;
    }
    bool start_input()
    {
        HRESULT result = input->Stop();
        if (FAILED(result)) return remember("DirectSound capture stop failed", result);
        result = input->Start(DSCBSTART_LOOPING);
        if (FAILED(result)) return remember("DirectSound capture start failed", result);
        result = input->GetCurrentPosition(nullptr, &previous_cursor);
        if (FAILED(result)) return remember("DirectSound capture cursor failed", result);
        previous_cursor -= previous_cursor % frame_bytes(host);
        last_service = Clock::now();
        restart = false;
        return true;
    }
    bool pump_input()
    {
        if (restart && !start_input()) return false;
        DWORD readable = 0;
        HRESULT result = input->GetCurrentPosition(nullptr, &readable);
        if (FAILED(result)) return remember("DirectSound capture cursor failed", result);
        const DWORD frame = static_cast<DWORD>(frame_bytes(host));
        readable -= readable % frame;
        const auto now = Clock::now();
        if (now - last_service >= std::chrono::milliseconds(200)) {
            previous_cursor = readable;
            source.clear();
            phase = 0;
        }
        last_service = now;
        const DWORD bytes = (readable + ring_bytes - previous_cursor) % ring_bytes;
        if (!bytes) return true;
        scratch.resize(bytes);
        void* first = nullptr;
        void* second = nullptr;
        DWORD first_bytes = 0, second_bytes = 0;
        result = input->Lock(previous_cursor, bytes, &first, &first_bytes,
            &second, &second_bytes, 0);
        if (FAILED(result)) return remember("DirectSound capture lock failed", result);
        std::memcpy(scratch.data(), first, first_bytes);
        if (second_bytes) std::memcpy(scratch.data() + first_bytes, second, second_bytes);
        result = input->Unlock(first, first_bytes, second, second_bytes);
        if (FAILED(result)) return remember("DirectSound capture unlock failed", result);
        previous_cursor = readable;
        source.append(scratch.data(), bytes);
        const size_t guest_frame = frame_bytes(guest);
        const size_t capacity = static_cast<size_t>(
            (std::uint64_t(source.size() / frame) + 1) * guest.freq / host.freq + 2);
        scratch.resize(capacity * guest_frame);
        const size_t produced = convert(source, host, guest, phase, scratch.data(), capacity) * guest_frame;
        if (captured.size() + produced > queue_limit) {
            const size_t excess = captured.size() + produced - queue_limit;
            captured.consume((std::min)(captured.size(),
                (excess + guest_frame - 1) / guest_frame * guest_frame));
        }
        captured.append(scratch.data(), produced);
        if (produced && owner.callback) owner.callback(owner.userdata);
        return true;
    }
    void run()
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
        while (!quitting) {
            try {
                if (device && !failed) {
                    if (capture) pump_input();
                    else pump_output();
                }
            } catch (...) {
                remember("DirectSound audio worker failed", E_OUTOFMEMORY);
            }
            wake.wait_for(lock, std::chrono::milliseconds(2));
        }
    }
};

NativeStream* native(Stream& stream)
{
    auto* result = static_cast<NativeStream*>(stream.native);
    if (!result) fail("Invalid DirectSound audio stream", E_POINTER);
    return result;
}

bool initialize() { return true; }
bool set_sample_frames_hint(const char*) { return true; }
const char* error() { return last_error; }

std::uintptr_t open_input_device()
{
    std::unique_ptr<Device> device(new (std::nothrow) Device);
    if (!device) { fail("DirectSound device allocation failed", E_OUTOFMEMORY); return 0; }
    const HRESULT result = DirectSoundCaptureCreate8(nullptr, &device->input, nullptr);
    if (FAILED(result)) { fail("DirectSound capture device creation failed", result); return 0; }
    return reinterpret_cast<std::uintptr_t>(device.release());
}

std::uintptr_t open_output_device()
{
    std::unique_ptr<Device> device(new (std::nothrow) Device);
    if (!device) { fail("DirectSound device allocation failed", E_OUTOFMEMORY); return 0; }
    HRESULT result = DirectSoundCreate8(nullptr, &device->output, nullptr);
    if (FAILED(result)) { fail("DirectSound playback device creation failed", result); return 0; }
    HWND window = GetConsoleWindow();
    result = device->output->SetCooperativeLevel(window ? window : GetDesktopWindow(), DSSCL_NORMAL);
    if (FAILED(result)) { fail("DirectSound cooperative level failed", result); return 0; }
    return reinterpret_cast<std::uintptr_t>(device.release());
}

bool output_device_format(std::uintptr_t handle, int* frequency, int* frames)
{
    auto* device = reinterpret_cast<Device*>(handle);
    if (!device || !device->output) return fail("Invalid DirectSound playback device", E_POINTER);
    *frequency = host_rate;
    *frames = period_frames;
    return true;
}

void close_device(std::uintptr_t handle) { delete reinterpret_cast<Device*>(handle); }

void create_stream(Stream& stream, const Format* source, const Format* target)
{
    stream.native = nullptr;
    const Format* guest = source ? source : target;
    if (!guest || !valid_format(*guest)) { fail("Invalid DirectSound stream format", E_INVALIDARG); return; }
    try {
        std::unique_ptr<NativeStream> instance(new NativeStream(stream, source == nullptr, *guest));
        instance->worker = std::thread(&NativeStream::run, instance.get());
        stream.native = instance.release();
    } catch (...) {
        fail("DirectSound stream allocation failed", E_OUTOFMEMORY);
    }
}

void destroy_stream(Stream& stream)
{
    auto* instance = static_cast<NativeStream*>(stream.native);
    if (!instance) return;
    {
        std::lock_guard<std::recursive_mutex> lock(instance->mutex);
        instance->quitting = true;
        instance->wake.notify_one();
    }
    instance->worker.join();
    delete instance;
    stream.native = nullptr;
}

bool set_callback(Stream& stream, Callback callback, void* userdata)
{
    auto* instance = native(stream);
    if (!instance) return false;
    std::lock_guard<std::recursive_mutex> lock(instance->mutex);
    if (!instance->check()) return false;
    stream.callback = callback;
    stream.userdata = userdata;
    return true;
}

bool set_format(Stream& stream, const Format* source, const Format* target)
{
    auto* instance = native(stream);
    if (!instance) return false;
    std::lock_guard<std::recursive_mutex> lock(instance->mutex);
    if (!instance->check()) return false;
    const Format* guest = instance->capture ? target : source;
    if (!guest) return true;
    if (!valid_format(*guest)) return fail("Invalid DirectSound stream format", E_INVALIDARG);
    if (same_format(*guest, instance->guest)) return true;
    instance->guest = *guest;
    instance->source.clear();
    instance->captured.clear();
    instance->phase = 0;
    instance->restart = instance->device != 0;
    if (instance->output) instance->output->Stop();
    if (instance->input) instance->input->Stop();
    instance->wake.notify_one();
    return true;
}

bool bind_stream(std::uintptr_t handle, Stream& stream)
{
    auto* instance = native(stream);
    auto* device = reinterpret_cast<Device*>(handle);
    if (!instance) return false;
    if (!device) return fail("Invalid DirectSound audio device", E_POINTER);
    std::lock_guard<std::recursive_mutex> lock(instance->mutex);
    if (!instance->check()) return false;
    if (instance->device == handle) return true;
    if (instance->device) return fail("DirectSound stream already bound", E_INVALIDARG);
    HRESULT result = E_FAIL;
    if (instance->capture) {
        if (!device->input) return fail("Expected a DirectSound capture device", E_INVALIDARG);
        const Format choices[] = {{48000, 2, SampleFormat::s16_le}, {44100, 2, SampleFormat::s16_le},
            {48000, 1, SampleFormat::s16_le}, {44100, 1, SampleFormat::s16_le}};
        for (const auto& format : choices) {
            WAVEFORMATEX wave = wave_format(format);
            DSCBUFFERDESC descriptor = {};
            descriptor.dwSize = sizeof(descriptor);
            descriptor.dwBufferBytes = ring_frames * wave.nBlockAlign;
            descriptor.lpwfxFormat = &wave;
            result = device->input->CreateCaptureBuffer(&descriptor, &instance->input, nullptr);
            if (SUCCEEDED(result)) {
                instance->host = format;
                instance->ring_bytes = descriptor.dwBufferBytes;
                break;
            }
        }
        if (FAILED(result)) return instance->remember("DirectSound capture buffer creation failed", result);
        instance->source.clear();
        instance->phase = 0;
    } else {
        if (!device->output) return fail("Expected a DirectSound playback device", E_INVALIDARG);
        WAVEFORMATEX wave = wave_format(instance->host);
        DSBUFFERDESC descriptor = {};
        descriptor.dwSize = sizeof(descriptor);
        descriptor.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
        descriptor.dwBufferBytes = ring_frames * wave.nBlockAlign;
        descriptor.lpwfxFormat = &wave;
        result = device->output->CreateSoundBuffer(&descriptor, &instance->output, nullptr);
        if (FAILED(result)) return instance->remember("DirectSound playback buffer creation failed", result);
        instance->ring_bytes = descriptor.dwBufferBytes;
    }
    instance->device = handle;
    instance->restart = true;
    instance->wake.notify_one();
    return true;
}

void unbind_stream(Stream& stream)
{
    auto* instance = native(stream);
    if (!instance) return;
    std::lock_guard<std::recursive_mutex> lock(instance->mutex);
    instance->release_buffers();
}

bool lock_stream(Stream& stream)
{
    auto* instance = native(stream);
    if (!instance) return false;
    instance->mutex.lock();
    return true;
}

bool unlock_stream(Stream& stream)
{
    auto* instance = native(stream);
    if (!instance) return false;
    instance->mutex.unlock();
    return true;
}

bool clear_stream(Stream& stream)
{
    auto* instance = native(stream);
    if (!instance) return false;
    std::lock_guard<std::recursive_mutex> lock(instance->mutex);
    if (!instance->check()) return false;
    instance->source.clear();
    instance->captured.clear();
    instance->phase = 0;
    instance->restart = instance->device != 0;
    if (instance->output) instance->output->Stop();
    if (instance->input) instance->input->Stop();
    instance->wake.notify_one();
    return true;
}

int available(Stream& stream)
{
    auto* instance = native(stream);
    if (!instance) return -1;
    std::lock_guard<std::recursive_mutex> lock(instance->mutex);
    if (!instance->check()) return -1;
    return static_cast<int>(instance->captured.size());
}

int queued(Stream& stream)
{
    auto* instance = native(stream);
    if (!instance) return -1;
    std::lock_guard<std::recursive_mutex> lock(instance->mutex);
    if (!instance->check()) return -1;
    return static_cast<int>(instance->source.size());
}

int read(Stream& stream, void* destination, int bytes)
{
    auto* instance = native(stream);
    if (!instance) return -1;
    std::lock_guard<std::recursive_mutex> lock(instance->mutex);
    if (!instance->check()) return -1;
    if (!instance->capture || bytes < 0 || (!destination && bytes)) {
        fail("Invalid DirectSound capture read", E_INVALIDARG);
        return -1;
    }
    const size_t count = (std::min)(static_cast<size_t>(bytes), instance->captured.size());
    if (count) std::memcpy(destination, instance->captured.front(), count);
    instance->captured.consume(count);
    return static_cast<int>(count);
}

bool write(Stream& stream, const void* source, int bytes)
{
    auto* instance = native(stream);
    if (!instance) return false;
    std::lock_guard<std::recursive_mutex> lock(instance->mutex);
    if (!instance->check()) return false;
    if (instance->capture || bytes < 0 || (!source && bytes))
        return fail("Invalid DirectSound playback write", E_INVALIDARG);
    if (static_cast<size_t>(bytes) > queue_limit - instance->source.size())
        return fail("DirectSound playback queue is full", E_OUTOFMEMORY);
    try {
        instance->source.append(source, bytes);
    } catch (...) {
        return fail("DirectSound playback queue allocation failed", E_OUTOFMEMORY);
    }
    instance->wake.notify_one();
    return true;
}
}

const Driver& dsound_driver()
{
    static const Driver driver = {initialize, set_sample_frames_hint,
        open_input_device, open_output_device, output_device_format, close_device, error,
        create_stream, destroy_stream, set_callback, set_callback, set_format, bind_stream,
        unbind_stream, lock_stream, unlock_stream, clear_stream, available, queued, read, write};
    return driver;
}
}
}
#endif // _WIN32
