#include "audio/SpeechAnalysisCore.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr float MinimumMagnitude = 0.000001f;
    constexpr float MinimumRmsLevel = 0.000001f;
    constexpr float MinimumNoiseFloorRms = 0.00001f;
    constexpr float MaximumNoiseFloorRms = 0.02f;
    constexpr float NoiseFloorDownwardCoefficient = 0.12f;
    constexpr float NoiseFloorUpwardCoefficient = 0.0025f;
    constexpr float SpeechActivityNoiseRatio = 2.5f;
    constexpr float SpeechActivityFullScaleRatio = 12.0f;
    constexpr float SpectrumMinimumHz = 80.0f;
    constexpr float SpectrumMaximumHz = 12000.0f;
    constexpr float HighBandStartHz = 2800.0f;
    constexpr std::size_t SpectralEnvelopeRadius = 6;
    constexpr std::size_t PitchCorrelationDecimation = 2;
    constexpr float MinimumPitchPeakCorrelation = 0.48f;
    constexpr float StrongPitchPeakRatio = 0.88f;
    constexpr float PitchContinuityPenalty = 0.025f;
    constexpr float PitchPeriodAttack = 0.24f;
    constexpr float PitchPeriodLargeJumpAttack = 0.12f;
    constexpr float VoicingAttack = 0.32f;
    constexpr float VoicingRelease = 0.10f;
    constexpr float VoicingHoldThreshold = 0.18f;
    constexpr float VoicingHoldDecay = 0.90f;
    constexpr std::size_t VoicingHoldFrameCount = 5;
    constexpr float TransientFluxThreshold = 0.14f;
    constexpr float TransientFluxRange = 0.34f;
    constexpr float OnsetEnergyRatio = 2.8f;
    constexpr float MinimumOnsetEnergy = 0.00000001f;
    constexpr float UnvoicedFlatnessStart = 0.17f;
    constexpr float UnvoicedFlatnessRange = 0.36f;
    constexpr float UnvoicedHighBandStart = 0.20f;
    constexpr float UnvoicedHighBandRange = 0.40f;
}

SpeechAnalysisCore::SpeechAnalysisCore() noexcept
{
    Reset();
}

bool SpeechAnalysisCore::AnalyzeFrame(
    const std::span<const float> timeDomainFrame,
    const std::span<const float> magnitudeSpectrum
) noexcept
{
    if (timeDomainFrame.size() != FrameSize ||
        magnitudeSpectrum.size() != BinCount)
    {
        return false;
    }

    PrepareTimeDomainFrame(timeDomainFrame);
    AnalyzeSpectrum(magnitudeSpectrum);
    AnalyzePitch();

    const float transientAmount = std::clamp(
        (normalizedSpectralFlux_ - TransientFluxThreshold) /
            TransientFluxRange,
        0.0f,
        1.0f
    ) * latestFrame_.speechActivity;

    const float frameEnergy = latestFrame_.rmsLevel * latestFrame_.rmsLevel;
    latestFrame_.onset = latestFrame_.speechActivity > 0.08f &&
        frameEnergy > MinimumOnsetEnergy &&
        (previousFrameEnergy_ <= MinimumOnsetEnergy ||
            frameEnergy > previousFrameEnergy_ * OnsetEnergyRatio ||
            transientAmount > 0.78f);
    previousFrameEnergy_ = previousFrameEnergy_ * 0.78f +
        frameEnergy * 0.22f;

    latestFrame_.transientProbability = latestFrame_.onset
        ? std::max(transientAmount, 0.82f)
        : transientAmount;

    const float flatnessAmount = std::clamp(
        (latestFrame_.spectralFlatness - UnvoicedFlatnessStart) /
            UnvoicedFlatnessRange,
        0.0f,
        1.0f
    );
    const float highBandAmount = std::clamp(
        (latestFrame_.highBandRatio - UnvoicedHighBandStart) /
            UnvoicedHighBandRange,
        0.0f,
        1.0f
    );
    const float aperiodicAmount = 1.0f - latestFrame_.voicingConfidence;
    latestFrame_.unvoicedProbability = std::clamp(
        latestFrame_.speechActivity * std::max({
            flatnessAmount * 0.82f,
            highBandAmount * 0.72f,
            aperiodicAmount * 0.38f
        }),
        0.0f,
        1.0f
    );

    return true;
}

const SpeechAnalysisFrame& SpeechAnalysisCore::LatestFrame() const noexcept
{
    return latestFrame_;
}

std::span<const float> SpeechAnalysisCore::SpectralEnvelope() const noexcept
{
    return spectralEnvelope_;
}

float SpeechAnalysisCore::SampleSpectralEnvelope(const float bin) const noexcept
{
    const float boundedBin = std::clamp(
        std::isfinite(bin) ? bin : 0.0f,
        0.0f,
        static_cast<float>(BinCount - 1U)
    );
    const std::size_t lower = static_cast<std::size_t>(boundedBin);
    const std::size_t upper = std::min(lower + 1U, BinCount - 1U);
    const float fraction = boundedBin - static_cast<float>(lower);
    return std::lerp(
        spectralEnvelope_[lower],
        spectralEnvelope_[upper],
        fraction
    );
}

void SpeechAnalysisCore::Reset() noexcept
{
    latestFrame_ = {};
    centeredFrame_.fill(0.0f);
    previousMagnitude_.fill(0.0f);
    logMagnitude_.fill(0.0f);
    smoothedLogMagnitude_.fill(0.0f);
    spectralEnvelope_.fill(MinimumMagnitude);
    pitchCorrelation_.fill(0.0f);
    noiseFloorRms_ = 0.0001f;
    smoothedPitchPeriodSamples_ = 240.0f;
    smoothedVoicingConfidence_ = 0.0f;
    previousFrameEnergy_ = 0.0f;
    normalizedSpectralFlux_ = 0.0f;
    voicingHoldFrames_ = 0;
    hasPreviousSpectrum_ = false;
}

void SpeechAnalysisCore::PrepareTimeDomainFrame(
    const std::span<const float> timeDomainFrame
) noexcept
{
    double sum = 0.0;
    for (const float sample : timeDomainFrame)
    {
        if (std::isfinite(sample))
        {
            sum += static_cast<double>(sample);
        }
    }
    const float mean = static_cast<float>(
        sum / static_cast<double>(FrameSize)
    );

    double squaredSum = 0.0;
    float peak = 0.0f;
    for (std::size_t index = 0; index < FrameSize; ++index)
    {
        const float sample = std::isfinite(timeDomainFrame[index])
            ? timeDomainFrame[index]
            : 0.0f;
        const float centered = sample - mean;
        centeredFrame_[index] = centered;
        squaredSum += static_cast<double>(centered) *
            static_cast<double>(centered);
        peak = std::max(peak, std::abs(centered));
    }

    latestFrame_.rmsLevel = static_cast<float>(std::sqrt(
        squaredSum / static_cast<double>(FrameSize)
    ));
    latestFrame_.peakLevel = peak;
    latestFrame_.speechActivity = EstimateSpeechActivity(
        latestFrame_.rmsLevel
    );
}

void SpeechAnalysisCore::AnalyzeSpectrum(
    const std::span<const float> magnitudeSpectrum
) noexcept
{
    constexpr float FrequencyPerBin =
        static_cast<float>(ProcessingSampleRate) /
        static_cast<float>(FrameSize);

    double magnitudeSum = 0.0;
    double logMagnitudeSum = 0.0;
    double weightedFrequencySum = 0.0;
    double highBandMagnitude = 0.0;
    double spectralFlux = 0.0;
    std::size_t analysisBinCount = 0;

    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const float magnitude = SanitizeMagnitude(magnitudeSpectrum[bin]);
        const float frequency = static_cast<float>(bin) * FrequencyPerBin;

        if (hasPreviousSpectrum_)
        {
            spectralFlux += static_cast<double>(std::max(
                magnitude - previousMagnitude_[bin],
                0.0f
            ));
        }
        previousMagnitude_[bin] = magnitude;

        if (frequency < SpectrumMinimumHz || frequency > SpectrumMaximumHz)
        {
            continue;
        }

        const float boundedMagnitude = std::max(magnitude, MinimumMagnitude);
        magnitudeSum += static_cast<double>(boundedMagnitude);
        logMagnitudeSum += static_cast<double>(std::log(boundedMagnitude));
        weightedFrequencySum += static_cast<double>(boundedMagnitude) *
            static_cast<double>(frequency);
        if (frequency >= HighBandStartHz)
        {
            highBandMagnitude += static_cast<double>(boundedMagnitude);
        }
        ++analysisBinCount;
    }

    normalizedSpectralFlux_ = hasPreviousSpectrum_
        ? static_cast<float>(spectralFlux /
            std::max(magnitudeSum, static_cast<double>(MinimumMagnitude)))
        : 0.0f;
    hasPreviousSpectrum_ = true;

    if (analysisBinCount == 0U)
    {
        latestFrame_.spectralFlatness = 1.0f;
        latestFrame_.spectralCentroidHz = 0.0f;
        latestFrame_.highBandRatio = 0.0f;
    }
    else
    {
        const double arithmeticMagnitude = magnitudeSum /
            static_cast<double>(analysisBinCount);
        const double geometricMagnitude = std::exp(
            logMagnitudeSum / static_cast<double>(analysisBinCount)
        );
        latestFrame_.spectralFlatness = std::clamp(
            static_cast<float>(geometricMagnitude /
                std::max(
                    arithmeticMagnitude,
                    static_cast<double>(MinimumMagnitude)
                )),
            0.0f,
            1.0f
        );
        latestFrame_.spectralCentroidHz = magnitudeSum > MinimumMagnitude
            ? static_cast<float>(weightedFrequencySum / magnitudeSum)
            : 0.0f;
        latestFrame_.highBandRatio = std::clamp(
            static_cast<float>(highBandMagnitude /
                std::max(
                    magnitudeSum,
                    static_cast<double>(MinimumMagnitude)
                )),
            0.0f,
            1.0f
        );
    }

    EstimateSpectralEnvelope(magnitudeSpectrum);
}

void SpeechAnalysisCore::AnalyzePitch() noexcept
{
    pitchCorrelation_.fill(0.0f);

    float bestCorrelation = -1.0f;
    float bestScore = -std::numeric_limits<float>::infinity();
    std::size_t bestLag = static_cast<std::size_t>(std::clamp(
        std::lround(smoothedPitchPeriodSamples_),
        static_cast<long>(MinimumPitchLag),
        static_cast<long>(MaximumPitchLag)
    ));

    if (latestFrame_.speechActivity > 0.015f &&
        latestFrame_.rmsLevel > MinimumRmsLevel)
    {
        for (std::size_t lag = MinimumPitchLag;
            lag <= MaximumPitchLag;
            ++lag)
        {
            pitchCorrelation_[lag] = NormalizedCorrelation(lag);
        }

        for (std::size_t lag = MinimumPitchLag;
            lag <= MaximumPitchLag;
            ++lag)
        {
            const float correlation = pitchCorrelation_[lag];
            const float left = lag > MinimumPitchLag
                ? pitchCorrelation_[lag - 1U]
                : correlation;
            const float right = lag < MaximumPitchLag
                ? pitchCorrelation_[lag + 1U]
                : correlation;
            if (correlation < left || correlation < right)
            {
                continue;
            }

            const float continuityPenalty = PitchContinuityPenalty *
                std::abs(std::log2(
                    static_cast<float>(lag) /
                    std::max(smoothedPitchPeriodSamples_, 1.0f)
                ));
            const float score = correlation - continuityPenalty;
            if (score > bestScore)
            {
                bestScore = score;
                bestCorrelation = correlation;
                bestLag = lag;
            }
        }

        if (bestCorrelation >= MinimumPitchPeakCorrelation)
        {
            const float strongThreshold = std::max(
                MinimumPitchPeakCorrelation,
                bestCorrelation * StrongPitchPeakRatio
            );
            for (std::size_t lag = MinimumPitchLag;
                lag <= bestLag;
                ++lag)
            {
                const float correlation = pitchCorrelation_[lag];
                const float left = lag > MinimumPitchLag
                    ? pitchCorrelation_[lag - 1U]
                    : correlation;
                const float right = lag < MaximumPitchLag
                    ? pitchCorrelation_[lag + 1U]
                    : correlation;
                if (correlation >= strongThreshold &&
                    correlation >= left && correlation >= right)
                {
                    bestCorrelation = correlation;
                    bestLag = lag;
                    break;
                }
            }
        }
    }

    float refinedLag = static_cast<float>(bestLag);
    if (bestCorrelation >= MinimumPitchPeakCorrelation &&
        bestLag > MinimumPitchLag && bestLag < MaximumPitchLag)
    {
        const float left = pitchCorrelation_[bestLag - 1U];
        const float center = pitchCorrelation_[bestLag];
        const float right = pitchCorrelation_[bestLag + 1U];
        const float denominator = left - 2.0f * center + right;
        if (std::abs(denominator) > 0.000001f)
        {
            refinedLag += std::clamp(
                0.5f * (left - right) / denominator,
                -0.5f,
                0.5f
            );
        }
    }

    const float rawPitchConfidence = std::clamp(
        (bestCorrelation - MinimumPitchPeakCorrelation) /
            (1.0f - MinimumPitchPeakCorrelation),
        0.0f,
        1.0f
    ) * latestFrame_.speechActivity;

    if (rawPitchConfidence > 0.04f)
    {
        const float periodRatio = refinedLag /
            std::max(smoothedPitchPeriodSamples_, 1.0f);
        const bool largeJump = periodRatio < 0.67f || periodRatio > 1.5f;
        const float coefficient = largeJump
            ? PitchPeriodLargeJumpAttack
            : PitchPeriodAttack;
        smoothedPitchPeriodSamples_ += coefficient *
            (refinedLag - smoothedPitchPeriodSamples_);
    }

    const float tonality = std::clamp(
        (0.58f - latestFrame_.spectralFlatness) / 0.50f,
        0.0f,
        1.0f
    );
    const float highBandPenalty = 1.0f - 0.35f * std::clamp(
        (latestFrame_.highBandRatio - 0.34f) / 0.45f,
        0.0f,
        1.0f
    );
    const float rawVoicingConfidence = rawPitchConfidence *
        (0.65f + tonality * 0.35f) * highBandPenalty;

    float voicingTarget = rawVoicingConfidence;
    if (rawVoicingConfidence >= VoicingHoldThreshold)
    {
        voicingHoldFrames_ = VoicingHoldFrameCount;
    }
    else if (voicingHoldFrames_ > 0U)
    {
        voicingTarget = std::max(
            voicingTarget,
            smoothedVoicingConfidence_ * VoicingHoldDecay
        );
        --voicingHoldFrames_;
    }

    const float voicingCoefficient =
        voicingTarget > smoothedVoicingConfidence_
            ? VoicingAttack
            : VoicingRelease;
    smoothedVoicingConfidence_ += voicingCoefficient *
        (voicingTarget - smoothedVoicingConfidence_);

    latestFrame_.pitchPeriodSamples = smoothedPitchPeriodSamples_;
    latestFrame_.pitchConfidence = rawPitchConfidence;
    latestFrame_.voicingConfidence = std::clamp(
        smoothedVoicingConfidence_,
        0.0f,
        1.0f
    );
    latestFrame_.fundamentalFrequencyHz = rawPitchConfidence > 0.02f
        ? static_cast<float>(ProcessingSampleRate) /
            std::max(smoothedPitchPeriodSamples_, 1.0f)
        : 0.0f;
}

void SpeechAnalysisCore::EstimateSpectralEnvelope(
    const std::span<const float> magnitudeSpectrum
) noexcept
{
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        logMagnitude_[bin] = std::log(std::max(
            SanitizeMagnitude(magnitudeSpectrum[bin]),
            MinimumMagnitude
        ));
    }

    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const std::size_t first = bin > SpectralEnvelopeRadius
            ? bin - SpectralEnvelopeRadius
            : 0U;
        const std::size_t last = std::min(
            BinCount - 1U,
            bin + SpectralEnvelopeRadius
        );
        double sum = 0.0;
        for (std::size_t neighbour = first; neighbour <= last; ++neighbour)
        {
            sum += static_cast<double>(logMagnitude_[neighbour]);
        }
        smoothedLogMagnitude_[bin] = static_cast<float>(
            sum / static_cast<double>(last - first + 1U)
        );
    }

    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const std::size_t first = bin > SpectralEnvelopeRadius
            ? bin - SpectralEnvelopeRadius
            : 0U;
        const std::size_t last = std::min(
            BinCount - 1U,
            bin + SpectralEnvelopeRadius
        );
        double sum = 0.0;
        for (std::size_t neighbour = first; neighbour <= last; ++neighbour)
        {
            sum += static_cast<double>(smoothedLogMagnitude_[neighbour]);
        }
        spectralEnvelope_[bin] = std::max(
            static_cast<float>(std::exp(
                sum / static_cast<double>(last - first + 1U)
            )),
            MinimumMagnitude
        );
    }
}

float SpeechAnalysisCore::NormalizedCorrelation(
    const std::size_t lag
) const noexcept
{
    double cross = 0.0;
    double firstEnergy = 0.0;
    double secondEnergy = 0.0;

    for (std::size_t index = lag;
        index < FrameSize;
        index += PitchCorrelationDecimation)
    {
        const double first = static_cast<double>(centeredFrame_[index]);
        const double second = static_cast<double>(centeredFrame_[index - lag]);
        cross += first * second;
        firstEnergy += first * first;
        secondEnergy += second * second;
    }

    const double denominator = firstEnergy + secondEnergy;
    if (denominator <= 0.000000000001)
    {
        return 0.0f;
    }

    return std::clamp(
        static_cast<float>((2.0 * cross) / denominator),
        -1.0f,
        1.0f
    );
}

float SpeechAnalysisCore::EstimateSpeechActivity(const float rmsLevel) noexcept
{
    const float boundedRms = std::clamp(
        std::isfinite(rmsLevel) ? rmsLevel : 0.0f,
        0.0f,
        4.0f
    );
    const float noiseCoefficient = boundedRms < noiseFloorRms_
        ? NoiseFloorDownwardCoefficient
        : (boundedRms < noiseFloorRms_ * 4.0f
            ? NoiseFloorUpwardCoefficient
            : 0.0f);
    noiseFloorRms_ += noiseCoefficient * (boundedRms - noiseFloorRms_);
    noiseFloorRms_ = std::clamp(
        noiseFloorRms_,
        MinimumNoiseFloorRms,
        MaximumNoiseFloorRms
    );

    const float activityFloor = std::max(
        noiseFloorRms_ * SpeechActivityNoiseRatio,
        MinimumRmsLevel
    );
    const float fullScale = activityFloor * SpeechActivityFullScaleRatio;
    return std::clamp(
        (boundedRms - activityFloor) /
            std::max(fullScale - activityFloor, MinimumRmsLevel),
        0.0f,
        1.0f
    );
}

float SpeechAnalysisCore::SanitizeMagnitude(const float magnitude) noexcept
{
    return std::isfinite(magnitude) && magnitude > 0.0f
        ? magnitude
        : 0.0f;
}
