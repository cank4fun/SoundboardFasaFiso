#include "audio/VoicePolishEngine2.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    float BuildTimeCoefficient(const float milliseconds) noexcept
    {
        return 1.0f - std::exp(
            -1.0f /
            (milliseconds * 0.001f *
                static_cast<float>(VoicePolishEngine2::ProcessingSampleRate))
        );
    }

    constexpr float DeEsserSplitFrequencyHz = 4200.0f;
    constexpr float MinimumEnvelope = 0.000001f;
    constexpr float MixSmoothingMilliseconds = 12.0f;
    constexpr float AmountSmoothingMilliseconds = 22.0f;
    constexpr float EqCoefficientSmoothingMilliseconds = 18.0f;
    constexpr float RackTransitionMilliseconds = 3.5f;
    constexpr float EnvelopeAttackMilliseconds = 1.8f;
    constexpr float EnvelopeReleaseMilliseconds = 65.0f;
    constexpr float DeEsserAttackMilliseconds = 1.2f;
    constexpr float DeEsserReleaseMilliseconds = 75.0f;
    constexpr float GateAttackMilliseconds = 2.5f;
    constexpr float GateReleaseMilliseconds = 135.0f;
    constexpr float GateHoldMilliseconds = 38.0f;
    constexpr float CompressorAttackMilliseconds = 7.0f;
    constexpr float CompressorReleaseMilliseconds = 115.0f;
    constexpr float MaximumDeEsserReductionDb = 12.0f;
    constexpr float MaximumGateReductionDb = 34.0f;
    constexpr float CompressorKneeDb = 5.0f;
    constexpr float MaximumOutputMagnitude = 4.0f;

    const float MixCoefficient = BuildTimeCoefficient(
        MixSmoothingMilliseconds
    );
    const float AmountCoefficient = BuildTimeCoefficient(
        AmountSmoothingMilliseconds
    );
    const float EqCoefficient = BuildTimeCoefficient(
        EqCoefficientSmoothingMilliseconds
    );
    const float RackTransitionCoefficient = BuildTimeCoefficient(
        RackTransitionMilliseconds
    );
    const float EnvelopeAttackCoefficient =
        BuildTimeCoefficient(EnvelopeAttackMilliseconds);
    const float EnvelopeReleaseCoefficient =
        BuildTimeCoefficient(EnvelopeReleaseMilliseconds);
    const float DeEsserAttackCoefficient =
        BuildTimeCoefficient(DeEsserAttackMilliseconds);
    const float DeEsserReleaseCoefficient =
        BuildTimeCoefficient(DeEsserReleaseMilliseconds);
    const float GateAttackCoefficient =
        BuildTimeCoefficient(GateAttackMilliseconds);
    const float GateReleaseCoefficient =
        BuildTimeCoefficient(GateReleaseMilliseconds);
    const float CompressorAttackCoefficient =
        BuildTimeCoefficient(CompressorAttackMilliseconds);
    const float CompressorReleaseCoefficient =
        BuildTimeCoefficient(CompressorReleaseMilliseconds);
    const float DeEsserLowPassCoefficient = 1.0f - std::exp(
        -2.0f * std::numbers::pi_v<float> * DeEsserSplitFrequencyHz /
        static_cast<float>(VoicePolishEngine2::ProcessingSampleRate)
    );
    constexpr float GateHoldSamples = GateHoldMilliseconds * 0.001f *
        static_cast<float>(VoicePolishEngine2::ProcessingSampleRate);
}

VoicePolishEngine2::VoicePolishEngine2() noexcept
{
    Reset();
}

void VoicePolishEngine2::UpdateSettings(
    const VoiceEffectSettings& settings
) noexcept
{
    const float lowGain = std::clamp(
        Sanitize(settings.eqLowGainDb),
        VoiceEffectLimits::MinimumEqGainDb,
        VoiceEffectLimits::MaximumEqGainDb
    );
    const float midGain = std::clamp(
        Sanitize(settings.eqMidGainDb),
        VoiceEffectLimits::MinimumEqGainDb,
        VoiceEffectLimits::MaximumEqGainDb
    );
    const float highGain = std::clamp(
        Sanitize(settings.eqHighGainDb),
        VoiceEffectLimits::MinimumEqGainDb,
        VoiceEffectLimits::MaximumEqGainDb
    );

    const float lowFrequencyHz = std::clamp(
        Sanitize(settings.eqLowFrequencyHz),
        VoiceEffectLimits::MinimumEqLowFrequencyHz,
        VoiceEffectLimits::MaximumEqLowFrequencyHz
    );
    const float midFrequencyHz = std::clamp(
        Sanitize(settings.eqMidFrequencyHz),
        VoiceEffectLimits::MinimumEqMidFrequencyHz,
        VoiceEffectLimits::MaximumEqMidFrequencyHz
    );
    const float midQ = std::clamp(
        Sanitize(settings.eqMidQ),
        VoiceEffectLimits::MinimumEqMidQ,
        VoiceEffectLimits::MaximumEqMidQ
    );
    const float highFrequencyHz = std::clamp(
        Sanitize(settings.eqHighFrequencyHz),
        VoiceEffectLimits::MinimumEqHighFrequencyHz,
        VoiceEffectLimits::MaximumEqHighFrequencyHz
    );

    targetLowEqCoefficients_ = BuildLowShelf(lowFrequencyHz, lowGain);
    targetMidEqCoefficients_ = BuildPeaking(
        midFrequencyHz,
        midQ,
        midGain
    );
    targetHighEqCoefficients_ = BuildHighShelf(
        highFrequencyHz,
        highGain
    );

    targetEqMix_ = settings.parametricEqEnabled ? 1.0f : 0.0f;
    targetDeEsserMix_ = settings.deEsserEnabled ? 1.0f : 0.0f;
    targetGateMix_ = settings.gateEnabled ? 1.0f : 0.0f;
    targetCompressorMix_ = settings.compressorEnabled ? 1.0f : 0.0f;
    targetDeEsserAmount_ = std::clamp(
        Sanitize(settings.deEsserAmount),
        VoiceEffectLimits::MinimumPolishAmount,
        VoiceEffectLimits::MaximumPolishAmount
    );
    targetGateAmount_ = std::clamp(
        Sanitize(settings.gateAmount),
        VoiceEffectLimits::MinimumPolishAmount,
        VoiceEffectLimits::MaximumPolishAmount
    );
    targetCompressorAmount_ = std::clamp(
        Sanitize(settings.compressorAmount),
        VoiceEffectLimits::MinimumPolishAmount,
        VoiceEffectLimits::MaximumPolishAmount
    );
    requestedRackOrder_ = IsValidVoiceEffectRackOrder(settings.rackOrder)
        ? settings.rackOrder
        : DefaultVoiceEffectRackOrder;
}

void VoicePolishEngine2::UpdateAnalysis(
    const SpeechAnalysisFrame& analysis
) noexcept
{
    const float speechActivity = std::clamp(
        Sanitize(analysis.speechActivity), 0.0f, 1.0f
    );
    const float voicing = std::clamp(
        Sanitize(analysis.voicingConfidence), 0.0f, 1.0f
    );
    const float unvoiced = std::clamp(
        Sanitize(analysis.unvoicedProbability), 0.0f, 1.0f
    );
    speechProtection_ = std::clamp(
        speechActivity * (0.55f + 0.30f * voicing + 0.15f * unvoiced),
        0.0f,
        1.0f
    );
    onsetProtection_ = analysis.onset ||
        Sanitize(analysis.transientProbability) > 0.72f;
}

float VoicePolishEngine2::ProcessSample(const float sample) noexcept
{
    const float safeSample = Sanitize(sample);
    AdvanceSmoothedSettings();
    UpdateRackTransition();

    float processed = safeSample;
    for (const VoiceEffectRackModule module : activeRackOrder_)
    {
        processed = ProcessModule(module, processed);
    }

    metrics_.gateGain = gateGain_;
    metrics_.deEsserReductionDb = -LinearToDecibels(
        std::max(deEsserGain_, MinimumEnvelope)
    );
    metrics_.compressorReductionDb = -compressorGainDb_;
    metrics_.broadbandEnvelope = broadbandEnvelope_;
    metrics_.sibilanceEnvelope = sibilanceEnvelope_;

    const float transitioned = std::lerp(
        safeSample,
        processed,
        std::clamp(rackTransitionMix_, 0.0f, 1.0f)
    );
    return std::clamp(
        transitioned,
        -MaximumOutputMagnitude,
        MaximumOutputMagnitude
    );
}

float VoicePolishEngine2::ProcessModule(
    const VoiceEffectRackModule module,
    const float sample
) noexcept
{
    switch (module)
    {
        case VoiceEffectRackModule::ParametricEq:
            return ProcessEqualizer(sample);
        case VoiceEffectRackModule::DeEsser:
            return ProcessDeEsser(sample);
        case VoiceEffectRackModule::Gate:
            return ProcessGate(sample);
        case VoiceEffectRackModule::Compressor:
            return ProcessCompressor(sample);
    }

    return sample;
}

float VoicePolishEngine2::ProcessEqualizer(const float sample) noexcept
{
    const float lowEq = ProcessBiquad(
        sample, lowEqCoefficients_, lowEqState_
    );
    const float midEq = ProcessBiquad(
        lowEq, midEqCoefficients_, midEqState_
    );
    const float equalized = ProcessBiquad(
        midEq, highEqCoefficients_, highEqState_
    );
    return std::lerp(
        sample,
        equalized,
        std::clamp(smoothedEqMix_, 0.0f, 1.0f)
    );
}

float VoicePolishEngine2::ProcessDeEsser(const float sample) noexcept
{
    deEsserLowState_ += DeEsserLowPassCoefficient *
        (sample - deEsserLowState_);
    const float sibilanceBand = sample - deEsserLowState_;
    const float absoluteBroadband = std::abs(sample);
    const float absoluteSibilance = std::abs(sibilanceBand);
    const float broadbandCoefficient = absoluteBroadband > broadbandEnvelope_
        ? EnvelopeAttackCoefficient
        : EnvelopeReleaseCoefficient;
    const float sibilanceCoefficient = absoluteSibilance > sibilanceEnvelope_
        ? EnvelopeAttackCoefficient
        : EnvelopeReleaseCoefficient;
    broadbandEnvelope_ += broadbandCoefficient *
        (absoluteBroadband - broadbandEnvelope_);
    sibilanceEnvelope_ += sibilanceCoefficient *
        (absoluteSibilance - sibilanceEnvelope_);

    const float deEsserAmount = std::clamp(
        smoothedDeEsserAmount_, 0.0f, 1.0f
    );
    const float sibilanceRatioDb = LinearToDecibels(
        sibilanceEnvelope_ / std::max(broadbandEnvelope_, MinimumEnvelope)
    );
    const float deEsserThresholdDb = std::lerp(-6.0f, -17.5f, deEsserAmount);
    const float excessSibilanceDb = std::max(
        sibilanceRatioDb - deEsserThresholdDb,
        0.0f
    );
    const float targetDeEsserReductionDb = std::clamp(
        excessSibilanceDb * (0.35f + 0.65f * deEsserAmount),
        0.0f,
        MaximumDeEsserReductionDb * deEsserAmount
    );
    const float targetDeEsserGain = DecibelsToLinear(
        -targetDeEsserReductionDb
    );
    const float deEsserGainCoefficient = targetDeEsserGain < deEsserGain_
        ? DeEsserAttackCoefficient
        : DeEsserReleaseCoefficient;
    deEsserGain_ += deEsserGainCoefficient *
        (targetDeEsserGain - deEsserGain_);
    const float deEssed = sample + sibilanceBand * (deEsserGain_ - 1.0f);
    return std::lerp(
        sample,
        deEssed,
        std::clamp(smoothedDeEsserMix_, 0.0f, 1.0f)
    );
}

float VoicePolishEngine2::ProcessGate(const float sample) noexcept
{
    const float absoluteGateInput = std::abs(sample);
    const float gateEnvelopeCoefficient = absoluteGateInput > gateEnvelope_
        ? EnvelopeAttackCoefficient
        : EnvelopeReleaseCoefficient;
    gateEnvelope_ += gateEnvelopeCoefficient *
        (absoluteGateInput - gateEnvelope_);
    const float gateAmount = std::clamp(smoothedGateAmount_, 0.0f, 1.0f);
    const float gateThresholdDb = std::lerp(-72.0f, -39.0f, gateAmount);
    const float gateInputDb = LinearToDecibels(gateEnvelope_);
    float targetGateGain = 1.0f;
    if (gateInputDb < gateThresholdDb)
    {
        const float belowThresholdDb = gateThresholdDb - gateInputDb;
        const float attenuationDb = std::min(
            belowThresholdDb * (0.75f + 2.25f * gateAmount),
            MaximumGateReductionDb * gateAmount
        );
        targetGateGain = DecibelsToLinear(-attenuationDb);
    }

    const bool protectSpeech = onsetProtection_ || speechProtection_ > 0.32f;
    if (protectSpeech || gateInputDb >= gateThresholdDb + 2.0f)
    {
        gateHoldSamplesRemaining_ = GateHoldSamples;
        targetGateGain = 1.0f;
    }
    else if (gateHoldSamplesRemaining_ > 0.0f)
    {
        gateHoldSamplesRemaining_ -= 1.0f;
        targetGateGain = 1.0f;
    }

    const float gateCoefficient = targetGateGain > gateGain_
        ? GateAttackCoefficient
        : GateReleaseCoefficient;
    gateGain_ += gateCoefficient * (targetGateGain - gateGain_);
    return sample * std::lerp(
        1.0f,
        gateGain_,
        std::clamp(smoothedGateMix_, 0.0f, 1.0f)
    );
}

float VoicePolishEngine2::ProcessCompressor(const float sample) noexcept
{
    const float absoluteCompressorInput = std::abs(sample);
    const float compressorEnvelopeCoefficient =
        absoluteCompressorInput > compressorEnvelope_
            ? CompressorAttackCoefficient
            : CompressorReleaseCoefficient;
    compressorEnvelope_ += compressorEnvelopeCoefficient *
        (absoluteCompressorInput - compressorEnvelope_);

    const float compressorAmount = std::clamp(
        smoothedCompressorAmount_, 0.0f, 1.0f
    );
    const float compressorThresholdDb = std::lerp(
        -8.0f, -27.0f, compressorAmount
    );
    const float compressorRatio = 1.0f + 5.0f * compressorAmount;
    const float compressorInputDb = LinearToDecibels(compressorEnvelope_);
    const float kneeStart = compressorThresholdDb - CompressorKneeDb * 0.5f;
    const float kneeEnd = compressorThresholdDb + CompressorKneeDb * 0.5f;
    float targetCompressorGainDb = 0.0f;
    if (compressorInputDb > kneeStart)
    {
        float compressedDb = compressorInputDb;
        if (compressorInputDb >= kneeEnd)
        {
            compressedDb = compressorThresholdDb +
                (compressorInputDb - compressorThresholdDb) /
                    compressorRatio;
        }
        else
        {
            const float kneePosition = compressorInputDb - kneeStart;
            const float slope = 1.0f / compressorRatio - 1.0f;
            compressedDb = compressorInputDb + slope *
                kneePosition * kneePosition /
                (2.0f * CompressorKneeDb);
        }
        targetCompressorGainDb = std::min(
            compressedDb - compressorInputDb,
            0.0f
        );
    }

    const float compressorGainCoefficient =
        targetCompressorGainDb < compressorGainDb_
            ? CompressorAttackCoefficient
            : CompressorReleaseCoefficient;
    compressorGainDb_ += compressorGainCoefficient *
        (targetCompressorGainDb - compressorGainDb_);
    const float autoMakeupDb = compressorAmount * 2.4f;
    const float compressed = sample * DecibelsToLinear(
        compressorGainDb_ + autoMakeupDb
    );
    return std::lerp(
        sample,
        compressed,
        std::clamp(smoothedCompressorMix_, 0.0f, 1.0f)
    );
}

void VoicePolishEngine2::AdvanceSmoothedSettings() noexcept
{
    smoothedEqMix_ += MixCoefficient * (targetEqMix_ - smoothedEqMix_);
    smoothedDeEsserMix_ += MixCoefficient *
        (targetDeEsserMix_ - smoothedDeEsserMix_);
    smoothedGateMix_ += MixCoefficient *
        (targetGateMix_ - smoothedGateMix_);
    smoothedCompressorMix_ += MixCoefficient *
        (targetCompressorMix_ - smoothedCompressorMix_);
    smoothedDeEsserAmount_ += AmountCoefficient *
        (targetDeEsserAmount_ - smoothedDeEsserAmount_);
    smoothedGateAmount_ += AmountCoefficient *
        (targetGateAmount_ - smoothedGateAmount_);
    smoothedCompressorAmount_ += AmountCoefficient *
        (targetCompressorAmount_ - smoothedCompressorAmount_);

    SmoothCoefficients(
        lowEqCoefficients_, targetLowEqCoefficients_, EqCoefficient
    );
    SmoothCoefficients(
        midEqCoefficients_, targetMidEqCoefficients_, EqCoefficient
    );
    SmoothCoefficients(
        highEqCoefficients_, targetHighEqCoefficients_, EqCoefficient
    );
}

void VoicePolishEngine2::UpdateRackTransition() noexcept
{
    if (requestedRackOrder_ != activeRackOrder_)
    {
        rackTransitionMix_ += RackTransitionCoefficient *
            (0.0f - rackTransitionMix_);
        if (rackTransitionMix_ <= 0.001f)
        {
            activeRackOrder_ = requestedRackOrder_;
            ResetModuleState();
            rackTransitionMix_ = 0.0f;
        }
        return;
    }

    rackTransitionMix_ += RackTransitionCoefficient *
        (1.0f - rackTransitionMix_);
}

void VoicePolishEngine2::ResetModuleState() noexcept
{
    lowEqState_ = {};
    midEqState_ = {};
    highEqState_ = {};
    deEsserLowState_ = 0.0f;
    broadbandEnvelope_ = 0.0f;
    sibilanceEnvelope_ = 0.0f;
    deEsserGain_ = 1.0f;
    gateEnvelope_ = 0.0f;
    gateGain_ = 1.0f;
    gateHoldSamplesRemaining_ = 0.0f;
    compressorEnvelope_ = 0.0f;
    compressorGainDb_ = 0.0f;
    metrics_ = {};
}

const VoicePolishEngine2Metrics& VoicePolishEngine2::Metrics() const noexcept
{
    return metrics_;
}

void VoicePolishEngine2::Reset() noexcept
{
    lowEqCoefficients_ = {};
    midEqCoefficients_ = {};
    highEqCoefficients_ = {};
    targetLowEqCoefficients_ = {};
    targetMidEqCoefficients_ = {};
    targetHighEqCoefficients_ = {};
    lowEqState_ = {};
    midEqState_ = {};
    highEqState_ = {};
    targetEqMix_ = 0.0f;
    targetDeEsserMix_ = 0.0f;
    targetGateMix_ = 0.0f;
    targetCompressorMix_ = 0.0f;
    targetDeEsserAmount_ = 0.0f;
    targetGateAmount_ = 0.0f;
    targetCompressorAmount_ = 0.0f;
    smoothedEqMix_ = 0.0f;
    smoothedDeEsserMix_ = 0.0f;
    smoothedGateMix_ = 0.0f;
    smoothedCompressorMix_ = 0.0f;
    smoothedDeEsserAmount_ = 0.0f;
    smoothedGateAmount_ = 0.0f;
    smoothedCompressorAmount_ = 0.0f;
    deEsserLowState_ = 0.0f;
    broadbandEnvelope_ = 0.0f;
    sibilanceEnvelope_ = 0.0f;
    deEsserGain_ = 1.0f;
    gateEnvelope_ = 0.0f;
    gateGain_ = 1.0f;
    gateHoldSamplesRemaining_ = 0.0f;
    speechProtection_ = 0.0f;
    onsetProtection_ = false;
    compressorEnvelope_ = 0.0f;
    compressorGainDb_ = 0.0f;
    activeRackOrder_ = DefaultVoiceEffectRackOrder;
    requestedRackOrder_ = DefaultVoiceEffectRackOrder;
    rackTransitionMix_ = 1.0f;
    metrics_ = {};
}

VoicePolishEngine2::BiquadCoefficients VoicePolishEngine2::BuildLowShelf(
    const float frequencyHz,
    const float gainDb
) noexcept
{
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float omega = 2.0f * std::numbers::pi_v<float> * frequencyHz /
        static_cast<float>(ProcessingSampleRate);
    const float cosine = std::cos(omega);
    const float sine = std::sin(omega);
    const float alpha = sine * std::sqrt(2.0f * a) * 0.5f;
    const float twoSqrtAAlpha = 2.0f * std::sqrt(a) * alpha;

    const float b0 = a * ((a + 1.0f) - (a - 1.0f) * cosine +
        twoSqrtAAlpha);
    const float b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosine);
    const float b2 = a * ((a + 1.0f) - (a - 1.0f) * cosine -
        twoSqrtAAlpha);
    const float a0 = (a + 1.0f) + (a - 1.0f) * cosine + twoSqrtAAlpha;
    const float a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosine);
    const float a2 = (a + 1.0f) + (a - 1.0f) * cosine - twoSqrtAAlpha;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

VoicePolishEngine2::BiquadCoefficients VoicePolishEngine2::BuildPeaking(
    const float frequencyHz,
    const float q,
    const float gainDb
) noexcept
{
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float omega = 2.0f * std::numbers::pi_v<float> * frequencyHz /
        static_cast<float>(ProcessingSampleRate);
    const float cosine = std::cos(omega);
    const float alpha = std::sin(omega) / (2.0f * q);
    const float a0 = 1.0f + alpha / a;
    return {
        (1.0f + alpha * a) / a0,
        (-2.0f * cosine) / a0,
        (1.0f - alpha * a) / a0,
        (-2.0f * cosine) / a0,
        (1.0f - alpha / a) / a0
    };
}

VoicePolishEngine2::BiquadCoefficients VoicePolishEngine2::BuildHighShelf(
    const float frequencyHz,
    const float gainDb
) noexcept
{
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float omega = 2.0f * std::numbers::pi_v<float> * frequencyHz /
        static_cast<float>(ProcessingSampleRate);
    const float cosine = std::cos(omega);
    const float sine = std::sin(omega);
    const float alpha = sine * std::sqrt(2.0f * a) * 0.5f;
    const float twoSqrtAAlpha = 2.0f * std::sqrt(a) * alpha;

    const float b0 = a * ((a + 1.0f) + (a - 1.0f) * cosine +
        twoSqrtAAlpha);
    const float b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosine);
    const float b2 = a * ((a + 1.0f) + (a - 1.0f) * cosine -
        twoSqrtAAlpha);
    const float a0 = (a + 1.0f) - (a - 1.0f) * cosine + twoSqrtAAlpha;
    const float a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosine);
    const float a2 = (a + 1.0f) - (a - 1.0f) * cosine - twoSqrtAAlpha;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

float VoicePolishEngine2::ProcessBiquad(
    const float sample,
    const BiquadCoefficients& coefficients,
    BiquadState& state
) noexcept
{
    const float output = coefficients.b0 * sample + state.z1;
    state.z1 = coefficients.b1 * sample - coefficients.a1 * output + state.z2;
    state.z2 = coefficients.b2 * sample - coefficients.a2 * output;
    if (!std::isfinite(output) || !std::isfinite(state.z1) ||
        !std::isfinite(state.z2))
    {
        state = {};
        return 0.0f;
    }
    return output;
}

void VoicePolishEngine2::SmoothCoefficients(
    BiquadCoefficients& current,
    const BiquadCoefficients& target,
    const float coefficient
) noexcept
{
    current.b0 += coefficient * (target.b0 - current.b0);
    current.b1 += coefficient * (target.b1 - current.b1);
    current.b2 += coefficient * (target.b2 - current.b2);
    current.a1 += coefficient * (target.a1 - current.a1);
    current.a2 += coefficient * (target.a2 - current.a2);
}

float VoicePolishEngine2::DecibelsToLinear(const float decibels) noexcept
{
    return std::pow(10.0f, decibels / 20.0f);
}

float VoicePolishEngine2::LinearToDecibels(const float linear) noexcept
{
    return 20.0f * std::log10(std::max(linear, MinimumEnvelope));
}

float VoicePolishEngine2::Sanitize(const float value) noexcept
{
    return std::isfinite(value) ? value : 0.0f;
}
