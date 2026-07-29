#pragma once

#include "audio/MicrophoneProcessor.hpp"
#include "audio/VoiceEffectsRuntime.hpp"
#include "miniaudio/miniaudio.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>


struct MicrophoneProcessingRuntimeDiagnostics
{
    std::uint64_t processedBlockCount = 0;
    std::uint64_t processingDeadlineMissCount = 0;
    std::uint64_t totalProcessingTimeNanoseconds = 0;
    std::uint64_t maximumProcessingTimeNanoseconds = 0;
    std::uint32_t peakQueuedInputFrames = 0;
};

class MicrophoneProcessingRuntime
{
public:
    using OutputCallback = void (*)(
        void* context,
        const float* interleavedStereoFrames,
        ma_uint32 frameCount
    ) noexcept;

#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    using RenderReferenceCallback = bool (*)(
        void* context,
        float* interleavedStereoFrames,
        ma_uint32 frameCount
    ) noexcept;
#endif

    static constexpr ma_uint32 RequiredSampleRate =
        MicrophoneProcessor::ProcessingSampleRate;
    static constexpr ma_uint32 RequiredInputChannels = 2;
    static constexpr ma_uint32 InputRingBufferFrames = 4800;

    MicrophoneProcessingRuntime();
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
        void* outputContext,
        const VoiceEffectSettings& voiceEffectSettings = {}
#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
        ,
        RenderReferenceCallback renderReferenceCallback = nullptr,
        void* renderReferenceContext = nullptr,
        int streamDelayMilliseconds = 20
#endif
    );

    bool UpdateVoiceEffectSettings(
        const VoiceEffectSettings& settings
    ) noexcept;

    ma_uint32 PushInputFrames(
        const float* interleavedStereoFrames,
        ma_uint32 frameCount
    ) noexcept;

    [[nodiscard]] MicrophoneProcessingSnapshot GetSnapshot() const;
    [[nodiscard]] std::uint64_t GetDroppedInputFrameCount() const noexcept;
    [[nodiscard]] std::uint64_t
        GetRejectedVoiceEffectUpdateCount() const noexcept;
    [[nodiscard]] MicrophoneProcessingRuntimeDiagnostics
        GetDiagnostics() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    void Shutdown();

private:
    struct AecDiagnosticRecorder;

    void WorkerMain();
    void RecordProcessingDuration(
        std::uint64_t elapsedNanoseconds
    ) noexcept;
    void UpdatePeakQueuedInputFrames() noexcept;
    void ProcessAndDispatchBlock(
        const std::array<float,
            MicrophoneProcessor::SamplesPerBlock *
                RequiredInputChannels>& stereoInput
    );

    MicrophoneProcessor processor_;
    VoiceEffectsRuntime voiceEffectsRuntime_;
    MicrophoneProcessingSettings settings_{};

    ma_pcm_rb inputRingBuffer_{};
    bool inputRingBufferInitialized_ = false;

    OutputCallback outputCallback_ = nullptr;
    void* outputContext_ = nullptr;

#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    std::unique_ptr<AecDiagnosticRecorder> aecDiagnosticRecorder_;
    RenderReferenceCallback renderReferenceCallback_ = nullptr;
    void* renderReferenceContext_ = nullptr;
    int streamDelayMilliseconds_ = 20;
#endif

    std::thread workerThread_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool acceptingInput_{false};
    std::atomic_uint32_t activePushCount_{0};
    std::atomic<std::uint64_t> droppedInputFrames_{0};
    std::atomic<std::uint64_t> echoCancellationReferenceUnderruns_{0};
    std::atomic<std::uint64_t> processedBlockCount_{0};
    std::atomic<std::uint64_t> processingDeadlineMissCount_{0};
    std::atomic<std::uint64_t> totalProcessingTimeNanoseconds_{0};
    std::atomic<std::uint64_t> maximumProcessingTimeNanoseconds_{0};
    std::atomic<std::uint32_t> peakQueuedInputFrames_{0};
    bool initialized_ = false;
};
