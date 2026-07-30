#include "audio/VoiceEffectsProcessor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <string_view>
#include <vector>

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
        const float first,
        const float second,
        const float tolerance = 0.00001f
    )
    {
        return std::abs(first - second) <= tolerance;
    }

    VoiceEffectSettings ActiveCustomSettings()
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
        settings.dryWet = 1.0f;
        settings.outputGainDb = 0.0f;
        return settings;
    }

    double ToneMagnitude(
        const std::vector<float>& samples,
        const std::size_t start,
        const std::size_t count,
        const double frequency
    )
    {
        double real = 0.0;
        double imaginary = 0.0;
        const double angularStep =
            2.0 * std::numbers::pi * frequency /
            static_cast<double>(VoiceEffectsProcessor::ProcessingSampleRate);

        for (std::size_t index = 0; index < count; ++index)
        {
            const double window = 0.5 - 0.5 * std::cos(
                2.0 * std::numbers::pi * static_cast<double>(index) /
                static_cast<double>(count - 1)
            );
            const double angle = angularStep * static_cast<double>(index);
            const double sample = static_cast<double>(samples[start + index]) *
                window;
            real += sample * std::cos(angle);
            imaginary -= sample * std::sin(angle);
        }

        return std::sqrt(real * real + imaginary * imaginary);
    }

    std::vector<float> RenderFormantProbe(const float formantSemitones)
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.formantSemitones = formantSemitones;
        settings.dryWet = 1.0f;
        Expect(processor.Initialize(settings),
            "formant probe settings initialize");

        constexpr double FundamentalFrequency = 120.0;
        constexpr std::size_t HarmonicCount = 50;
        constexpr std::size_t BlockCount = 180;
        constexpr std::size_t TotalSamples =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> rendered(TotalSamples, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::size_t sampleSequence = 0;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                double sample = 0.0;
                for (std::size_t harmonic = 1;
                    harmonic <= HarmonicCount;
                    ++harmonic)
                {
                    const double frequency = FundamentalFrequency *
                        static_cast<double>(harmonic);
                    const double firstEnvelope = std::exp(
                        -0.5 * std::pow((frequency - 950.0) / 260.0, 2.0)
                    );
                    const double secondEnvelope = std::exp(
                        -0.5 * std::pow((frequency - 2400.0) / 450.0, 2.0)
                    );
                    const double amplitude = 0.0045 *
                        (0.08 + 1.1 * firstEnvelope +
                            0.65 * secondEnvelope);
                    const double phase =
                        2.0 * std::numbers::pi * frequency *
                            static_cast<double>(sampleSequence) /
                            static_cast<double>(
                                VoiceEffectsProcessor::ProcessingSampleRate
                            ) +
                        static_cast<double>(harmonic) * 0.13;
                    sample += amplitude * std::sin(phase);
                }

                input[index] = static_cast<float>(sample);
                ++sampleSequence;
            }

            Expect(processor.ProcessBlock(input, output),
                "formant probe block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
        }

        return rendered;
    }

    double HarmonicCentroid(
        const std::vector<float>& samples,
        const std::size_t start,
        const std::size_t count
    )
    {
        constexpr double FundamentalFrequency = 120.0;
        constexpr std::size_t HarmonicCount = 50;
        double magnitudeSum = 0.0;
        double weightedFrequencySum = 0.0;

        for (std::size_t harmonic = 1;
            harmonic <= HarmonicCount;
            ++harmonic)
        {
            const double frequency = FundamentalFrequency *
                static_cast<double>(harmonic);
            const double magnitude = ToneMagnitude(
                samples,
                start,
                count,
                frequency
            );
            magnitudeSum += magnitude;
            weightedFrequencySum += magnitude * frequency;
        }

        return magnitudeSum > 0.0
            ? weightedFrequencySum / magnitudeSum
            : 0.0;
    }

    double HarmonicGridMagnitude(
        const std::vector<float>& samples,
        const std::size_t start,
        const std::size_t count,
        const double offset
    )
    {
        constexpr double FundamentalFrequency = 120.0;
        constexpr std::size_t HarmonicCount = 50;
        double magnitudeSum = 0.0;

        for (std::size_t harmonic = 1;
            harmonic <= HarmonicCount;
            ++harmonic)
        {
            magnitudeSum += ToneMagnitude(
                samples,
                start,
                count,
                FundamentalFrequency * static_cast<double>(harmonic) +
                    offset
            );
        }

        return magnitudeSum;
    }

    std::vector<float> RenderCharacterProbe(const float character)
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.character = character;
        Expect(processor.Initialize(settings),
            "character probe settings initialize");

        constexpr std::array<double, 3> Frequencies{
            150.0,
            1500.0,
            7000.0
        };
        constexpr std::size_t BlockCount = 140;
        constexpr std::size_t TotalSamples =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> rendered(TotalSamples, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::size_t sampleSequence = 0;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                double sample = 0.0;
                for (const double frequency : Frequencies)
                {
                    const double phase =
                        2.0 * std::numbers::pi * frequency *
                        static_cast<double>(sampleSequence) /
                        static_cast<double>(
                            VoiceEffectsProcessor::ProcessingSampleRate
                        );
                    sample += 0.08 * std::sin(phase);
                }
                input[index] = static_cast<float>(sample);
                ++sampleSequence;
            }

            Expect(processor.ProcessBlock(input, output),
                "character probe block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
        }

        return rendered;
    }

    std::vector<float> RenderBodyProbe(const float body)
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.body = body;
        Expect(processor.Initialize(settings),
            "body probe settings initialize");

        constexpr std::array<double, 3> Frequencies{
            140.0,
            500.0,
            1800.0
        };
        constexpr std::size_t BlockCount = 140;
        constexpr std::size_t TotalSamples =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> rendered(TotalSamples, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::size_t sampleSequence = 0;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                double sample = 0.0;
                for (const double frequency : Frequencies)
                {
                    const double phase =
                        2.0 * std::numbers::pi * frequency *
                        static_cast<double>(sampleSequence) /
                        static_cast<double>(
                            VoiceEffectsProcessor::ProcessingSampleRate
                        );
                    sample += 0.06 * std::sin(phase);
                }
                input[index] = static_cast<float>(sample);
                ++sampleSequence;
            }

            Expect(processor.ProcessBlock(input, output),
                "body probe block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
        }

        return rendered;
    }

    std::vector<float> RenderBodyVoicedProbe(const float body)
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.body = body;
        Expect(processor.Initialize(settings),
            "voiced body probe settings initialize");

        constexpr double FundamentalFrequency = 120.0;
        constexpr std::size_t HarmonicCount = 8;
        constexpr std::size_t BlockCount = 160;
        constexpr std::size_t TotalSamples =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> rendered(TotalSamples, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::size_t sampleSequence = 0;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                double sample = 0.0;
                for (std::size_t harmonic = 1;
                    harmonic <= HarmonicCount;
                    ++harmonic)
                {
                    const double frequency = FundamentalFrequency *
                        static_cast<double>(harmonic);
                    const double amplitude = 0.055 /
                        static_cast<double>(harmonic);
                    const double phase =
                        2.0 * std::numbers::pi * frequency *
                        static_cast<double>(sampleSequence) /
                        static_cast<double>(
                            VoiceEffectsProcessor::ProcessingSampleRate
                        );
                    sample += amplitude * std::sin(phase);
                }

                input[index] = static_cast<float>(sample);
                ++sampleSequence;
            }

            Expect(processor.ProcessBlock(input, output),
                "voiced body probe block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
        }

        return rendered;
    }

    std::vector<float> RenderDriveProbe(const float drive)
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.drive = drive;
        Expect(processor.Initialize(settings),
            "drive probe settings initialize");

        constexpr double Frequency = 220.0;
        constexpr std::size_t BlockCount = 140;
        constexpr std::size_t TotalSamples =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> rendered(TotalSamples, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::size_t sampleSequence = 0;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                const double phase =
                    2.0 * std::numbers::pi * Frequency *
                    static_cast<double>(sampleSequence) /
                    static_cast<double>(
                        VoiceEffectsProcessor::ProcessingSampleRate
                    );
                input[index] = 0.4f * static_cast<float>(std::sin(phase));
                ++sampleSequence;
            }

            Expect(processor.ProcessBlock(input, output),
                "drive probe block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
        }

        return rendered;
    }

    std::vector<float> RenderRadioProbe()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.preset = VoiceEffectPreset::Radio;
        Expect(processor.Initialize(settings),
            "radio probe settings initialize");

        constexpr std::array<double, 3> Frequencies{
            120.0,
            1200.0,
            7000.0
        };
        constexpr std::size_t BlockCount = 140;
        constexpr std::size_t TotalSamples =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> rendered(TotalSamples, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::size_t sampleSequence = 0;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                double sample = 0.0;
                for (const double frequency : Frequencies)
                {
                    const double phase =
                        2.0 * std::numbers::pi * frequency *
                        static_cast<double>(sampleSequence) /
                        static_cast<double>(
                            VoiceEffectsProcessor::ProcessingSampleRate
                        );
                    sample += 0.08 * std::sin(phase);
                }
                input[index] = static_cast<float>(sample);
                ++sampleSequence;
            }

            Expect(processor.ProcessBlock(input, output),
                "radio probe block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
        }

        return rendered;
    }

    std::vector<float> RenderRobotProbe()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.preset = VoiceEffectPreset::Robot;
        Expect(processor.Initialize(settings),
            "robot probe settings initialize");

        constexpr double Frequency = 440.0;
        constexpr std::size_t BlockCount = 140;
        constexpr std::size_t TotalSamples =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> rendered(TotalSamples, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::size_t sampleSequence = 0;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                const double phase =
                    2.0 * std::numbers::pi * Frequency *
                    static_cast<double>(sampleSequence) /
                    static_cast<double>(
                        VoiceEffectsProcessor::ProcessingSampleRate
                    );
                input[index] = 0.2f * static_cast<float>(std::sin(phase));
                ++sampleSequence;
            }

            Expect(processor.ProcessBlock(input, output),
                "robot probe block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
        }

        return rendered;
    }

    std::vector<float> RenderTinyImpulse(
        const VoiceEffectPreset preset
    )
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.preset = preset;
        Expect(processor.Initialize(settings),
            "tiny impulse settings initialize");

        constexpr std::size_t BlockCount = 6;
        constexpr std::size_t TotalSamples =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> rendered(TotalSamples, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        input[0] = 0.5f;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            Expect(processor.ProcessBlock(input, output),
                "tiny impulse block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
            input.fill(0.0f);
        }

        return rendered;
    }

    void TestConstantsAndResetState()
    {
        Expect(
            VoiceEffectsProcessor::ProcessingSampleRate == 48000,
            "voice effects use the 48 kHz pipeline rate"
        );
        Expect(
            VoiceEffectsProcessor::SamplesPerBlock == 480,
            "voice effects use the 10 ms pipeline block"
        );
        Expect(
            VoiceEffectsProcessor::ProcessingLatencySamples == 768,
            "the spectral core publishes its fixed 16 ms latency"
        );

        VoiceEffectsProcessor processor;
        Expect(!processor.IsInitialized(), "processor starts uninitialized");
        Expect(processor.LatencySamples() == 0,
            "an uninitialized processor reports no active latency");
    }

    void TestDisabledAndBypassedProcessingAreTransparent()
    {
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        input[0] = 0.5f;
        input[1] = -0.25f;
        input[479] = 0.125f;

        VoiceEffectsProcessor disabledProcessor;
        VoiceEffectSettings disabled;
        disabled.enabled = false;
        Expect(disabledProcessor.Initialize(disabled),
            "disabled settings initialize");
        Expect(disabledProcessor.ProcessBlock(input, output),
            "a disabled block is accepted");
        Expect(input == output,
            "disabled voice effects are sample-transparent");
        Expect(disabledProcessor.LatencySamples() == 0,
            "disabled processing reports zero active latency");

        VoiceEffectsProcessor bypassedProcessor;
        VoiceEffectSettings bypassed = ActiveCustomSettings();
        bypassed.bypassed = true;
        Expect(bypassedProcessor.Initialize(bypassed),
            "bypassed settings initialize");
        output.fill(0.0f);
        Expect(bypassedProcessor.ProcessBlock(input, output),
            "a bypassed block is accepted");
        Expect(input == output,
            "the independent bypass is sample-transparent");
        Expect(bypassedProcessor.LatencySamples() == 0,
            "bypassed processing reports zero active latency");
    }

    void TestOutputGainIsDeferredToFinalMicrophoneStage()
    {
        VoiceEffectSettings unitySettings = ActiveCustomSettings();
        unitySettings.dryWet = 0.0f;
        unitySettings.outputGainDb = 0.0f;
        VoiceEffectSettings boostedSettings = unitySettings;
        boostedSettings.outputGainDb = 12.0f;

        VoiceEffectsProcessor unityProcessor;
        VoiceEffectsProcessor boostedProcessor;
        Expect(unityProcessor.Initialize(unitySettings),
            "unity output-gain settings initialize");
        Expect(boostedProcessor.Initialize(boostedSettings),
            "boosted output-gain settings initialize");

        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> unity{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> boosted{};
        input.fill(0.125f);

        for (int block = 0; block < 8; ++block)
        {
            Expect(unityProcessor.ProcessBlock(input, unity),
                "unity output-gain block is processed");
            Expect(boostedProcessor.ProcessBlock(input, boosted),
                "boosted output-gain block is processed");
            Expect(unity == boosted,
                "effect processor defers output gain to the final chain");
        }
    }

    void TestNeutralPitchUsesAlignedDryLatency()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.pitchSemitones = 0.0f;
        settings.dryWet = 0.0f;
        Expect(processor.Initialize(settings),
            "neutral active settings initialize");
        Expect(
            processor.LatencySamples() ==
                VoiceEffectsProcessor::ProcessingLatencySamples,
            "active processing reports fixed latency"
        );

        constexpr std::size_t BlockCount = 5;
        std::vector<float> rendered(
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock,
            0.0f
        );
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        input[0] = 0.5f;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            Expect(processor.ProcessBlock(input, output),
                "neutral latency block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
            input.fill(0.0f);
        }

        for (std::size_t index = 0; index < rendered.size(); ++index)
        {
            const float expected =
                index == VoiceEffectsProcessor::ProcessingLatencySamples
                    ? 0.5f
                    : 0.0f;
            Expect(NearlyEqual(rendered[index], expected),
                "dry/wet alignment preserves a single delayed impulse");
        }
    }

    void TestOctavePitchShiftMovesToneEnergy()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.pitchSemitones = 12.0f;
        settings.dryWet = 1.0f;
        Expect(processor.Initialize(settings),
            "octave-up settings initialize");

        constexpr double InputFrequency = 220.0;
        constexpr double ExpectedFrequency = 440.0;
        constexpr std::size_t BlockCount = 140;
        constexpr std::size_t TotalSamples =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> rendered(TotalSamples, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::size_t sampleSequence = 0;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                const double phase =
                    2.0 * std::numbers::pi * InputFrequency *
                    static_cast<double>(sampleSequence) /
                    static_cast<double>(
                        VoiceEffectsProcessor::ProcessingSampleRate
                    );
                input[index] = 0.2f * static_cast<float>(std::sin(phase));
                ++sampleSequence;
            }

            Expect(processor.ProcessBlock(input, output),
                "octave-up block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
        }

        constexpr std::size_t AnalysisCount = 24000;
        const std::size_t analysisStart = rendered.size() - AnalysisCount;
        const double inputMagnitude = ToneMagnitude(
            rendered,
            analysisStart,
            AnalysisCount,
            InputFrequency
        );
        const double shiftedMagnitude = ToneMagnitude(
            rendered,
            analysisStart,
            AnalysisCount,
            ExpectedFrequency
        );

        Expect(shiftedMagnitude > inputMagnitude * 2.0,
            "an octave-up setting moves dominant energy toward 440 Hz");
        Expect(shiftedMagnitude > 100.0,
            "the shifted tone remains materially present");
    }

    void TestHybridSpeechPitchKeepsVoicedHarmonicsCoherent()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.pitchSemitones = 2.0f;
        settings.formantSemitones = 2.0f;
        Expect(processor.Initialize(settings),
            "hybrid voiced-pitch settings initialize");

        constexpr double FundamentalFrequency = 150.0;
        constexpr double PitchRatio = 1.122462048309373;
        constexpr double ShiftedFrequency =
            FundamentalFrequency * PitchRatio;
        constexpr std::size_t BlockCount = 180;
        constexpr std::size_t TotalSamples =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> rendered(TotalSamples, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::size_t sampleSequence = 0;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                double sample = 0.0;
                for (std::size_t harmonic = 1; harmonic <= 18; ++harmonic)
                {
                    const double frequency = FundamentalFrequency *
                        static_cast<double>(harmonic);
                    const double phase = 2.0 * std::numbers::pi *
                        frequency * static_cast<double>(sampleSequence) /
                        static_cast<double>(
                            VoiceEffectsProcessor::ProcessingSampleRate
                        );
                    sample += 0.035 / static_cast<double>(harmonic) *
                        std::sin(phase);
                }
                input[index] = static_cast<float>(sample);
                ++sampleSequence;
            }

            Expect(processor.ProcessBlock(input, output),
                "hybrid voiced-pitch block is processed");
            std::copy(
                output.begin(),
                output.end(),
                rendered.begin() + static_cast<std::ptrdiff_t>(
                    block * VoiceEffectsProcessor::SamplesPerBlock
                )
            );
        }

        constexpr std::size_t AnalysisCount = 24000;
        const std::size_t start = rendered.size() - AnalysisCount;
        const std::size_t targetLag = static_cast<std::size_t>(std::lround(
            static_cast<double>(VoiceEffectsProcessor::ProcessingSampleRate) /
                ShiftedFrequency
        ));
        double cross = 0.0;
        double firstEnergy = 0.0;
        double secondEnergy = 0.0;
        for (std::size_t index = targetLag; index < AnalysisCount; ++index)
        {
            const double first = rendered[start + index];
            const double second = rendered[start + index - targetLag];
            cross += first * second;
            firstEnergy += first * first;
            secondEnergy += second * second;
        }
        const double periodicCorrelation = cross / std::sqrt(
            std::max(firstEnergy * secondEnergy, 0.000000000001)
        );

        Expect(periodicCorrelation > 0.58,
            "hybrid speech pitch keeps a coherent voiced waveform");
    }

    void TestIndependentFormantShiftMovesSpectralEnvelope()
    {
        const std::vector<float> lowFormant = RenderFormantProbe(-6.0f);
        const std::vector<float> highFormant = RenderFormantProbe(6.0f);
        constexpr std::size_t AnalysisCount = 24000;
        const std::size_t lowStart = lowFormant.size() - AnalysisCount;
        const std::size_t highStart = highFormant.size() - AnalysisCount;

        const double lowCentroid = HarmonicCentroid(
            lowFormant,
            lowStart,
            AnalysisCount
        );
        const double highCentroid = HarmonicCentroid(
            highFormant,
            highStart,
            AnalysisCount
        );
        Expect(highCentroid > lowCentroid * 1.25,
            "positive formant shift raises the harmonic envelope centroid");

        const double lowOnGrid = HarmonicGridMagnitude(
            lowFormant,
            lowStart,
            AnalysisCount,
            0.0
        );
        const double lowOffGrid = HarmonicGridMagnitude(
            lowFormant,
            lowStart,
            AnalysisCount,
            60.0
        );
        const double highOnGrid = HarmonicGridMagnitude(
            highFormant,
            highStart,
            AnalysisCount,
            0.0
        );
        const double highOffGrid = HarmonicGridMagnitude(
            highFormant,
            highStart,
            AnalysisCount,
            60.0
        );
        Expect(lowOnGrid > lowOffGrid * 20.0,
            "negative formant shift preserves the original pitch grid");
        Expect(highOnGrid > highOffGrid * 20.0,
            "positive formant shift preserves the original pitch grid");
    }

    void TestCharacterFocusesTheSpeechBand()
    {
        const std::vector<float> neutral = RenderCharacterProbe(0.0f);
        const std::vector<float> focused = RenderCharacterProbe(1.0f);
        constexpr std::size_t AnalysisCount = 24000;
        const std::size_t neutralStart = neutral.size() - AnalysisCount;
        const std::size_t focusedStart = focused.size() - AnalysisCount;

        const double neutralLow = ToneMagnitude(
            neutral,
            neutralStart,
            AnalysisCount,
            150.0
        );
        const double neutralMid = ToneMagnitude(
            neutral,
            neutralStart,
            AnalysisCount,
            1500.0
        );
        const double neutralHigh = ToneMagnitude(
            neutral,
            neutralStart,
            AnalysisCount,
            7000.0
        );
        const double focusedLow = ToneMagnitude(
            focused,
            focusedStart,
            AnalysisCount,
            150.0
        );
        const double focusedMid = ToneMagnitude(
            focused,
            focusedStart,
            AnalysisCount,
            1500.0
        );
        const double focusedHigh = ToneMagnitude(
            focused,
            focusedStart,
            AnalysisCount,
            7000.0
        );

        Expect(focusedLow < neutralLow * 0.80,
            "neutral-formant character reduces low-band energy");
        Expect(focusedMid > neutralMid * 1.25,
            "neutral-formant character raises speech-band energy");
        Expect(focusedHigh < neutralHigh * 0.85,
            "neutral-formant character softens high-band energy");
    }

    void TestBodyAddsChestWithoutABoxyTube()
    {
        const std::vector<float> neutral = RenderBodyProbe(0.0f);
        const std::vector<float> thick = RenderBodyProbe(1.0f);
        constexpr std::size_t AnalysisCount = 24000;
        const std::size_t neutralStart = neutral.size() - AnalysisCount;
        const std::size_t thickStart = thick.size() - AnalysisCount;

        const double neutralBody = ToneMagnitude(
            neutral, neutralStart, AnalysisCount, 140.0
        );
        const double neutralBox = ToneMagnitude(
            neutral, neutralStart, AnalysisCount, 500.0
        );
        const double neutralPresence = ToneMagnitude(
            neutral, neutralStart, AnalysisCount, 1800.0
        );
        const double thickBody = ToneMagnitude(
            thick, thickStart, AnalysisCount, 140.0
        );
        const double thickBox = ToneMagnitude(
            thick, thickStart, AnalysisCount, 500.0
        );
        const double thickPresence = ToneMagnitude(
            thick, thickStart, AnalysisCount, 1800.0
        );

        Expect(thickBody > neutralBody * 1.35,
            "body control reinforces the chest band");
        Expect(thickBox < neutralBox * 0.90,
            "body control does not inflate the box band");
        Expect(thickPresence > neutralPresence * 0.90,
            "body control preserves speech presence");
    }

    void TestBodyWeightPreservesPitchPeriodicity()
    {
        const std::vector<float> neutral = RenderBodyVoicedProbe(0.0f);
        const std::vector<float> weighted = RenderBodyVoicedProbe(0.85f);
        constexpr std::size_t AnalysisCount = 24000;
        const std::size_t neutralStart = neutral.size() - AnalysisCount;
        const std::size_t weightedStart = weighted.size() - AnalysisCount;

        const double neutralFundamental = ToneMagnitude(
            neutral, neutralStart, AnalysisCount, 120.0
        );
        const double weightedFundamental = ToneMagnitude(
            weighted, weightedStart, AnalysisCount, 120.0
        );
        const double weightedInharmonicOne = ToneMagnitude(
            weighted, weightedStart, AnalysisCount, 180.0
        );
        const double weightedInharmonicTwo = ToneMagnitude(
            weighted, weightedStart, AnalysisCount, 300.0
        );

        Expect(weightedFundamental > neutralFundamental * 1.30,
            "vocal weight reinforces the original fundamental");
        Expect(weightedInharmonicOne < weightedFundamental * 0.015,
            "vocal weight does not create a metallic lower sideband");
        Expect(weightedInharmonicTwo < weightedFundamental * 0.015,
            "vocal weight does not create a metallic upper sideband");
    }

    void TestDriveAddsControlledOddHarmonics()
    {
        const std::vector<float> neutral = RenderDriveProbe(0.0f);
        const std::vector<float> driven = RenderDriveProbe(1.0f);
        constexpr std::size_t AnalysisCount = 24000;
        const std::size_t neutralStart = neutral.size() - AnalysisCount;
        const std::size_t drivenStart = driven.size() - AnalysisCount;

        const double neutralThird = ToneMagnitude(
            neutral,
            neutralStart,
            AnalysisCount,
            660.0
        );
        const double drivenFundamental = ToneMagnitude(
            driven,
            drivenStart,
            AnalysisCount,
            220.0
        );
        const double drivenThird = ToneMagnitude(
            driven,
            drivenStart,
            AnalysisCount,
            660.0
        );

        Expect(drivenThird > neutralThird * 20.0,
            "drive adds a measurable third harmonic");
        Expect(drivenThird > drivenFundamental * 0.05,
            "drive harmonic content remains audible but controlled");
        Expect(drivenFundamental > 100.0,
            "drive preserves a material fundamental component");
    }

    void TestRadioPresetAppliesSpeechBandPass()
    {
        const std::vector<float> radio = RenderRadioProbe();
        constexpr std::size_t AnalysisCount = 24000;
        const std::size_t analysisStart = radio.size() - AnalysisCount;

        const double low = ToneMagnitude(
            radio,
            analysisStart,
            AnalysisCount,
            120.0
        );
        const double mid = ToneMagnitude(
            radio,
            analysisStart,
            AnalysisCount,
            1200.0
        );
        const double high = ToneMagnitude(
            radio,
            analysisStart,
            AnalysisCount,
            7000.0
        );

        Expect(mid > low * 3.0,
            "radio preset suppresses low frequencies below speech");
        Expect(mid > high * 3.0,
            "radio preset suppresses high frequencies above speech");
    }

    void TestRobotPresetCreatesCarrierSidebands()
    {
        const std::vector<float> robot = RenderRobotProbe();
        constexpr std::size_t AnalysisCount = 24000;
        const std::size_t analysisStart = robot.size() - AnalysisCount;

        const double fundamental = ToneMagnitude(
            robot,
            analysisStart,
            AnalysisCount,
            440.0
        );
        const double lowerSideband = ToneMagnitude(
            robot,
            analysisStart,
            AnalysisCount,
            345.0
        );
        const double upperSideband = ToneMagnitude(
            robot,
            analysisStart,
            AnalysisCount,
            535.0
        );

        Expect(lowerSideband > fundamental * 1.5,
            "robot preset creates the lower ring-modulation sideband");
        Expect(upperSideband > fundamental * 1.5,
            "robot preset creates the upper ring-modulation sideband");
    }

    void TestTinyPresetAddsACompactDoublerTail()
    {
        const std::vector<float> neutral = RenderTinyImpulse(
            VoiceEffectPreset::Custom
        );
        const std::vector<float> tiny = RenderTinyImpulse(
            VoiceEffectPreset::TinyHighVoice
        );
        constexpr std::size_t TailStart =
            VoiceEffectsProcessor::ProcessingLatencySamples + 480;
        constexpr std::size_t TailEnd =
            VoiceEffectsProcessor::ProcessingLatencySamples + 720;
        float neutralTailPeak = 0.0f;
        float tinyTailPeak = 0.0f;

        for (std::size_t index = TailStart; index < TailEnd; ++index)
        {
            neutralTailPeak = std::max(
                neutralTailPeak,
                std::abs(neutral[index])
            );
            tinyTailPeak = std::max(
                tinyTailPeak,
                std::abs(tiny[index])
            );
        }

        Expect(neutralTailPeak < 0.00001f,
            "neutral processing has no short doubler tail");
        Expect(tinyTailPeak > 0.01f && tinyTailPeak < 0.03f,
            "tiny/high retains a subtle modulated doubler tail");
    }

    void TestSettingsUpdateIsSmoothedAndFinite()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.preset = VoiceEffectPreset::Radio;
        settings.pitchSemitones = -12.0f;
        Expect(processor.Initialize(settings),
            "initial smoothing settings initialize");

        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        for (std::size_t index = 0; index < input.size(); ++index)
        {
            input[index] = 0.2f * std::sin(
                0.03125f * static_cast<float>(index)
            );
        }

        Expect(processor.ProcessBlock(input, output),
            "the first smoothed block is processed");

        settings.pitchSemitones = 12.0f;
        settings.formantSemitones = 6.0f;
        settings.character = 1.0f;
        settings.body = 1.0f;
        settings.drive = 1.0f;
        settings.preset = VoiceEffectPreset::Robot;
        settings.dryWet = 0.35f;
        settings.outputGainDb = -3.0f;
        Expect(processor.UpdateSettings(settings),
            "a live pitch/mix/gain update is accepted");
        Expect(processor.ProcessBlock(input, output),
            "the updated block is processed");

        for (const float sample : output)
        {
            Expect(std::isfinite(sample),
                "a live parameter transition remains finite");
        }
    }

    void TestSilenceRemainsFinite()
    {
        VoiceEffectsProcessor processor;
        const auto settings = BuildVoiceEffectPreset(
            VoiceEffectPreset::Robot,
            true
        );
        Expect(settings.has_value(), "robot preset exists");
        if (!settings.has_value())
        {
            return;
        }

        Expect(processor.Initialize(*settings), "robot settings initialize");

        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        output.fill(std::numeric_limits<float>::quiet_NaN());

        Expect(processor.ProcessBlock(input, output), "silence is processed");
        for (const float sample : output)
        {
            Expect(std::isfinite(sample), "silence output remains finite");
            Expect(sample == 0.0f, "silence output remains zero");
        }
    }

    void TestInvalidUpdatesAreTransactional()
    {
        VoiceEffectsProcessor processor;
        const auto settings = BuildVoiceEffectPreset(
            VoiceEffectPreset::DarkVocal,
            true
        );
        Expect(settings.has_value(), "dark-vocal preset exists");
        if (!settings.has_value())
        {
            return;
        }

        Expect(processor.Initialize(*settings), "initial settings initialize");
        const VoiceEffectSettings accepted = processor.GetSettings();

        VoiceEffectSettings invalid = accepted;
        invalid.pitchSemitones = 13.0f;
        Expect(!processor.UpdateSettings(invalid),
            "invalid update is rejected");
        Expect(
            processor.GetSettings().pitchSemitones ==
                accepted.pitchSemitones,
            "rejected update preserves the accepted settings"
        );
    }

    void TestInvalidBuffersAreRejected()
    {
        VoiceEffectsProcessor processor;
        Expect(processor.Initialize({}), "default settings initialize");

        std::array<float, VoiceEffectsProcessor::SamplesPerBlock - 1>
            shortInput{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        output.fill(0.75f);
        const auto originalOutput = output;

        Expect(
            !processor.ProcessBlock(shortInput, output),
            "short input is rejected"
        );
        Expect(output == originalOutput,
            "rejected input does not modify output");
    }

    void TestUnvoicedNoiseRetainsNaturalDryTiming()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        settings.pitchSemitones = 4.0f;
        settings.formantSemitones = 2.0f;
        Expect(processor.Initialize(settings),
            "unvoiced preservation settings initialize");

        constexpr std::size_t BlockCount = 120;
        constexpr std::size_t SampleCount =
            BlockCount * VoiceEffectsProcessor::SamplesPerBlock;
        std::vector<float> inputHistory(SampleCount, 0.0f);
        std::vector<float> outputHistory(SampleCount, 0.0f);
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> input{};
        std::array<float, VoiceEffectsProcessor::SamplesPerBlock> output{};
        std::uint32_t randomState = 0x31415926U;
        float previousNoise = 0.0f;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            for (std::size_t index = 0; index < input.size(); ++index)
            {
                randomState ^= randomState << 13U;
                randomState ^= randomState >> 17U;
                randomState ^= randomState << 5U;
                const float noise =
                    static_cast<float>(randomState & 0x00FFFFFFU) /
                        static_cast<float>(0x00FFFFFFU) * 2.0f - 1.0f;
                const float highPassed = noise - previousNoise * 0.85f;
                previousNoise = noise;
                input[index] = highPassed * 0.08f;
                inputHistory[block * input.size() + index] = input[index];
            }

            Expect(processor.ProcessBlock(input, output),
                "unvoiced preservation block is processed");
            std::copy(
                output.begin(),
                output.end(),
                outputHistory.begin() + static_cast<std::ptrdiff_t>(
                    block * output.size()
                )
            );
        }

        constexpr std::size_t WarmupSamples =
            30 * VoiceEffectsProcessor::SamplesPerBlock;
        constexpr std::size_t Latency =
            VoiceEffectsProcessor::ProcessingLatencySamples;
        double cross = 0.0;
        double dryEnergy = 0.0;
        double outputEnergy = 0.0;
        for (std::size_t index = WarmupSamples;
            index < SampleCount;
            ++index)
        {
            const double dry = inputHistory[index - Latency];
            const double wet = outputHistory[index];
            cross += dry * wet;
            dryEnergy += dry * dry;
            outputEnergy += wet * wet;
        }

        const double correlation = cross / std::sqrt(
            std::max(dryEnergy * outputEnergy, 0.000000000001)
        );
        Expect(correlation > 0.75,
            "unvoiced consonant energy keeps natural dry timing");
    }

    void TestResetClearsState()
    {
        VoiceEffectsProcessor processor;
        VoiceEffectSettings settings = ActiveCustomSettings();
        Expect(processor.Initialize(settings),
            "active settings initialize before reset");
        processor.Reset();

        Expect(!processor.IsInitialized(), "reset clears initialization");
        Expect(!processor.GetSettings().enabled,
            "reset restores disabled settings");
        Expect(processor.LatencySamples() == 0,
            "reset clears active latency");
    }
}

int main()
{
    TestConstantsAndResetState();
    TestDisabledAndBypassedProcessingAreTransparent();
    TestOutputGainIsDeferredToFinalMicrophoneStage();
    TestNeutralPitchUsesAlignedDryLatency();
    TestOctavePitchShiftMovesToneEnergy();
    TestHybridSpeechPitchKeepsVoicedHarmonicsCoherent();
    TestIndependentFormantShiftMovesSpectralEnvelope();
    TestCharacterFocusesTheSpeechBand();
    TestBodyAddsChestWithoutABoxyTube();
    TestBodyWeightPreservesPitchPeriodicity();
    TestDriveAddsControlledOddHarmonics();
    TestRadioPresetAppliesSpeechBandPass();
    TestRobotPresetCreatesCarrierSidebands();
    TestTinyPresetAddsACompactDoublerTail();
    TestSettingsUpdateIsSmoothedAndFinite();
    TestSilenceRemainsFinite();
    TestInvalidUpdatesAreTransactional();
    TestInvalidBuffersAreRejected();
    TestUnvoicedNoiseRetainsNaturalDryTiming();
    TestResetClearsState();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Voice-effects processor tests passed.\n";
    return 0;
}
