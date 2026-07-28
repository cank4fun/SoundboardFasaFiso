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

bool AecRenderReferenceMixer::InitializeLoopback(
    ma_context& context,
    const ma_device_id* const playbackDeviceId,
    const bool excludeCurrentProcess,
    const ma_uint32 currentProcessId
) noexcept
{
    ResetLoopback();

    if (context.backend != ma_backend_wasapi ||
        ma_context_is_loopback_supported(&context) == MA_FALSE ||
        (excludeCurrentProcess && currentProcessId == 0))
    {
        lastError_.store(MA_NOT_IMPLEMENTED, std::memory_order_relaxed);
        return false;
    }

    const ma_result ringResult = ma_pcm_rb_init(
        ma_format_f32,
        ChannelCount,
        LoopbackRingBufferFrames,
        nullptr,
        nullptr,
        &loopbackRingBuffer_
    );

    if (ringResult != MA_SUCCESS)
    {
        lastError_.store(ringResult, std::memory_order_relaxed);
        return false;
    }

    loopbackRingBufferInitialized_ = true;
    ma_pcm_rb_set_sample_rate(&loopbackRingBuffer_, SampleRate);

    if (excludeCurrentProcess)
    {
        if (StartLoopbackDevice(
                context,
                nullptr,
                currentProcessId,
                LoopbackMode::ProcessExcluded
            ))
        {
            return true;
        }
    }
    else
    {
        if (StartLoopbackDevice(
                context,
                playbackDeviceId,
                0,
                LoopbackMode::Endpoint
            ))
        {
            return true;
        }

        if (currentProcessId != 0 &&
            StartLoopbackDevice(
                context,
                nullptr,
                currentProcessId,
                LoopbackMode::ProcessExcluded
            ))
        {
            return true;
        }
    }

    const ma_result failure = lastError_.load(
        std::memory_order_relaxed
    );
    ResetLoopback();
    lastError_.store(failure, std::memory_order_relaxed);
    return false;
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

bool AecRenderReferenceMixer::ReadStereoBlock(
    const std::span<float> interleavedStereoOutput,
    const bool allowEndpointLoopback
) noexcept
{
    std::fill(
        interleavedStereoOutput.begin(),
        interleavedStereoOutput.end(),
        0.0f
    );

    if (interleavedStereoOutput.size() != SamplesPerStereoBlock)
    {
        lastError_.store(MA_INVALID_ARGS, std::memory_order_relaxed);
        return false;
    }

    const bool internalAvailable =
        ReadInternalStereoBlock(internalStereoScratch_);
    const bool endpointLoopbackAllowed =
        loopbackMode_ != LoopbackMode::Endpoint ||
        allowEndpointLoopback;
    const bool loopbackBlockAvailable =
        loopbackDeviceInitialized_ && endpointLoopbackAllowed &&
        ReadLoopbackStereoBlock(loopbackStereoScratch_);

    if (loopbackMode_ == LoopbackMode::Endpoint &&
        endpointLoopbackAllowed)
    {
        if (!loopbackBlockAvailable)
        {
            return false;
        }

        std::copy(
            loopbackStereoScratch_.begin(),
            loopbackStereoScratch_.end(),
            interleavedStereoOutput.begin()
        );
        lastError_.store(MA_SUCCESS, std::memory_order_relaxed);
        return true;
    }

    if (loopbackMode_ == LoopbackMode::ProcessExcluded &&
        loopbackDeviceInitialized_ && !loopbackBlockAvailable)
    {
        return false;
    }

    if (internalAvailable)
    {
        std::copy(
            internalStereoScratch_.begin(),
            internalStereoScratch_.end(),
            interleavedStereoOutput.begin()
        );
    }

    if (loopbackMode_ == LoopbackMode::ProcessExcluded &&
        loopbackBlockAvailable)
    {
        for (std::size_t sample = 0;
            sample < SamplesPerStereoBlock;
            ++sample)
        {
            interleavedStereoOutput[sample] = std::clamp(
                interleavedStereoOutput[sample] +
                    loopbackStereoScratch_[sample],
                -1.0f,
                1.0f
            );
        }
    }

    const bool referenceAvailable = internalAvailable ||
        (loopbackMode_ == LoopbackMode::ProcessExcluded &&
            loopbackBlockAvailable);

    if (referenceAvailable)
    {
        lastError_.store(MA_SUCCESS, std::memory_order_relaxed);
    }

    return referenceAvailable;
}

bool AecRenderReferenceMixer::ReadMonoBlock(
    const std::span<float> output,
    const bool allowEndpointLoopback
) noexcept
{
    std::fill(output.begin(), output.end(), 0.0f);

    if (output.size() != FramesPerBlock)
    {
        lastError_.store(MA_INVALID_ARGS, std::memory_order_relaxed);
        return false;
    }

    if (!ReadStereoBlock(
            mixedStereoScratch_,
            allowEndpointLoopback
        ))
    {
        return false;
    }

    for (std::size_t frame = 0; frame < FramesPerBlock; ++frame)
    {
        const std::size_t sampleIndex = frame * ChannelCount;
        output[frame] = (
            mixedStereoScratch_[sampleIndex] +
            mixedStereoScratch_[sampleIndex + 1]
        ) * 0.5f;
    }

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

bool AecRenderReferenceMixer::IsLoopbackInitialized() const noexcept
{
    return loopbackDeviceInitialized_;
}

bool AecRenderReferenceMixer::LoopbackIncludesCurrentProcess() const noexcept
{
    return loopbackMode_ == LoopbackMode::Endpoint;
}

AecRenderReferenceMixer::LoopbackMode
AecRenderReferenceMixer::GetLoopbackMode() const noexcept
{
    return loopbackMode_;
}

ma_result AecRenderReferenceMixer::LastError() const noexcept
{
    return lastError_.load(std::memory_order_relaxed);
}

void AecRenderReferenceMixer::Reset() noexcept
{
    ResetLoopback();

    if (initialized_)
    {
        ma_engine_uninit(&engine_);
    }

    initialized_ = false;
    mixedStereoScratch_.fill(0.0f);
    internalStereoScratch_.fill(0.0f);
    std::memset(&engine_, 0, sizeof(engine_));
    lastError_.store(MA_SUCCESS, std::memory_order_relaxed);
}

void AecRenderReferenceMixer::LoopbackDataCallback(
    ma_device* const device,
    void* const outputFrames,
    const void* const inputFrames,
    const ma_uint32 frameCount
)
{
    static_cast<void>(outputFrames);

    if (device == nullptr || inputFrames == nullptr || frameCount == 0)
    {
        return;
    }

    auto* const instance = static_cast<AecRenderReferenceMixer*>(
        device->pUserData
    );

    if (instance == nullptr)
    {
        return;
    }

    static_cast<void>(instance->PushLoopbackFrames(
        static_cast<const float*>(inputFrames),
        frameCount
    ));
}

bool AecRenderReferenceMixer::StartLoopbackDevice(
    ma_context& context,
    const ma_device_id* const playbackDeviceId,
    const ma_uint32 excludedProcessId,
    const LoopbackMode mode
) noexcept
{
    std::memset(&loopbackDevice_, 0, sizeof(loopbackDevice_));

    ma_device_config config =
        ma_device_config_init(ma_device_type_loopback);
    config.capture.format = ma_format_f32;
    config.capture.channels = ChannelCount;
    config.capture.pDeviceID = playbackDeviceId;
    config.sampleRate = SampleRate;
    config.periodSizeInMilliseconds = 10;
    config.dataCallback = &AecRenderReferenceMixer::LoopbackDataCallback;
    config.pUserData = this;

    if (excludedProcessId != 0)
    {
        config.wasapi.loopbackProcessID = excludedProcessId;
        config.wasapi.loopbackProcessExclude =
            static_cast<ma_bool8>(MA_TRUE);
    }

    ma_result result = ma_device_init(
        &context,
        &config,
        &loopbackDevice_
    );

    if (result != MA_SUCCESS)
    {
        lastError_.store(result, std::memory_order_relaxed);
        std::memset(&loopbackDevice_, 0, sizeof(loopbackDevice_));
        return false;
    }

    loopbackDeviceInitialized_ = true;
    result = ma_device_start(&loopbackDevice_);

    if (result != MA_SUCCESS)
    {
        ma_device_uninit(&loopbackDevice_);
        loopbackDeviceInitialized_ = false;
        lastError_.store(result, std::memory_order_relaxed);
        std::memset(&loopbackDevice_, 0, sizeof(loopbackDevice_));
        return false;
    }

    loopbackMode_ = mode;
    lastError_.store(MA_SUCCESS, std::memory_order_relaxed);
    return true;
}

ma_uint32 AecRenderReferenceMixer::PushLoopbackFrames(
    const float* const interleavedStereoFrames,
    const ma_uint32 frameCount
) noexcept
{
    if (!loopbackRingBufferInitialized_ ||
        interleavedStereoFrames == nullptr || frameCount == 0)
    {
        return 0;
    }

    ma_uint32 writtenFrames = 0;

    while (writtenFrames < frameCount)
    {
        ma_uint32 writableFrames = frameCount - writtenFrames;
        void* destination = nullptr;

        const ma_result acquireResult = ma_pcm_rb_acquire_write(
            &loopbackRingBuffer_,
            &writableFrames,
            &destination
        );

        if (acquireResult != MA_SUCCESS || writableFrames == 0 ||
            destination == nullptr)
        {
            break;
        }

        std::memcpy(
            destination,
            interleavedStereoFrames +
                static_cast<std::size_t>(writtenFrames) * ChannelCount,
            static_cast<std::size_t>(writableFrames) *
                ChannelCount * sizeof(float)
        );

        if (ma_pcm_rb_commit_write(
                &loopbackRingBuffer_,
                writableFrames
            ) != MA_SUCCESS)
        {
            break;
        }

        writtenFrames += writableFrames;
    }

    return writtenFrames;
}

bool AecRenderReferenceMixer::DiscardLoopbackFrames(
    ma_uint32 frameCount
) noexcept
{
    while (frameCount != 0)
    {
        ma_uint32 readableFrames = frameCount;
        void* source = nullptr;

        const ma_result acquireResult = ma_pcm_rb_acquire_read(
            &loopbackRingBuffer_,
            &readableFrames,
            &source
        );

        if (acquireResult != MA_SUCCESS || readableFrames == 0 ||
            source == nullptr)
        {
            return false;
        }

        if (ma_pcm_rb_commit_read(
                &loopbackRingBuffer_,
                readableFrames
            ) != MA_SUCCESS)
        {
            return false;
        }

        frameCount -= readableFrames;
    }

    return true;
}

bool AecRenderReferenceMixer::ReadInternalStereoBlock(
    const std::span<float> output
) noexcept
{
    std::fill(output.begin(), output.end(), 0.0f);

    if (!initialized_ || output.size() != SamplesPerStereoBlock)
    {
        return false;
    }

    ma_uint64 framesRead = 0;
    const ma_result result = ma_engine_read_pcm_frames(
        &engine_,
        output.data(),
        FramesPerBlock,
        &framesRead
    );

    if (result != MA_SUCCESS)
    {
        std::fill(output.begin(), output.end(), 0.0f);
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
            output.begin() + static_cast<std::ptrdiff_t>(
                validFrames * ChannelCount
            ),
            output.end(),
            0.0f
        );
    }

    for (float& sample : output)
    {
        if (!std::isfinite(sample))
        {
            sample = 0.0f;
        }
    }

    return true;
}

bool AecRenderReferenceMixer::ReadLoopbackStereoBlock(
    const std::span<float> output
) noexcept
{
    std::fill(output.begin(), output.end(), 0.0f);

    if (!loopbackDeviceInitialized_ ||
        !loopbackRingBufferInitialized_ ||
        output.size() != SamplesPerStereoBlock)
    {
        return false;
    }

    ma_uint32 availableFrames = ma_pcm_rb_available_read(
        &loopbackRingBuffer_
    );

    if (availableFrames > MaximumQueuedLoopbackFrames)
    {
        const ma_uint32 staleFrames =
            availableFrames - MaximumQueuedLoopbackFrames;

        if (!DiscardLoopbackFrames(staleFrames))
        {
            return false;
        }

        availableFrames -= staleFrames;
    }

    // Build a one-block scheduling cushion once, then consume one 10 ms
    // block at a time. Requiring the two-block high-water mark on every
    // read creates periodic false underruns even while WASAPI is producing
    // audio normally. Keep partial data across a real underrun instead of
    // discarding it so the next callback can complete the block.
    if (!loopbackPrimed_)
    {
        if (availableFrames < MaximumQueuedLoopbackFrames)
        {
            return false;
        }

        loopbackPrimed_ = true;
    }

    if (availableFrames < FramesPerBlock)
    {
        return false;
    }
    ma_uint32 readFrames = 0;

    while (readFrames < FramesPerBlock)
    {
        ma_uint32 readableFrames = static_cast<ma_uint32>(
            FramesPerBlock
        ) - readFrames;
        void* source = nullptr;

        const ma_result acquireResult = ma_pcm_rb_acquire_read(
            &loopbackRingBuffer_,
            &readableFrames,
            &source
        );

        if (acquireResult != MA_SUCCESS || readableFrames == 0 ||
            source == nullptr)
        {
            std::fill(output.begin(), output.end(), 0.0f);
            return false;
        }

        std::memcpy(
            output.data() +
                static_cast<std::size_t>(readFrames) * ChannelCount,
            source,
            static_cast<std::size_t>(readableFrames) *
                ChannelCount * sizeof(float)
        );

        if (ma_pcm_rb_commit_read(
                &loopbackRingBuffer_,
                readableFrames
            ) != MA_SUCCESS)
        {
            std::fill(output.begin(), output.end(), 0.0f);
            return false;
        }

        readFrames += readableFrames;
    }

    for (float& sample : output)
    {
        if (!std::isfinite(sample))
        {
            sample = 0.0f;
        }
    }

    return true;
}

void AecRenderReferenceMixer::ResetLoopback() noexcept
{
    if (loopbackDeviceInitialized_)
    {
        ma_device_uninit(&loopbackDevice_);
    }

    loopbackDeviceInitialized_ = false;
    loopbackMode_ = LoopbackMode::None;
    loopbackPrimed_ = false;
    std::memset(&loopbackDevice_, 0, sizeof(loopbackDevice_));

    if (loopbackRingBufferInitialized_)
    {
        ma_pcm_rb_uninit(&loopbackRingBuffer_);
    }

    loopbackRingBufferInitialized_ = false;
    std::memset(
        &loopbackRingBuffer_,
        0,
        sizeof(loopbackRingBuffer_)
    );
    loopbackStereoScratch_.fill(0.0f);
}
