/* ISA Windows Sound System, adapted from 86Box src/sound/snd_wss.c.
 * Copyright 2012-2018 Sarah Walker, 2018 TheCollector1995.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ES40_WSS_H
#define ES40_WSS_H

#include "StdAfx.h"
#include "emu/SystemComponent.h"
#include "base/Thread.h"
#include "HostAudio.h"
#include "WSSCodec.h"
#include <atomic>
#include <chrono>

class CWSS : public CSystemComponent, public CRunnable
{
public:
    CWSS(CConfigurator* cfg, CSystem* system);
    ~CWSS() override;
    void init() override;
    void ResetPCI() override;
    void start_threads() override;
    void stop_threads() override;
    void check_state() override;
    void run() override;
    void bind_isa_bridge(CAliM1543C* owner) override { bridge = owner; }
    void bind_isa_dma(CDMA* controller) override { dma = controller; }
    const CSystemComponent* memory_decode_owner() const noexcept override;
    bool decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept override;
    bool uses_subtractive_decode(int index, u64 address, int dsize, bool write) const noexcept override;
    u64 ReadMem(int index, u64 address, int dsize) override;
    void WriteMem(int index, u64 address, int dsize, u64 data) override;
    int SaveState(FILE* file) override;
    int RestoreState(FILE* file) override;

private:
    struct {
        WSSCodec codec;
        u8 config, frame[4], position;
        u8 drq, irq;
    } state{};

    CAliM1543C* bridge = nullptr;
    CDMA* dma = nullptr;
    host_audio::Backend backend{};
    host_audio::DeviceId output{}, input{};
    host_audio::Stream playback{}, capture{};
    bool capture_requested = false;
    unsigned host_rate = 0;
    CThread worker{"wss"};
    std::atomic<bool> running{false}, failed{false};
    std::chrono::steady_clock::time_point last_tick{};
    s64 frame_credit = 0;

    void set_drq(bool asserted);
    void set_irq(bool asserted);
    void update_streams();
    void close_capture();
    void service_audio(std::chrono::steady_clock::time_point now);
};

#endif
