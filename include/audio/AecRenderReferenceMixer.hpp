#pragma once

#include "miniaudio/miniaudio.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <span>

class AecRenderReferenceMixer
{
public:
    static constexpr ma_uint32 SampleRate = 48'000;
    static constexpr ma_uint32 ChannelCount = 2;
    static constexpr std::size_t FramesPerBlock = 480;

    enum class LoopbackMode
    {
        None,
        Endpoint,
        ProcessExcluded
    };

    AecRenderReferenceMixer() = default;
    ~AecRenderReferenceMixer();

    AecRenderReferenceMixer(const AecRenderReferenceMixer&) = delete;
    AecRenderReferenceMixer& operator=(
        const AecRenderReferenceMixer&
    ) = delete;

    AecRenderReferenceMixer(AecRenderReferenceMixer&&) = delete;
    AecRenderReferenceMixer& operator=(
        AecRenderReferenceMixer&&
    ) = delete;

    bool Initialize(float volume = 1.0f);
    bool InitializeLoopback(
        ma_context& context,
        const ma_device_id* playbackDeviceId,
        bool excludeCurrentProcess,
        ma_uint32 currentProcessId
    ) noexcept;
    bool SetVolume(float volume) noexcept;

    bool ReadMonoBlock(
        std::span<float> output,
        bool allowEndpointLoopback = true
    ) noexcept;

    [[nodiscard]] ma_engine* GetEngine() noexcept;
    [[nodiscard]] const ma_engine* GetEngine() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsLoopbackInitialized() const noexcept;
    [[nodiscard]] bool LoopbackIncludesCurrentProcess() const noexcept;
    [[nodiscard]] LoopbackMode GetLoopbackMode() const noexcept;
    [[nodiscard]] ma_result LastError() const noexcept;

    void Reset() noexcept;

private:
    static constexpr std::size_t SamplesPerStereoBlock =
        FramesPerBlock * ChannelCount;
    static constexpr ma_uint32 LoopbackRingBufferFrames =
        SampleRate / 5;
    static constexpr ma_uint32 MaximumQueuedLoopbackFrames =
        static_cast<ma_uint32>(FramesPerBlock * 2);

    static void LoopbackDataCallback(
        ma_device* device,
        void* outputFrames,
        const void* inputFrames,
        ma_uint32 frameCount
    );

    bool StartLoopbackDevice(
        ma_context& context,
        const ma_device_id* playbackDeviceId,
        ma_uint32 excludedProcessId,
        LoopbackMode mode
    ) noexcept;
    ma_uint32 PushLoopbackFrames(
        const float* interleavedStereoFrames,
        ma_uint32 frameCount
    ) noexcept;
    bool DiscardLoopbackFrames(ma_uint32 frameCount) noexcept;
    bool ReadInternalMonoBlock(std::span<float> output) noexcept;
    bool ReadLoopbackMonoBlock(std::span<float> output) noexcept;
    void ResetLoopback() noexcept;

    ma_engine engine_{};
    ma_device loopbackDevice_{};
    ma_pcm_rb loopbackRingBuffer_{};

    std::array<float, SamplesPerStereoBlock> stereoScratch_{};
    std::array<float, SamplesPerStereoBlock> loopbackStereoScratch_{};
    std::array<float, FramesPerBlock> internalMonoScratch_{};
    std::array<float, FramesPerBlock> loopbackMonoScratch_{};

    std::atomic<ma_result> lastError_{MA_SUCCESS};
    LoopbackMode loopbackMode_ = LoopbackMode::None;
    ma_uint32 loopbackUnderflowStreak_ = 0;
    bool initialized_ = false;
    bool loopbackDeviceInitialized_ = false;
    bool loopbackRingBufferInitialized_ = false;
};
