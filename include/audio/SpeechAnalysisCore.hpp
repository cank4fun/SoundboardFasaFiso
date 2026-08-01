#pragma once

#include <array>
#include <cstddef>
#include <span>

struct SpeechAnalysisFrame
{
    float fundamentalFrequencyHz = 0.0f;
    float pitchPeriodSamples = 240.0f;
    float pitchConfidence = 0.0f;
    float voicingConfidence = 0.0f;
    float speechActivity = 0.0f;
    float rmsLevel = 0.0f;
    float peakLevel = 0.0f;
    float spectralFlatness = 1.0f;
    float spectralCentroidHz = 0.0f;
    float highBandRatio = 0.0f;
    float transientProbability = 0.0f;
    float unvoicedProbability = 0.0f;
    bool onset = false;
};

class SpeechAnalysisCore
{
public:
    static constexpr unsigned int ProcessingSampleRate = 48000;
    static constexpr std::size_t FrameSize = 1024;
    static constexpr std::size_t HopSize = 256;
    static constexpr std::size_t BinCount = FrameSize / 2 + 1;
    static constexpr unsigned int MinimumFundamentalFrequencyHz = 70;
    static constexpr unsigned int MaximumFundamentalFrequencyHz = 700;

    SpeechAnalysisCore() noexcept;

    SpeechAnalysisCore(const SpeechAnalysisCore&) = delete;
    SpeechAnalysisCore& operator=(const SpeechAnalysisCore&) = delete;

    SpeechAnalysisCore(SpeechAnalysisCore&&) = delete;
    SpeechAnalysisCore& operator=(SpeechAnalysisCore&&) = delete;

    bool AnalyzeFrame(
        std::span<const float> timeDomainFrame,
        std::span<const float> magnitudeSpectrum
    ) noexcept;

    [[nodiscard]] const SpeechAnalysisFrame& LatestFrame() const noexcept;
    [[nodiscard]] std::span<const float> SpectralEnvelope() const noexcept;
    [[nodiscard]] float SampleSpectralEnvelope(float bin) const noexcept;

    void Reset() noexcept;

private:
    static constexpr std::size_t MinimumPitchLag =
        ProcessingSampleRate / MaximumFundamentalFrequencyHz;
    static constexpr std::size_t MaximumPitchLag =
        (ProcessingSampleRate + MinimumFundamentalFrequencyHz - 1U) /
        MinimumFundamentalFrequencyHz;

    static_assert(MinimumPitchLag > 1U);
    static_assert(MaximumPitchLag + 1U < FrameSize);

    void PrepareTimeDomainFrame(
        std::span<const float> timeDomainFrame
    ) noexcept;
    void AnalyzeSpectrum(
        std::span<const float> magnitudeSpectrum
    ) noexcept;
    void AnalyzePitch() noexcept;
    void EstimateSpectralEnvelope(
        std::span<const float> magnitudeSpectrum
    ) noexcept;

    [[nodiscard]] float NormalizedCorrelation(std::size_t lag) const noexcept;
    [[nodiscard]] float EstimateSpeechActivity(float rmsLevel) noexcept;
    [[nodiscard]] static float SanitizeMagnitude(float magnitude) noexcept;

    SpeechAnalysisFrame latestFrame_{};
    std::array<float, FrameSize> centeredFrame_{};
    std::array<float, BinCount> previousMagnitude_{};
    std::array<float, BinCount> logMagnitude_{};
    std::array<float, BinCount> smoothedLogMagnitude_{};
    std::array<float, BinCount> spectralEnvelope_{};
    std::array<float, MaximumPitchLag + 2U> pitchCorrelation_{};

    float noiseFloorRms_ = 0.0001f;
    float smoothedPitchPeriodSamples_ = 240.0f;
    float smoothedVoicingConfidence_ = 0.0f;
    float previousFrameEnergy_ = 0.0f;
    float normalizedSpectralFlux_ = 0.0f;
    std::size_t voicingHoldFrames_ = 0;
    bool hasPreviousSpectrum_ = false;
};
