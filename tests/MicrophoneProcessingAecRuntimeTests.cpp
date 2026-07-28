#include "audio/MicrophoneProcessingRuntime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <thread>

namespace
{
    constexpr std::size_t StereoSampleCount =
        MicrophoneProcessor::SamplesPerBlock *
        MicrophoneProcessingRuntime::RequiredInputChannels;

    struct TestSink
    {
        std::array<float, StereoSampleCount> samples{};
        std::atomic_uint32_t blockCount{0};
    };

    struct RenderSource
    {
        std::array<
            float,
            WebRtcAec3Processor::RenderSamplesPerBlock
        > samples{};
        std::atomic_uint32_t callCount{0};
        bool fail = false;
    };

    void CaptureOutput(
        void* const context,
        const float* const samples,
        const ma_uint32 frameCount
    ) noexcept
    {
        auto* const sink = static_cast<TestSink*>(context);

        if (sink == nullptr || samples == nullptr ||
            frameCount != MicrophoneProcessor::SamplesPerBlock)
        {
            return;
        }

        std::copy_n(samples, StereoSampleCount, sink->samples.begin());
        sink->blockCount.fetch_add(1, std::memory_order_release);
    }

    bool SupplyRenderReference(
        void* const context,
        float* const samples,
        const ma_uint32 frameCount
    ) noexcept
    {
        auto* const source = static_cast<RenderSource*>(context);

        if (source == nullptr || samples == nullptr ||
            frameCount != MicrophoneProcessor::SamplesPerBlock)
        {
            return false;
        }

        source->callCount.fetch_add(1, std::memory_order_relaxed);

        const std::size_t sampleCount =
            static_cast<std::size_t>(frameCount) *
            WebRtcAec3Processor::RenderChannelCount;

        if (source->fail)
        {
            std::fill_n(samples, sampleCount, 0.0f);
            return false;
        }

        std::copy_n(source->samples.begin(), sampleCount, samples);
        return true;
    }

    bool WaitForBlockCount(
        const TestSink& sink,
        const ma_uint32 expected
    )
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (sink.blockCount.load(std::memory_order_acquire) >= expected)
            {
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return false;
    }

    std::array<float, StereoSampleCount> MakeStereoTone()
    {
        std::array<float, StereoSampleCount> input{};
        constexpr float TwoPi = 6.28318530717958647692f;
        float phase = 0.0f;

        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock;
            ++frame)
        {
            const float sample = 0.15f * std::sin(phase);
            const std::size_t index = frame * 2;
            input[index] = sample;
            input[index + 1] = sample;
            phase += TwoPi * 440.0f /
                static_cast<float>(
                    MicrophoneProcessingRuntime::RequiredSampleRate
                );
        }

        return input;
    }

    bool PushOneBlock(
        MicrophoneProcessingRuntime& runtime,
        TestSink& sink,
        const std::array<float, StereoSampleCount>& input,
        const ma_uint32 expectedBlockCount
    )
    {
        return runtime.PushInputFrames(
            input.data(),
            static_cast<ma_uint32>(MicrophoneProcessor::SamplesPerBlock)
        ) == MicrophoneProcessor::SamplesPerBlock &&
            WaitForBlockCount(sink, expectedBlockCount);
    }

    bool TestDisabledAecDoesNotConsumeReference()
    {
        MicrophoneProcessingRuntime runtime;
        MicrophoneProcessingSettings settings;
        settings.enabled = false;
        settings.echoCancellationEnabled = false;
        TestSink sink;
        RenderSource source;
        const auto input = MakeStereoTone();

        if (!runtime.Initialize(
                MicrophoneProcessingRuntime::RequiredSampleRate,
                MicrophoneProcessingRuntime::RequiredInputChannels,
                settings,
                &CaptureOutput,
                &sink,
                &SupplyRenderReference,
                &source,
                20
            ))
        {
            return false;
        }

        const bool received = PushOneBlock(runtime, sink, input, 1);
        const MicrophoneProcessingSnapshot snapshot = runtime.GetSnapshot();
        const bool identical = received && std::equal(
            input.begin(),
            input.end(),
            sink.samples.begin()
        );
        runtime.Shutdown();

        return received &&
            source.callCount.load(std::memory_order_relaxed) == 0 &&
            !snapshot.echoCancellationRequested &&
            !snapshot.echoCancellationReady &&
            !snapshot.echoCancellationReferenceAvailable &&
            !snapshot.echoCancellationActive &&
            !snapshot.echoCancellationFailed &&
            snapshot.echoCancellationReferenceUnderrunCount == 0 &&
            snapshot.echoCancellationFailureCount == 0 &&
            snapshot.bypassed &&
            identical;
    }

    bool TestLiveAecPathConsumesReference()
    {
        MicrophoneProcessingRuntime runtime;
        MicrophoneProcessingSettings settings;
        settings.enabled = false;
        settings.echoCancellationEnabled = true;
        TestSink sink;
        RenderSource source;
        const auto input = MakeStereoTone();

        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock;
            ++frame)
        {
            const std::size_t index =
                frame * WebRtcAec3Processor::RenderChannelCount;
            source.samples[index] = input[frame * 2];
            source.samples[index + 1] = input[frame * 2 + 1];
        }

        if (!runtime.Initialize(
                MicrophoneProcessingRuntime::RequiredSampleRate,
                MicrophoneProcessingRuntime::RequiredInputChannels,
                settings,
                &CaptureOutput,
                &sink,
                &SupplyRenderReference,
                &source,
                20
            ))
        {
            return false;
        }

        constexpr ma_uint32 BlockCount = 40;

        for (ma_uint32 block = 0; block < BlockCount; ++block)
        {
            if (!PushOneBlock(runtime, sink, input, block + 1))
            {
                runtime.Shutdown();
                return false;
            }
        }

        const MicrophoneProcessingSnapshot snapshot = runtime.GetSnapshot();
        const bool outputFinite = std::all_of(
            sink.samples.begin(),
            sink.samples.end(),
            [](const float sample) { return std::isfinite(sample); }
        );
        runtime.Shutdown();

        return source.callCount.load(std::memory_order_relaxed) >= BlockCount &&
            snapshot.echoCancellationRequested &&
            snapshot.echoCancellationReady &&
            snapshot.echoCancellationReferenceAvailable &&
            snapshot.echoCancellationActive &&
            !snapshot.echoCancellationFailed &&
            snapshot.echoCancellationError == 0 &&
            snapshot.echoCancellationReferenceUnderrunCount == 0 &&
            snapshot.echoCancellationFailureCount == 0 &&
            !snapshot.bypassed &&
            outputFinite;
    }

    bool TestReferenceUnderrunSafelyBypassesAndRecovers()
    {
        MicrophoneProcessingRuntime runtime;
        MicrophoneProcessingSettings settings;
        settings.enabled = false;
        settings.echoCancellationEnabled = true;
        TestSink sink;
        RenderSource source;
        source.fail = true;
        const auto input = MakeStereoTone();

        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock;
            ++frame)
        {
            const std::size_t index =
                frame * WebRtcAec3Processor::RenderChannelCount;
            source.samples[index] = input[frame * 2];
            source.samples[index + 1] = input[frame * 2 + 1];
        }

        if (!runtime.Initialize(
                MicrophoneProcessingRuntime::RequiredSampleRate,
                MicrophoneProcessingRuntime::RequiredInputChannels,
                settings,
                &CaptureOutput,
                &sink,
                &SupplyRenderReference,
                &source,
                20
            ))
        {
            return false;
        }

        const bool firstReceived = PushOneBlock(runtime, sink, input, 1);
        const MicrophoneProcessingSnapshot missingSnapshot =
            runtime.GetSnapshot();
        const bool firstIdentical = firstReceived && std::equal(
            input.begin(),
            input.end(),
            sink.samples.begin()
        );

        source.fail = false;
        const bool secondReceived = PushOneBlock(runtime, sink, input, 2);
        const MicrophoneProcessingSnapshot recoveredSnapshot =
            runtime.GetSnapshot();
        runtime.Shutdown();

        return firstReceived &&
            firstIdentical &&
            missingSnapshot.echoCancellationRequested &&
            missingSnapshot.echoCancellationReady &&
            !missingSnapshot.echoCancellationReferenceAvailable &&
            !missingSnapshot.echoCancellationActive &&
            !missingSnapshot.echoCancellationFailed &&
            missingSnapshot.echoCancellationError == 0 &&
            missingSnapshot.echoCancellationReferenceUnderrunCount == 1 &&
            missingSnapshot.echoCancellationFailureCount == 0 &&
            missingSnapshot.bypassed &&
            secondReceived &&
            recoveredSnapshot.echoCancellationRequested &&
            recoveredSnapshot.echoCancellationReady &&
            recoveredSnapshot.echoCancellationReferenceAvailable &&
            recoveredSnapshot.echoCancellationActive &&
            !recoveredSnapshot.echoCancellationFailed &&
            recoveredSnapshot.echoCancellationReferenceUnderrunCount == 1 &&
            recoveredSnapshot.echoCancellationFailureCount == 0 &&
            !recoveredSnapshot.bypassed;
    }
}

int main()
{
    if (!TestDisabledAecDoesNotConsumeReference())
    {
        std::cerr << "FAILED: disabled AEC still consumed render reference\n";
        return 1;
    }

    if (!TestLiveAecPathConsumesReference())
    {
        std::cerr << "FAILED: live AEC path did not consume render reference\n";
        return 1;
    }

    if (!TestReferenceUnderrunSafelyBypassesAndRecovers())
    {
        std::cerr
            << "FAILED: AEC reference underrun did not safely bypass and "
               "recover\n";
        return 1;
    }

    std::cout
        << "Microphone AEC runtime tests passed: disabled, active, "
           "no-reference, and recovery paths.\n";
    return 0;
}
