#include "audio/MicrophoneProcessingRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <system_error>

MicrophoneProcessingRuntime::~MicrophoneProcessingRuntime()
{
    Shutdown();
}

bool MicrophoneProcessingRuntime::Initialize(
    const ma_uint32 inputSampleRate,
    const ma_uint32 inputChannels,
    const MicrophoneProcessingSettings& settings,
    const OutputCallback outputCallback,
    void* const outputContext
#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    ,
    const RenderReferenceCallback renderReferenceCallback,
    void* const renderReferenceContext,
    const int streamDelayMilliseconds
#endif
)
{
    if (initialized_ ||
        inputSampleRate != RequiredSampleRate ||
        inputChannels != RequiredInputChannels ||
        outputCallback == nullptr ||
        !IsValidMicrophoneProcessingSettings(settings)
#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
        || streamDelayMilliseconds < 0 ||
        streamDelayMilliseconds > 500
#endif
        )
    {
        return false;
    }

    if (!processor_.Initialize(settings))
    {
        return false;
    }

    const ma_result ringResult = ma_pcm_rb_init(
        ma_format_f32,
        RequiredInputChannels,
        InputRingBufferFrames,
        nullptr,
        nullptr,
        &inputRingBuffer_
    );

    if (ringResult != MA_SUCCESS)
    {
        processor_.Reset();
        return false;
    }

    inputRingBufferInitialized_ = true;
    ma_pcm_rb_set_sample_rate(
        &inputRingBuffer_,
        RequiredSampleRate
    );

    settings_ = settings;
    outputCallback_ = outputCallback;
    outputContext_ = outputContext;
#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    renderReferenceCallback_ = renderReferenceCallback;
    renderReferenceContext_ = renderReferenceContext;
    streamDelayMilliseconds_ = streamDelayMilliseconds;
#endif
    stopRequested_.store(false, std::memory_order_relaxed);
    droppedInputFrames_.store(0, std::memory_order_relaxed);
    echoCancellationReferenceUnderruns_.store(
        0,
        std::memory_order_relaxed
    );
    acceptingInput_.store(true, std::memory_order_release);

    try
    {
        workerThread_ = std::thread(
            &MicrophoneProcessingRuntime::WorkerMain,
            this
        );
    }
    catch (const std::system_error&)
    {
        acceptingInput_.store(false, std::memory_order_release);
        ma_pcm_rb_uninit(&inputRingBuffer_);
        inputRingBufferInitialized_ = false;
        processor_.Reset();
        outputCallback_ = nullptr;
        outputContext_ = nullptr;
#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
        renderReferenceCallback_ = nullptr;
        renderReferenceContext_ = nullptr;
        streamDelayMilliseconds_ = 20;
#endif
        std::memset(
            &inputRingBuffer_,
            0,
            sizeof(inputRingBuffer_)
        );
        return false;
    }

    initialized_ = true;
    return true;
}

ma_uint32 MicrophoneProcessingRuntime::PushInputFrames(
    const float* const interleavedStereoFrames,
    const ma_uint32 frameCount
) noexcept
{
    if (interleavedStereoFrames == nullptr || frameCount == 0 ||
        !acceptingInput_.load(std::memory_order_acquire))
    {
        return 0;
    }

    activePushCount_.fetch_add(1, std::memory_order_acq_rel);

    if (!acceptingInput_.load(std::memory_order_acquire))
    {
        activePushCount_.fetch_sub(1, std::memory_order_release);
        return 0;
    }

    ma_uint32 writtenFrames = 0;

    while (writtenFrames < frameCount)
    {
        ma_uint32 writableFrames = frameCount - writtenFrames;
        void* destination = nullptr;

        const ma_result acquireResult = ma_pcm_rb_acquire_write(
            &inputRingBuffer_,
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
                static_cast<std::size_t>(writtenFrames) *
                    RequiredInputChannels,
            static_cast<std::size_t>(writableFrames) *
                RequiredInputChannels * sizeof(float)
        );

        if (ma_pcm_rb_commit_write(
                &inputRingBuffer_,
                writableFrames
            ) != MA_SUCCESS)
        {
            break;
        }

        writtenFrames += writableFrames;
    }

    if (writtenFrames < frameCount)
    {
        droppedInputFrames_.fetch_add(
            frameCount - writtenFrames,
            std::memory_order_relaxed
        );
    }

    activePushCount_.fetch_sub(1, std::memory_order_release);
    return writtenFrames;
}

MicrophoneProcessingSnapshot
MicrophoneProcessingRuntime::GetSnapshot() const
{
    MicrophoneProcessingSnapshot snapshot = processor_.GetSnapshot();
    snapshot.echoCancellationReferenceUnderrunCount =
        echoCancellationReferenceUnderruns_.load(
            std::memory_order_relaxed
        );
    return snapshot;
}

std::uint64_t
MicrophoneProcessingRuntime::GetDroppedInputFrameCount() const noexcept
{
    return droppedInputFrames_.load(std::memory_order_relaxed);
}

bool MicrophoneProcessingRuntime::IsInitialized() const noexcept
{
    return initialized_;
}

void MicrophoneProcessingRuntime::Shutdown()
{
    acceptingInput_.store(false, std::memory_order_release);

    while (activePushCount_.load(std::memory_order_acquire) != 0)
    {
        std::this_thread::yield();
    }

    stopRequested_.store(true, std::memory_order_release);

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }

    if (inputRingBufferInitialized_)
    {
        ma_pcm_rb_uninit(&inputRingBuffer_);
    }

    inputRingBufferInitialized_ = false;
    initialized_ = false;
    outputCallback_ = nullptr;
    outputContext_ = nullptr;
#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    renderReferenceCallback_ = nullptr;
    renderReferenceContext_ = nullptr;
    streamDelayMilliseconds_ = 20;
#endif
    settings_ = {};
    echoCancellationReferenceUnderruns_.store(
        0,
        std::memory_order_relaxed
    );
    processor_.Reset();
    std::memset(
        &inputRingBuffer_,
        0,
        sizeof(inputRingBuffer_)
    );
}

void MicrophoneProcessingRuntime::WorkerMain()
{
    constexpr std::size_t StereoSamplesPerBlock =
        MicrophoneProcessor::SamplesPerBlock *
        RequiredInputChannels;

    std::array<float, StereoSamplesPerBlock> stereoInput{};
    ma_uint32 accumulatedFrames = 0;

    while (!stopRequested_.load(std::memory_order_acquire))
    {
        ma_uint32 readableFrames =
            static_cast<ma_uint32>(
                MicrophoneProcessor::SamplesPerBlock
            ) - accumulatedFrames;
        void* source = nullptr;

        const ma_result acquireResult = ma_pcm_rb_acquire_read(
            &inputRingBuffer_,
            &readableFrames,
            &source
        );

        if (acquireResult != MA_SUCCESS || readableFrames == 0 ||
            source == nullptr)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1)
            );
            continue;
        }

        std::memcpy(
            stereoInput.data() +
                static_cast<std::size_t>(accumulatedFrames) *
                    RequiredInputChannels,
            source,
            static_cast<std::size_t>(readableFrames) *
                RequiredInputChannels * sizeof(float)
        );

        if (ma_pcm_rb_commit_read(
                &inputRingBuffer_,
                readableFrames
            ) != MA_SUCCESS)
        {
            continue;
        }

        accumulatedFrames += readableFrames;

        if (accumulatedFrames ==
            MicrophoneProcessor::SamplesPerBlock)
        {
            ProcessAndDispatchBlock(stereoInput);
            accumulatedFrames = 0;
        }
    }
}

void MicrophoneProcessingRuntime::ProcessAndDispatchBlock(
    const std::array<
        float,
        MicrophoneProcessor::SamplesPerBlock *
            RequiredInputChannels>& stereoInput
)
{
    std::array<float, MicrophoneProcessor::SamplesPerBlock> monoInput{};
    std::array<float, MicrophoneProcessor::SamplesPerBlock> monoOutput{};
    std::array<
        float,
        MicrophoneProcessor::SamplesPerBlock * RequiredInputChannels
    > stereoOutput{};

    for (std::size_t frame = 0;
        frame < MicrophoneProcessor::SamplesPerBlock;
        ++frame)
    {
        const std::size_t sampleIndex =
            frame * RequiredInputChannels;
        monoInput[frame] =
            (stereoInput[sampleIndex] +
                stereoInput[sampleIndex + 1]) * 0.5f;
    }

#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    std::array<float, MicrophoneProcessor::SamplesPerBlock>
        renderReference{};
    std::span<const float> renderReferenceView;

    if (settings_.echoCancellationEnabled)
    {
        const bool referenceAvailable =
            renderReferenceCallback_ != nullptr &&
            renderReferenceCallback_(
                renderReferenceContext_,
                renderReference.data(),
                static_cast<ma_uint32>(renderReference.size())
            );

        if (referenceAvailable)
        {
            renderReferenceView = renderReference;
        }
        else
        {
            echoCancellationReferenceUnderruns_.fetch_add(
                1,
                std::memory_order_relaxed
            );
        }
    }

    if (!processor_.ProcessBlock(
            monoInput,
            monoOutput,
            renderReferenceView,
            streamDelayMilliseconds_
        ))
#else
    if (!processor_.ProcessBlock(monoInput, monoOutput))
#endif
    {
        return;
    }

    const MicrophoneProcessingSnapshot snapshot =
        processor_.GetSnapshot();

    if (snapshot.bypassed)
    {
        stereoOutput = stereoInput;
    }
    else
    {
        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock;
            ++frame)
        {
            const std::size_t sampleIndex =
                frame * RequiredInputChannels;
            stereoOutput[sampleIndex] = monoOutput[frame];
            stereoOutput[sampleIndex + 1] = monoOutput[frame];
        }
    }

    outputCallback_(
        outputContext_,
        stereoOutput.data(),
        static_cast<ma_uint32>(
            MicrophoneProcessor::SamplesPerBlock
        )
    );
}
