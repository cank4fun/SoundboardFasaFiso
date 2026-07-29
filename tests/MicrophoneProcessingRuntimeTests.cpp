#include "audio/MicrophoneProcessingRuntime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

    bool TestNoiseSuppressionProducesProcessedStereo()
    {
        MicrophoneProcessingRuntime runtime;
        TestSink sink;
        MicrophoneProcessingSettings settings;
        settings.enabled = true;
        settings.highPassEnabled = false;
        settings.noiseSuppressionEnabled = true;
        settings.noiseSuppressionLevel =
            MicrophoneNoiseSuppressionLevel::Balanced;
        settings.compressorEnabled = false;
        settings.limiterEnabled = false;

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
        std::uint32_t randomState = 0x12345678U;
        float phase = 0.0f;
        constexpr float twoPi = 6.28318530717958647692f;

        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock;
            ++frame)
        {
            randomState = randomState * 1664525U + 1013904223U;
            const float noise =
                (static_cast<float>((randomState >> 8U) & 0xFFFFU) /
                    32767.5f - 1.0f) * 0.08f;
            const float sample = 0.12f * std::sin(phase) + noise;
            const std::size_t index = frame * 2;
            input[index] = sample;
            input[index + 1] = sample;
            phase += twoPi * 220.0f /
                static_cast<float>(
                    MicrophoneProcessingRuntime::RequiredSampleRate
                );
        }

        const ma_uint32 written = runtime.PushInputFrames(
            input.data(),
            static_cast<ma_uint32>(
                MicrophoneProcessor::SamplesPerBlock
            )
        );

        const bool received = WaitForOutput(sink);
        const auto snapshot = runtime.GetSnapshot();
        bool outputValid = received;
        bool outputChanged = false;

        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock && outputValid;
            ++frame)
        {
            const std::size_t index = frame * 2;
            outputValid = std::isfinite(sink.samples[index]) &&
                std::isfinite(sink.samples[index + 1]) &&
                NearlyEqual(
                    sink.samples[index],
                    sink.samples[index + 1]
                );
            outputChanged = outputChanged ||
                !NearlyEqual(sink.samples[index], input[index], 0.000001f);
        }

        runtime.Shutdown();

        return written == MicrophoneProcessor::SamplesPerBlock &&
            outputValid &&
            outputChanged &&
            snapshot.noiseSuppressionRequested &&
            snapshot.noiseSuppressionActive &&
            !snapshot.noiseSuppressionFailed &&
            !snapshot.bypassed;
    }

    bool TestInitialVoiceEffectSettingsAreApplied()
    {
        MicrophoneProcessingRuntime runtime;
        TestSink sink;
        MicrophoneProcessingSettings microphoneSettings;
        microphoneSettings.enabled = false;
        const auto voiceSettings = BuildVoiceEffectPreset(
            VoiceEffectPreset::Radio,
            true
        );

        if (!voiceSettings.has_value() ||
            !runtime.Initialize(
                MicrophoneProcessingRuntime::RequiredSampleRate,
                MicrophoneProcessingRuntime::RequiredInputChannels,
                microphoneSettings,
                &CaptureOutput,
                &sink,
                *voiceSettings
            ))
        {
            return false;
        }

        std::array<float, StereoSampleCount> input{};
        for (std::size_t index = 0; index < input.size(); ++index)
        {
            input[index] = static_cast<float>(index % 29) / 28.0f - 0.5f;
        }

        const ma_uint32 written = runtime.PushInputFrames(
            input.data(),
            static_cast<ma_uint32>(MicrophoneProcessor::SamplesPerBlock)
        );
        const bool received = WaitForOutput(sink);
        const MicrophoneProcessingSnapshot snapshot = runtime.GetSnapshot();
        bool outputProcessed = received;
        bool outputChanged = false;
        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock && outputProcessed;
            ++frame)
        {
            const std::size_t index = frame * 2;
            outputProcessed = std::isfinite(sink.samples[index]) &&
                std::isfinite(sink.samples[index + 1]) &&
                NearlyEqual(sink.samples[index], sink.samples[index + 1]);
            outputChanged = outputChanged ||
                !NearlyEqual(sink.samples[index], input[index], 0.000001f);
        }
        runtime.Shutdown();

        return written == MicrophoneProcessor::SamplesPerBlock &&
            outputProcessed &&
            outputChanged &&
            snapshot.voiceEffectsEnabled &&
            !snapshot.voiceEffectsBypassed &&
            snapshot.voiceEffectPreset == VoiceEffectPreset::Radio &&
            !snapshot.bypassed;
    }

    bool TestVoiceEffectSettingsApplyAtBlockBoundary()
    {
        MicrophoneProcessingRuntime runtime;
        TestSink sink;
        MicrophoneProcessingSettings microphoneSettings;
        microphoneSettings.enabled = false;

        if (!runtime.Initialize(
                MicrophoneProcessingRuntime::RequiredSampleRate,
                MicrophoneProcessingRuntime::RequiredInputChannels,
                microphoneSettings,
                &CaptureOutput,
                &sink
            ))
        {
            return false;
        }

        const auto voiceSettings = BuildVoiceEffectPreset(
            VoiceEffectPreset::DarkVocal,
            true
        );
        if (!voiceSettings.has_value() ||
            !runtime.UpdateVoiceEffectSettings(*voiceSettings))
        {
            runtime.Shutdown();
            return false;
        }

        std::array<float, StereoSampleCount> input{};
        for (std::size_t index = 0; index < input.size(); ++index)
        {
            input[index] = static_cast<float>(index % 31) / 30.0f - 0.5f;
        }

        const ma_uint32 written = runtime.PushInputFrames(
            input.data(),
            static_cast<ma_uint32>(MicrophoneProcessor::SamplesPerBlock)
        );
        const bool received = WaitForOutput(sink);
        const MicrophoneProcessingSnapshot snapshot = runtime.GetSnapshot();
        bool outputProcessed = received;
        bool outputChanged = false;
        for (std::size_t frame = 0;
            frame < MicrophoneProcessor::SamplesPerBlock && outputProcessed;
            ++frame)
        {
            const std::size_t index = frame * 2;
            outputProcessed = std::isfinite(sink.samples[index]) &&
                std::isfinite(sink.samples[index + 1]) &&
                NearlyEqual(sink.samples[index], sink.samples[index + 1]);
            outputChanged = outputChanged ||
                !NearlyEqual(sink.samples[index], input[index], 0.000001f);
        }
        const std::uint64_t rejectedUpdates =
            runtime.GetRejectedVoiceEffectUpdateCount();
        runtime.Shutdown();

        return written == MicrophoneProcessor::SamplesPerBlock &&
            outputProcessed &&
            outputChanged &&
            snapshot.voiceEffectsEnabled &&
            !snapshot.voiceEffectsBypassed &&
            snapshot.voiceEffectPreset == VoiceEffectPreset::DarkVocal &&
            !snapshot.bypassed &&
            rejectedUpdates == 0;
    }


    bool TestRuntimeDiagnosticsTrackProcessingBudget()
    {
        MicrophoneProcessingRuntime runtime;
        TestSink sink;
        MicrophoneProcessingSettings microphoneSettings;
        microphoneSettings.enabled = false;
        const auto voiceSettings = BuildVoiceEffectPreset(
            VoiceEffectPreset::Robot,
            true
        );

        if (!voiceSettings.has_value() ||
            !runtime.Initialize(
                MicrophoneProcessingRuntime::RequiredSampleRate,
                MicrophoneProcessingRuntime::RequiredInputChannels,
                microphoneSettings,
                &CaptureOutput,
                &sink,
                *voiceSettings
            ))
        {
            return false;
        }

        const auto initialDiagnostics = runtime.GetDiagnostics();
        std::array<float, StereoSampleCount> input{};
        for (std::size_t index = 0; index < input.size(); ++index)
        {
            input[index] = static_cast<float>(index % 43) / 42.0f - 0.5f;
        }

        const ma_uint32 written = runtime.PushInputFrames(
            input.data(),
            static_cast<ma_uint32>(MicrophoneProcessor::SamplesPerBlock)
        );
        const bool received = WaitForOutput(sink);
        const auto diagnostics = runtime.GetDiagnostics();
        runtime.Shutdown();

        if (written != MicrophoneProcessor::SamplesPerBlock ||
            !received ||
            initialDiagnostics.processedBlockCount != 0 ||
            initialDiagnostics.processingDeadlineMissCount != 0 ||
            initialDiagnostics.totalProcessingTimeNanoseconds != 0 ||
            initialDiagnostics.maximumProcessingTimeNanoseconds != 0 ||
            initialDiagnostics.peakQueuedInputFrames != 0 ||
            diagnostics.processedBlockCount == 0 ||
            diagnostics.maximumProcessingTimeNanoseconds == 0 ||
            diagnostics.totalProcessingTimeNanoseconds <
                diagnostics.maximumProcessingTimeNanoseconds ||
            diagnostics.processingDeadlineMissCount >
                diagnostics.processedBlockCount ||
            diagnostics.peakQueuedInputFrames == 0 ||
            diagnostics.peakQueuedInputFrames >
                MicrophoneProcessingRuntime::InputRingBufferFrames)
        {
            return false;
        }

        if (!runtime.Initialize(
                MicrophoneProcessingRuntime::RequiredSampleRate,
                MicrophoneProcessingRuntime::RequiredInputChannels,
                microphoneSettings,
                &CaptureOutput,
                &sink,
                *voiceSettings
            ))
        {
            return false;
        }

        const auto resetDiagnostics = runtime.GetDiagnostics();
        runtime.Shutdown();
        return resetDiagnostics.processedBlockCount == 0 &&
            resetDiagnostics.processingDeadlineMissCount == 0 &&
            resetDiagnostics.totalProcessingTimeNanoseconds == 0 &&
            resetDiagnostics.maximumProcessingTimeNanoseconds == 0 &&
            resetDiagnostics.peakQueuedInputFrames == 0;
    }

    bool TestRejectsInvalidInitialVoiceEffectSettings()
    {
        MicrophoneProcessingRuntime runtime;
        TestSink sink;
        MicrophoneProcessingSettings microphoneSettings;
        VoiceEffectSettings voiceSettings;
        voiceSettings.pitchSemitones =
            VoiceEffectLimits::MaximumPitchSemitones + 1.0f;

        return !runtime.Initialize(
            MicrophoneProcessingRuntime::RequiredSampleRate,
            MicrophoneProcessingRuntime::RequiredInputChannels,
            microphoneSettings,
            &CaptureOutput,
            &sink,
            voiceSettings
        );
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
        {"Noise suppression emits processed stereo",
            &TestNoiseSuppressionProducesProcessedStereo},
        {"Initial voice-effect settings are applied",
            &TestInitialVoiceEffectSettingsAreApplied},
        {"Voice-effect update applies at block boundary",
            &TestVoiceEffectSettingsApplyAtBlockBoundary},
        {"Runtime diagnostics track processing budget",
            &TestRuntimeDiagnosticsTrackProcessingBudget},
        {"Rejects invalid initial voice-effect settings",
            &TestRejectsInvalidInitialVoiceEffectSettings},
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
