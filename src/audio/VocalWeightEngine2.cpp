#include "audio/VocalWeightEngine2.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    constexpr float MinimumChestCenterHz = 125.0f;
    constexpr float MaximumChestCenterHz = 235.0f;
    constexpr float MinimumPitchConfidence = 0.12f;
    constexpr float FundamentalCenterScale = 0.72f;
    constexpr float FundamentalCenterOffsetHz = 70.0f;

    constexpr float AmountSmoothingMilliseconds = 30.0f;
    constexpr float CoefficientSmoothingMilliseconds = 42.0f;
    constexpr float GateAttackMilliseconds = 10.0f;
    constexpr float GateReleaseMilliseconds = 105.0f;
    constexpr float EnvelopeAttackMilliseconds = 5.0f;
    constexpr float EnvelopeReleaseMilliseconds = 90.0f;

    constexpr float EnvelopeFloor = 0.00018f;
    constexpr float EnvelopeRange = 0.0058f;
    constexpr float MinimumNormalization = 0.0012f;
    constexpr float EnvelopeNormalization = 2.8f;
    constexpr float DensityMinimum = 0.38f;
    constexpr float DensityRange = 0.78f;
    constexpr float DensityMix = 0.56f;
    constexpr float ParallelGain = 0.92f;
    constexpr float BoxReduction = 0.48f;
    constexpr float MaximumContribution = 0.38f;

    const float AmountCoefficient =
        1.0f - std::exp(
            -1.0f /
            (AmountSmoothingMilliseconds * 0.001f *
                static_cast<float>(VocalWeightEngine2::ProcessingSampleRate))
        );
    const float FilterCoefficient =
        1.0f - std::exp(
            -1.0f /
            (CoefficientSmoothingMilliseconds * 0.001f *
                static_cast<float>(VocalWeightEngine2::ProcessingSampleRate))
        );
    const float GateAttackCoefficient =
        1.0f - std::exp(
            -1.0f /
            (GateAttackMilliseconds * 0.001f *
                static_cast<float>(VocalWeightEngine2::ProcessingSampleRate))
        );
    const float GateReleaseCoefficient =
        1.0f - std::exp(
            -1.0f /
            (GateReleaseMilliseconds * 0.001f *
                static_cast<float>(VocalWeightEngine2::ProcessingSampleRate))
        );
    const float EnvelopeAttackCoefficient =
        1.0f - std::exp(
            -1.0f /
            (EnvelopeAttackMilliseconds * 0.001f *
                static_cast<float>(VocalWeightEngine2::ProcessingSampleRate))
        );
    const float EnvelopeReleaseCoefficient =
        1.0f - std::exp(
            -1.0f /
            (EnvelopeReleaseMilliseconds * 0.001f *
                static_cast<float>(VocalWeightEngine2::ProcessingSampleRate))
        );
}

VocalWeightEngine2::VocalWeightEngine2() noexcept
{
    Reset();
}

void VocalWeightEngine2::UpdateAnalysis(
    const SpeechAnalysisFrame& analysis
) noexcept
{
    const float pitchConfidence = std::clamp(
        Sanitize(analysis.pitchConfidence),
        0.0f,
        1.0f
    );
    const float voicingConfidence = std::clamp(
        Sanitize(analysis.voicingConfidence),
        0.0f,
        1.0f
    );
    const float speechActivity = std::clamp(
        Sanitize(analysis.speechActivity),
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

    const float fundamentalFrequencyHz = Sanitize(
        analysis.fundamentalFrequencyHz
    );
    if (pitchConfidence >= MinimumPitchConfidence &&
        fundamentalFrequencyHz >= static_cast<float>(
            SpeechAnalysisCore::MinimumFundamentalFrequencyHz
        ) &&
        fundamentalFrequencyHz <= static_cast<float>(
            SpeechAnalysisCore::MaximumFundamentalFrequencyHz
        ))
    {
        targetChestCenterFrequencyHz_ = std::clamp(
            fundamentalFrequencyHz * FundamentalCenterScale +
                FundamentalCenterOffsetHz,
            MinimumChestCenterHz,
            MaximumChestCenterHz
        );
    }
    else
    {
        targetChestCenterFrequencyHz_ = DefaultChestCenterHz;
    }
    targetChestCoefficients_ = BuildCoefficients(
        targetChestCenterFrequencyHz_,
        ChestQ
    );

    const float periodicSupport = 0.20f + 0.80f * voicingConfidence;
    const float consonantProtection = 1.0f - 0.88f * unvoicedProbability;
    const float transientProtection = 1.0f - 0.82f * transientProbability;
    const float onsetProtection = analysis.onset ? 0.28f : 1.0f;
    targetVoiceGate_ = std::clamp(
        speechActivity * periodicSupport * consonantProtection *
            transientProtection * onsetProtection,
        0.0f,
        1.0f
    );
}

float VocalWeightEngine2::ProcessSample(
    const float sample,
    const float amount
) noexcept
{
    const float safeSample = Sanitize(sample);
    const float safeAmount = std::clamp(Sanitize(amount), 0.0f, 1.0f);
    smoothedAmount_ += AmountCoefficient * (safeAmount - smoothedAmount_);

    chestCoefficients_.a1 += FilterCoefficient *
        (targetChestCoefficients_.a1 - chestCoefficients_.a1);
    chestCoefficients_.a2 += FilterCoefficient *
        (targetChestCoefficients_.a2 - chestCoefficients_.a2);
    chestCoefficients_.a3 += FilterCoefficient *
        (targetChestCoefficients_.a3 - chestCoefficients_.a3);
    metrics_.chestCenterFrequencyHz += FilterCoefficient *
        (targetChestCenterFrequencyHz_ - metrics_.chestCenterFrequencyHz);

    const float chestBand = ProcessBandPass(
        safeSample,
        chestCoefficients_,
        chestFilter_
    );
    const float boxBand = ProcessBandPass(
        safeSample,
        boxCoefficients_,
        boxFilter_
    );

    const float absoluteChest = std::abs(chestBand);
    const float envelopeCoefficient = absoluteChest > chestEnvelope_
        ? EnvelopeAttackCoefficient
        : EnvelopeReleaseCoefficient;
    chestEnvelope_ += envelopeCoefficient *
        (absoluteChest - chestEnvelope_);

    const float levelGate = SmoothStep(std::clamp(
        (chestEnvelope_ - EnvelopeFloor) / EnvelopeRange,
        0.0f,
        1.0f
    ));
    const float combinedGate = std::clamp(
        std::max(targetVoiceGate_, levelGate * 0.16f),
        0.0f,
        1.0f
    );
    const float gateCoefficient = combinedGate > metrics_.voiceGate
        ? GateAttackCoefficient
        : GateReleaseCoefficient;
    metrics_.voiceGate += gateCoefficient *
        (combinedGate - metrics_.voiceGate);

    const float normalization = std::max(
        chestEnvelope_ * EnvelopeNormalization,
        MinimumNormalization
    );
    const float normalizedChest = std::clamp(
        chestBand / normalization,
        -2.0f,
        2.0f
    );
    const float density = DensityMinimum +
        smoothedAmount_ * DensityRange;
    const float denseNormalized = normalizedChest * (1.0f + density) /
        (1.0f + density * std::abs(normalizedChest));
    const float denseChest = denseNormalized * normalization;
    const float parallelChest = std::lerp(
        chestBand,
        denseChest,
        DensityMix
    );

    const float weightActivity = smoothedAmount_ * metrics_.voiceGate;
    const float chestContribution = parallelChest *
        ParallelGain * weightActivity;
    const float boxContribution = boxBand *
        BoxReduction * smoothedAmount_ *
        (0.35f + 0.65f * metrics_.voiceGate);
    metrics_.contribution = std::clamp(
        chestContribution - boxContribution,
        -MaximumContribution,
        MaximumContribution
    );
    metrics_.chestEnvelope = chestEnvelope_;

    if (smoothedAmount_ <= 0.000001f)
    {
        return safeSample;
    }
    return safeSample + metrics_.contribution;
}

const VocalWeightEngine2Metrics& VocalWeightEngine2::Metrics() const noexcept
{
    return metrics_;
}

void VocalWeightEngine2::Reset() noexcept
{
    chestCoefficients_ = BuildCoefficients(DefaultChestCenterHz, ChestQ);
    targetChestCoefficients_ = chestCoefficients_;
    boxCoefficients_ = BuildCoefficients(BoxCenterHz, BoxQ);
    chestFilter_ = {};
    boxFilter_ = {};
    metrics_ = {};
    metrics_.chestCenterFrequencyHz = DefaultChestCenterHz;
    targetChestCenterFrequencyHz_ = DefaultChestCenterHz;
    targetVoiceGate_ = 0.0f;
    smoothedAmount_ = 0.0f;
    chestEnvelope_ = 0.0f;
}

VocalWeightEngine2::StateVariableCoefficients
VocalWeightEngine2::BuildCoefficients(
    const float frequencyHz,
    const float q
) noexcept
{
    const float safeFrequency = std::clamp(
        Sanitize(frequencyHz),
        20.0f,
        static_cast<float>(ProcessingSampleRate) * 0.45f
    );
    const float safeQ = std::clamp(Sanitize(q), 0.25f, 4.0f);
    const float g = std::tan(
        std::numbers::pi_v<float> * safeFrequency /
        static_cast<float>(ProcessingSampleRate)
    );
    const float damping = 1.0f / safeQ;
    const float a1 = 1.0f / (1.0f + g * (g + damping));
    const float a2 = g * a1;
    return {
        a1,
        a2,
        g * a2
    };
}

float VocalWeightEngine2::ProcessBandPass(
    const float sample,
    const StateVariableCoefficients& coefficients,
    StateVariableFilter& state
) noexcept
{
    const float v3 = sample - state.integralTwo;
    const float v1 = coefficients.a1 * state.integralOne +
        coefficients.a2 * v3;
    const float v2 = state.integralTwo +
        coefficients.a2 * state.integralOne +
        coefficients.a3 * v3;
    state.integralOne = 2.0f * v1 - state.integralOne;
    state.integralTwo = 2.0f * v2 - state.integralTwo;
    return v1;
}

float VocalWeightEngine2::SmoothStep(const float value) noexcept
{
    const float bounded = std::clamp(value, 0.0f, 1.0f);
    return bounded * bounded * (3.0f - 2.0f * bounded);
}

float VocalWeightEngine2::Sanitize(const float value) noexcept
{
    return std::isfinite(value) ? value : 0.0f;
}
