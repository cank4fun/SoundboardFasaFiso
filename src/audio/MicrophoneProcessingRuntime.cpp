#include "audio/MicrophoneProcessingRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <string_view>
#include <system_error>
#include <vector>


#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
namespace
{
    constexpr std::size_t AecDiagnosticSeconds = 15;
    constexpr std::size_t AecDiagnosticFrames =
        static_cast<std::size_t>(
            MicrophoneProcessingRuntime::RequiredSampleRate
        ) * AecDiagnosticSeconds;

    bool AecDiagnosticsRequested() noexcept
    {
        const char* const value = std::getenv(
            "SOUNDBOARD_AEC_DIAGNOSTICS"
        );
        return value != nullptr && std::string_view(value) == "1";
    }

    void WriteU16(std::ofstream& stream, const std::uint16_t value)
    {
        const char bytes[]{
            static_cast<char>(value & 0xFFU),
            static_cast<char>((value >> 8U) & 0xFFU)
        };
        stream.write(bytes, sizeof(bytes));
    }

    void WriteU32(std::ofstream& stream, const std::uint32_t value)
    {
        const char bytes[]{
            static_cast<char>(value & 0xFFU),
            static_cast<char>((value >> 8U) & 0xFFU),
            static_cast<char>((value >> 16U) & 0xFFU),
            static_cast<char>((value >> 24U) & 0xFFU)
        };
        stream.write(bytes, sizeof(bytes));
    }

    bool WriteFloatWave(
        const std::filesystem::path& path,
        const std::uint16_t channelCount,
        const std::span<const float> samples
    )
    {
        constexpr std::uint16_t FormatIeeeFloat = 3;
        constexpr std::uint16_t BitsPerSample = 32;
        constexpr std::uint16_t BytesPerSample = BitsPerSample / 8;
        const std::uint32_t dataBytes = static_cast<std::uint32_t>(
            samples.size() * sizeof(float)
        );
        const std::uint16_t blockAlign = static_cast<std::uint16_t>(
            channelCount * BytesPerSample
        );
        const std::uint32_t byteRate =
            MicrophoneProcessingRuntime::RequiredSampleRate * blockAlign;

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }

        stream.write("RIFF", 4);
        WriteU32(stream, 36U + dataBytes);
        stream.write("WAVE", 4);
        stream.write("fmt ", 4);
        WriteU32(stream, 16U);
        WriteU16(stream, FormatIeeeFloat);
        WriteU16(stream, channelCount);
        WriteU32(stream, MicrophoneProcessingRuntime::RequiredSampleRate);
        WriteU32(stream, byteRate);
        WriteU16(stream, blockAlign);
        WriteU16(stream, BitsPerSample);
        stream.write("data", 4);
        WriteU32(stream, dataBytes);
        stream.write(
            reinterpret_cast<const char*>(samples.data()),
            static_cast<std::streamsize>(dataBytes)
        );
        return stream.good();
    }
}

struct MicrophoneProcessingRuntime::AecDiagnosticRecorder
{
    std::vector<float> renderReference;
    std::vector<float> microphoneRaw;
    std::vector<float> microphoneProcessed;
    std::uint64_t referenceMissingBlocks = 0;
    bool flushed = false;

    AecDiagnosticRecorder()
    {
        renderReference.reserve(
            AecDiagnosticFrames * WebRtcAec3Processor::RenderChannelCount
        );
        microphoneRaw.reserve(AecDiagnosticFrames);
        microphoneProcessed.reserve(AecDiagnosticFrames);
    }

    void Append(
        const std::span<const float> render,
        const bool referenceAvailable,
        const std::span<const float> raw,
        const std::span<const float> processed,
        const int streamDelayMilliseconds
    ) noexcept
    {
        if (flushed || raw.size() != processed.size() || raw.empty())
        {
            return;
        }

        const std::size_t remainingFrames = AecDiagnosticFrames -
            std::min(microphoneRaw.size(), AecDiagnosticFrames);
        const std::size_t framesToCopy = std::min(
            remainingFrames,
            raw.size()
        );

        if (framesToCopy == 0)
        {
            Flush(streamDelayMilliseconds);
            return;
        }

        try
        {
            microphoneRaw.insert(
                microphoneRaw.end(),
                raw.begin(),
                raw.begin() + static_cast<std::ptrdiff_t>(framesToCopy)
            );
            microphoneProcessed.insert(
                microphoneProcessed.end(),
                processed.begin(),
                processed.begin() +
                    static_cast<std::ptrdiff_t>(framesToCopy)
            );

            const std::size_t requiredRenderSamples =
                framesToCopy * WebRtcAec3Processor::RenderChannelCount;
            if (referenceAvailable &&
                render.size() >= requiredRenderSamples)
            {
                renderReference.insert(
                    renderReference.end(),
                    render.begin(),
                    render.begin() + static_cast<std::ptrdiff_t>(
                        requiredRenderSamples
                    )
                );
            }
            else
            {
                renderReference.insert(
                    renderReference.end(),
                    requiredRenderSamples,
                    0.0f
                );
                ++referenceMissingBlocks;
            }
        }
        catch (const std::bad_alloc&)
        {
            flushed = true;
            return;
        }

        if (microphoneRaw.size() >= AecDiagnosticFrames)
        {
            Flush(streamDelayMilliseconds);
        }
    }

    void Flush(const int streamDelayMilliseconds) noexcept
    {
        if (flushed || microphoneRaw.empty())
        {
            return;
        }
        flushed = true;

        try
        {
            std::error_code error;
            std::filesystem::path directory =
                std::filesystem::temp_directory_path(error);
            if (error)
            {
                return;
            }
            directory /= "SoundBoardFasaFiso-AEC-Diagnostics";
            std::filesystem::create_directories(directory, error);
            if (error)
            {
                return;
            }

            static_cast<void>(WriteFloatWave(
                directory / "render-reference-stereo.wav",
                2,
                renderReference
            ));
            static_cast<void>(WriteFloatWave(
                directory / "microphone-raw-mono.wav",
                1,
                microphoneRaw
            ));
            static_cast<void>(WriteFloatWave(
                directory / "microphone-processed-mono.wav",
                1,
                microphoneProcessed
            ));

            std::ofstream metadata(
                directory / "metadata.txt",
                std::ios::trunc
            );
            if (metadata)
            {
                metadata
                    << "sample_rate=48000\n"
                    << "captured_frames=" << microphoneRaw.size() << '\n'
                    << "stream_delay_ms=" << streamDelayMilliseconds << '\n'
                    << "reference_missing_blocks="
                    << referenceMissingBlocks << '\n';
            }
        }
        catch (...)
        {
        }
    }
};
#endif

MicrophoneProcessingRuntime::MicrophoneProcessingRuntime() = default;

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
    if (settings.echoCancellationEnabled && AecDiagnosticsRequested())
    {
        try
        {
            aecDiagnosticRecorder_ =
                std::make_unique<AecDiagnosticRecorder>();
        }
        catch (const std::bad_alloc&)
        {
            aecDiagnosticRecorder_.reset();
        }
    }
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
        aecDiagnosticRecorder_.reset();
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
    if (aecDiagnosticRecorder_)
    {
        aecDiagnosticRecorder_->Flush(streamDelayMilliseconds_);
        aecDiagnosticRecorder_.reset();
    }
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
    std::array<
        float,
        WebRtcAec3Processor::RenderSamplesPerBlock
    > renderReference{};
    std::span<const float> renderReferenceView;

    bool referenceAvailable = false;

    if (settings_.echoCancellationEnabled)
    {
        referenceAvailable =
            renderReferenceCallback_ != nullptr &&
            renderReferenceCallback_(
                renderReferenceContext_,
                renderReference.data(),
                static_cast<ma_uint32>(
                    MicrophoneProcessor::SamplesPerBlock
                )
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

#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    if (aecDiagnosticRecorder_)
    {
        aecDiagnosticRecorder_->Append(
            renderReference,
            referenceAvailable,
            monoInput,
            monoOutput,
            streamDelayMilliseconds_
        );
    }
#endif

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
