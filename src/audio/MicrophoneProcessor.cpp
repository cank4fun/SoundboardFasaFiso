#include "audio/MicrophoneProcessor.hpp"

#include <algorithm>
#include <cmath>

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

    float peak = 0.0f;
    double squareSum = 0.0;
    bool inputClipped = false;
    bool invalidSampleDetected = false;

    for (std::size_t index = 0; index < SamplesPerBlock; ++index)
    {
        float sample = input[index];

        if (!std::isfinite(sample))
        {
            sample = 0.0f;
            invalidSampleDetected = true;
        }

        output[index] = sample;

        const float magnitude = std::abs(sample);
        peak = std::max(peak, magnitude);
        squareSum += static_cast<double>(sample) *
            static_cast<double>(sample);
        inputClipped = inputClipped || magnitude > 1.0f;
    }

    const float rms = static_cast<float>(
        std::sqrt(squareSum / static_cast<double>(SamplesPerBlock))
    );

    MicrophoneProcessingSnapshot snapshot;
    snapshot.rawPeak = peak;
    snapshot.rawRms = rms;
    snapshot.processedPeak = peak;
    snapshot.processedRms = rms;
    snapshot.inputClipped = inputClipped;
    snapshot.invalidSampleDetected = invalidSampleDetected;
    snapshot.bypassed = true;
    snapshot.configurationValid = configurationValid_;
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
        snapshot.bypassed = bypassed_.load(std::memory_order_relaxed);
        snapshot.configurationValid =
            snapshotConfigurationValid_.load(std::memory_order_relaxed);

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
    PublishSnapshot({});
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
    bypassed_.store(snapshot.bypassed, std::memory_order_relaxed);
    snapshotConfigurationValid_.store(
        snapshot.configurationValid,
        std::memory_order_relaxed
    );

    snapshotSequence_.fetch_add(1, std::memory_order_release);
}
