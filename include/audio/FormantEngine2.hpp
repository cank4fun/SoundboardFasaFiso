#pragma once

#include "audio/SpeechAnalysisCore.hpp"

#include <array>
#include <cstddef>
#include <span>

class FormantEngine2
{
public:
    static constexpr unsigned int ProcessingSampleRate =
        SpeechAnalysisCore::ProcessingSampleRate;
    static constexpr std::size_t FrameSize = SpeechAnalysisCore::FrameSize;
    static constexpr std::size_t BinCount = SpeechAnalysisCore::BinCount;

    static_assert(BinCount == FrameSize / 2U + 1U);

    FormantEngine2() noexcept;

    FormantEngine2(const FormantEngine2&) = delete;
    FormantEngine2& operator=(const FormantEngine2&) = delete;

    FormantEngine2(FormantEngine2&&) = delete;
    FormantEngine2& operator=(FormantEngine2&&) = delete;

    bool PrepareFrame(
        std::span<const float> magnitudeSpectrum,
        std::span<const float> spectralEnvelope,
        const SpeechAnalysisFrame& analysis,
        float pitchRatio,
        float formantRatio
    ) noexcept;

    [[nodiscard]] float CorrectionForBin(std::size_t bin) const noexcept;
    [[nodiscard]] std::span<const float> Corrections() const noexcept;
    [[nodiscard]] float CurrentStrength() const noexcept;
    [[nodiscard]] float CurrentEnergyGain() const noexcept;

    void Reset() noexcept;

private:
    static constexpr float MinimumRatio = 0.45f;
    static constexpr float MaximumRatio = 2.25f;
    static constexpr float MinimumMagnitude = 0.000001f;
    static constexpr float MinimumCorrection = 0.48f;
    static constexpr float MaximumCorrection = 2.10f;
    static constexpr float MinimumEnergyGain = 0.82f;
    static constexpr float MaximumEnergyGain = 1.20f;
    static constexpr float EnergyNormalizationStrength = 0.68f;
    static constexpr float EnvelopeCorrectionStrength = 0.78f;
    static constexpr float MinimumActiveSemitones = 0.08f;
    static constexpr float FullActiveSemitones = 1.25f;
    static constexpr float LowFrequencyFadeStartHz = 70.0f;
    static constexpr float LowFrequencyFadeEndHz = 180.0f;
    static constexpr float HighFrequencyFadeStartHz = 7600.0f;
    static constexpr float HighFrequencyFadeEndHz = 12000.0f;
    static constexpr float TemporalAttack = 0.58f;
    static constexpr float TemporalRelease = 0.28f;
    static constexpr float OnsetTemporalCoefficient = 0.86f;

    [[nodiscard]] static float SampleEnvelope(
        std::span<const float> envelope,
        float bin
    ) noexcept;
    [[nodiscard]] static float FrequencyWeight(float frequencyHz) noexcept;
    [[nodiscard]] static float SmoothStep(float value) noexcept;
    [[nodiscard]] static float Sanitize(float value) noexcept;

    std::array<float, BinCount> targetLogCorrection_{};
    std::array<float, BinCount> spatialLogCorrection_{};
    std::array<float, BinCount> smoothedLogCorrection_{};
    std::array<float, BinCount> corrections_{};

    float currentStrength_ = 0.0f;
    float currentEnergyGain_ = 1.0f;
    bool hasPreparedFrame_ = false;
};
