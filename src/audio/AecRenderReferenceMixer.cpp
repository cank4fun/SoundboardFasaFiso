#include "audio/AecRenderReferenceMixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

AecRenderReferenceMixer::~AecRenderReferenceMixer()
{
    Reset();
}

bool AecRenderReferenceMixer::Initialize(const float volume)
{
    Reset();

    if (!std::isfinite(volume) || volume < 0.0f || volume > 1.0f)
    {
        lastError_.store(MA_INVALID_ARGS, std::memory_order_relaxed);
        return false;
    }

    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE;
    config.channels = ChannelCount;
    config.sampleRate = SampleRate;
    config.periodSizeInFrames = static_cast<ma_uint32>(FramesPerBlock);

    ma_result result = ma_engine_init(&config, &engine_);

    if (result != MA_SUCCESS)
    {
        lastError_.store(result, std::memory_order_relaxed);
        std::memset(&engine_, 0, sizeof(engine_));
        return false;
    }

    initialized_ = true;

    result = ma_engine_set_volume(&engine_, volume);

    if (result != MA_SUCCESS)
    {
        lastError_.store(result, std::memory_order_relaxed);
        Reset();
        lastError_.store(result, std::memory_order_relaxed);
        return false;
    }

    lastError_.store(MA_SUCCESS, std::memory_order_relaxed);
    return true;
}

bool AecRenderReferenceMixer::SetVolume(const float volume) noexcept
{
    if (!initialized_ || !std::isfinite(volume) ||
        volume < 0.0f || volume > 1.0f)
    {
        lastError_.store(MA_INVALID_ARGS, std::memory_order_relaxed);
        return false;
    }

    const ma_result result = ma_engine_set_volume(&engine_, volume);
    lastError_.store(result, std::memory_order_relaxed);
    return result == MA_SUCCESS;
}

bool AecRenderReferenceMixer::ReadMonoBlock(
    const std::span<float> output
) noexcept
{
    std::fill(output.begin(), output.end(), 0.0f);

    if (!initialized_ || output.size() != FramesPerBlock)
    {
        lastError_.store(MA_INVALID_ARGS, std::memory_order_relaxed);
        return false;
    }

    ma_uint64 framesRead = 0;
    const ma_result result = ma_engine_read_pcm_frames(
        &engine_,
        stereoScratch_.data(),
        FramesPerBlock,
        &framesRead
    );

    if (result != MA_SUCCESS)
    {
        stereoScratch_.fill(0.0f);
        lastError_.store(result, std::memory_order_relaxed);
        return false;
    }

    const std::size_t validFrames = std::min<std::size_t>(
        static_cast<std::size_t>(framesRead),
        FramesPerBlock
    );

    if (validFrames < FramesPerBlock)
    {
        std::fill(
            stereoScratch_.begin() +
                static_cast<std::ptrdiff_t>(validFrames * ChannelCount),
            stereoScratch_.end(),
            0.0f
        );
    }

    for (std::size_t frame = 0; frame < FramesPerBlock; ++frame)
    {
        const std::size_t sampleIndex = frame * ChannelCount;
        const float left = stereoScratch_[sampleIndex];
        const float right = stereoScratch_[sampleIndex + 1];
        const float mono = (left + right) * 0.5f;
        output[frame] = std::isfinite(mono) ? mono : 0.0f;
    }

    lastError_.store(MA_SUCCESS, std::memory_order_relaxed);
    return true;
}

ma_engine* AecRenderReferenceMixer::GetEngine() noexcept
{
    return initialized_ ? &engine_ : nullptr;
}

const ma_engine* AecRenderReferenceMixer::GetEngine() const noexcept
{
    return initialized_ ? &engine_ : nullptr;
}

bool AecRenderReferenceMixer::IsInitialized() const noexcept
{
    return initialized_;
}

ma_result AecRenderReferenceMixer::LastError() const noexcept
{
    return lastError_.load(std::memory_order_relaxed);
}

void AecRenderReferenceMixer::Reset() noexcept
{
    if (initialized_)
    {
        ma_engine_uninit(&engine_);
    }

    initialized_ = false;
    stereoScratch_.fill(0.0f);
    std::memset(&engine_, 0, sizeof(engine_));
    lastError_.store(MA_SUCCESS, std::memory_order_relaxed);
}
