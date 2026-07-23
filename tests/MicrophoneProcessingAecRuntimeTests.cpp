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
        std::array<float, MicrophoneProcessor::SamplesPerBlock> samples{};
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

        if (source->fail)
        {
            std::fill_n(samples, frameCount, 0.0f);
            return false;
        }

        std::copy(source->samples.begin(), source->samples.end(), samples);
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

    bool TestLiveAecPathConsumesReference()
    {
        MicrophoneProcessingRuntime runtime;
        MicrophoneProcessingSettings settings;
        settings.enabled = false;
        TestSink sink;
        RenderSource source;
        const auto input = MakeStereoTone();

        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock;
            ++frame)
        {
            source.samples[frame] = input[frame * 2];
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
            if (runtime.PushInputFrames(
                    input.data(),
                    static_cast<ma_uint32>(
                        MicrophoneProcessor::SamplesPerBlock
                    )
                ) != MicrophoneProcessor::SamplesPerBlock)
            {
                runtime.Shutdown();
                return false;
            }

            if (!WaitForBlockCount(sink, block + 1))
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
            snapshot.echoCancellationActive &&
            !snapshot.echoCancellationFailed &&
            snapshot.echoCancellationError == 0 &&
            !snapshot.bypassed &&
            outputFinite;
    }

    bool TestReferenceFailureFallsBackToStereoBypass()
    {
        MicrophoneProcessingRuntime runtime;
        MicrophoneProcessingSettings settings;
        settings.enabled = false;
        TestSink sink;
        RenderSource source;
        source.fail = true;
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

        const ma_uint32 written = runtime.PushInputFrames(
            input.data(),
            static_cast<ma_uint32>(MicrophoneProcessor::SamplesPerBlock)
        );
        const bool received = WaitForBlockCount(sink, 1);
        const MicrophoneProcessingSnapshot snapshot = runtime.GetSnapshot();
        const bool identical = received && std::equal(
            input.begin(),
            input.end(),
            sink.samples.begin()
        );
        runtime.Shutdown();

        return written == MicrophoneProcessor::SamplesPerBlock &&
            source.callCount.load(std::memory_order_relaxed) == 1 &&
            snapshot.echoCancellationRequested &&
            !snapshot.echoCancellationActive &&
            snapshot.echoCancellationFailed &&
            snapshot.echoCancellationError != 0 &&
            snapshot.bypassed &&
            identical;
    }
}

int main()
{
    if (!TestLiveAecPathConsumesReference())
    {
        std::cerr << "FAILED: live AEC path did not consume render reference\n";
        return 1;
    }

    if (!TestReferenceFailureFallsBackToStereoBypass())
    {
        std::cerr << "FAILED: AEC reference failure did not safely bypass\n";
        return 1;
    }

    std::cout
        << "Microphone AEC runtime tests passed: live reference and safe "
           "fallback.\n";
    return 0;
}
