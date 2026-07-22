#include "audio/MicrophoneProcessingRuntime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
    constexpr std::size_t StereoSampleCount =
        MicrophoneProcessor::SamplesPerBlock *
        MicrophoneProcessingRuntime::RequiredInputChannels;

    struct TestSink
    {
        std::array<float, StereoSampleCount> samples{};
        std::atomic_bool received{false};
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

        std::copy_n(
            samples,
            StereoSampleCount,
            sink->samples.begin()
        );
        sink->received.store(true, std::memory_order_release);
    }

    bool WaitForOutput(TestSink& sink)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(2);

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (sink.received.load(std::memory_order_acquire))
            {
                return true;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(1)
            );
        }

        return false;
    }

    bool NearlyEqual(
        const float left,
        const float right,
        const float tolerance = 0.0001f
    )
    {
        return std::abs(left - right) <= tolerance;
    }

    bool TestRejectsUnsupportedFormat()
    {
        MicrophoneProcessingRuntime runtime;
        TestSink sink;
        MicrophoneProcessingSettings settings;

        return !runtime.Initialize(
                44100,
                MicrophoneProcessingRuntime::RequiredInputChannels,
                settings,
                &CaptureOutput,
                &sink
            ) &&
            !runtime.Initialize(
                MicrophoneProcessingRuntime::RequiredSampleRate,
                1,
                settings,
                &CaptureOutput,
                &sink
            ) &&
            !runtime.IsInitialized();
    }

    bool TestBypassPreservesStereoSamples()
    {
        MicrophoneProcessingRuntime runtime;
        TestSink sink;
        MicrophoneProcessingSettings settings;
        settings.enabled = false;

        if (!runtime.Initialize(
                MicrophoneProcessingRuntime::RequiredSampleRate,
                MicrophoneProcessingRuntime::RequiredInputChannels,
                settings,
                &CaptureOutput,
                &sink
            ))
        {
            return false;
        }

        std::array<float, StereoSampleCount> input{};

        for (std::size_t index = 0; index < input.size(); ++index)
        {
            input[index] = static_cast<float>(index % 97) / 96.0f - 0.5f;
        }

        const ma_uint32 written = runtime.PushInputFrames(
            input.data(),
            static_cast<ma_uint32>(
                MicrophoneProcessor::SamplesPerBlock
            )
        );

        const bool received = WaitForOutput(sink);
        const bool identical = received &&
            std::equal(
                input.begin(),
                input.end(),
                sink.samples.begin()
            );
        const auto snapshot = runtime.GetSnapshot();
        runtime.Shutdown();

        return written == MicrophoneProcessor::SamplesPerBlock &&
            identical &&
            snapshot.bypassed &&
            snapshot.rawPeak > 0.0f;
    }

    bool TestLimiterProducesMonoStereoOutput()
    {
        MicrophoneProcessingRuntime runtime;
        TestSink sink;
        MicrophoneProcessingSettings settings;
        settings.enabled = true;
        settings.highPassEnabled = false;
        settings.compressorEnabled = false;
        settings.limiterEnabled = true;
        settings.limiterCeilingDb = -6.0f;

        if (!runtime.Initialize(
                MicrophoneProcessingRuntime::RequiredSampleRate,
                MicrophoneProcessingRuntime::RequiredInputChannels,
                settings,
                &CaptureOutput,
                &sink
            ))
        {
            return false;
        }

        std::array<float, StereoSampleCount> input{};

        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock;
            ++frame)
        {
            const std::size_t index = frame * 2;
            input[index] = 0.9f;
            input[index + 1] = 0.7f;
        }

        const ma_uint32 written = runtime.PushInputFrames(
            input.data(),
            static_cast<ma_uint32>(
                MicrophoneProcessor::SamplesPerBlock
            )
        );

        const bool received = WaitForOutput(sink);
        const float expectedCeiling = std::pow(10.0f, -6.0f / 20.0f);
        bool outputValid = received;

        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock && outputValid;
            ++frame)
        {
            const std::size_t index = frame * 2;
            outputValid = NearlyEqual(
                    sink.samples[index],
                    sink.samples[index + 1]
                ) &&
                std::abs(sink.samples[index]) <=
                    expectedCeiling + 0.0001f;
        }

        const auto snapshot = runtime.GetSnapshot();
        runtime.Shutdown();

        return written == MicrophoneProcessor::SamplesPerBlock &&
            outputValid &&
            !snapshot.bypassed &&
            snapshot.processedPeak <= expectedCeiling + 0.0001f;
    }

    bool TestRejectsInvalidSettings()
    {
        MicrophoneProcessingRuntime runtime;
        TestSink sink;
        MicrophoneProcessingSettings settings;
        settings.highPassHz =
            std::numeric_limits<float>::quiet_NaN();

        return !runtime.Initialize(
            MicrophoneProcessingRuntime::RequiredSampleRate,
            MicrophoneProcessingRuntime::RequiredInputChannels,
            settings,
            &CaptureOutput,
            &sink
        );
    }
}

int main()
{
    struct NamedTest
    {
        const char* name;
        bool (*test)();
    };

    const NamedTest tests[]{
        {"Rejects unsupported format", &TestRejectsUnsupportedFormat},
        {"Bypass preserves stereo", &TestBypassPreservesStereoSamples},
        {"Limiter emits mono stereo", &TestLimiterProducesMonoStereoOutput},
        {"Rejects invalid settings", &TestRejectsInvalidSettings}
    };

    bool success = true;

    for (const NamedTest& test : tests)
    {
        const bool passed = test.test();
        std::cout << (passed ? "PASS: " : "FAIL: ")
            << test.name << '\n';
        success = success && passed;
    }

    return success ? 0 : 1;
}
