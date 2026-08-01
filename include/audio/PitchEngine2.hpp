#pragma once

#include "audio/SpeechAnalysisCore.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

struct PitchEngine2Result
{
    float delayedDry = 0.0f;
    float timeDomainPitch = 0.0f;
    float hybridPitch = 0.0f;
    float voicedBlend = 0.0f;
    float transientDryMix = 0.0f;
    float unvoicedDryMix = 0.0f;
};

class PitchEngine2
{
public:
    static constexpr unsigned int ProcessingSampleRate = 48000;
    static constexpr std::size_t ProcessingLatencySamples = 768;

    PitchEngine2() noexcept;

    PitchEngine2(const PitchEngine2&) = delete;
    PitchEngine2& operator=(const PitchEngine2&) = delete;

    PitchEngine2(PitchEngine2&&) = delete;
    PitchEngine2& operator=(PitchEngine2&&) = delete;

    void UpdateAnalysis(const SpeechAnalysisFrame& analysis) noexcept;

    [[nodiscard]] PitchEngine2Result ProcessSample(
        float inputSample,
        float spectralPitchSample,
        float pitchRatio,
        float formantCompatibility
    ) noexcept;

    [[nodiscard]] float CurrentGrainSpanSamples() const noexcept;
    [[nodiscard]] float CurrentVoicedBlend() const noexcept;

    void Reset() noexcept;

private:
    static constexpr std::size_t DelayLineSize = 2048;
    static constexpr std::size_t DelayLineMask = DelayLineSize - 1;

    static_assert((DelayLineSize & (DelayLineSize - 1U)) == 0U);
    static_assert(DelayLineSize > ProcessingLatencySamples * 2U);

    [[nodiscard]] float ReadDelayCubic(float delaySamples) const noexcept;
    [[nodiscard]] float RenderPitchShifted(float pitchRatio) noexcept;
    [[nodiscard]] static float SmoothingCoefficient(
        float milliseconds
    ) noexcept;
    [[nodiscard]] static float Sanitize(float value) noexcept;

    std::array<float, DelayLineSize> delayLine_{};
    std::uint64_t writeSequence_ = 0;

    float grainPhase_ = 0.25f;
    float grainSpanSamples_ = 768.0f;
    float targetGrainSpanSamples_ = 768.0f;
    float voicedBlend_ = 0.0f;
    float targetVoicedQuality_ = 0.0f;
    float transientDryMix_ = 0.0f;
    float targetTransientDryMix_ = 0.0f;
    float unvoicedDryMix_ = 0.0f;
    float targetUnvoicedDryMix_ = 0.0f;
    float spectralLevel_ = 0.0f;
    float timeDomainLevel_ = 0.0f;
    float levelMatchGain_ = 1.0f;

    float grainSpanCoefficient_ = 1.0f;
    float blendAttackCoefficient_ = 1.0f;
    float blendReleaseCoefficient_ = 1.0f;
    float protectionAttackCoefficient_ = 1.0f;
    float protectionReleaseCoefficient_ = 1.0f;
    float levelSmoothingCoefficient_ = 1.0f;

    bool reanchorPending_ = false;
};
