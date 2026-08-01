#include "audio/VoiceEngineSelfTest.hpp"

#include "audio/VoiceEffectsProcessor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

namespace
{
    using Block = std::array<
        float,
        VoiceEffectsProcessor::SamplesPerBlock
    >;

    constexpr std::size_t ActiveWarmupBlocks = 28;
    constexpr std::size_t ActiveMeasuredBlocks = 72;
    constexpr std::size_t BypassSettleBlocks = 12;
    constexpr std::size_t RackWarmupBlocks = 30;
    constexpr std::size_t RackMeasuredBlocks = 60;
    constexpr float MaximumSafeOutputMagnitude = 4.0001f;

    constexpr std::uint32_t CheckMask(
        const VoiceEngineSelfTestCheck check
    ) noexcept
    {
        return static_cast<std::uint32_t>(check);
    }

    void RecordCheck(
        VoiceEngineSelfTestReport& report,
        const VoiceEngineSelfTestCheck check,
        const bool passed
    ) noexcept
    {
        const std::uint32_t mask = CheckMask(check);
        report.completedMask |= mask;
        ++report.checkCount;
        if (!passed)
        {
            report.failedMask |= mask;
            ++report.failureCount;
        }
    }

    VoiceEffectSettings BuildActiveSettings() noexcept
    {
        VoiceEffectSettings settings;
        settings.enabled = true;
        settings.bypassed = false;
        settings.preset = VoiceEffectPreset::Custom;
        settings.pitchSemitones = -3.4f;
        settings.formantSemitones = -1.1f;
        settings.character = 0.28f;
        settings.body = 0.52f;
        settings.drive = 0.08f;
        settings.dryWet = 0.92f;
        settings.outputGainDb = 0.0f;
        settings.parametricEqEnabled = true;
        settings.deEsserEnabled = true;
        settings.gateEnabled = true;
        settings.compressorEnabled = true;
        settings.eqLowGainDb = 2.8f;
        settings.eqLowFrequencyHz = 145.0f;
        settings.eqMidGainDb = -1.7f;
        settings.eqMidFrequencyHz = 1180.0f;
        settings.eqMidQ = 0.95f;
        settings.eqHighGainDb = 1.6f;
        settings.eqHighFrequencyHz = 7200.0f;
        settings.deEsserAmount = 0.42f;
        settings.gateAmount = 0.28f;
        settings.compressorAmount = 0.58f;
        settings.rackOrder = DefaultVoiceEffectRackOrder;
        return settings;
    }

    float DeterministicNoise(std::uint32_t& state) noexcept
    {
        state = state * 1664525U + 1013904223U;
        const std::uint32_t value = (state >> 8U) & 0x00FFFFFFU;
        return static_cast<float>(value) / 8388607.5f - 1.0f;
    }

    void FillVoiceBlock(
        Block& block,
        std::uint64_t& sampleSequence,
        std::uint32_t& noiseState,
        const float amplitude,
        const bool consonantBurst
    ) noexcept
    {
        constexpr float FundamentalHz = 132.0f;
        constexpr std::size_t HarmonicCount = 24;
        constexpr float SampleRate = static_cast<float>(
            VoiceEffectsProcessor::ProcessingSampleRate
        );
        constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

        for (std::size_t index = 0; index < block.size(); ++index)
        {
            const float time = static_cast<float>(sampleSequence) /
                SampleRate;
            const float vibrato = 1.0f + 0.012f * std::sin(
                TwoPi * 4.1f * time
            );
            float sample = 0.0f;

            for (std::size_t harmonic = 1;
                harmonic <= HarmonicCount;
                ++harmonic)
            {
                const float harmonicFrequency = FundamentalHz *
                    static_cast<float>(harmonic);
                const float firstFormant = std::exp(
                    -0.5f * std::pow(
                        (harmonicFrequency - 760.0f) / 260.0f,
                        2.0f
                    )
                );
                const float secondFormant = std::exp(
                    -0.5f * std::pow(
                        (harmonicFrequency - 1850.0f) / 430.0f,
                        2.0f
                    )
                );
                const float thirdFormant = std::exp(
                    -0.5f * std::pow(
                        (harmonicFrequency - 2950.0f) / 650.0f,
                        2.0f
                    )
                );
                const float envelope = 0.07f + firstFormant +
                    0.72f * secondFormant + 0.38f * thirdFormant;
                const float harmonicAmplitude = envelope /
                    std::pow(static_cast<float>(harmonic), 0.78f);
                const float phase = TwoPi * harmonicFrequency * vibrato *
                    time + static_cast<float>(harmonic) * 0.19f;
                sample += harmonicAmplitude * std::sin(phase);
            }

            float attack = 1.0f;
            if (sampleSequence < 720U)
            {
                attack = std::clamp(
                    static_cast<float>(sampleSequence) / 720.0f,
                    0.0f,
                    1.0f
                );
            }

            const float noise = DeterministicNoise(noiseState);
            const float burstEnvelope = consonantBurst && index < 112U
                ? 1.0f - static_cast<float>(index) / 112.0f
                : 0.0f;
            block[index] = amplitude * attack * sample * 0.052f +
                burstEnvelope * noise * 0.055f;
            ++sampleSequence;
        }
    }

    void AccumulateSignalMetrics(
        const Block& input,
        const Block& output,
        double& inputSquareSum,
        double& outputSquareSum,
        double& differenceSquareSum,
        std::uint64_t& sampleCount,
        float& outputPeak,
        float& maximumSampleStep,
        float& previousOutput,
        bool& finiteOutput
    ) noexcept
    {
        for (std::size_t index = 0; index < output.size(); ++index)
        {
            const float inputSample = input[index];
            const float outputSample = output[index];
            finiteOutput = finiteOutput && std::isfinite(outputSample);
            outputPeak = std::max(outputPeak, std::abs(outputSample));
            maximumSampleStep = std::max(
                maximumSampleStep,
                std::abs(outputSample - previousOutput)
            );
            previousOutput = outputSample;
            inputSquareSum += static_cast<double>(inputSample) *
                static_cast<double>(inputSample);
            outputSquareSum += static_cast<double>(outputSample) *
                static_cast<double>(outputSample);
            const double difference = static_cast<double>(outputSample) -
                static_cast<double>(inputSample);
            differenceSquareSum += difference * difference;
            ++sampleCount;
        }
    }

    float RootMeanSquare(
        const double squareSum,
        const std::uint64_t sampleCount
    ) noexcept
    {
        if (sampleCount == 0U || !std::isfinite(squareSum) ||
            squareSum < 0.0)
        {
            return 0.0f;
        }

        return static_cast<float>(std::sqrt(
            squareSum / static_cast<double>(sampleCount)
        ));
    }

    bool ProcessVoiceBlocks(
        VoiceEffectsProcessor& processor,
        const std::size_t blockCount,
        std::uint64_t& sampleSequence,
        std::uint32_t& noiseState,
        VoiceEngineSelfTestReport& report,
        const bool measure,
        double& inputSquareSum,
        double& outputSquareSum,
        double& differenceSquareSum,
        std::uint64_t& measuredSampleCount,
        float& previousOutput,
        bool& finiteOutput
    ) noexcept
    {
        Block input{};
        Block output{};

        for (std::size_t blockIndex = 0;
            blockIndex < blockCount;
            ++blockIndex)
        {
            const bool consonantBurst = blockIndex % 19U == 0U;
            FillVoiceBlock(
                input,
                sampleSequence,
                noiseState,
                0.82f,
                consonantBurst
            );
            if (!processor.ProcessBlock(input, output))
            {
                return false;
            }
            ++report.processedBlockCount;

            if (measure)
            {
                AccumulateSignalMetrics(
                    input,
                    output,
                    inputSquareSum,
                    outputSquareSum,
                    differenceSquareSum,
                    measuredSampleCount,
                    report.outputPeak,
                    report.maximumSampleStep,
                    previousOutput,
                    finiteOutput
                );
            }
            else
            {
                previousOutput = output.back();
                for (const float sample : output)
                {
                    finiteOutput = finiteOutput && std::isfinite(sample);
                    report.outputPeak = std::max(
                        report.outputPeak,
                        std::abs(sample)
                    );
                }
            }
        }

        return true;
    }

    bool TestRackOrderResponse(
        VoiceEngineSelfTestReport& report
    ) noexcept
    {
        VoiceEffectSettings firstSettings = BuildActiveSettings();
        firstSettings.pitchSemitones = 0.0f;
        firstSettings.formantSemitones = 0.0f;
        firstSettings.character = 0.0f;
        firstSettings.body = 0.0f;
        firstSettings.drive = 0.0f;
        firstSettings.dryWet = 0.0f;
        firstSettings.eqLowGainDb = 10.0f;
        firstSettings.eqMidGainDb = -8.0f;
        firstSettings.eqHighGainDb = 7.0f;
        firstSettings.deEsserAmount = 0.82f;
        firstSettings.gateAmount = 0.52f;
        firstSettings.compressorAmount = 0.95f;

        VoiceEffectSettings secondSettings = firstSettings;
        secondSettings.rackOrder = {
            VoiceEffectRackModule::Compressor,
            VoiceEffectRackModule::Gate,
            VoiceEffectRackModule::DeEsser,
            VoiceEffectRackModule::ParametricEq
        };

        VoiceEffectsProcessor firstProcessor;
        VoiceEffectsProcessor secondProcessor;
        if (!firstProcessor.Initialize(firstSettings) ||
            !secondProcessor.Initialize(secondSettings))
        {
            return false;
        }

        Block input{};
        Block firstOutput{};
        Block secondOutput{};
        std::uint64_t sampleSequence = 0;
        std::uint32_t noiseState = 0xA341316CU;
        double differenceSquareSum = 0.0;
        std::uint64_t differenceSampleCount = 0;
        bool finite = true;

        const std::size_t totalBlocks = RackWarmupBlocks +
            RackMeasuredBlocks;
        for (std::size_t blockIndex = 0;
            blockIndex < totalBlocks;
            ++blockIndex)
        {
            const float amplitude = blockIndex % 8U < 4U
                ? 0.22f
                : 1.15f;
            FillVoiceBlock(
                input,
                sampleSequence,
                noiseState,
                amplitude,
                blockIndex % 13U == 0U
            );
            if (!firstProcessor.ProcessBlock(input, firstOutput) ||
                !secondProcessor.ProcessBlock(input, secondOutput))
            {
                return false;
            }
            report.processedBlockCount += 2U;

            if (blockIndex < RackWarmupBlocks)
            {
                continue;
            }

            for (std::size_t index = 0; index < input.size(); ++index)
            {
                finite = finite && std::isfinite(firstOutput[index]) &&
                    std::isfinite(secondOutput[index]);
                const double difference = static_cast<double>(
                    firstOutput[index]
                ) - static_cast<double>(secondOutput[index]);
                differenceSquareSum += difference * difference;
                ++differenceSampleCount;
            }
        }

        report.rackDifferenceRms = RootMeanSquare(
            differenceSquareSum,
            differenceSampleCount
        );
        return finite && report.rackDifferenceRms > 0.00035f;
    }
}

VoiceEngineSelfTestReport RunVoiceEngineSelfTest() noexcept
{
    VoiceEngineSelfTestReport report;

    VoiceEffectsProcessor contractProcessor;
    Block contractInput{};
    Block contractOutput{};
    const bool contractPassed =
        VoiceEffectsProcessor::ProcessingSampleRate == 48000U &&
        VoiceEffectsProcessor::SamplesPerBlock == 480U &&
        VoiceEffectsProcessor::ProcessingLatencySamples == 768U &&
        !contractProcessor.IsInitialized() &&
        contractProcessor.LatencySamples() == 0U &&
        !contractProcessor.ProcessBlock(contractInput, contractOutput);
    RecordCheck(
        report,
        VoiceEngineSelfTestCheck::ProcessorContract,
        contractPassed
    );

    VoiceEffectSettings disabledSettings;
    disabledSettings.enabled = false;
    VoiceEffectsProcessor disabledProcessor;
    bool disabledPassed = disabledProcessor.Initialize(disabledSettings);
    for (std::size_t index = 0; index < contractInput.size(); ++index)
    {
        contractInput[index] = 0.18f * std::sin(
            2.0f * std::numbers::pi_v<float> * 431.0f *
            static_cast<float>(index) /
            static_cast<float>(VoiceEffectsProcessor::ProcessingSampleRate)
        );
    }
    disabledPassed = disabledPassed && disabledProcessor.ProcessBlock(
        contractInput,
        contractOutput
    );
    ++report.processedBlockCount;
    for (std::size_t index = 0; index < contractInput.size(); ++index)
    {
        report.disabledPathMaximumError = std::max(
            report.disabledPathMaximumError,
            std::abs(contractOutput[index] - contractInput[index])
        );
    }
    disabledPassed = disabledPassed &&
        report.disabledPathMaximumError <= 0.0000001f &&
        disabledProcessor.LatencySamples() == 0U;
    RecordCheck(
        report,
        VoiceEngineSelfTestCheck::TransparentDisabledPath,
        disabledPassed
    );

    VoiceEffectSettings activeSettings = BuildActiveSettings();
    VoiceEffectsProcessor activeProcessor;
    bool activeProcessorReady = activeProcessor.Initialize(activeSettings);
    report.latencySamples = activeProcessor.LatencySamples();

    std::uint64_t sampleSequence = 0;
    std::uint32_t noiseState = 0xC001D00DU;
    double inputSquareSum = 0.0;
    double outputSquareSum = 0.0;
    double differenceSquareSum = 0.0;
    std::uint64_t measuredSampleCount = 0;
    float previousOutput = 0.0f;
    bool finiteOutput = true;

    if (activeProcessorReady)
    {
        activeProcessorReady = ProcessVoiceBlocks(
            activeProcessor,
            ActiveWarmupBlocks,
            sampleSequence,
            noiseState,
            report,
            false,
            inputSquareSum,
            outputSquareSum,
            differenceSquareSum,
            measuredSampleCount,
            previousOutput,
            finiteOutput
        );
    }
    if (activeProcessorReady)
    {
        activeProcessorReady = ProcessVoiceBlocks(
            activeProcessor,
            ActiveMeasuredBlocks,
            sampleSequence,
            noiseState,
            report,
            true,
            inputSquareSum,
            outputSquareSum,
            differenceSquareSum,
            measuredSampleCount,
            previousOutput,
            finiteOutput
        );
    }

    report.inputRms = RootMeanSquare(inputSquareSum, measuredSampleCount);
    report.outputRms = RootMeanSquare(outputSquareSum, measuredSampleCount);
    const float differenceRms = RootMeanSquare(
        differenceSquareSum,
        measuredSampleCount
    );

    const bool activeSignalPassed = activeProcessorReady &&
        report.latencySamples ==
            VoiceEffectsProcessor::ProcessingLatencySamples &&
        report.inputRms > 0.01f &&
        report.outputRms > 0.002f &&
        differenceRms > 0.002f;
    RecordCheck(
        report,
        VoiceEngineSelfTestCheck::ActiveSignalPath,
        activeSignalPassed
    );

    const bool boundedOutputPassed = activeProcessorReady &&
        finiteOutput &&
        std::isfinite(report.outputRms) &&
        std::isfinite(report.outputPeak) &&
        report.outputPeak <= MaximumSafeOutputMagnitude &&
        report.maximumSampleStep < 3.5f;
    RecordCheck(
        report,
        VoiceEngineSelfTestCheck::FiniteBoundedOutput,
        boundedOutputPassed
    );

    VoiceEffectSettings bypassSettings = activeSettings;
    bypassSettings.bypassed = true;
    bool bypassPassed = activeProcessorReady &&
        activeProcessor.UpdateSettings(bypassSettings);
    Block bypassInput{};
    Block bypassOutput{};
    for (std::size_t blockIndex = 0;
        bypassPassed && blockIndex < BypassSettleBlocks;
        ++blockIndex)
    {
        FillVoiceBlock(
            bypassInput,
            sampleSequence,
            noiseState,
            0.75f,
            false
        );
        bypassPassed = activeProcessor.ProcessBlock(
            bypassInput,
            bypassOutput
        );
        ++report.processedBlockCount;

        if (blockIndex + 3U < BypassSettleBlocks)
        {
            continue;
        }

        for (std::size_t index = 0; index < bypassInput.size(); ++index)
        {
            report.bypassMaximumError = std::max(
                report.bypassMaximumError,
                std::abs(bypassOutput[index] - bypassInput[index])
            );
        }
    }
    bypassPassed = bypassPassed &&
        report.bypassMaximumError < 0.0025f &&
        activeProcessor.LatencySamples() == 0U;
    RecordCheck(
        report,
        VoiceEngineSelfTestCheck::DynamicBypass,
        bypassPassed
    );

    RecordCheck(
        report,
        VoiceEngineSelfTestCheck::RackOrderResponse,
        TestRackOrderResponse(report)
    );

    activeProcessor.Reset();
    bool resetPassed = !activeProcessor.IsInitialized() &&
        activeProcessor.LatencySamples() == 0U &&
        activeProcessor.Initialize(activeSettings) &&
        activeProcessor.IsInitialized() &&
        activeProcessor.LatencySamples() ==
            VoiceEffectsProcessor::ProcessingLatencySamples;
    if (resetPassed)
    {
        FillVoiceBlock(
            contractInput,
            sampleSequence,
            noiseState,
            0.70f,
            true
        );
        resetPassed = activeProcessor.ProcessBlock(
            contractInput,
            contractOutput
        );
        ++report.processedBlockCount;
        for (const float sample : contractOutput)
        {
            resetPassed = resetPassed && std::isfinite(sample) &&
                std::abs(sample) <= MaximumSafeOutputMagnitude;
        }
    }
    RecordCheck(
        report,
        VoiceEngineSelfTestCheck::ResetAndReinitialize,
        resetPassed
    );

    return report;
}
