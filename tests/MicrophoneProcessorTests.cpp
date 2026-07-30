#include "audio/MicrophoneProcessor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    int failureCount = 0;

    void Expect(const bool condition, const std::string_view message)
    {
        if (condition)
        {
            return;
        }

        ++failureCount;
        std::cerr << "FAILED: " << message << '\n';
    }

    bool NearlyEqual(
        const float left,
        const float right,
        const float tolerance = 0.00001f
    )
    {
        return std::abs(left - right) <= tolerance;
    }

    void TestConstantsAndResetState()
    {
        Expect(
            MicrophoneProcessor::ProcessingSampleRate == 48000,
            "processing sample rate is 48 kHz"
        );
        Expect(
            MicrophoneProcessor::SamplesPerBlock == 480,
            "processing block is 10 ms"
        );
        Expect(
            MicrophoneProcessor::MinimumAgcGainDb == -12.0f,
            "AGC attenuation limit is stable"
        );
        Expect(
            MicrophoneProcessor::MaximumAgcGainDb == 18.0f,
            "AGC boost limit is stable"
        );

        MicrophoneProcessor processor;
        Expect(!processor.IsInitialized(), "processor starts uninitialized");

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        Expect(snapshot.bypassed, "reset snapshot is bypassed");
        Expect(
            snapshot.configurationValid,
            "reset snapshot has a valid fallback configuration"
        );
        Expect(
            !snapshot.noiseSuppressionRequested,
            "reset snapshot does not request noise suppression"
        );
        Expect(
            !snapshot.noiseSuppressionActive,
            "reset snapshot has no active noise suppression"
        );
        Expect(
            !snapshot.noiseSuppressionFailed,
            "reset snapshot has no noise suppression failure"
        );
        Expect(!snapshot.agcActive, "reset snapshot has no active AGC");
        Expect(
            NearlyEqual(snapshot.agcGainDb, 0.0f),
            "reset AGC gain is zero"
        );
        Expect(NearlyEqual(snapshot.rawPeak, 0.0f), "reset raw peak is zero");
        Expect(
            NearlyEqual(snapshot.processedPeak, 0.0f),
            "reset processed peak is zero"
        );
    }

    void TestDisabledProcessingIsTransparent()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings;
        settings.enabled = false;

        Expect(processor.Initialize(settings), "valid settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input[0] = 0.5f;
        input[1] = -0.25f;

        Expect(
            processor.ProcessBlock(input, output),
            "a complete block is accepted"
        );
        Expect(input == output, "disabled processing is sample-transparent");

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        const float expectedRms = std::sqrt(
            (0.5f * 0.5f + 0.25f * 0.25f) /
            static_cast<float>(MicrophoneProcessor::SamplesPerBlock)
        );

        Expect(snapshot.bypassed, "disabled processing reports bypass");
        Expect(
            snapshot.configurationValid,
            "valid settings are reported as valid"
        );
        Expect(NearlyEqual(snapshot.rawPeak, 0.5f), "raw peak is measured");
        Expect(
            NearlyEqual(snapshot.processedPeak, 0.5f),
            "processed peak is measured"
        );
        Expect(
            NearlyEqual(snapshot.rawRms, expectedRms),
            "raw RMS is measured"
        );
        Expect(
            NearlyEqual(snapshot.processedRms, expectedRms),
            "processed RMS is measured"
        );
        Expect(!snapshot.inputClipped, "safe input is not clipped");
        Expect(
            !snapshot.invalidSampleDetected,
            "finite input is accepted"
        );
    }

    MicrophoneProcessingSettings NativeStageSettings()
    {
        MicrophoneProcessingSettings settings;
        settings.enabled = true;
        settings.noiseSuppressionEnabled = false;
        settings.agcEnabled = false;
        return settings;
    }

    VoiceEffectSettings ActiveNeutralVoiceEffectSettings()
    {
        VoiceEffectSettings settings;
        settings.enabled = true;
        settings.bypassed = false;
        settings.preset = VoiceEffectPreset::Custom;
        settings.pitchSemitones = 0.0f;
        settings.formantSemitones = 0.0f;
        settings.character = 0.0f;
        settings.body = 0.0f;
        settings.drive = 0.0f;
        settings.dryWet = 0.0f;
        settings.outputGainDb = 0.0f;
        return settings;
    }

    void TestEnabledWithoutNativeStagesIsTransparent()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = false;
        settings.compressorEnabled = false;
        settings.limiterEnabled = false;

        Expect(processor.Initialize(settings), "enabled settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> samples{};
        samples.fill(0.125f);
        const auto original = samples;

        Expect(
            processor.ProcessBlock(samples, samples),
            "in-place processing is supported"
        );
        Expect(
            samples == original,
            "no native stages is sample-transparent"
        );
        Expect(
            processor.GetSnapshot().bypassed,
            "no native stages reports bypass"
        );
    }

    void TestHighPassRejectsDc()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = true;
        settings.highPassHz = 80.0f;
        settings.compressorEnabled = false;
        settings.limiterEnabled = false;

        Expect(processor.Initialize(settings), "high-pass settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input.fill(0.5f);

        for (int block = 0; block < 100; ++block)
        {
            Expect(
                processor.ProcessBlock(input, output),
                "high-pass DC block is processed"
            );
        }

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        Expect(
            snapshot.processedRms < 0.001f,
            "high-pass strongly rejects settled DC"
        );
        Expect(!snapshot.bypassed, "high-pass reports active processing");
    }

    void TestHighPassPreservesVoiceBandTone()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = true;
        settings.highPassHz = 80.0f;
        settings.compressorEnabled = false;
        settings.limiterEnabled = false;

        Expect(processor.Initialize(settings), "voice-band settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        constexpr float frequency = 1000.0f;
        constexpr float amplitude = 0.25f;
        constexpr float twoPi = 6.28318530717958647692f;

        for (std::size_t index = 0; index < input.size(); ++index)
        {
            input[index] = amplitude * std::sin(
                twoPi * frequency * static_cast<float>(index) /
                static_cast<float>(MicrophoneProcessor::ProcessingSampleRate)
            );
        }

        for (int block = 0; block < 4; ++block)
        {
            Expect(
                processor.ProcessBlock(input, output),
                "voice-band tone block is processed"
            );
        }

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        Expect(
            snapshot.processedRms > snapshot.rawRms * 0.98f,
            "80 Hz high-pass preserves a 1 kHz tone"
        );
        Expect(
            snapshot.processedRms < snapshot.rawRms * 1.02f,
            "high-pass does not boost the voice-band tone"
        );
    }

    void TestNoiseSuppressionProcessesCompleteFrames()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = false;
        settings.noiseSuppressionEnabled = true;
        settings.noiseSuppressionLevel =
            MicrophoneNoiseSuppressionLevel::Balanced;
        settings.compressorEnabled = false;
        settings.limiterEnabled = false;

        Expect(
            processor.Initialize(settings),
            "noise suppression settings initialize"
        );

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        float phase = 0.0f;
        std::uint32_t randomState = 0x12345678U;
        bool outputChanged = false;
        constexpr float twoPi = 6.28318530717958647692f;

        for (int block = 0; block < 8; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                randomState = randomState * 1664525U + 1013904223U;
                const float noise =
                    (static_cast<float>((randomState >> 8U) & 0xFFFFU) /
                        32767.5f - 1.0f) * 0.08f;
                input[index] = 0.12f * std::sin(phase) + noise;
                phase += twoPi * 220.0f /
                    static_cast<float>(
                        MicrophoneProcessor::ProcessingSampleRate
                    );

                if (phase >= twoPi)
                {
                    phase -= twoPi;
                }
            }

            Expect(
                processor.ProcessBlock(input, output),
                "noise suppression frame is processed"
            );

            for (std::size_t index = 0; index < output.size(); ++index)
            {
                Expect(
                    std::isfinite(output[index]),
                    "noise suppression output stays finite"
                );
                outputChanged = outputChanged ||
                    !NearlyEqual(output[index], input[index], 0.000001f);
            }
        }

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        Expect(
            snapshot.noiseSuppressionRequested,
            "snapshot reports requested noise suppression"
        );
        Expect(
            snapshot.noiseSuppressionActive,
            "snapshot reports active noise suppression"
        );
        Expect(
            !snapshot.noiseSuppressionFailed,
            "noise suppression completes without fallback"
        );
        Expect(!snapshot.bypassed, "noise suppression is a processing stage");
        Expect(outputChanged, "noise suppression changes the input signal");
        Expect(
            snapshot.voiceActivityProbability >= 0.0f &&
                snapshot.voiceActivityProbability <= 1.0f,
            "voice activity probability stays normalized"
        );
    }

    void TestAgcRaisesQuietSpeechTowardTarget()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = false;
        settings.agcEnabled = true;
        settings.agcTargetDbfs = -18.0f;
        settings.compressorEnabled = false;
        settings.limiterEnabled = false;

        Expect(processor.Initialize(settings), "quiet AGC settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input.fill(0.05f);

        for (int block = 0; block < 200; ++block)
        {
            Expect(
                processor.ProcessBlock(input, output),
                "quiet AGC block is processed"
            );
        }

        const MicrophoneProcessingSnapshot snapshot = processor.GetSnapshot();
        Expect(snapshot.agcActive, "AGC is reported as active");
        Expect(
            snapshot.agcGainDb > 7.0f && snapshot.agcGainDb < 8.5f,
            "AGC converges near the requested quiet-speech gain"
        );
        Expect(
            snapshot.processedRms > 0.11f && snapshot.processedRms < 0.14f,
            "AGC raises quiet speech toward the target"
        );
        Expect(!snapshot.bypassed, "AGC is a processing stage");
    }

    void TestAgcAttenuatesLoudSpeechWithinLimit()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = false;
        settings.agcEnabled = true;
        settings.agcTargetDbfs = -18.0f;
        settings.compressorEnabled = false;
        settings.limiterEnabled = false;

        Expect(processor.Initialize(settings), "loud AGC settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input.fill(0.5f);

        for (int block = 0; block < 100; ++block)
        {
            Expect(
                processor.ProcessBlock(input, output),
                "loud AGC block is processed"
            );
        }

        const MicrophoneProcessingSnapshot snapshot = processor.GetSnapshot();
        Expect(
            snapshot.agcGainDb >= MicrophoneProcessor::MinimumAgcGainDb &&
                snapshot.agcGainDb < -11.0f,
            "AGC attenuation respects its lower gain limit"
        );
        Expect(
            snapshot.processedRms > 0.12f && snapshot.processedRms < 0.14f,
            "AGC attenuates sustained loud speech"
        );
    }

    void TestAgcDoesNotRaiseSilenceNoiseFloor()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = false;
        settings.agcEnabled = true;
        settings.agcTargetDbfs = -3.0f;
        settings.compressorEnabled = false;
        settings.limiterEnabled = false;

        Expect(processor.Initialize(settings), "silence AGC settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input.fill(0.0001f);

        for (int block = 0; block < 200; ++block)
        {
            Expect(
                processor.ProcessBlock(input, output),
                "silence AGC block is processed"
            );
        }

        const MicrophoneProcessingSnapshot snapshot = processor.GetSnapshot();
        Expect(snapshot.agcActive, "AGC remains enabled during silence");
        Expect(
            NearlyEqual(snapshot.agcGainDb, 0.0f, 0.01f),
            "AGC does not increase gain below the silence threshold"
        );
        Expect(
            NearlyEqual(snapshot.processedRms, 0.0001f, 0.000001f),
            "silence-level input is not amplified"
        );
    }

    void TestAgcBoostIsCapped()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = false;
        settings.agcEnabled = true;
        settings.agcTargetDbfs = -3.0f;
        settings.compressorEnabled = false;
        settings.limiterEnabled = false;

        Expect(processor.Initialize(settings), "AGC cap settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input.fill(0.002f);

        for (int block = 0; block < 300; ++block)
        {
            Expect(
                processor.ProcessBlock(input, output),
                "AGC cap block is processed"
            );
        }

        const MicrophoneProcessingSnapshot snapshot = processor.GetSnapshot();
        Expect(
            snapshot.agcGainDb <= MicrophoneProcessor::MaximumAgcGainDb &&
                snapshot.agcGainDb > 17.0f,
            "AGC boost respects its upper gain limit"
        );
    }

    void TestCompressorReducesSustainedLoudSignal()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = false;
        settings.compressorEnabled = true;
        settings.compressorThresholdDb = -24.0f;
        settings.compressorRatio = 4.0f;
        settings.compressorAttackMs = 1.0f;
        settings.compressorReleaseMs = 100.0f;
        settings.compressorMakeupDb = 0.0f;
        settings.limiterEnabled = false;

        Expect(processor.Initialize(settings), "compressor settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input.fill(0.5f);

        for (int block = 0; block < 20; ++block)
        {
            Expect(
                processor.ProcessBlock(input, output),
                "compressor loud block is processed"
            );
        }

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        Expect(
            snapshot.processedRms < 0.14f,
            "compressor reduces sustained loud speech"
        );
        Expect(
            snapshot.processedRms > 0.08f,
            "compressor output remains within the expected range"
        );
    }

    void TestCompressorLeavesQuietSignalNearUnity()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = false;
        settings.compressorEnabled = true;
        settings.compressorThresholdDb = -24.0f;
        settings.compressorRatio = 4.0f;
        settings.compressorAttackMs = 1.0f;
        settings.compressorReleaseMs = 100.0f;
        settings.compressorMakeupDb = 0.0f;
        settings.limiterEnabled = false;

        Expect(processor.Initialize(settings), "quiet compressor settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input.fill(0.02f);

        Expect(
            processor.ProcessBlock(input, output),
            "compressor quiet block is processed"
        );
        Expect(
            NearlyEqual(output.back(), 0.02f, 0.0001f),
            "compressor leaves below-threshold signal near unity"
        );
    }

    void TestLimiterEnforcesCeiling()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings = NativeStageSettings();
        settings.highPassEnabled = false;
        settings.compressorEnabled = false;
        settings.limiterEnabled = true;
        settings.limiterCeilingDb = -6.0f;

        Expect(processor.Initialize(settings), "limiter settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input[0] = 0.9f;
        input[1] = -0.9f;
        input[2] = 0.25f;

        Expect(
            processor.ProcessBlock(input, output),
            "limiter block is processed"
        );

        const float ceiling = std::pow(10.0f, -6.0f / 20.0f);
        Expect(
            NearlyEqual(output[0], ceiling),
            "positive peak is limited to the ceiling"
        );
        Expect(
            NearlyEqual(output[1], -ceiling),
            "negative peak is limited to the ceiling"
        );
        Expect(
            NearlyEqual(output[2], 0.25f),
            "signal below the ceiling is unchanged"
        );
        Expect(
            processor.GetSnapshot().processedPeak <= ceiling + 0.00001f,
            "published processed peak respects the limiter ceiling"
        );
    }

    void TestInvalidConfigurationFallsBackSafely()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings;
        settings.compressorRatio =
            std::numeric_limits<float>::quiet_NaN();

        Expect(
            !processor.Initialize(settings),
            "invalid initial settings are rejected"
        );
        Expect(
            processor.IsInitialized(),
            "invalid initialization still creates a safe bypass runtime"
        );

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input[0] = 0.25f;

        Expect(
            processor.ProcessBlock(input, output),
            "safe fallback still handles audio"
        );
        Expect(input == output, "safe fallback is transparent");

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        Expect(snapshot.bypassed, "invalid configuration is bypassed");
        Expect(
            !snapshot.configurationValid,
            "invalid initial configuration is visible in the snapshot"
        );
    }

    void TestInvalidUpdatesAreTransactional()
    {
        MicrophoneProcessor processor;
        MicrophoneProcessingSettings settings;
        Expect(processor.Initialize(settings), "valid baseline initializes");

        MicrophoneProcessingSettings invalid = settings;
        invalid.highPassHz = 1000.0f;
        Expect(
            !processor.UpdateSettings(invalid),
            "invalid update is rejected"
        );

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input[0] = -0.75f;
        Expect(
            processor.ProcessBlock(input, output),
            "processor remains usable after a rejected update"
        );
        Expect(input == output, "rejected update does not corrupt bypass");
        Expect(
            processor.GetSnapshot().configurationValid,
            "the last accepted configuration remains active"
        );
    }

    void TestInvalidBuffersAreRejected()
    {
        MicrophoneProcessor processor;
        Expect(
            processor.Initialize({}),
            "default settings initialize for buffer tests"
        );

        std::array<float, MicrophoneProcessor::SamplesPerBlock - 1> shortInput{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        output.fill(0.75f);
        const auto originalOutput = output;

        Expect(
            !processor.ProcessBlock(shortInput, output),
            "short input is rejected"
        );
        Expect(
            output == originalOutput,
            "rejected input does not modify output"
        );
    }

    void TestVoiceEffectOutputGainRunsAfterAgc()
    {
        MicrophoneProcessingSettings processing = NativeStageSettings();
        processing.highPassEnabled = false;
        processing.agcEnabled = true;
        processing.agcTargetDbfs = -18.0f;
        processing.compressorEnabled = false;
        processing.limiterEnabled = false;

        MicrophoneProcessor unityProcessor;
        MicrophoneProcessor boostedProcessor;
        Expect(unityProcessor.Initialize(processing),
            "unity final-gain processor initializes");
        Expect(boostedProcessor.Initialize(processing),
            "boosted final-gain processor initializes");

        VoiceEffectSettings unity = ActiveNeutralVoiceEffectSettings();
        VoiceEffectSettings boosted = unity;
        boosted.outputGainDb = 6.0f;
        Expect(unityProcessor.UpdateVoiceEffectSettings(unity),
            "unity final-gain settings update");
        Expect(boostedProcessor.UpdateVoiceEffectSettings(boosted),
            "boosted final-gain settings update");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> unityOutput{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> boostedOutput{};
        input.fill(0.05f);

        for (int block = 0; block < 240; ++block)
        {
            Expect(unityProcessor.ProcessBlock(input, unityOutput),
                "unity AGC/final-gain block is processed");
            Expect(boostedProcessor.ProcessBlock(input, boostedOutput),
                "boosted AGC/final-gain block is processed");
        }

        const MicrophoneProcessingSnapshot unitySnapshot =
            unityProcessor.GetSnapshot();
        const MicrophoneProcessingSnapshot boostedSnapshot =
            boostedProcessor.GetSnapshot();
        Expect(
            std::abs(unitySnapshot.agcGainDb - boostedSnapshot.agcGainDb) <
                0.05f,
            "output gain does not change the AGC detector or gain"
        );
        const float gainRatio = boostedSnapshot.processedRms /
            std::max(unitySnapshot.processedRms, 0.000001f);
        Expect(gainRatio > 1.94f && gainRatio < 2.05f,
            "six dB output gain remains after AGC");
    }

    void TestVoiceEffectOutputGainRunsBeforeLimiter()
    {
        MicrophoneProcessingSettings processing = NativeStageSettings();
        processing.highPassEnabled = false;
        processing.compressorEnabled = false;
        processing.limiterEnabled = true;
        processing.limiterCeilingDb = -6.0f;

        MicrophoneProcessor processor;
        Expect(processor.Initialize(processing),
            "limited final-gain processor initializes");

        VoiceEffectSettings settings = ActiveNeutralVoiceEffectSettings();
        settings.outputGainDb = 12.0f;
        Expect(processor.UpdateVoiceEffectSettings(settings),
            "limited final-gain settings update");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input.fill(0.40f);

        for (int block = 0; block < 20; ++block)
        {
            Expect(processor.ProcessBlock(input, output),
                "limited final-gain block is processed");
        }

        const float ceiling = std::pow(10.0f, -6.0f / 20.0f);
        const float outputPeak = *std::max_element(
            output.begin(),
            output.end()
        );
        Expect(outputPeak <= ceiling + 0.00001f,
            "limiter contains positive output gain");
        Expect(outputPeak >= ceiling - 0.00001f,
            "output gain reaches the limiter ceiling");
    }

    void TestVoiceEffectSettingsUpdateAtBlockBoundary()
    {
        MicrophoneProcessor processor;
        Expect(processor.Initialize({}), "default settings initialize");

        const auto settings = BuildVoiceEffectPreset(
            VoiceEffectPreset::Radio,
            true
        );
        Expect(settings.has_value(), "radio preset exists");
        if (!settings.has_value())
        {
            return;
        }

        VoiceEffectSettings enabled = *settings;
        enabled.bypassed = true;
        Expect(
            processor.UpdateVoiceEffectSettings(enabled),
            "valid voice-effect settings update is accepted"
        );

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input[0] = 0.25f;
        input[1] = -0.125f;

        Expect(
            processor.ProcessBlock(input, output),
            "voice-effect settings are observed on the next block"
        );
        Expect(input == output,
            "the infrastructure stage remains transparent");

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        Expect(snapshot.voiceEffectsEnabled,
            "snapshot reports enabled voice effects");
        Expect(snapshot.voiceEffectsBypassed,
            "snapshot reports the independent effect bypass");
        Expect(snapshot.voiceEffectPreset == VoiceEffectPreset::Radio,
            "snapshot reports the applied preset");
        Expect(snapshot.bypassed,
            "the no-op infrastructure stage does not force mono routing");

        VoiceEffectSettings invalid = enabled;
        invalid.formantSemitones = 7.0f;
        Expect(
            !processor.UpdateVoiceEffectSettings(invalid),
            "invalid voice-effect settings update is rejected"
        );
    }

    void TestNonFiniteAndClippedSamplesAreContained()
    {
        MicrophoneProcessor processor;
        Expect(processor.Initialize({}), "default settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input[0] = std::numeric_limits<float>::infinity();
        input[1] = 1.25f;
        input[2] = -0.5f;

        Expect(
            processor.ProcessBlock(input, output),
            "mixed-quality input is handled"
        );
        Expect(
            NearlyEqual(output[0], 0.0f),
            "non-finite input is replaced with silence"
        );
        Expect(
            NearlyEqual(output[1], 1.25f),
            "finite samples remain unchanged in bypass"
        );

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        Expect(
            snapshot.invalidSampleDetected,
            "non-finite input is reported"
        );
        Expect(snapshot.inputClipped, "out-of-range input is reported");
        Expect(NearlyEqual(snapshot.rawPeak, 1.25f), "peak ignores infinity");
    }

    void TestResetClearsPublishedState()
    {
        MicrophoneProcessor processor;
        Expect(processor.Initialize({}), "default settings initialize");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        input[0] = 0.5f;
        Expect(processor.ProcessBlock(input, output), "block is processed");

        processor.Reset();
        Expect(!processor.IsInitialized(), "reset clears initialization");

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        Expect(NearlyEqual(snapshot.rawPeak, 0.0f), "reset clears raw peak");
        Expect(
            NearlyEqual(snapshot.processedPeak, 0.0f),
            "reset clears processed peak"
        );
        Expect(snapshot.bypassed, "reset restores bypass state");
    }
}

int main()
{
    TestConstantsAndResetState();
    TestDisabledProcessingIsTransparent();
    TestEnabledWithoutNativeStagesIsTransparent();
    TestHighPassRejectsDc();
    TestHighPassPreservesVoiceBandTone();
    TestNoiseSuppressionProcessesCompleteFrames();
    TestAgcRaisesQuietSpeechTowardTarget();
    TestAgcAttenuatesLoudSpeechWithinLimit();
    TestAgcDoesNotRaiseSilenceNoiseFloor();
    TestAgcBoostIsCapped();
    TestCompressorReducesSustainedLoudSignal();
    TestCompressorLeavesQuietSignalNearUnity();
    TestLimiterEnforcesCeiling();
    TestInvalidConfigurationFallsBackSafely();
    TestInvalidUpdatesAreTransactional();
    TestInvalidBuffersAreRejected();
    TestVoiceEffectOutputGainRunsAfterAgc();
    TestVoiceEffectOutputGainRunsBeforeLimiter();
    TestVoiceEffectSettingsUpdateAtBlockBoundary();
    TestNonFiniteAndClippedSamplesAreContained();
    TestResetClearsPublishedState();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Microphone processor tests passed.\n";
    return 0;
}
