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
    bool SetVolume(float volume) noexcept;

    bool ReadMonoBlock(std::span<float> output) noexcept;

    [[nodiscard]] ma_engine* GetEngine() noexcept;
    [[nodiscard]] const ma_engine* GetEngine() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] ma_result LastError() const noexcept;

    void Reset() noexcept;

private:
    static constexpr std::size_t SamplesPerStereoBlock =
        FramesPerBlock * ChannelCount;

    ma_engine engine_{};
    std::array<float, SamplesPerStereoBlock> stereoScratch_{};
    std::atomic<ma_result> lastError_{MA_SUCCESS};
    bool initialized_ = false;
};
