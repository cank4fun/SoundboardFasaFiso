#pragma once

#include "audio/MicrophoneProcessor.hpp"
#include "miniaudio/miniaudio.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

class MicrophoneProcessingRuntime
{
public:
    using OutputCallback = void (*)(
        void* context,
        const float* interleavedStereoFrames,
        ma_uint32 frameCount
    ) noexcept;

    static constexpr ma_uint32 RequiredSampleRate =
        MicrophoneProcessor::ProcessingSampleRate;
    static constexpr ma_uint32 RequiredInputChannels = 2;
    static constexpr ma_uint32 InputRingBufferFrames = 4800;

    MicrophoneProcessingRuntime() = default;
    ~MicrophoneProcessingRuntime();

    MicrophoneProcessingRuntime(
        const MicrophoneProcessingRuntime&
    ) = delete;
    MicrophoneProcessingRuntime& operator=(
        const MicrophoneProcessingRuntime&
    ) = delete;

    MicrophoneProcessingRuntime(
        MicrophoneProcessingRuntime&&
    ) = delete;
    MicrophoneProcessingRuntime& operator=(
        MicrophoneProcessingRuntime&&
    ) = delete;

    bool Initialize(
        ma_uint32 inputSampleRate,
        ma_uint32 inputChannels,
        const MicrophoneProcessingSettings& settings,
        OutputCallback outputCallback,
        void* outputContext
    );

    ma_uint32 PushInputFrames(
        const float* interleavedStereoFrames,
        ma_uint32 frameCount
    ) noexcept;

    [[nodiscard]] MicrophoneProcessingSnapshot GetSnapshot() const;
    [[nodiscard]] std::uint64_t GetDroppedInputFrameCount() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    void Shutdown();

private:
    void WorkerMain();
    void ProcessAndDispatchBlock(
        const std::array<float,
            MicrophoneProcessor::SamplesPerBlock *
                RequiredInputChannels>& stereoInput
    );

    MicrophoneProcessor processor_;
    MicrophoneProcessingSettings settings_{};

    ma_pcm_rb inputRingBuffer_{};
    bool inputRingBufferInitialized_ = false;

    OutputCallback outputCallback_ = nullptr;
    void* outputContext_ = nullptr;

    std::thread workerThread_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool acceptingInput_{false};
    std::atomic_uint32_t activePushCount_{0};
    std::atomic<std::uint64_t> droppedInputFrames_{0};
    bool initialized_ = false;
};
