/* ES40 emulator.
 * Copyright (C) 2026 by the ES40 Emulator Project
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

#ifndef ES40_HOST_WAKE_EVENT_H
#define ES40_HOST_WAKE_EVENT_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#if defined(_WIN32) && !defined(ES40_HOST_WAKE_FORCE_PORTABLE)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

// Host scheduling only: this class knows nothing about a CPU model, interrupt
// masks, or guest clock rates. 
// There may be many notifiers but only one waiter.
// Capture generation() BEFORE checking the work that would prevent sleeping;
// then pass that generation to wait_until(). 
// A notification in between those operations is retained. 
// Notifications do not replace synchronization of the caller's work flags. 
// Stop/join the waiter before destroying this object.
class CHostWakeEvent
{
public:
    using Clock = std::chrono::steady_clock;
    using Generation = std::uint64_t;

    CHostWakeEvent()
    {
#if defined(_WIN32) && !defined(ES40_HOST_WAKE_FORCE_PORTABLE)
        // Resolve at runtime so older Windows versions can use the portable
        // path. The high-resolution flag requires Windows 10 version 1803.
        using CreateTimer = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR,
                                           DWORD, DWORD);
        const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        const auto create_timer = kernel ? reinterpret_cast<CreateTimer>(
            GetProcAddress(kernel, "CreateWaitableTimerExW")) : nullptr;
        if (create_timer)
        {
            constexpr DWORD high_resolution_flag = 0x00000002;
            m_timer = create_timer(nullptr, nullptr, high_resolution_flag,
                                   TIMER_MODIFY_STATE | SYNCHRONIZE);
            if (m_timer)
                m_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            m_high_resolution = m_timer && m_event;
        }
#endif
    }

    ~CHostWakeEvent()
    {
#if defined(_WIN32) && !defined(ES40_HOST_WAKE_FORCE_PORTABLE)
        if (m_timer)
        {
            CancelWaitableTimer(m_timer);
            CloseHandle(m_timer);
        }
        if (m_event)
            CloseHandle(m_event);
#endif
    }

    CHostWakeEvent(const CHostWakeEvent&) = delete;
    CHostWakeEvent& operator=(const CHostWakeEvent&) = delete;

    Generation generation() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_generation;
    }

    void notify()
    {
        bool notify_portable = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_generation;
            if (m_waiting)
            {
#if defined(_WIN32) && !defined(ES40_HOST_WAKE_FORCE_PORTABLE)
                if (m_high_resolution)
                    SetEvent(m_event);
                else
#endif
                    notify_portable = true;
            }
        }
        if (notify_portable)
            m_changed.notify_one();
    }

    // Returns true for a notification since observed, false at the deadline.
    bool wait_until(Generation observed, Clock::time_point deadline,
                    std::chrono::microseconds fallback_poll)
    {
        if (fallback_poll <= std::chrono::microseconds::zero())
            fallback_poll = std::chrono::microseconds(100);

        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_generation == observed)
        {
            const auto now = Clock::now();
            if (now >= deadline)
                return false;

#if defined(_WIN32) && !defined(ES40_HOST_WAKE_FORCE_PORTABLE)
            if (m_high_resolution)
            {
                // Round UP to 100 ns units. 
                using TimerTicks = std::chrono::duration<LONGLONG,
                                                        std::ratio<1, 10000000>>;
                const auto remaining = deadline - now;
                auto ticks = std::chrono::duration_cast<TimerTicks>(remaining);
                if (ticks < remaining)
                    ++ticks;
                LARGE_INTEGER due;
                due.QuadPart = -ticks.count();

                // Reset and register while holding the generation mutex. 
                if (!ResetEvent(m_event) ||
                    !SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE))
                {
                    m_high_resolution = false;
                    CancelWaitableTimer(m_timer);
                    continue;
                }
                const HANDLE handles[] = { m_event, m_timer };
                m_waiting = true;
                lock.unlock();
                const DWORD result = WaitForMultipleObjects(2, handles, FALSE,
                                                            INFINITE);
                lock.lock();
                m_waiting = false;
                CancelWaitableTimer(m_timer);
                if (result != WAIT_OBJECT_0 && result != WAIT_OBJECT_0 + 1)
                    m_high_resolution = false;
                continue;
            }
#endif

            const auto remaining = deadline - now;
            const auto slice_end = remaining > fallback_poll
                ? now + fallback_poll : deadline;
            m_waiting = true;
            m_changed.wait_until(lock, slice_end);
            m_waiting = false;
        }
        return true;
    }

    bool high_resolution() const
    {
#if defined(_WIN32) && !defined(ES40_HOST_WAKE_FORCE_PORTABLE)
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_high_resolution;
#else
        return false;
#endif
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    Generation m_generation = 0;
    bool m_waiting = false;
#if defined(_WIN32) && !defined(ES40_HOST_WAKE_FORCE_PORTABLE)
    HANDLE m_event = nullptr;
    HANDLE m_timer = nullptr;
    bool m_high_resolution = false;
#endif
};

#endif
