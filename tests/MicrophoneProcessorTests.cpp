#include "audio/MicrophoneProcessor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

        MicrophoneProcessor processor;
        Expect(!processor.IsInitialized(), "processor starts uninitialized");

        const MicrophoneProcessingSnapshot snapshot =
            processor.GetSnapshot();
        Expect(snapshot.bypassed, "reset snapshot is bypassed");
        Expect(
            snapshot.configurationValid,
            "reset snapshot has a valid fallback configuration"
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
    TestCompressorReducesSustainedLoudSignal();
    TestCompressorLeavesQuietSignalNearUnity();
    TestLimiterEnforcesCeiling();
    TestInvalidConfigurationFallsBackSafely();
    TestInvalidUpdatesAreTransactional();
    TestInvalidBuffersAreRejected();
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
