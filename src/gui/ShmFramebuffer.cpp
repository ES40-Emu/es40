/* ES40 emulator.
 * Copyright (C) 2026 ES40 Emulator contributors
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
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
#include "ShmFramebuffer.h"

#include <cstring>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__VMS)
#define ES40_SHM_POSIX
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr std::uint32_t magic = 0x31424649; // IFB1, little endian
constexpr std::size_t header_bytes = 64;
constexpr unsigned max_dimension = 4096;
// Fixed capacity preserves reader mappings across mode changes.
constexpr std::size_t capacity = header_bytes + std::size_t(max_dimension) * max_dimension * 4;

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error("IFB1 framebuffer: " + message);
}

[[noreturn]] void system_fail(const char* operation)
{
#if defined(_WIN32)
    fail(std::string(operation) + " (Windows error " + std::to_string(GetLastError()) + ")");
#elif defined(ES40_SHM_POSIX)
    fail(std::string(operation) + ": " + std::strerror(errno));
#else
    fail(operation);
#endif
}

void store_sequence(std::uint64_t* address, std::uint64_t value)
{
#if defined(_WIN32)
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(address), static_cast<LONG64>(value));
#elif defined(ES40_SHM_POSIX)
    static_assert(__atomic_always_lock_free(8, nullptr), "IFB1 requires lock-free 64-bit atomics");
    // Order sequence updates around pixel writes.
    __atomic_exchange_n(address, value, __ATOMIC_SEQ_CST);
#endif
}
}

struct CShmFramebuffer::Impl
{
    unsigned char* view = nullptr;
    std::uint64_t sequence = 0;
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
#elif defined(ES40_SHM_POSIX)
    int file = -1;
#endif

    ~Impl()
    {
#if defined(_WIN32)
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
#elif defined(ES40_SHM_POSIX)
        if (view) munmap(view, capacity);
        if (file >= 0) close(file);
#endif
    }

    void open(const char* path)
    {
        const std::uint32_t endian = 1;
        if (*reinterpret_cast<const unsigned char*>(&endian) != 1)
            fail("export requires a little-endian host");
        if (!path || !*path) fail("empty path");
        std::uint64_t size = 0;
#if defined(_WIN32)
        // Allow readers while excluding other writers.
        file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) system_fail("open (path must be writable and have no other writer)");
        LARGE_INTEGER length;
        if (!GetFileSizeEx(file, &length)) system_fail("get size");
        size = static_cast<std::uint64_t>(length.QuadPart);
#elif defined(ES40_SHM_POSIX)
        file = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (file < 0) system_fail("open");
        if (flock(file, LOCK_EX | LOCK_NB) != 0) system_fail("lock (another writer may be using this path)");
        struct stat info;
        if (fstat(file, &info) != 0) system_fail("stat");
        if (!S_ISREG(info.st_mode)) fail("path must name a regular file");
        size = static_cast<std::uint64_t>(info.st_size);
#else
        fail("export is not supported on this host");
#endif
        if (size != 0 && size < capacity)
            fail("existing file is too small; use a new path or remove it after stopping its readers");
        if (size == 0)
        {
#if defined(_WIN32)
            LARGE_INTEGER length;
            length.QuadPart = capacity;
            if (!SetFilePointerEx(file, length, nullptr, FILE_BEGIN) || !SetEndOfFile(file))
                system_fail("allocate file");
#elif defined(ES40_SHM_POSIX)
            if (ftruncate(file, static_cast<off_t>(capacity)) != 0) system_fail("allocate file");
#endif
        }
#if defined(_WIN32)
        mapping = CreateFileMappingA(file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
        if (!mapping) system_fail("create mapping");
        view = static_cast<unsigned char*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, capacity));
        if (!view) system_fail("map file");
#elif defined(ES40_SHM_POSIX)
        void* mapped = mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
        if (mapped == MAP_FAILED) system_fail("map file");
        view = static_cast<unsigned char*>(mapped);
#endif
        auto* header = reinterpret_cast<std::uint32_t*>(view);
        if (size != 0 && (header[0] != magic || header[1] != 1 || header[5] != 32))
            fail("existing file is not an IFB1 v1 framebuffer");
        // Preserve the sequence across producer restarts.
        std::memcpy(&sequence, view + 24, sizeof(sequence));
        sequence &= ~std::uint64_t(1);
        store_sequence(reinterpret_cast<std::uint64_t*>(view + 24), sequence + 1);
        header[1] = 1;
        header[5] = 32;
        // Publish header constants before magic for existing IFB1 readers.
        std::memset(view + 48, 0, 16);
#if defined(_WIN32)
        InterlockedExchange(reinterpret_cast<volatile LONG*>(view), magic);
#elif defined(ES40_SHM_POSIX)
        __atomic_store_n(header, magic, __ATOMIC_RELEASE);
#endif
    }
};

CShmFramebuffer::CShmFramebuffer(const char* path) : m_impl(new Impl)
{
    m_impl->open(path);
}

CShmFramebuffer::~CShmFramebuffer() = default;

void CShmFramebuffer::publish(const std::uint32_t* pixels, unsigned width, unsigned height)
{
    if (!width || !height || width > max_dimension || height > max_dimension)
        fail("frame dimensions must be in 1..4096");
    auto* view = m_impl->view;
    auto* sequence = reinterpret_cast<std::uint64_t*>(view + 24);
    store_sequence(sequence, m_impl->sequence + 1);
    auto* header = reinterpret_cast<std::uint32_t*>(view);
    header[2] = width;
    header[3] = height;
    header[4] = width * 4;
    header[8] = header[9] = 0;
    header[10] = width;
    header[11] = height;
    const std::size_t bytes = std::size_t(width) * height * 4;
    if (pixels) std::memcpy(view + header_bytes, pixels, bytes);
    else std::memset(view + header_bytes, 0, bytes);
    m_impl->sequence += 2;
    store_sequence(sequence, m_impl->sequence);
}
