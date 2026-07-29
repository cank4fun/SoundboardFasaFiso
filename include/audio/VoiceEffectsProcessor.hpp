#pragma once

#include "audio/VoiceEffectSettings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

class VoiceEffectsProcessor
{
public:
    static constexpr unsigned int ProcessingSampleRate = 48000;
    static constexpr std::size_t SamplesPerBlock = 480;
    static constexpr std::size_t FftSize = 1024;
    static constexpr std::size_t Oversampling = 4;
    static constexpr std::size_t HopSize = FftSize / Oversampling;
    static constexpr std::size_t ProcessingLatencySamples = FftSize - HopSize;

    VoiceEffectsProcessor() = default;

    VoiceEffectsProcessor(const VoiceEffectsProcessor&) = delete;
    VoiceEffectsProcessor& operator=(const VoiceEffectsProcessor&) = delete;

    VoiceEffectsProcessor(VoiceEffectsProcessor&&) = delete;
    VoiceEffectsProcessor& operator=(VoiceEffectsProcessor&&) = delete;

    bool Initialize(const VoiceEffectSettings& settings) noexcept;
    bool UpdateSettings(const VoiceEffectSettings& settings) noexcept;

    bool ProcessBlock(
        std::span<const float> input,
        std::span<float> output
    ) noexcept;

    [[nodiscard]] const VoiceEffectSettings& GetSettings() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::size_t LatencySamples() const noexcept;

    void Reset() noexcept;

private:
    static constexpr std::size_t BinCount = FftSize / 2 + 1;
    static constexpr std::size_t DryDelayLineSize = 2048;
    static constexpr std::size_t DryDelayLineMask = DryDelayLineSize - 1;
    static constexpr std::size_t TinyDelayLineSize = 2048;
    static constexpr std::size_t TinyDelayLineMask = TinyDelayLineSize - 1;
    static constexpr float PitchSmoothingMilliseconds = 30.0f;
    static constexpr float FormantSmoothingMilliseconds = 30.0f;
    static constexpr float CharacterSmoothingMilliseconds = 25.0f;
    static constexpr float DriveSmoothingMilliseconds = 15.0f;
    static constexpr float SpecialEffectSmoothingMilliseconds = 20.0f;
    static constexpr float MixSmoothingMilliseconds = 15.0f;
    static constexpr float BypassSmoothingMilliseconds = 10.0f;
    static constexpr float UnvoicedSmoothingMilliseconds = 4.0f;

    static_assert((FftSize & (FftSize - 1)) == 0);
    static_assert(FftSize % Oversampling == 0);
    static_assert((DryDelayLineSize & (DryDelayLineSize - 1)) == 0);
    static_assert(DryDelayLineSize > ProcessingLatencySamples);
    static_assert((TinyDelayLineSize & (TinyDelayLineSize - 1)) == 0);

    void ProcessPitchFrame(
        float pitchRatio,
        float formantRatio
    ) noexcept;
    void EstimateSpectralEnvelope() noexcept;
    void EstimateSpeechPeriod() noexcept;
    void AssignPhaseLockPeaks(float maximumMagnitude) noexcept;
    void TransformFft(bool inverse) noexcept;

    [[nodiscard]] float ProcessSpeechPitch(
        float pitchRatio,
        float delayedDry
    ) noexcept;
    [[nodiscard]] float ReadDryDelayAt(float delaySamples) const noexcept;
    [[nodiscard]] float ProcessCharacterEq(float sample) noexcept;
    [[nodiscard]] float ProcessDrive(float sample) const noexcept;
    [[nodiscard]] float ProcessSpecialEffects(float sample) noexcept;
    [[nodiscard]] float ProcessRadio(float sample) noexcept;
    [[nodiscard]] float ProcessRobot(float sample) noexcept;
    [[nodiscard]] float ProcessTinyHigh(float sample) noexcept;
    [[nodiscard]] float ReadTinyDelay(float delaySamples) const noexcept;

    [[nodiscard]] float SampleSpectralEnvelope(float bin) const noexcept;
    [[nodiscard]] float ReadDryDelay() const noexcept;
    [[nodiscard]] static float SmoothingCoefficient(
        float milliseconds
    ) noexcept;

    VoiceEffectSettings settings_{};

    std::array<float, FftSize> inputFifo_{};
    std::array<float, FftSize> outputFifo_{};
    std::array<float, FftSize> outputAccumulator_{};
    std::array<float, FftSize> fftReal_{};
    std::array<float, FftSize> fftImaginary_{};
    std::array<float, FftSize> window_{};
    std::array<float, BinCount> lastPhase_{};
    std::array<float, BinCount> sumPhase_{};
    std::array<float, BinCount> analysisMagnitude_{};
    std::array<float, BinCount> previousAnalysisMagnitude_{};
    std::array<float, BinCount> analysisPhase_{};
    std::array<float, BinCount> analysisFrequency_{};
    std::array<float, BinCount> logMagnitude_{};
    std::array<float, BinCount> smoothedLogMagnitude_{};
    std::array<float, BinCount> spectralEnvelope_{};
    std::array<float, BinCount> synthesisMagnitude_{};
    std::array<float, BinCount> synthesisReal_{};
    std::array<float, BinCount> synthesisImaginary_{};
    std::array<std::uint16_t, BinCount> nearestPeak_{};
    std::array<std::uint8_t, BinCount> spectralPeak_{};

    std::array<float, DryDelayLineSize> dryDelayLine_{};
    std::array<float, TinyDelayLineSize> tinyDelayLine_{};
    std::uint64_t dryWriteSequence_ = 0;
    std::uint64_t tinyWriteSequence_ = 0;
    std::size_t rover_ = ProcessingLatencySamples;

    float smoothedPitchSemitones_ = 0.0f;
    float smoothedFormantSemitones_ = 0.0f;
    float smoothedCharacterLowGain_ = 1.0f;
    float smoothedCharacterMidGain_ = 1.0f;
    float smoothedCharacterHighGain_ = 1.0f;
    float smoothedDrive_ = 0.0f;
    float smoothedRadioMix_ = 0.0f;
    float smoothedRobotMix_ = 0.0f;
    float smoothedTinyMix_ = 0.0f;
    float smoothedDryWet_ = 1.0f;
    float smoothedActiveMix_ = 0.0f;
    float pitchSmoothingCoefficient_ = 1.0f;
    float formantSmoothingCoefficient_ = 1.0f;
    float characterSmoothingCoefficient_ = 1.0f;
    float driveSmoothingCoefficient_ = 1.0f;
    float specialEffectSmoothingCoefficient_ = 1.0f;
    float mixSmoothingCoefficient_ = 1.0f;
    float bypassSmoothingCoefficient_ = 1.0f;
    float characterLowPassCoefficient_ = 1.0f;
    float characterHighCutCoefficient_ = 1.0f;
    float airPreservationCoefficient_ = 1.0f;
    float characterLowState_ = 0.0f;
    float characterHighCutState_ = 0.0f;
    float dryAirLowState_ = 0.0f;
    float wetAirLowState_ = 0.0f;
    float transientDryMixTarget_ = 0.0f;
    float smoothedTransientDryMix_ = 0.0f;
    float transientSmoothingCoefficient_ = 1.0f;
    float unvoicedDryMixTarget_ = 0.0f;
    float smoothedUnvoicedDryMix_ = 0.0f;
    float unvoicedSmoothingCoefficient_ = 1.0f;
    float previousFrameEnergy_ = 0.0f;
    float speechPitchPhase_ = 0.25f;
    float speechPitchPeriodSamples_ = 240.0f;
    float speechPitchDelayRangeSamples_ = 768.0f;
    float speechVoicingConfidence_ = 0.0f;
    float radioLowCutCoefficient_ = 1.0f;
    float radioHighCutCoefficient_ = 1.0f;
    float radioLowCutStateOne_ = 0.0f;
    float radioLowCutStateTwo_ = 0.0f;
    float radioHighCutStateOne_ = 0.0f;
    float radioHighCutStateTwo_ = 0.0f;
    float robotPhase_ = 0.0f;
    float tinyDoublerPhase_ = 0.0f;
    bool hasPreviousSpectrum_ = false;
    bool initialized_ = false;
};
