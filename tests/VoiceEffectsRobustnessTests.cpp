#include "audio/MicrophoneProcessor.hpp"
#include "audio/VoiceEffectsProcessor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
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

    constexpr std::array<VoiceEffectPreset, 6> BuiltInPresets{
        VoiceEffectPreset::DeepHeavy,
        VoiceEffectPreset::HighNasalRap,
        VoiceEffectPreset::DarkVocal,
        VoiceEffectPreset::Radio,
        VoiceEffectPreset::Robot,
        VoiceEffectPreset::TinyHighVoice
    };

    class DeterministicSignal
    {
    public:
        void Fill(
            std::array<float, VoiceEffectsProcessor::SamplesPerBlock>& block,
            const bool injectInvalidSamples = false
        ) noexcept
        {
            for (std::size_t index = 0; index < block.size(); ++index)
            {
                const double time = static_cast<double>(sampleSequence_) /
                    static_cast<double>(
                        VoiceEffectsProcessor::ProcessingSampleRate
                    );
                const float noise = NextNoise() * 0.015f;
                block[index] =
                    0.17f * static_cast<float>(std::sin(
                        2.0 * std::numbers::pi * 137.0 * time
                    )) +
                    0.11f * static_cast<float>(std::sin(
                        2.0 * std::numbers::pi * 431.0 * time
                    )) +
                    noise;
                ++sampleSequence_;
            }

            if (injectInvalidSamples)
            {
                block[17] = std::numeric_limits<float>::quiet_NaN();
                block[103] = std::numeric_limits<float>::infinity();
                block[271] = -std::numeric_limits<float>::infinity();
            }
        }

        void Reset() noexcept
        {
            sampleSequence_ = 0;
            randomState_ = 0x9E3779B9U;
        }

    private:
        float NextNoise() noexcept
        {
            randomState_ ^= randomState_ << 13U;
            randomState_ ^= randomState_ >> 17U;
            randomState_ ^= randomState_ << 5U;
            const float normalized = static_cast<float>(
                randomState_ & 0x00FFFFFFU
            ) / static_cast<float>(0x00FFFFFFU);
            return normalized * 2.0f - 1.0f;
        }

        std::uint64_t sampleSequence_ = 0;
        std::uint32_t randomState_ = 0x9E3779B9U;
    };

    VoiceEffectSettings RequirePreset(const VoiceEffectPreset preset)
    {
        const auto settings = BuildVoiceEffectPreset(preset, true);
        Expect(settings.has_value(), "built-in stress preset exists");
        return settings.value_or(VoiceEffectSettings{});
    }

    void TestPresetSweepRemainsFiniteAndContinuous()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = RequirePreset(
            VoiceEffectPreset::DeepHeavy
        );
        Expect(processor.Initialize(settings),
            "stress processor initializes");

        DeterministicSignal signal;
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        float previousSample = 0.0f;
        float maximumAdjacentDelta = 0.0f;
        float maximumAbsoluteSample = 0.0f;

        constexpr std::size_t BlocksPerPreset = 36;
        for (std::size_t presetIndex = 0;
            presetIndex < BuiltInPresets.size();
            ++presetIndex)
        {
            settings = RequirePreset(BuiltInPresets[presetIndex]);
            settings.dryWet = presetIndex % 2U == 0U ? 1.0f : 0.55f;
            Expect(processor.UpdateSettings(settings),
                "stress preset transition is accepted");

            for (std::size_t block = 0; block < BlocksPerPreset; ++block)
            {
                signal.Fill(input, block == 3U);
                Expect(processor.ProcessBlock(input, output),
                    "stress preset block is processed");

                for (const float sample : output)
                {
                    Expect(std::isfinite(sample),
                        "preset sweep output remains finite");
                    maximumAbsoluteSample = std::max(
                        maximumAbsoluteSample,
                        std::abs(sample)
                    );
                    maximumAdjacentDelta = std::max(
                        maximumAdjacentDelta,
                        std::abs(sample - previousSample)
                    );
                    previousSample = sample;
                }
            }
        }

        Expect(maximumAbsoluteSample < 8.0f,
            "preset sweep output remains numerically bounded");
        Expect(maximumAdjacentDelta < 3.0f,
            "smoothed preset changes avoid full-scale discontinuities");
    }

    void TestBypassConvergesToCurrentDrySignal()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = RequirePreset(
            VoiceEffectPreset::Robot
        );
        Expect(processor.Initialize(settings),
            "bypass stress processor initializes");

        DeterministicSignal signal;
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};

        for (int block = 0; block < 24; ++block)
        {
            signal.Fill(input);
            Expect(processor.ProcessBlock(input, output),
                "active pre-bypass block is processed");
        }

        settings.bypassed = true;
        Expect(processor.UpdateSettings(settings),
            "live bypass update is accepted");

        for (int block = 0; block < 48; ++block)
        {
            signal.Fill(input);
            Expect(processor.ProcessBlock(input, output),
                "bypass convergence block is processed");
        }

        float maximumDifference = 0.0f;
        for (std::size_t index = 0; index < input.size(); ++index)
        {
            maximumDifference = std::max(
                maximumDifference,
                std::abs(output[index] - input[index])
            );
        }

        Expect(maximumDifference < 0.0001f,
            "bypass converges to the current zero-latency dry signal");
        Expect(processor.LatencySamples() == 0,
            "bypassed processor reports zero active latency");
    }

    void TestResetAndReinitializeAreDeterministic()
    {
        VoiceEffectsProcessor processor;
        const VoiceEffectSettings settings = RequirePreset(
            VoiceEffectPreset::TinyHighVoice
        );
        DeterministicSignal signal;
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> reference{};

        const auto render = [&](auto& destination)
        {
            signal.Reset();
            for (int block = 0; block < 32; ++block)
            {
                signal.Fill(input);
                Expect(processor.ProcessBlock(input, output),
                    "determinism block is processed");
            }
            destination = output;
        };

        Expect(processor.Initialize(settings),
            "first deterministic initialization succeeds");
        render(reference);

        processor.Reset();
        Expect(processor.Initialize(settings),
            "second deterministic initialization succeeds");
        render(output);

        float maximumDifference = 0.0f;
        for (std::size_t index = 0; index < output.size(); ++index)
        {
            maximumDifference = std::max(
                maximumDifference,
                std::abs(output[index] - reference[index])
            );
        }

        Expect(maximumDifference < 0.000001f,
            "reset removes all hidden effect state");
    }

    void TestFinalLimiterContainsEveryBuiltInPreset()
    {
        MicrophoneProcessingSettings processing;
        processing.enabled = true;
        processing.highPassEnabled = false;
        processing.noiseSuppressionEnabled = false;
        processing.agcEnabled = false;
        processing.compressorEnabled = false;
        processing.limiterEnabled = true;
        processing.limiterCeilingDb = -1.0f;

        MicrophoneProcessor processor;
        Expect(processor.Initialize(processing),
            "limited end-to-end processor initializes");

        std::array<float, MicrophoneProcessor::SamplesPerBlock> input{};
        std::array<float, MicrophoneProcessor::SamplesPerBlock> output{};
        const float ceiling = std::pow(10.0f, -1.0f / 20.0f);
        std::uint64_t sampleSequence = 0;

        for (const VoiceEffectPreset preset : BuiltInPresets)
        {
            VoiceEffectSettings settings = RequirePreset(preset);
            settings.outputGainDb = 12.0f;
            Expect(processor.UpdateVoiceEffectSettings(settings),
                "limited preset update is accepted");

            for (int block = 0; block < 30; ++block)
            {
                for (std::size_t index = 0; index < input.size(); ++index)
                {
                    const double phase =
                        2.0 * std::numbers::pi * 220.0 *
                        static_cast<double>(sampleSequence) /
                        static_cast<double>(
                            MicrophoneProcessor::ProcessingSampleRate
                        );
                    input[index] = 0.82f * static_cast<float>(std::sin(phase));
                    ++sampleSequence;
                }

                Expect(processor.ProcessBlock(input, output),
                    "limited preset block is processed");
                for (const float sample : output)
                {
                    Expect(std::isfinite(sample),
                        "limited preset output remains finite");
                    Expect(std::abs(sample) <= ceiling + 0.00001f,
                        "final limiter contains preset output gain");
                }
            }
        }
    }

    void TestAverageProcessingTimeFitsTenMillisecondCadence()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = RequirePreset(
            VoiceEffectPreset::TinyHighVoice
        );
        settings.pitchSemitones = VoiceEffectLimits::MaximumPitchSemitones;
        settings.formantSemitones =
            VoiceEffectLimits::MaximumFormantSemitones;
        settings.character = VoiceEffectLimits::MaximumCharacter;
        settings.body = VoiceEffectLimits::MaximumBody;
        settings.drive = VoiceEffectLimits::MaximumDrive;
        Expect(processor.Initialize(settings),
            "worst-case timing settings initialize");

        DeterministicSignal signal;
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};

        constexpr std::size_t WarmupBlocks = 40;
        for (std::size_t block = 0; block < WarmupBlocks; ++block)
        {
            signal.Fill(input);
            Expect(processor.ProcessBlock(input, output),
                "timing warmup block is processed");
        }

        constexpr std::size_t MeasuredBlocks = 1200;
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t block = 0; block < MeasuredBlocks; ++block)
        {
            signal.Fill(input);
            Expect(processor.ProcessBlock(input, output),
                "timed block is processed");
        }
        const auto end = std::chrono::steady_clock::now();
        const double elapsedMicroseconds =
            std::chrono::duration<double, std::micro>(end - start).count();
        const double averageMicroseconds = elapsedMicroseconds /
            static_cast<double>(MeasuredBlocks);

        std::cout << "Voice-effects average block time: "
            << averageMicroseconds << " us / 10000 us budget\n";
        Expect(averageMicroseconds < 10000.0,
            "average DSP processing fits the 10 ms cadence");
    }
}

int main()
{
    TestPresetSweepRemainsFiniteAndContinuous();
    TestBypassConvergesToCurrentDrySignal();
    TestResetAndReinitializeAreDeterministic();
    TestFinalLimiterContainsEveryBuiltInPreset();
    TestAverageProcessingTimeFitsTenMillisecondCadence();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Voice-effects robustness tests passed.\n";
    return 0;
}
