#pragma once

#include "audio/SpeechAnalysisCore.hpp"
#include "audio/VoiceEffectSettings.hpp"

#include <array>

struct VoicePolishEngine2Metrics
{
    float gateGain = 1.0f;
    float deEsserReductionDb = 0.0f;
    float compressorReductionDb = 0.0f;
    float broadbandEnvelope = 0.0f;
    float sibilanceEnvelope = 0.0f;
};

class VoicePolishEngine2
{
public:
    static constexpr unsigned int ProcessingSampleRate = 48000;

    VoicePolishEngine2() noexcept;

    VoicePolishEngine2(const VoicePolishEngine2&) = delete;
    VoicePolishEngine2& operator=(const VoicePolishEngine2&) = delete;
    VoicePolishEngine2(VoicePolishEngine2&&) = delete;
    VoicePolishEngine2& operator=(VoicePolishEngine2&&) = delete;

    void UpdateSettings(const VoiceEffectSettings& settings) noexcept;
    void UpdateAnalysis(const SpeechAnalysisFrame& analysis) noexcept;

    [[nodiscard]] float ProcessSample(float sample) noexcept;
    [[nodiscard]] const VoicePolishEngine2Metrics& Metrics() const noexcept;

    void Reset() noexcept;

private:
    struct BiquadCoefficients
    {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
    };

    struct BiquadState
    {
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    [[nodiscard]] static BiquadCoefficients BuildLowShelf(
        float frequencyHz,
        float gainDb
    ) noexcept;
    [[nodiscard]] static BiquadCoefficients BuildPeaking(
        float frequencyHz,
        float q,
        float gainDb
    ) noexcept;
    [[nodiscard]] static BiquadCoefficients BuildHighShelf(
        float frequencyHz,
        float gainDb
    ) noexcept;
    [[nodiscard]] static float ProcessBiquad(
        float sample,
        const BiquadCoefficients& coefficients,
        BiquadState& state
    ) noexcept;
    static void SmoothCoefficients(
        BiquadCoefficients& current,
        const BiquadCoefficients& target,
        float coefficient
    ) noexcept;

    [[nodiscard]] float ProcessModule(
        VoiceEffectRackModule module,
        float sample
    ) noexcept;
    [[nodiscard]] float ProcessEqualizer(float sample) noexcept;
    [[nodiscard]] float ProcessDeEsser(float sample) noexcept;
    [[nodiscard]] float ProcessGate(float sample) noexcept;
    [[nodiscard]] float ProcessCompressor(float sample) noexcept;
    void AdvanceSmoothedSettings() noexcept;
    void UpdateRackTransition() noexcept;
    void ResetModuleState() noexcept;

    [[nodiscard]] static float DecibelsToLinear(float decibels) noexcept;
    [[nodiscard]] static float LinearToDecibels(float linear) noexcept;
    [[nodiscard]] static float Sanitize(float value) noexcept;

    BiquadCoefficients lowEqCoefficients_{};
    BiquadCoefficients midEqCoefficients_{};
    BiquadCoefficients highEqCoefficients_{};
    BiquadCoefficients targetLowEqCoefficients_{};
    BiquadCoefficients targetMidEqCoefficients_{};
    BiquadCoefficients targetHighEqCoefficients_{};
    BiquadState lowEqState_{};
    BiquadState midEqState_{};
    BiquadState highEqState_{};

    float targetEqMix_ = 0.0f;
    float targetDeEsserMix_ = 0.0f;
    float targetGateMix_ = 0.0f;
    float targetCompressorMix_ = 0.0f;
    float targetDeEsserAmount_ = 0.0f;
    float targetGateAmount_ = 0.0f;
    float targetCompressorAmount_ = 0.0f;

    float smoothedEqMix_ = 0.0f;
    float smoothedDeEsserMix_ = 0.0f;
    float smoothedGateMix_ = 0.0f;
    float smoothedCompressorMix_ = 0.0f;
    float smoothedDeEsserAmount_ = 0.0f;
    float smoothedGateAmount_ = 0.0f;
    float smoothedCompressorAmount_ = 0.0f;

    float deEsserLowState_ = 0.0f;
    float broadbandEnvelope_ = 0.0f;
    float sibilanceEnvelope_ = 0.0f;
    float deEsserGain_ = 1.0f;

    float gateEnvelope_ = 0.0f;
    float gateGain_ = 1.0f;
    float gateHoldSamplesRemaining_ = 0.0f;
    float speechProtection_ = 0.0f;
    bool onsetProtection_ = false;

    float compressorEnvelope_ = 0.0f;
    float compressorGainDb_ = 0.0f;

    VoiceEffectRackOrder activeRackOrder_ = DefaultVoiceEffectRackOrder;
    VoiceEffectRackOrder requestedRackOrder_ = DefaultVoiceEffectRackOrder;
    float rackTransitionMix_ = 1.0f;

    VoicePolishEngine2Metrics metrics_{};
};
