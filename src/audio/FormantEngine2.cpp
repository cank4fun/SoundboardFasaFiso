#include "audio/FormantEngine2.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float SemitonesPerOctave = 12.0f;
    constexpr float FrequencyPerBin =
        static_cast<float>(FormantEngine2::ProcessingSampleRate) /
        static_cast<float>(FormantEngine2::FrameSize);
}

FormantEngine2::FormantEngine2() noexcept
{
    Reset();
}

bool FormantEngine2::PrepareFrame(
    const std::span<const float> magnitudeSpectrum,
    const std::span<const float> spectralEnvelope,
    const SpeechAnalysisFrame& analysis,
    const float pitchRatio,
    const float formantRatio
) noexcept
{
    if (magnitudeSpectrum.size() != BinCount ||
        spectralEnvelope.size() != BinCount)
    {
        return false;
    }

    const float safePitchRatio = std::clamp(
        std::isfinite(pitchRatio) && pitchRatio > 0.0f
            ? pitchRatio
            : 1.0f,
        MinimumRatio,
        MaximumRatio
    );
    const float safeFormantRatio = std::clamp(
        std::isfinite(formantRatio) && formantRatio > 0.0f
            ? formantRatio
            : 1.0f,
        MinimumRatio,
        MaximumRatio
    );

    // The correction is neutral when pitch and formants move together.
    // Independent movement warps the source envelope at each destination bin.
    const float relativeRatio = safeFormantRatio / safePitchRatio;
    const float relativeSemitones = std::abs(
        SemitonesPerOctave * std::log2(relativeRatio)
    );
    const float shiftActivity = SmoothStep(std::clamp(
        (relativeSemitones - MinimumActiveSemitones) /
            (FullActiveSemitones - MinimumActiveSemitones),
        0.0f,
        1.0f
    ));

    const float speechActivity = std::clamp(
        Sanitize(analysis.speechActivity),
        0.0f,
        1.0f
    );
    const float voicingConfidence = std::clamp(
        Sanitize(analysis.voicingConfidence),
        0.0f,
        1.0f
    );
    const float unvoicedProbability = std::clamp(
        Sanitize(analysis.unvoicedProbability),
        0.0f,
        1.0f
    );
    const float transientProbability = std::clamp(
        Sanitize(analysis.transientProbability),
        0.0f,
        1.0f
    );

    // Vowels accept the full warp; consonants and onsets retain their
    // broadband identity instead of inheriting narrow resonant boosts.
    const float periodicSupport = 0.42f + voicingConfidence * 0.58f;
    const float unvoicedProtection = 1.0f - unvoicedProbability * 0.72f;
    const float transientProtection = 1.0f - transientProbability * 0.82f;
    currentStrength_ = std::clamp(
        shiftActivity * speechActivity * periodicSupport *
            unvoicedProtection * transientProtection,
        0.0f,
        1.0f
    );

    float maximumEnvelope = MinimumMagnitude;
    float maximumMagnitude = MinimumMagnitude;
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        maximumEnvelope = std::max(
            maximumEnvelope,
            std::max(Sanitize(spectralEnvelope[bin]), 0.0f)
        );
        maximumMagnitude = std::max(
            maximumMagnitude,
            std::max(Sanitize(magnitudeSpectrum[bin]), 0.0f)
        );
    }
    const float envelopeFloor = std::max(
        MinimumMagnitude,
        maximumEnvelope * 0.00012f
    );
    const float reliabilityFloor = maximumMagnitude * 0.004f;

    const float minimumLogCorrection = std::log(MinimumCorrection);
    const float maximumLogCorrection = std::log(MaximumCorrection);
    double originalEnergy = 0.0;
    double correctedEnergy = 0.0;
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const float frequencyHz = static_cast<float>(bin) * FrequencyPerBin;
        const float destinationBin = static_cast<float>(bin) * safePitchRatio;
        const float targetEnvelopeBin = destinationBin / safeFormantRatio;
        const float sourceEnvelope = std::max(
            Sanitize(spectralEnvelope[bin]),
            envelopeFloor
        );
        const float targetEnvelope = std::max(
            SampleEnvelope(spectralEnvelope, targetEnvelopeBin),
            envelopeFloor
        );

        const float rawLogCorrection = std::clamp(
            std::log(targetEnvelope / sourceEnvelope) *
                EnvelopeCorrectionStrength,
            minimumLogCorrection,
            maximumLogCorrection
        );
        const float magnitude = std::max(
            Sanitize(magnitudeSpectrum[bin]),
            0.0f
        );
        const float reliability = SmoothStep(std::clamp(
            magnitude / std::max(reliabilityFloor, MinimumMagnitude),
            0.0f,
            1.0f
        ));
        // Sparse/quiet bins may be attenuated, but they cannot receive the
        // same boost as a resolved harmonic and expose the analysis floor.
        const float boostReliability = rawLogCorrection > 0.0f
            ? 0.30f + reliability * 0.70f
            : 0.62f + reliability * 0.38f;
        const float binStrength = currentStrength_ *
            FrequencyWeight(frequencyHz) * boostReliability;
        targetLogCorrection_[bin] = rawLogCorrection * binStrength;

        const double energy = static_cast<double>(magnitude) *
            static_cast<double>(magnitude);
        const double correction = std::exp(
            static_cast<double>(targetLogCorrection_[bin])
        );
        originalEnergy += energy;
        correctedEnergy += energy * correction * correction;
    }

    // Partial frame-energy normalization keeps presets level-compatible
    // without cancelling the intended spectral-envelope movement.
    float targetEnergyGain = 1.0f;
    if (originalEnergy > static_cast<double>(MinimumMagnitude) &&
        correctedEnergy > static_cast<double>(MinimumMagnitude))
    {
        targetEnergyGain = std::clamp(
            static_cast<float>(std::sqrt(originalEnergy / correctedEnergy)),
            MinimumEnergyGain,
            MaximumEnergyGain
        );
        targetEnergyGain = std::pow(
            targetEnergyGain,
            EnergyNormalizationStrength * currentStrength_
        );
    }
    currentEnergyGain_ = targetEnergyGain;
    const float energyLogGain = std::log(targetEnergyGain);

    // Smooth in log magnitude so neighbouring bins remain coherent and
    // frame-to-frame changes cannot create zippering or isolated resonances.
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const std::size_t first = bin > 2U ? bin - 2U : 0U;
        const std::size_t last = std::min(BinCount - 1U, bin + 2U);
        float weightedSum = 0.0f;
        float weightSum = 0.0f;
        for (std::size_t neighbour = first; neighbour <= last; ++neighbour)
        {
            const std::size_t distance = neighbour > bin
                ? neighbour - bin
                : bin - neighbour;
            const float weight = distance == 0U
                ? 3.0f
                : distance == 1U ? 2.0f : 1.0f;
            weightedSum += targetLogCorrection_[neighbour] * weight;
            weightSum += weight;
        }
        spatialLogCorrection_[bin] = weightedSum /
            std::max(weightSum, 1.0f) + energyLogGain;
    }

    const float temporalCoefficient = analysis.onset
        ? OnsetTemporalCoefficient
        : 0.0f;
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const float target = spatialLogCorrection_[bin];
        if (!hasPreparedFrame_)
        {
            smoothedLogCorrection_[bin] = target;
        }
        else
        {
            const float regularCoefficient =
                std::abs(target) > std::abs(smoothedLogCorrection_[bin])
                    ? TemporalAttack
                    : TemporalRelease;
            const float coefficient = std::max(
                regularCoefficient,
                temporalCoefficient
            );
            smoothedLogCorrection_[bin] += coefficient *
                (target - smoothedLogCorrection_[bin]);
        }

        corrections_[bin] = std::clamp(
            std::exp(smoothedLogCorrection_[bin]),
            MinimumCorrection,
            MaximumCorrection
        );
    }

    hasPreparedFrame_ = true;
    return true;
}

float FormantEngine2::CorrectionForBin(const std::size_t bin) const noexcept
{
    return bin < BinCount ? corrections_[bin] : 1.0f;
}

std::span<const float> FormantEngine2::Corrections() const noexcept
{
    return corrections_;
}

float FormantEngine2::CurrentStrength() const noexcept
{
    return currentStrength_;
}

float FormantEngine2::CurrentEnergyGain() const noexcept
{
    return currentEnergyGain_;
}

void FormantEngine2::Reset() noexcept
{
    targetLogCorrection_.fill(0.0f);
    spatialLogCorrection_.fill(0.0f);
    smoothedLogCorrection_.fill(0.0f);
    corrections_.fill(1.0f);
    currentStrength_ = 0.0f;
    currentEnergyGain_ = 1.0f;
    hasPreparedFrame_ = false;
}

float FormantEngine2::SampleEnvelope(
    const std::span<const float> envelope,
    const float bin
) noexcept
{
    const float boundedBin = std::clamp(
        Sanitize(bin),
        0.0f,
        static_cast<float>(BinCount - 1U)
    );
    const std::size_t lower = static_cast<std::size_t>(boundedBin);
    const std::size_t upper = std::min(lower + 1U, BinCount - 1U);
    const float fraction = boundedBin - static_cast<float>(lower);
    return std::lerp(
        std::max(Sanitize(envelope[lower]), 0.0f),
        std::max(Sanitize(envelope[upper]), 0.0f),
        fraction
    );
}

float FormantEngine2::FrequencyWeight(const float frequencyHz) noexcept
{
    const float lowWeight = SmoothStep(std::clamp(
        (frequencyHz - LowFrequencyFadeStartHz) /
            (LowFrequencyFadeEndHz - LowFrequencyFadeStartHz),
        0.0f,
        1.0f
    ));
    const float highWeight = 1.0f - SmoothStep(std::clamp(
        (frequencyHz - HighFrequencyFadeStartHz) /
            (HighFrequencyFadeEndHz - HighFrequencyFadeStartHz),
        0.0f,
        1.0f
    ));
    return lowWeight * highWeight;
}

float FormantEngine2::SmoothStep(const float value) noexcept
{
    const float bounded = std::clamp(value, 0.0f, 1.0f);
    return bounded * bounded * (3.0f - 2.0f * bounded);
}

float FormantEngine2::Sanitize(const float value) noexcept
{
    return std::isfinite(value) ? value : 0.0f;
}
