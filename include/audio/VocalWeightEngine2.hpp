#pragma once

#include "audio/SpeechAnalysisCore.hpp"


struct VocalWeightEngine2Metrics
{
    float chestCenterFrequencyHz = 165.0f;
    float voiceGate = 0.0f;
    float chestEnvelope = 0.0f;
    float contribution = 0.0f;
};

class VocalWeightEngine2
{
public:
    static constexpr unsigned int ProcessingSampleRate = 48000;

    VocalWeightEngine2() noexcept;

    VocalWeightEngine2(const VocalWeightEngine2&) = delete;
    VocalWeightEngine2& operator=(const VocalWeightEngine2&) = delete;

    VocalWeightEngine2(VocalWeightEngine2&&) = delete;
    VocalWeightEngine2& operator=(VocalWeightEngine2&&) = delete;

    void UpdateAnalysis(const SpeechAnalysisFrame& analysis) noexcept;

    [[nodiscard]] float ProcessSample(
        float sample,
        float amount
    ) noexcept;

    [[nodiscard]] const VocalWeightEngine2Metrics& Metrics() const noexcept;

    void Reset() noexcept;

private:
    struct StateVariableFilter
    {
        float integralOne = 0.0f;
        float integralTwo = 0.0f;
    };

    struct StateVariableCoefficients
    {
        float a1 = 1.0f;
        float a2 = 0.0f;
        float a3 = 0.0f;
    };

    static constexpr float DefaultChestCenterHz = 165.0f;
    static constexpr float BoxCenterHz = 520.0f;
    static constexpr float ChestQ = 0.62f;
    static constexpr float BoxQ = 0.78f;

    [[nodiscard]] static StateVariableCoefficients BuildCoefficients(
        float frequencyHz,
        float q
    ) noexcept;

    [[nodiscard]] static float ProcessBandPass(
        float sample,
        const StateVariableCoefficients& coefficients,
        StateVariableFilter& state
    ) noexcept;

    [[nodiscard]] static float SmoothStep(float value) noexcept;
    [[nodiscard]] static float Sanitize(float value) noexcept;

    StateVariableCoefficients chestCoefficients_{};
    StateVariableCoefficients targetChestCoefficients_{};
    StateVariableCoefficients boxCoefficients_{};
    StateVariableFilter chestFilter_{};
    StateVariableFilter boxFilter_{};

    VocalWeightEngine2Metrics metrics_{};
    float targetChestCenterFrequencyHz_ = DefaultChestCenterHz;
    float targetVoiceGate_ = 0.0f;
    float smoothedAmount_ = 0.0f;
    float chestEnvelope_ = 0.0f;
};
