#include "audio/MicrophoneProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    constexpr float MinimumEnvelope = 0.000001f;
    constexpr float AgcGainReductionMilliseconds = 80.0f;
    constexpr float AgcGainIncreaseMilliseconds = 500.0f;
    constexpr float AgcSilenceReturnMilliseconds = 200.0f;
    constexpr float AgcDeadbandDb = 0.5f;

    float DecibelsToLinear(const float decibels)
    {
        return std::pow(10.0f, decibels / 20.0f);
    }

    float LinearToDecibels(const float linear)
    {
        return 20.0f * std::log10(std::max(linear, MinimumEnvelope));
    }

    float TimeCoefficient(const float milliseconds)
    {
        const float seconds = milliseconds / 1000.0f;
        return std::exp(
            -1.0f /
            (seconds *
                static_cast<float>(
                    MicrophoneProcessor::ProcessingSampleRate
                ))
        );
    }

    float BlockTimeCoefficient(const float milliseconds)
    {
        const float seconds = milliseconds / 1000.0f;
        return std::exp(
            -static_cast<float>(MicrophoneProcessor::SamplesPerBlock) /
            (seconds *
                static_cast<float>(
                    MicrophoneProcessor::ProcessingSampleRate
                ))
        );
    }
}

bool MicrophoneProcessor::Initialize(
    const MicrophoneProcessingSettings& settings
)
{
    Reset();
    initialized_ = true;

    if (!IsValidMicrophoneProcessingSettings(settings))
    {
        configurationValid_ = false;

        MicrophoneProcessingSnapshot snapshot;
        snapshot.configurationValid = false;
        PublishSnapshot(snapshot);
        return false;
    }

    settings_ = settings;
    configurationValid_ = true;
    ConfigureProcessingState();
    return true;
}

bool MicrophoneProcessor::UpdateSettings(
    const MicrophoneProcessingSettings& settings
)
{
    if (!initialized_ || !IsValidMicrophoneProcessingSettings(settings))
    {
        return false;
    }

    settings_ = settings;
    configurationValid_ = true;
    ConfigureProcessingState();
    return true;
}

bool MicrophoneProcessor::ProcessBlock(
    const std::span<const float> input,
    const std::span<float> output
)
{
    if (!initialized_ ||
        input.size() != SamplesPerBlock ||
        output.size() != SamplesPerBlock)
    {
        return false;
    }

    float rawPeak = 0.0f;
    double rawSquareSum = 0.0;
    float processedPeak = 0.0f;
    double processedSquareSum = 0.0;
    bool inputClipped = false;
    bool invalidSampleDetected = false;

    const bool processingEnabled = settings_.enabled;
    const bool noiseSuppressionRequested = processingEnabled &&
        settings_.noiseSuppressionEnabled;

    for (std::size_t index = 0; index < SamplesPerBlock; ++index)
    {
        float sample = input[index];

        if (!std::isfinite(sample))
        {
            sample = 0.0f;
            invalidSampleDetected = true;
        }

        const float rawMagnitude = std::abs(sample);
        rawPeak = std::max(rawPeak, rawMagnitude);
        rawSquareSum += static_cast<double>(sample) *
            static_cast<double>(sample);
        inputClipped = inputClipped || rawMagnitude > 1.0f;

        if (processingEnabled && settings_.highPassEnabled)
        {
            sample = ProcessHighPass(sample);
        }

        preNoiseSuppressionBuffer_[index] = sample;
    }

    bool noiseSuppressionActive = false;

    if (noiseSuppressionRequested && noiseSuppressionAvailable_)
    {
        if (noiseSuppressor_.ProcessFrame(
                preNoiseSuppressionBuffer_,
                noiseSuppressedBuffer_
            ))
        {
            const float mix = NoiseSuppressionMix();

            for (std::size_t index = 0; index < SamplesPerBlock; ++index)
            {
                preNoiseSuppressionBuffer_[index] = std::lerp(
                    preNoiseSuppressionBuffer_[index],
                    noiseSuppressedBuffer_[index],
                    mix
                );
            }

            noiseSuppressionActive = true;
        }
        else
        {
            noiseSuppressionAvailable_ = false;
            noiseSuppressionFailed_ = true;
            noiseSuppressor_.Reset();
        }
    }

    const bool agcActive = processingEnabled && settings_.agcEnabled;
    const float previousAgcGainDb = agcGainDb_;

    if (agcActive)
    {
        double agcSquareSum = 0.0;

        for (const float sample : preNoiseSuppressionBuffer_)
        {
            agcSquareSum += static_cast<double>(sample) *
                static_cast<double>(sample);
        }

        const float agcInputRms = static_cast<float>(std::sqrt(
            agcSquareSum / static_cast<double>(SamplesPerBlock)
        ));
        UpdateAgcGain(agcInputRms);
    }
    else
    {
        agcGainDb_ = 0.0f;
    }

    for (std::size_t index = 0; index < SamplesPerBlock; ++index)
    {
        float sample = preNoiseSuppressionBuffer_[index];

        if (agcActive)
        {
            const float interpolation =
                static_cast<float>(index + 1) /
                static_cast<float>(SamplesPerBlock);
            const float gainDb = std::lerp(
                previousAgcGainDb,
                agcGainDb_,
                interpolation
            );
            sample *= DecibelsToLinear(gainDb);
        }

        if (processingEnabled && settings_.compressorEnabled)
        {
            sample = ProcessCompressor(sample);
        }

        if (processingEnabled && settings_.limiterEnabled)
        {
            sample = ProcessLimiter(sample);
        }

        output[index] = sample;

        const float processedMagnitude = std::abs(sample);
        processedPeak = std::max(processedPeak, processedMagnitude);
        processedSquareSum += static_cast<double>(sample) *
            static_cast<double>(sample);
    }

    const float rawRms = static_cast<float>(
        std::sqrt(
            rawSquareSum /
            static_cast<double>(SamplesPerBlock)
        )
    );
    const float processedRms = static_cast<float>(
        std::sqrt(
            processedSquareSum /
            static_cast<double>(SamplesPerBlock)
        )
    );

    const bool processingStageApplied = processingEnabled &&
        (settings_.highPassEnabled ||
            noiseSuppressionActive ||
            settings_.agcEnabled ||
            settings_.compressorEnabled ||
            settings_.limiterEnabled);

    MicrophoneProcessingSnapshot snapshot;
    snapshot.rawPeak = rawPeak;
    snapshot.rawRms = rawRms;
    snapshot.processedPeak = processedPeak;
    snapshot.processedRms = processedRms;
    snapshot.inputClipped = inputClipped;
    snapshot.invalidSampleDetected = invalidSampleDetected;
    snapshot.noiseSuppressionRequested = noiseSuppressionRequested;
    snapshot.noiseSuppressionActive = noiseSuppressionActive;
    snapshot.noiseSuppressionFailed = noiseSuppressionFailed_;
    snapshot.agcActive = agcActive;
    snapshot.bypassed = !processingStageApplied;
    snapshot.configurationValid = configurationValid_;
    snapshot.voiceActivityProbability = noiseSuppressionActive
        ? noiseSuppressor_.GetLastVadProbability()
        : 0.0f;
    snapshot.agcGainDb = agcActive ? agcGainDb_ : 0.0f;
    PublishSnapshot(snapshot);

    return true;
}

MicrophoneProcessingSnapshot MicrophoneProcessor::GetSnapshot() const
{
    MicrophoneProcessingSnapshot snapshot;

    for (;;)
    {
        const std::uint64_t sequenceBefore =
            snapshotSequence_.load(std::memory_order_acquire);

        if ((sequenceBefore & 1U) != 0U)
        {
            continue;
        }

        snapshot.rawPeak = rawPeak_.load(std::memory_order_relaxed);
        snapshot.rawRms = rawRms_.load(std::memory_order_relaxed);
        snapshot.processedPeak =
            processedPeak_.load(std::memory_order_relaxed);
        snapshot.processedRms =
            processedRms_.load(std::memory_order_relaxed);
        snapshot.inputClipped =
            inputClipped_.load(std::memory_order_relaxed);
        snapshot.invalidSampleDetected =
            invalidSampleDetected_.load(std::memory_order_relaxed);
        snapshot.noiseSuppressionRequested =
            noiseSuppressionRequested_.load(std::memory_order_relaxed);
        snapshot.noiseSuppressionActive =
            noiseSuppressionActive_.load(std::memory_order_relaxed);
        snapshot.noiseSuppressionFailed =
            noiseSuppressionFailedSnapshot_.load(
                std::memory_order_relaxed
            );
        snapshot.agcActive =
            agcActive_.load(std::memory_order_relaxed);
        snapshot.bypassed = bypassed_.load(std::memory_order_relaxed);
        snapshot.configurationValid =
            snapshotConfigurationValid_.load(std::memory_order_relaxed);
        snapshot.voiceActivityProbability =
            voiceActivityProbability_.load(std::memory_order_relaxed);
        snapshot.agcGainDb =
            agcGainDbSnapshot_.load(std::memory_order_relaxed);

        const std::uint64_t sequenceAfter =
            snapshotSequence_.load(std::memory_order_acquire);

        if (sequenceBefore == sequenceAfter)
        {
            return snapshot;
        }
    }
}

bool MicrophoneProcessor::IsInitialized() const noexcept
{
    return initialized_;
}

void MicrophoneProcessor::Reset()
{
    settings_ = {};
    initialized_ = false;
    configurationValid_ = true;
    noiseSuppressor_.Reset();
    noiseSuppressionAvailable_ = false;
    noiseSuppressionFailed_ = false;
    preNoiseSuppressionBuffer_.fill(0.0f);
    noiseSuppressedBuffer_.fill(0.0f);
    ResetProcessingState();
    PublishSnapshot({});
}

void MicrophoneProcessor::ConfigureProcessingState()
{
    const float sampleRate =
        static_cast<float>(ProcessingSampleRate);
    const float frequency = settings_.highPassHz;
    const float angularFrequency =
        2.0f * std::numbers::pi_v<float> * frequency / sampleRate;
    const float cosine = std::cos(angularFrequency);
    const float sine = std::sin(angularFrequency);
    const float inverseSqrtTwo = std::numbers::sqrt2_v<float> / 2.0f;
    const float alpha = sine * inverseSqrtTwo;
    const float inverseA0 = 1.0f / (1.0f + alpha);

    highPassB0_ = ((1.0f + cosine) / 2.0f) * inverseA0;
    highPassB1_ = -(1.0f + cosine) * inverseA0;
    highPassB2_ = highPassB0_;
    highPassA1_ = (-2.0f * cosine) * inverseA0;
    highPassA2_ = (1.0f - alpha) * inverseA0;

    agcGainReductionCoefficient_ =
        BlockTimeCoefficient(AgcGainReductionMilliseconds);
    agcGainIncreaseCoefficient_ =
        BlockTimeCoefficient(AgcGainIncreaseMilliseconds);
    agcSilenceReturnCoefficient_ =
        BlockTimeCoefficient(AgcSilenceReturnMilliseconds);

    compressorAttackCoefficient_ =
        TimeCoefficient(settings_.compressorAttackMs);
    compressorReleaseCoefficient_ =
        TimeCoefficient(settings_.compressorReleaseMs);
    limiterCeilingLinear_ =
        DecibelsToLinear(settings_.limiterCeilingDb);

    noiseSuppressor_.Reset();
    noiseSuppressionAvailable_ = false;
    noiseSuppressionFailed_ = false;

    if (settings_.enabled && settings_.noiseSuppressionEnabled)
    {
        noiseSuppressionAvailable_ = noiseSuppressor_.Initialize();
        noiseSuppressionFailed_ = !noiseSuppressionAvailable_;
    }

    preNoiseSuppressionBuffer_.fill(0.0f);
    noiseSuppressedBuffer_.fill(0.0f);
    ResetProcessingState();
}

void MicrophoneProcessor::ResetProcessingState()
{
    highPassState1_ = 0.0f;
    highPassState2_ = 0.0f;
    agcGainDb_ = 0.0f;
    compressorEnvelope_ = 0.0f;
}

float MicrophoneProcessor::ProcessHighPass(const float sample)
{
    const float output = highPassB0_ * sample + highPassState1_;
    highPassState1_ = highPassB1_ * sample -
        highPassA1_ * output + highPassState2_;
    highPassState2_ = highPassB2_ * sample -
        highPassA2_ * output;
    return output;
}

void MicrophoneProcessor::UpdateAgcGain(const float blockRms)
{
    const float inputDbfs = LinearToDecibels(blockRms);
    float desiredGainDb = 0.0f;
    float coefficient = agcSilenceReturnCoefficient_;

    if (inputDbfs >= AgcSilenceThresholdDbfs)
    {
        desiredGainDb = std::clamp(
            settings_.agcTargetDbfs - inputDbfs,
            MinimumAgcGainDb,
            MaximumAgcGainDb
        );

        if (std::abs(desiredGainDb - agcGainDb_) <= AgcDeadbandDb)
        {
            desiredGainDb = agcGainDb_;
        }

        coefficient = desiredGainDb < agcGainDb_
            ? agcGainReductionCoefficient_
            : agcGainIncreaseCoefficient_;
    }

    agcGainDb_ = std::clamp(
        coefficient * agcGainDb_ +
            (1.0f - coefficient) * desiredGainDb,
        MinimumAgcGainDb,
        MaximumAgcGainDb
    );
}

float MicrophoneProcessor::ProcessCompressor(const float sample)
{
    const float magnitude = std::abs(sample);
    const float coefficient = magnitude > compressorEnvelope_
        ? compressorAttackCoefficient_
        : compressorReleaseCoefficient_;

    compressorEnvelope_ = coefficient * compressorEnvelope_ +
        (1.0f - coefficient) * magnitude;

    const float envelopeDb = LinearToDecibels(compressorEnvelope_);
    float gainDb = settings_.compressorMakeupDb;

    if (envelopeDb > settings_.compressorThresholdDb)
    {
        const float compressedDb =
            settings_.compressorThresholdDb +
            (envelopeDb - settings_.compressorThresholdDb) /
                settings_.compressorRatio;
        gainDb += compressedDb - envelopeDb;
    }

    return sample * DecibelsToLinear(gainDb);
}

float MicrophoneProcessor::ProcessLimiter(const float sample) const
{
    return std::clamp(
        sample,
        -limiterCeilingLinear_,
        limiterCeilingLinear_
    );
}

float MicrophoneProcessor::NoiseSuppressionMix() const noexcept
{
    switch (settings_.noiseSuppressionLevel)
    {
        case MicrophoneNoiseSuppressionLevel::Light:
            return 0.40f;

        case MicrophoneNoiseSuppressionLevel::Balanced:
            return 0.70f;

        case MicrophoneNoiseSuppressionLevel::Strong:
            return 1.0f;

        default:
            return 0.70f;
    }
}

void MicrophoneProcessor::PublishSnapshot(
    const MicrophoneProcessingSnapshot& snapshot
)
{
    snapshotSequence_.fetch_add(1, std::memory_order_acq_rel);

    rawPeak_.store(snapshot.rawPeak, std::memory_order_relaxed);
    rawRms_.store(snapshot.rawRms, std::memory_order_relaxed);
    processedPeak_.store(
        snapshot.processedPeak,
        std::memory_order_relaxed
    );
    processedRms_.store(
        snapshot.processedRms,
        std::memory_order_relaxed
    );
    inputClipped_.store(
        snapshot.inputClipped,
        std::memory_order_relaxed
    );
    invalidSampleDetected_.store(
        snapshot.invalidSampleDetected,
        std::memory_order_relaxed
    );
    noiseSuppressionRequested_.store(
        snapshot.noiseSuppressionRequested,
        std::memory_order_relaxed
    );
    noiseSuppressionActive_.store(
        snapshot.noiseSuppressionActive,
        std::memory_order_relaxed
    );
    noiseSuppressionFailedSnapshot_.store(
        snapshot.noiseSuppressionFailed,
        std::memory_order_relaxed
    );
    agcActive_.store(snapshot.agcActive, std::memory_order_relaxed);
    bypassed_.store(snapshot.bypassed, std::memory_order_relaxed);
    snapshotConfigurationValid_.store(
        snapshot.configurationValid,
        std::memory_order_relaxed
    );
    voiceActivityProbability_.store(
        snapshot.voiceActivityProbability,
        std::memory_order_relaxed
    );
    agcGainDbSnapshot_.store(
        snapshot.agcGainDb,
        std::memory_order_relaxed
    );

    snapshotSequence_.fetch_add(1, std::memory_order_release);
}
