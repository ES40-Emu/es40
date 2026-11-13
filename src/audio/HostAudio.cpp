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
#include "HostAudioBackend.h"
#include "base/Exception.h"
#include <cstring>
#include <string>

namespace host_audio
{
namespace
{
const detail::Driver* driver(Backend backend)
{
    switch (backend)
    {
#ifdef HAVE_SDL
    case Backend::sdl: return &detail::sdl_driver();
#endif
#ifdef _WIN32
    case Backend::dsound: return &detail::dsound_driver();
#endif
    case Backend::null: return &detail::null_driver();
    default: return nullptr;
    }
}
}

SetupGuard::SetupGuard(Backend backend, std::initializer_list<DeviceId*> devices,
    std::initializer_list<Stream*> streams)
    : backend_(backend), active_(backend != Backend::sdl)
{
    if (active_) {
        devices_.assign(devices);
        streams_.assign(streams);
    }
}

SetupGuard::~SetupGuard()
{
    if (!active_) return;
    for (auto stream : streams_)
        if (*stream) destroy_stream(*stream);
    for (auto device : devices_)
        if (*device) close_device(*device);
}

void SetupGuard::complete()
{
    if (active_)
        for (auto stream : streams_)
            if (!*stream) HOST_AUDIO_FAILURE(backend_, "Failed to initialize audio streams");
    active_ = false;
}

Backend backend_for_gui(const char* gui)
{
    if (gui && std::strcmp(gui, "null") == 0) return Backend::null;
    if (gui && std::strcmp(gui, "win32") == 0) return Backend::dsound;
    if (gui && std::strcmp(gui, "sdl") == 0) return Backend::sdl;
#ifdef HAVE_SDL
    return Backend::sdl;
#elif defined(_WIN32)
    return gui ? Backend::null : Backend::dsound;
#else
    return Backend::null;
#endif
}

bool initialize(Backend backend)
{
    const auto* impl = driver(backend);
    return impl && impl->initialize();
}

bool set_sample_frames_hint(const char* frames, Backend backend)
{
    const auto* impl = driver(backend);
    return impl && impl->set_sample_frames_hint(frames);
}

DeviceId open_input_device(Backend backend)
{
    const auto* impl = driver(backend);
    return { impl ? impl->open_input_device() : 0, backend };
}

DeviceId open_output_device(Backend backend)
{
    const auto* impl = driver(backend);
    return { impl ? impl->open_output_device() : 0, backend };
}

bool output_device_format(DeviceId device, int* frequency, int* sample_frames)
{
    const auto* impl = driver(device.backend);
    return impl && impl->output_device_format(device.native, frequency, sample_frames);
}

void close_device(DeviceId device)
{
    const auto* impl = driver(device.backend);
    if (impl) impl->close_device(device.native);
}

const char* error(Backend backend)
{
    const auto* impl = driver(backend);
    return impl ? impl->error() : "Audio backend is not compiled in";
}

[[noreturn]] void fail(Backend backend, const char* context, const char* file,
    int line, const char* function)
{
    std::string message(context);
    const auto position = message.find("%s");
    if (position != std::string::npos)
        message.replace(position, 2, backend == Backend::sdl ? "SDL" :
            backend == Backend::dsound ? "DirectSound" : "null");
    message += ": ";
    message += error(backend);
    std::string where = std::string(file) + ", line " + std::to_string(line);
    if (function) where += std::string(", function '") + function + "'";
    if (backend == Backend::sdl) throw CSDLException(message, where);
    if (backend == Backend::dsound) throw CWin32Exception(message, where);
    throw CRuntimeException(message, where);
}

void create_stream(Stream& stream, const Format* source, const Format* target,
    Backend backend)
{
    stream.backend = backend;
    const auto* impl = driver(backend);
    if (impl) impl->create_stream(stream, source, target);
    else stream.native = nullptr;
}

void destroy_stream(Stream& stream)
{
    const auto* impl = driver(stream.backend);
    if (impl) impl->destroy_stream(stream);
}

bool set_input_callback(Stream& stream, Callback callback, void* userdata)
{
    const auto* impl = driver(stream.backend);
    return impl && impl->set_input_callback(stream, callback, userdata);
}

bool set_output_callback(Stream& stream, Callback callback, void* userdata)
{
    const auto* impl = driver(stream.backend);
    return impl && impl->set_output_callback(stream, callback, userdata);
}

bool set_format(Stream& stream, const Format* source, const Format* target)
{
    const auto* impl = driver(stream.backend);
    return impl && impl->set_format(stream, source, target);
}

bool bind_stream(DeviceId device, Stream& stream)
{
    if (device.backend != stream.backend) return false;
    const auto* impl = driver(stream.backend);
    return impl && impl->bind_stream(device.native, stream);
}

void unbind_stream(Stream& stream)
{
    const auto* impl = driver(stream.backend);
    if (impl) impl->unbind_stream(stream);
}

bool lock_stream(Stream& stream)
{
    const auto* impl = driver(stream.backend);
    return impl && impl->lock_stream(stream);
}

bool unlock_stream(Stream& stream)
{
    const auto* impl = driver(stream.backend);
    return impl && impl->unlock_stream(stream);
}

bool clear_stream(Stream& stream)
{
    const auto* impl = driver(stream.backend);
    return impl && impl->clear_stream(stream);
}

int available(Stream& stream)
{
    const auto* impl = driver(stream.backend);
    return impl ? impl->available(stream) : -1;
}

int queued(Stream& stream)
{
    const auto* impl = driver(stream.backend);
    return impl ? impl->queued(stream) : -1;
}

int read(Stream& stream, void* data, int bytes)
{
    const auto* impl = driver(stream.backend);
    return impl ? impl->read(stream, data, bytes) : -1;
}

bool write(Stream& stream, const void* data, int bytes)
{
    const auto* impl = driver(stream.backend);
    return impl && impl->write(stream, data, bytes);
}
}
