#include "audio/VoiceEffectsProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    constexpr float SemitonesPerOctave = 12.0f;
    constexpr float MinimumSpectralBlendSemitones = 0.25f;
    constexpr float OverlapAddScale = 2.0f / 3.0f;
    constexpr std::size_t SpectralEnvelopeRadius = 6;
    constexpr float MinimumEnvelopeMagnitude = 0.000001f;
    constexpr float MinimumEnvelopeCorrection = 0.55f;
    constexpr float MaximumEnvelopeCorrection = 1.85f;
    constexpr float EnvelopeCorrectionStrength = 0.65f;
    constexpr float PhaseLockPeakThreshold = 0.012f;
    constexpr float PhaseLockMaximumHz = 8000.0f;
    constexpr float TransientFluxThreshold = 0.16f;
    constexpr float TransientFluxRange = 0.30f;
    constexpr float MaximumTransientDryMix = 0.52f;
    constexpr float TransientSmoothingMilliseconds = 3.0f;
    constexpr float AirPreservationCrossoverHz = 3200.0f;
    constexpr float MaximumAirPreservation = 0.82f;
    constexpr float UnvoicedFlatnessStart = 0.18f;
    constexpr float UnvoicedFlatnessRange = 0.32f;
    constexpr float UnvoicedHighBandStart = 0.20f;
    constexpr float UnvoicedHighBandRange = 0.38f;
    constexpr float UnvoicedWholeBandDryMix = 0.30f;
    constexpr float UnvoicedAirPreservation = 0.78f;
    constexpr float VoiceAnalysisMinimumHz = 250.0f;
    constexpr float VoiceAnalysisMaximumHz = 10000.0f;
    constexpr float VoiceAnalysisHighBandHz = 2800.0f;
    constexpr float OnsetEnergyRatio = 2.8f;
    constexpr float MinimumOnsetEnergy = 0.00001f;
    constexpr float CharacterLowCrossoverHz = 400.0f;
    constexpr float CharacterHighCrossoverHz = 3000.0f;
    constexpr float CharacterFormantScaleSemitones = 2.5f;
    constexpr float BodyPeakHz = 165.0f;
    constexpr float BodyPeakQ = 0.64f;
    constexpr float BodyPeakGainDb = 3.5f;
    constexpr float BodyBoxHz = 540.0f;
    constexpr float BodyBoxQ = 0.82f;
    constexpr float BodyBoxGainDb = -2.25f;
    constexpr float BodyHighPassHz = 70.0f;
    constexpr float BodyLowPassHz = 285.0f;
    constexpr float BodyEnvelopeFloor = 0.00020f;
    constexpr float BodyEnvelopeRange = 0.0055f;
    constexpr float BodyVoicingStart = 0.07f;
    constexpr float BodyVoicingRange = 0.28f;
    constexpr float BodyUnvoicedGateAmount = 0.24f;
    constexpr float BodyEnvelopeNormalization = 3.0f;
    constexpr float BodyMinimumNormalization = 0.0015f;
    constexpr float BodyDensityMinimum = 0.42f;
    constexpr float BodyDensityRange = 0.48f;
    constexpr float BodyDensityMix = 0.42f;
    constexpr float BodyParallelGain = 0.82f;
    constexpr float BodyMaximumContribution = 0.40f;
    constexpr float MaximumDrivePreGain = 9.0f;
    constexpr float RadioLowCutHz = 300.0f;
    constexpr float RadioHighCutHz = 3400.0f;
    constexpr float RobotCarrierHz = 95.0f;
    constexpr float RobotDryAmount = 0.15f;
    constexpr float TinyDoublerBaseDelaySamples = 576.0f;
    constexpr float TinyDoublerDepthSamples = 72.0f;
    constexpr float TinyDoublerRateHz = 0.8f;
    constexpr float TinyDoublerAmount = 0.035f;
    constexpr float SpeechPitchCentreDelaySamples =
        static_cast<float>(VoiceEffectsProcessor::ProcessingLatencySamples);
    constexpr float SpeechPitchDefaultDelayRangeSamples = 768.0f;
    constexpr float SpeechPitchMinimumDelayRangeSamples = 480.0f;
    constexpr float SpeechPitchMaximumDelayRangeSamples = 1050.0f;
    constexpr float SpeechPitchMinimumPeriodSamples = 96.0f;
    constexpr float SpeechPitchMaximumPeriodSamples = 480.0f;
    constexpr float SpeechPitchVoicingThreshold = 0.38f;
    constexpr float SpeechPitchMinimumSemitones = 0.08f;
    constexpr float MaximumSpeechPitchBlend = 0.88f;
    constexpr float SpeechPitchCompensationRangeSemitones = 5.5f;
    constexpr float SpeechPitchBlendAttackMilliseconds = 9.0f;
    constexpr float SpeechPitchBlendReleaseMilliseconds = 48.0f;
    constexpr float HybridLevelSmoothingMilliseconds = 42.0f;
    constexpr float HybridLevelMinimum = 0.00025f;
    constexpr float HybridLevelMinimumGain = 0.72f;
    constexpr float HybridLevelMaximumGain = 1.32f;
    constexpr std::uint8_t SpeechVoicingHoldFrameCount = 5;
    constexpr float SpeechVoicingHoldThreshold = 0.16f;
    constexpr float SpeechVoicingHoldDecay = 0.90f;

    struct CharacterGains
    {
        float low = 1.0f;
        float mid = 1.0f;
        float high = 1.0f;
    };

    float DecibelsToLinear(const float decibels) noexcept
    {
        return std::pow(10.0f, decibels / 20.0f);
    }

    float OnePoleCoefficient(const float frequency) noexcept
    {
        return 1.0f - std::exp(
            -2.0f * std::numbers::pi_v<float> * frequency /
            static_cast<float>(VoiceEffectsProcessor::ProcessingSampleRate)
        );
    }


    std::array<float, 5> PeakingBiquadCoefficients(
        const float frequency,
        const float q,
        const float gainDb
    ) noexcept
    {
        const float amplitude = std::pow(10.0f, gainDb / 40.0f);
        const float angularFrequency =
            2.0f * std::numbers::pi_v<float> * frequency /
            static_cast<float>(VoiceEffectsProcessor::ProcessingSampleRate);
        const float alpha = std::sin(angularFrequency) / (2.0f * q);
        const float cosine = std::cos(angularFrequency);
        const float inverseA0 = 1.0f /
            (1.0f + alpha / amplitude);

        return {
            (1.0f + alpha * amplitude) * inverseA0,
            (-2.0f * cosine) * inverseA0,
            (1.0f - alpha * amplitude) * inverseA0,
            (-2.0f * cosine) * inverseA0,
            (1.0f - alpha / amplitude) * inverseA0
        };
    }


    float PresetAirPreservationFloor(
        const VoiceEffectPreset preset
    ) noexcept
    {
        switch (preset)
        {
            case VoiceEffectPreset::DeepHeavy:
                return 0.05f;
            case VoiceEffectPreset::HighNasalRap:
                return 0.13f;
            case VoiceEffectPreset::DarkVocal:
                return 0.08f;
            case VoiceEffectPreset::TinyHighVoice:
                return 0.20f;
            case VoiceEffectPreset::Radio:
            case VoiceEffectPreset::Robot:
            case VoiceEffectPreset::Custom:
                return 0.0f;
        }

        return 0.0f;
    }

    CharacterGains BuildCharacterGains(
        const VoiceEffectSettings& settings
    ) noexcept
    {
        const float amount = std::clamp(settings.character, 0.0f, 1.0f);
        const float tone = std::clamp(
            settings.formantSemitones / CharacterFormantScaleSemitones,
            -1.0f,
            1.0f
        );
        const float warmth = std::max(-tone, 0.0f);
        const float brightness = std::max(tone, 0.0f);

        // Character is a compact three-band vocal contour. Negative formant
        // settings lean warm/dark, positive settings lean nasal/bright, and
        // neutral formant settings focus the speech band without pretending
        // to be the later dedicated radio or robot stages.
        const float lowDb = amount * (-8.0f + 12.0f * warmth);
        const float midDb = amount * (
            4.0f - 1.5f * warmth + 3.0f * brightness
        );
        const float highDb = amount * (
            -7.0f - warmth + 10.0f * brightness
        );

        return {
            DecibelsToLinear(lowDb),
            DecibelsToLinear(midDb),
            DecibelsToLinear(highDb)
        };
    }
}

bool VoiceEffectsProcessor::Initialize(
    const VoiceEffectSettings& settings
) noexcept
{
    Reset();

    if (!IsValidVoiceEffectSettings(settings))
    {
        return false;
    }

    settings_ = settings;
    const CharacterGains characterGains = BuildCharacterGains(settings);
    smoothedPitchSemitones_ = settings.pitchSemitones;
    smoothedFormantSemitones_ = settings.formantSemitones;
    smoothedCharacterLowGain_ = characterGains.low;
    smoothedCharacterMidGain_ = characterGains.mid;
    smoothedCharacterHighGain_ = characterGains.high;
    smoothedBody_ = settings.body;
    smoothedDrive_ = settings.drive;
    smoothedRadioMix_ = settings.preset == VoiceEffectPreset::Radio
        ? 1.0f
        : 0.0f;
    smoothedRobotMix_ = settings.preset == VoiceEffectPreset::Robot
        ? 1.0f
        : 0.0f;
    smoothedTinyMix_ = settings.preset == VoiceEffectPreset::TinyHighVoice
        ? 1.0f
        : 0.0f;
    smoothedDryWet_ = settings.dryWet;
    smoothedActiveMix_ = settings.enabled && !settings.bypassed
        ? 1.0f
        : 0.0f;
    pitchSmoothingCoefficient_ = SmoothingCoefficient(
        PitchSmoothingMilliseconds
    );
    formantSmoothingCoefficient_ = SmoothingCoefficient(
        FormantSmoothingMilliseconds
    );
    characterSmoothingCoefficient_ = SmoothingCoefficient(
        CharacterSmoothingMilliseconds
    );
    bodySmoothingCoefficient_ = SmoothingCoefficient(
        BodySmoothingMilliseconds
    );
    driveSmoothingCoefficient_ = SmoothingCoefficient(
        DriveSmoothingMilliseconds
    );
    specialEffectSmoothingCoefficient_ = SmoothingCoefficient(
        SpecialEffectSmoothingMilliseconds
    );
    mixSmoothingCoefficient_ = SmoothingCoefficient(
        MixSmoothingMilliseconds
    );
    bypassSmoothingCoefficient_ = SmoothingCoefficient(
        BypassSmoothingMilliseconds
    );
    transientSmoothingCoefficient_ = SmoothingCoefficient(
        TransientSmoothingMilliseconds
    );
    unvoicedSmoothingCoefficient_ = SmoothingCoefficient(
        UnvoicedSmoothingMilliseconds
    );
    speechPitchBlendAttackCoefficient_ = SmoothingCoefficient(
        SpeechPitchBlendAttackMilliseconds
    );
    speechPitchBlendReleaseCoefficient_ = SmoothingCoefficient(
        SpeechPitchBlendReleaseMilliseconds
    );
    hybridLevelSmoothingCoefficient_ = SmoothingCoefficient(
        HybridLevelSmoothingMilliseconds
    );
    characterLowPassCoefficient_ = OnePoleCoefficient(
        CharacterLowCrossoverHz
    );
    characterHighCutCoefficient_ = OnePoleCoefficient(
        CharacterHighCrossoverHz
    );
    bodyHighPassCoefficient_ = OnePoleCoefficient(BodyHighPassHz);
    bodyLowPassCoefficient_ = OnePoleCoefficient(BodyLowPassHz);
    bodyEnvelopeAttackCoefficient_ = SmoothingCoefficient(
        BodyEnvelopeAttackMilliseconds
    );
    bodyEnvelopeReleaseCoefficient_ = SmoothingCoefficient(
        BodyEnvelopeReleaseMilliseconds
    );
    bodyGateAttackCoefficient_ = SmoothingCoefficient(
        BodyGateAttackMilliseconds
    );
    bodyGateReleaseCoefficient_ = SmoothingCoefficient(
        BodyGateReleaseMilliseconds
    );
    bodyPeakCoefficients_ = PeakingBiquadCoefficients(
        BodyPeakHz,
        BodyPeakQ,
        BodyPeakGainDb
    );
    bodyBoxCoefficients_ = PeakingBiquadCoefficients(
        BodyBoxHz,
        BodyBoxQ,
        BodyBoxGainDb
    );
    airPreservationCoefficient_ = OnePoleCoefficient(
        AirPreservationCrossoverHz
    );
    radioLowCutCoefficient_ = OnePoleCoefficient(RadioLowCutHz);
    radioHighCutCoefficient_ = OnePoleCoefficient(RadioHighCutHz);

    for (std::size_t index = 0; index < FftSize; ++index)
    {
        window_[index] = 0.5f - 0.5f * std::cos(
            2.0f * std::numbers::pi_v<float> *
            static_cast<float>(index) /
            static_cast<float>(FftSize)
        );
    }

    initialized_ = true;
    return true;
}

bool VoiceEffectsProcessor::UpdateSettings(
    const VoiceEffectSettings& settings
) noexcept
{
    if (!initialized_ || !IsValidVoiceEffectSettings(settings))
    {
        return false;
    }

    settings_ = settings;
    return true;
}

bool VoiceEffectsProcessor::ProcessBlock(
    const std::span<const float> input,
    const std::span<float> output
) noexcept
{
    if (!initialized_ ||
        input.size() != SamplesPerBlock ||
        output.size() != SamplesPerBlock)
    {
        return false;
    }

    const float targetActiveMix = settings_.enabled && !settings_.bypassed
        ? 1.0f
        : 0.0f;
    const CharacterGains targetCharacterGains = BuildCharacterGains(
        settings_
    );
    const float targetRadioMix = settings_.preset == VoiceEffectPreset::Radio
        ? 1.0f
        : 0.0f;
    const float targetRobotMix = settings_.preset == VoiceEffectPreset::Robot
        ? 1.0f
        : 0.0f;
    const float targetTinyMix =
        settings_.preset == VoiceEffectPreset::TinyHighVoice
            ? 1.0f
            : 0.0f;

    for (std::size_t index = 0; index < SamplesPerBlock; ++index)
    {
        float inputSample = input[index];
        if (!std::isfinite(inputSample))
        {
            inputSample = 0.0f;
        }

        dryDelayLine_[
            static_cast<std::size_t>(dryWriteSequence_) &
                DryDelayLineMask
        ] = inputSample;

        smoothedPitchSemitones_ += pitchSmoothingCoefficient_ *
            (settings_.pitchSemitones - smoothedPitchSemitones_);
        smoothedFormantSemitones_ += formantSmoothingCoefficient_ *
            (settings_.formantSemitones - smoothedFormantSemitones_);
        smoothedCharacterLowGain_ += characterSmoothingCoefficient_ *
            (targetCharacterGains.low - smoothedCharacterLowGain_);
        smoothedCharacterMidGain_ += characterSmoothingCoefficient_ *
            (targetCharacterGains.mid - smoothedCharacterMidGain_);
        smoothedCharacterHighGain_ += characterSmoothingCoefficient_ *
            (targetCharacterGains.high - smoothedCharacterHighGain_);
        smoothedBody_ += bodySmoothingCoefficient_ *
            (settings_.body - smoothedBody_);
        smoothedDrive_ += driveSmoothingCoefficient_ *
            (settings_.drive - smoothedDrive_);
        smoothedRadioMix_ += specialEffectSmoothingCoefficient_ *
            (targetRadioMix - smoothedRadioMix_);
        smoothedRobotMix_ += specialEffectSmoothingCoefficient_ *
            (targetRobotMix - smoothedRobotMix_);
        smoothedTinyMix_ += specialEffectSmoothingCoefficient_ *
            (targetTinyMix - smoothedTinyMix_);
        smoothedDryWet_ += mixSmoothingCoefficient_ *
            (settings_.dryWet - smoothedDryWet_);
        smoothedActiveMix_ += bypassSmoothingCoefficient_ *
            (targetActiveMix - smoothedActiveMix_);
        smoothedTransientDryMix_ += transientSmoothingCoefficient_ *
            (transientDryMixTarget_ - smoothedTransientDryMix_);
        smoothedUnvoicedDryMix_ += unvoicedSmoothingCoefficient_ *
            (unvoicedDryMixTarget_ - smoothedUnvoicedDryMix_);

        inputFifo_[rover_] = inputSample;
        const float pitchOutput = outputFifo_[
            rover_ - ProcessingLatencySamples
        ];
        ++rover_;

        if (rover_ >= FftSize)
        {
            const float pitchRatio = std::exp2(
                smoothedPitchSemitones_ / SemitonesPerOctave
            );
            const float formantRatio = std::exp2(
                smoothedFormantSemitones_ / SemitonesPerOctave
            );
            ProcessPitchFrame(pitchRatio, formantRatio);
            rover_ = ProcessingLatencySamples;
        }

        const float delayedDry = ReadDryDelay();
        const float pitchRatio = std::exp2(
            smoothedPitchSemitones_ / SemitonesPerOctave
        );
        const float speechPitch = ProcessSpeechPitch(
            pitchRatio,
            delayedDry
        );
        const float pitchActivity = std::clamp(
            (std::abs(smoothedPitchSemitones_) -
                SpeechPitchMinimumSemitones) / 0.60f,
            0.0f,
            1.0f
        );
        const float voicedConfidence = std::clamp(
            speechVoicingConfidence_ *
                (1.0f - smoothedUnvoicedDryMix_) *
                (1.0f - smoothedTransientDryMix_ /
                    std::max(MaximumTransientDryMix, 0.0001f)),
            0.0f,
            1.0f
        );
        const float formantCompensationDistance = std::abs(
            smoothedFormantSemitones_ - smoothedPitchSemitones_
        );
        const float formantCompatibility = std::clamp(
            1.0f - formantCompensationDistance /
                SpeechPitchCompensationRangeSemitones,
            0.35f,
            1.0f
        );
        const float speechPitchBlendBase = std::clamp(
            MaximumSpeechPitchBlend * pitchActivity * voicedConfidence *
                formantCompatibility,
            0.0f,
            MaximumSpeechPitchBlend
        );
        const float speechPitchMixTarget = 1.0f -
            (1.0f - speechPitchBlendBase) *
                (1.0f - speechPitchBlendBase);
        const float speechPitchBlendCoefficient =
            speechPitchMixTarget > smoothedSpeechPitchMix_
                ? speechPitchBlendAttackCoefficient_
                : speechPitchBlendReleaseCoefficient_;
        smoothedSpeechPitchMix_ += speechPitchBlendCoefficient *
            (speechPitchMixTarget - smoothedSpeechPitchMix_);

        spectralPitchLevel_ += hybridLevelSmoothingCoefficient_ *
            (std::abs(pitchOutput) - spectralPitchLevel_);
        speechPitchLevel_ += hybridLevelSmoothingCoefficient_ *
            (std::abs(speechPitch) - speechPitchLevel_);
        float targetSpeechLevelGain = 1.0f;
        if (spectralPitchLevel_ > HybridLevelMinimum &&
            speechPitchLevel_ > HybridLevelMinimum)
        {
            targetSpeechLevelGain = std::clamp(
                spectralPitchLevel_ / speechPitchLevel_,
                HybridLevelMinimumGain,
                HybridLevelMaximumGain
            );
        }
        smoothedSpeechLevelGain_ += hybridLevelSmoothingCoefficient_ *
            (targetSpeechLevelGain - smoothedSpeechLevelGain_);
        const float levelMatchedSpeechPitch = speechPitch *
            smoothedSpeechLevelGain_;
        const float hybridPitchOutput = std::lerp(
            pitchOutput,
            levelMatchedSpeechPitch,
            std::clamp(smoothedSpeechPitchMix_, 0.0f, 0.96f)
        );
        const float spectralBlend = std::clamp(
            std::max(
                std::abs(smoothedPitchSemitones_),
                std::abs(smoothedFormantSemitones_)
            ) / MinimumSpectralBlendSemitones,
            0.0f,
            1.0f
        );
        const float protectiveDryMix = std::clamp(
            smoothedTransientDryMix_ +
                smoothedUnvoicedDryMix_ * UnvoicedWholeBandDryMix,
            0.0f,
            0.80f
        ) * spectralBlend;
        const float transientProtected = std::lerp(
            hybridPitchOutput,
            delayedDry,
            protectiveDryMix
        );
        dryAirLowState_ += airPreservationCoefficient_ *
            (delayedDry - dryAirLowState_);
        wetAirLowState_ += airPreservationCoefficient_ *
            (transientProtected - wetAirLowState_);
        const float dryAir = delayedDry - dryAirLowState_;
        const float wetAir = transientProtected - wetAirLowState_;
        const float shiftedAirPreservation =
            std::abs(smoothedPitchSemitones_) /
                    VoiceEffectLimits::MaximumPitchSemitones * 0.40f +
                std::abs(smoothedFormantSemitones_) /
                    VoiceEffectLimits::MaximumFormantSemitones * 0.15f;
        const float airPreservation = std::clamp(
            std::max({
                shiftedAirPreservation,
                smoothedUnvoicedDryMix_ * UnvoicedAirPreservation,
                PresetAirPreservationFloor(settings_.preset)
            }),
            0.0f,
            MaximumAirPreservation
        );
        const float naturalPitch = transientProtected +
            (dryAir - wetAir) * airPreservation;
        const float pitchedWet = std::lerp(
            delayedDry,
            naturalPitch,
            spectralBlend
        );
        const float characterWet = ProcessCharacterEq(pitchedWet);
        const float bodyWet = ProcessBody(characterWet);
        const float drivenWet = ProcessDrive(bodyWet);
        const float specialWet = ProcessSpecialEffects(drivenWet);
        // Output gain is intentionally deferred to MicrophoneProcessor so
        // AGC and compression see the untrimmed effect signal. The final gain
        // then runs after dynamics and before the limiter.
        const float processed = std::lerp(
            delayedDry,
            specialWet,
            std::clamp(smoothedDryWet_, 0.0f, 1.0f)
        );

        output[index] = std::lerp(
            inputSample,
            processed,
            std::clamp(smoothedActiveMix_, 0.0f, 1.0f)
        );
        ++dryWriteSequence_;
    }

    return true;
}

const VoiceEffectSettings& VoiceEffectsProcessor::GetSettings() const noexcept
{
    return settings_;
}

bool VoiceEffectsProcessor::IsInitialized() const noexcept
{
    return initialized_;
}

std::size_t VoiceEffectsProcessor::LatencySamples() const noexcept
{
    return initialized_ && settings_.enabled && !settings_.bypassed
        ? ProcessingLatencySamples
        : 0;
}

void VoiceEffectsProcessor::Reset() noexcept
{
    settings_ = {};
    inputFifo_.fill(0.0f);
    outputFifo_.fill(0.0f);
    outputAccumulator_.fill(0.0f);
    fftReal_.fill(0.0f);
    fftImaginary_.fill(0.0f);
    window_.fill(0.0f);
    lastPhase_.fill(0.0f);
    sumPhase_.fill(0.0f);
    analysisMagnitude_.fill(0.0f);
    previousAnalysisMagnitude_.fill(0.0f);
    analysisPhase_.fill(0.0f);
    analysisFrequency_.fill(0.0f);
    logMagnitude_.fill(0.0f);
    smoothedLogMagnitude_.fill(0.0f);
    spectralEnvelope_.fill(0.0f);
    synthesisMagnitude_.fill(0.0f);
    synthesisReal_.fill(0.0f);
    synthesisImaginary_.fill(0.0f);
    nearestPeak_.fill(0);
    spectralPeak_.fill(0);
    dryDelayLine_.fill(0.0f);
    tinyDelayLine_.fill(0.0f);
    dryWriteSequence_ = 0;
    tinyWriteSequence_ = 0;
    rover_ = ProcessingLatencySamples;
    smoothedPitchSemitones_ = 0.0f;
    smoothedFormantSemitones_ = 0.0f;
    smoothedCharacterLowGain_ = 1.0f;
    smoothedCharacterMidGain_ = 1.0f;
    smoothedCharacterHighGain_ = 1.0f;
    smoothedBody_ = 0.0f;
    smoothedDrive_ = 0.0f;
    smoothedRadioMix_ = 0.0f;
    smoothedRobotMix_ = 0.0f;
    smoothedTinyMix_ = 0.0f;
    smoothedDryWet_ = 1.0f;
    smoothedActiveMix_ = 0.0f;
    pitchSmoothingCoefficient_ = 1.0f;
    formantSmoothingCoefficient_ = 1.0f;
    characterSmoothingCoefficient_ = 1.0f;
    bodySmoothingCoefficient_ = 1.0f;
    driveSmoothingCoefficient_ = 1.0f;
    specialEffectSmoothingCoefficient_ = 1.0f;
    mixSmoothingCoefficient_ = 1.0f;
    bypassSmoothingCoefficient_ = 1.0f;
    transientSmoothingCoefficient_ = 1.0f;
    characterLowPassCoefficient_ = 1.0f;
    characterHighCutCoefficient_ = 1.0f;
    bodyHighPassCoefficient_ = 1.0f;
    bodyLowPassCoefficient_ = 1.0f;
    bodyEnvelopeAttackCoefficient_ = 1.0f;
    bodyEnvelopeReleaseCoefficient_ = 1.0f;
    bodyGateAttackCoefficient_ = 1.0f;
    bodyGateReleaseCoefficient_ = 1.0f;
    bodyPeakCoefficients_ = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    bodyBoxCoefficients_ = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    airPreservationCoefficient_ = 1.0f;
    characterLowState_ = 0.0f;
    characterHighCutState_ = 0.0f;
    bodyPeakState_.fill(0.0f);
    bodyBoxState_.fill(0.0f);
    bodyHighPassLowState_ = 0.0f;
    bodyLowPassStateOne_ = 0.0f;
    bodyLowPassStateTwo_ = 0.0f;
    bodyEnvelope_ = 0.0f;
    smoothedBodyVoiceGate_ = 0.0f;
    dryAirLowState_ = 0.0f;
    wetAirLowState_ = 0.0f;
    transientDryMixTarget_ = 0.0f;
    smoothedTransientDryMix_ = 0.0f;
    unvoicedDryMixTarget_ = 0.0f;
    smoothedUnvoicedDryMix_ = 0.0f;
    unvoicedSmoothingCoefficient_ = 1.0f;
    speechPitchBlendAttackCoefficient_ = 1.0f;
    speechPitchBlendReleaseCoefficient_ = 1.0f;
    hybridLevelSmoothingCoefficient_ = 1.0f;
    smoothedSpeechPitchMix_ = 0.0f;
    spectralPitchLevel_ = 0.0f;
    speechPitchLevel_ = 0.0f;
    smoothedSpeechLevelGain_ = 1.0f;
    previousFrameEnergy_ = 0.0f;
    speechPitchPhase_ = 0.25f;
    speechPitchPeriodSamples_ = 240.0f;
    speechPitchDelayRangeSamples_ = SpeechPitchDefaultDelayRangeSamples;
    speechVoicingConfidence_ = 0.0f;
    speechVoicingHoldFrames_ = 0;
    radioLowCutCoefficient_ = 1.0f;
    radioHighCutCoefficient_ = 1.0f;
    radioLowCutStateOne_ = 0.0f;
    radioLowCutStateTwo_ = 0.0f;
    radioHighCutStateOne_ = 0.0f;
    radioHighCutStateTwo_ = 0.0f;
    robotPhase_ = 0.0f;
    tinyDoublerPhase_ = 0.0f;
    hasPreviousSpectrum_ = false;
    initialized_ = false;
}

float VoiceEffectsProcessor::ProcessSpeechPitch(
    const float pitchRatio,
    const float delayedDry
) noexcept
{
    const float semitoneDistance = std::abs(
        12.0f * std::log2(std::max(pitchRatio, 0.0001f))
    );
    if (semitoneDistance <= SpeechPitchMinimumSemitones)
    {
        return delayedDry;
    }

    const float delayRange = std::clamp(
        speechPitchDelayRangeSamples_,
        SpeechPitchMinimumDelayRangeSamples,
        SpeechPitchMaximumDelayRangeSamples
    );
    speechPitchPhase_ += (1.0f - pitchRatio) / delayRange;
    speechPitchPhase_ -= std::floor(speechPitchPhase_);

    float secondPhase = speechPitchPhase_ + 0.5f;
    secondPhase -= std::floor(secondPhase);

    const float minimumDelay = SpeechPitchCentreDelaySamples -
        delayRange * 0.5f;
    const float firstDelay = minimumDelay +
        speechPitchPhase_ * delayRange;
    const float secondDelay = minimumDelay +
        secondPhase * delayRange;

    const float firstWindow = std::sin(
        std::numbers::pi_v<float> * speechPitchPhase_
    );
    const float secondWindow = std::sin(
        std::numbers::pi_v<float> * secondPhase
    );
    const float firstWeight = firstWindow * firstWindow;
    const float secondWeight = secondWindow * secondWindow;

    return ReadDryDelayAt(firstDelay) * firstWeight +
        ReadDryDelayAt(secondDelay) * secondWeight;
}

float VoiceEffectsProcessor::ReadDryDelayAt(
    const float delaySamples
) const noexcept
{
    const float clampedDelay = std::clamp(
        delaySamples,
        1.0f,
        static_cast<float>(DryDelayLineSize - 2)
    );
    const std::uint64_t integerDelay = static_cast<std::uint64_t>(
        clampedDelay
    );
    const float fraction = clampedDelay -
        static_cast<float>(integerDelay);
    const std::uint64_t firstSequence = dryWriteSequence_ - integerDelay;
    const std::uint64_t secondSequence = firstSequence - 1U;
    const float first = dryDelayLine_[
        static_cast<std::size_t>(firstSequence) & DryDelayLineMask
    ];
    const float second = dryDelayLine_[
        static_cast<std::size_t>(secondSequence) & DryDelayLineMask
    ];
    return std::lerp(first, second, fraction);
}

float VoiceEffectsProcessor::ProcessCharacterEq(const float sample) noexcept
{
    characterLowState_ += characterLowPassCoefficient_ *
        (sample - characterLowState_);
    characterHighCutState_ += characterHighCutCoefficient_ *
        (sample - characterHighCutState_);

    const float low = characterLowState_;
    const float mid = characterHighCutState_ - characterLowState_;
    const float high = sample - characterHighCutState_;
    return low * smoothedCharacterLowGain_ +
        mid * smoothedCharacterMidGain_ +
        high * smoothedCharacterHighGain_;
}

float VoiceEffectsProcessor::ProcessBody(const float sample) noexcept
{
    const float amount = std::clamp(smoothedBody_, 0.0f, 1.0f);

    // Isolate a broad chest band without touching pitch or formants. Two
    // low-pass stages keep the parallel reinforcement out of the boxy
    // 400-800 Hz region that made large formant shifts sound tube-like.
    bodyHighPassLowState_ += bodyHighPassCoefficient_ *
        (sample - bodyHighPassLowState_);
    const float highPassed = sample - bodyHighPassLowState_;
    bodyLowPassStateOne_ += bodyLowPassCoefficient_ *
        (highPassed - bodyLowPassStateOne_);
    bodyLowPassStateTwo_ += bodyLowPassCoefficient_ *
        (bodyLowPassStateOne_ - bodyLowPassStateTwo_);
    const float bodyBand = bodyLowPassStateTwo_;

    const float absoluteBody = std::abs(bodyBand);
    const float envelopeCoefficient = absoluteBody > bodyEnvelope_
        ? bodyEnvelopeAttackCoefficient_
        : bodyEnvelopeReleaseCoefficient_;
    bodyEnvelope_ += envelopeCoefficient *
        (absoluteBody - bodyEnvelope_);

    const float levelGate = std::clamp(
        (bodyEnvelope_ - BodyEnvelopeFloor) / BodyEnvelopeRange,
        0.0f,
        1.0f
    );
    const float voicedGate = std::clamp(
        (speechVoicingConfidence_ - BodyVoicingStart) / BodyVoicingRange,
        0.0f,
        1.0f
    );
    const float targetGate = std::clamp(
        std::max(voicedGate, levelGate * BodyUnvoicedGateAmount),
        0.0f,
        1.0f
    );
    const float gateCoefficient = targetGate > smoothedBodyVoiceGate_
        ? bodyGateAttackCoefficient_
        : bodyGateReleaseCoefficient_;
    smoothedBodyVoiceGate_ += gateCoefficient *
        (targetGate - smoothedBodyVoiceGate_);

    // Normalize the isolated band against its own envelope before applying a
    // gentle symmetric density curve. This behaves like parallel low-band
    // compression: vowels gain weight and sustain, while pitch and timing
    // remain untouched and consonants do not acquire a synthetic sub voice.
    const float normalization = std::max(
        bodyEnvelope_ * BodyEnvelopeNormalization,
        BodyMinimumNormalization
    );
    const float normalizedBody = std::clamp(
        bodyBand / normalization,
        -2.0f,
        2.0f
    );
    const float density = BodyDensityMinimum + amount * BodyDensityRange;
    const float denseNormalized = normalizedBody * (1.0f + density) /
        (1.0f + density * std::abs(normalizedBody));
    const float denseBody = denseNormalized * normalization;
    const float parallelBody = std::lerp(
        bodyBand,
        denseBody,
        BodyDensityMix
    );
    const float contribution = std::clamp(
        parallelBody * BodyParallelGain * smoothedBodyVoiceGate_,
        -BodyMaximumContribution,
        BodyMaximumContribution
    );

    const float bodyShaped = ProcessBiquad(
        sample,
        bodyPeakCoefficients_,
        bodyPeakState_
    );
    const float boxControlled = ProcessBiquad(
        bodyShaped,
        bodyBoxCoefficients_,
        bodyBoxState_
    );
    const float staticallyShaped = std::lerp(sample, boxControlled, amount);
    return staticallyShaped + contribution * amount;
}

float VoiceEffectsProcessor::ProcessBiquad(
    const float sample,
    const std::array<float, 5>& coefficients,
    std::array<float, 2>& state
) noexcept
{
    const float output = coefficients[0] * sample + state[0];
    state[0] = coefficients[1] * sample -
        coefficients[3] * output + state[1];
    state[1] = coefficients[2] * sample -
        coefficients[4] * output;
    return output;
}

float VoiceEffectsProcessor::ProcessDrive(const float sample) const noexcept
{
    const float amount = std::clamp(smoothedDrive_, 0.0f, 1.0f);
    if (amount <= 0.0f)
    {
        return sample;
    }

    const float preGain = 1.0f + amount * (MaximumDrivePreGain - 1.0f);
    const float driven = sample * preGain;
    const float softClipped = driven / (1.0f + std::abs(driven));
    return std::lerp(sample, softClipped, amount);
}

float VoiceEffectsProcessor::ProcessSpecialEffects(
    const float sample
) noexcept
{
    const float radio = ProcessRadio(sample);
    const float robot = ProcessRobot(sample);
    const float tiny = ProcessTinyHigh(sample);
    float radioMix = std::clamp(smoothedRadioMix_, 0.0f, 1.0f);
    float robotMix = std::clamp(smoothedRobotMix_, 0.0f, 1.0f);
    float tinyMix = std::clamp(smoothedTinyMix_, 0.0f, 1.0f);
    const float totalMix = radioMix + robotMix + tinyMix;

    if (totalMix > 1.0f)
    {
        radioMix /= totalMix;
        robotMix /= totalMix;
        tinyMix /= totalMix;
    }

    const float dryMix = std::max(
        1.0f - radioMix - robotMix - tinyMix,
        0.0f
    );
    return sample * dryMix + radio * radioMix + robot * robotMix +
        tiny * tinyMix;
}

float VoiceEffectsProcessor::ProcessRadio(const float sample) noexcept
{
    radioLowCutStateOne_ += radioLowCutCoefficient_ *
        (sample - radioLowCutStateOne_);
    const float highPassedOne = sample - radioLowCutStateOne_;

    radioLowCutStateTwo_ += radioLowCutCoefficient_ *
        (highPassedOne - radioLowCutStateTwo_);
    const float highPassedTwo = highPassedOne - radioLowCutStateTwo_;

    radioHighCutStateOne_ += radioHighCutCoefficient_ *
        (highPassedTwo - radioHighCutStateOne_);
    radioHighCutStateTwo_ += radioHighCutCoefficient_ *
        (radioHighCutStateOne_ - radioHighCutStateTwo_);
    return radioHighCutStateTwo_;
}

float VoiceEffectsProcessor::ProcessRobot(const float sample) noexcept
{
    const float carrier = std::sin(robotPhase_);
    robotPhase_ += 2.0f * std::numbers::pi_v<float> * RobotCarrierHz /
        static_cast<float>(ProcessingSampleRate);
    if (robotPhase_ >= 2.0f * std::numbers::pi_v<float>)
    {
        robotPhase_ -= 2.0f * std::numbers::pi_v<float>;
    }

    return std::lerp(sample, sample * carrier, 1.0f - RobotDryAmount);
}

float VoiceEffectsProcessor::ProcessTinyHigh(const float sample) noexcept
{
    tinyDelayLine_[
        static_cast<std::size_t>(tinyWriteSequence_) & TinyDelayLineMask
    ] = sample;

    const float modulatedDelay = TinyDoublerBaseDelaySamples +
        TinyDoublerDepthSamples * std::sin(tinyDoublerPhase_);
    const float doubled = std::lerp(
        sample,
        ReadTinyDelay(modulatedDelay),
        TinyDoublerAmount
    );
    tinyDoublerPhase_ += 2.0f * std::numbers::pi_v<float> *
        TinyDoublerRateHz / static_cast<float>(ProcessingSampleRate);
    if (tinyDoublerPhase_ >= 2.0f * std::numbers::pi_v<float>)
    {
        tinyDoublerPhase_ -= 2.0f * std::numbers::pi_v<float>;
    }
    ++tinyWriteSequence_;

    return doubled;
}

float VoiceEffectsProcessor::ReadTinyDelay(
    const float delaySamples
) const noexcept
{
    const float clampedDelay = std::clamp(
        delaySamples,
        1.0f,
        static_cast<float>(TinyDelayLineSize - 2)
    );
    const std::uint64_t integerDelay = static_cast<std::uint64_t>(
        clampedDelay
    );
    if (tinyWriteSequence_ <= integerDelay)
    {
        return 0.0f;
    }
    const float fraction = clampedDelay -
        static_cast<float>(integerDelay);
    const std::uint64_t firstSequence = tinyWriteSequence_ >= integerDelay
        ? tinyWriteSequence_ - integerDelay
        : 0;
    const std::uint64_t secondSequence = firstSequence > 0
        ? firstSequence - 1
        : 0;
    const float first = tinyDelayLine_[
        static_cast<std::size_t>(firstSequence) & TinyDelayLineMask
    ];
    const float second = tinyDelayLine_[
        static_cast<std::size_t>(secondSequence) & TinyDelayLineMask
    ];
    return std::lerp(first, second, fraction);
}

void VoiceEffectsProcessor::ProcessPitchFrame(
    const float pitchRatio,
    const float formantRatio
) noexcept
{
    constexpr float ExpectedPhaseAdvance =
        2.0f * std::numbers::pi_v<float> /
        static_cast<float>(Oversampling);
    constexpr float FrequencyPerBin =
        static_cast<float>(ProcessingSampleRate) /
        static_cast<float>(FftSize);
    constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

    for (std::size_t index = 0; index < FftSize; ++index)
    {
        fftReal_[index] = inputFifo_[index] * window_[index];
        fftImaginary_[index] = 0.0f;
    }

    TransformFft(false);
    EstimateSpeechPeriod();

    float maximumMagnitude = 0.0f;
    float spectralFlux = 0.0f;
    float magnitudeSum = 0.0f;
    float analysisBandMagnitude = 0.0f;
    float analysisBandLogMagnitude = 0.0f;
    float highBandMagnitude = 0.0f;
    float frameEnergy = 0.0f;
    std::size_t analysisBandBinCount = 0;
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const float real = fftReal_[bin];
        const float imaginary = fftImaginary_[bin];
        const float magnitude = std::hypot(real, imaginary);
        const float phase = std::atan2(imaginary, real);
        analysisMagnitude_[bin] = magnitude;
        analysisPhase_[bin] = phase;
        maximumMagnitude = std::max(maximumMagnitude, magnitude);
        magnitudeSum += magnitude;
        frameEnergy += magnitude * magnitude;

        const float binFrequency = static_cast<float>(bin) * FrequencyPerBin;
        if (binFrequency >= VoiceAnalysisMinimumHz &&
            binFrequency <= VoiceAnalysisMaximumHz)
        {
            const float boundedMagnitude = std::max(
                magnitude,
                MinimumEnvelopeMagnitude
            );
            analysisBandMagnitude += boundedMagnitude;
            analysisBandLogMagnitude += std::log(boundedMagnitude);
            ++analysisBandBinCount;
            if (binFrequency >= VoiceAnalysisHighBandHz)
            {
                highBandMagnitude += boundedMagnitude;
            }
        }

        if (hasPreviousSpectrum_)
        {
            spectralFlux += std::max(
                magnitude - previousAnalysisMagnitude_[bin],
                0.0f
            );
        }
        previousAnalysisMagnitude_[bin] = magnitude;

        float phaseDifference = phase - lastPhase_[bin];
        lastPhase_[bin] = phase;
        phaseDifference -= static_cast<float>(bin) *
            ExpectedPhaseAdvance;
        phaseDifference = std::remainder(phaseDifference, TwoPi);

        const float binDeviation = phaseDifference *
            static_cast<float>(Oversampling) / TwoPi;
        analysisFrequency_[bin] =
            (static_cast<float>(bin) + binDeviation) * FrequencyPerBin;
    }

    const float normalizedFlux = hasPreviousSpectrum_
        ? spectralFlux / std::max(magnitudeSum, MinimumEnvelopeMagnitude)
        : 0.0f;
    const float transientAmount = std::clamp(
        (normalizedFlux - TransientFluxThreshold) / TransientFluxRange,
        0.0f,
        1.0f
    );

    const float arithmeticMagnitude = analysisBandBinCount > 0
        ? analysisBandMagnitude /
            static_cast<float>(analysisBandBinCount)
        : MinimumEnvelopeMagnitude;
    const float geometricMagnitude = analysisBandBinCount > 0
        ? std::exp(
            analysisBandLogMagnitude /
            static_cast<float>(analysisBandBinCount)
        )
        : MinimumEnvelopeMagnitude;
    const float spectralFlatness = std::clamp(
        geometricMagnitude / std::max(
            arithmeticMagnitude,
            MinimumEnvelopeMagnitude
        ),
        0.0f,
        1.0f
    );
    const float highBandRatio = highBandMagnitude / std::max(
        analysisBandMagnitude,
        MinimumEnvelopeMagnitude
    );
    const float flatnessAmount = std::clamp(
        (spectralFlatness - UnvoicedFlatnessStart) /
            UnvoicedFlatnessRange,
        0.0f,
        1.0f
    );
    const float highBandAmount = std::clamp(
        (highBandRatio - UnvoicedHighBandStart) /
            UnvoicedHighBandRange,
        0.0f,
        1.0f
    );
    const float unvoicedAmount = std::clamp(
        std::max(flatnessAmount * 0.82f, highBandAmount * 0.72f),
        0.0f,
        1.0f
    );
    unvoicedDryMixTarget_ = unvoicedAmount;
    transientDryMixTarget_ = transientAmount * MaximumTransientDryMix;

    const bool onset = frameEnergy > MinimumOnsetEnergy &&
        (previousFrameEnergy_ <= MinimumOnsetEnergy ||
            frameEnergy > previousFrameEnergy_ * OnsetEnergyRatio);
    previousFrameEnergy_ = previousFrameEnergy_ * 0.78f +
        frameEnergy * 0.22f;
    const bool resetTransientPhases = onset ||
        (transientAmount > 0.55f && unvoicedAmount > 0.30f);
    const bool preserveUnvoicedPhase = unvoicedAmount > 0.68f;

    EstimateSpectralEnvelope();
    AssignPhaseLockPeaks(maximumMagnitude);
    synthesisMagnitude_.fill(0.0f);
    synthesisReal_.fill(0.0f);
    synthesisImaginary_.fill(0.0f);

    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const float shiftedFrequency = std::clamp(
            analysisFrequency_[bin] * pitchRatio,
            0.0f,
            static_cast<float>(ProcessingSampleRate) * 0.5f
        );
        const float phaseIncrement = TwoPi * shiftedFrequency *
            static_cast<float>(HopSize) /
            static_cast<float>(ProcessingSampleRate);

        if (!hasPreviousSpectrum_ || resetTransientPhases ||
            preserveUnvoicedPhase)
        {
            sumPhase_[bin] = analysisPhase_[bin];
        }
        else
        {
            sumPhase_[bin] = std::remainder(
                sumPhase_[bin] + phaseIncrement,
                TwoPi
            );
        }
    }
    hasPreviousSpectrum_ = true;

    const float phaseLockMaximumBin = PhaseLockMaximumHz / FrequencyPerBin;
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const float shiftedBin = static_cast<float>(bin) * pitchRatio;
        const std::size_t lowerBin = static_cast<std::size_t>(shiftedBin);
        if (lowerBin >= BinCount)
        {
            continue;
        }

        const float fraction = shiftedBin - static_cast<float>(lowerBin);
        const float sourceEnvelope = std::max(
            spectralEnvelope_[bin],
            MinimumEnvelopeMagnitude
        );
        const float targetEnvelope = SampleSpectralEnvelope(
            shiftedBin / formantRatio
        );
        const float rawEnvelopeCorrection = std::max(
            targetEnvelope / sourceEnvelope,
            MinimumEnvelopeMagnitude
        );
        const float envelopeCorrection = std::clamp(
            std::pow(rawEnvelopeCorrection, EnvelopeCorrectionStrength),
            MinimumEnvelopeCorrection,
            MaximumEnvelopeCorrection
        );
        const float correctedMagnitude = analysisMagnitude_[bin] *
            envelopeCorrection;

        const std::size_t peak = static_cast<std::size_t>(
            nearestPeak_[bin]
        );
        const bool phaseLock =
            !preserveUnvoicedPhase &&
            static_cast<float>(bin) <= phaseLockMaximumBin &&
            spectralPeak_[peak] != 0U &&
            analysisMagnitude_[bin] >=
                maximumMagnitude * PhaseLockPeakThreshold * 0.05f;
        const float synthesisPhase = phaseLock
            ? std::remainder(
                sumPhase_[peak] +
                    std::remainder(
                        analysisPhase_[bin] - analysisPhase_[peak],
                        TwoPi
                    ),
                TwoPi
            )
            : sumPhase_[bin];
        const float synthesisReal = correctedMagnitude *
            std::cos(synthesisPhase);
        const float synthesisImaginary = correctedMagnitude *
            std::sin(synthesisPhase);
        const float lowerWeight = 1.0f - fraction;

        synthesisMagnitude_[lowerBin] +=
            correctedMagnitude * lowerWeight;
        synthesisReal_[lowerBin] += synthesisReal * lowerWeight;
        synthesisImaginary_[lowerBin] +=
            synthesisImaginary * lowerWeight;

        if (fraction > 0.0f && lowerBin + 1 < BinCount)
        {
            synthesisMagnitude_[lowerBin + 1] +=
                correctedMagnitude * fraction;
            synthesisReal_[lowerBin + 1] += synthesisReal * fraction;
            synthesisImaginary_[lowerBin + 1] +=
                synthesisImaginary * fraction;
        }
    }

    fftReal_.fill(0.0f);
    fftImaginary_.fill(0.0f);
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const float phase = std::atan2(
            synthesisImaginary_[bin],
            synthesisReal_[bin]
        );
        fftReal_[bin] = synthesisMagnitude_[bin] * std::cos(phase);
        fftImaginary_[bin] = synthesisMagnitude_[bin] * std::sin(phase);
    }

    fftImaginary_[0] = 0.0f;
    fftImaginary_[FftSize / 2] = 0.0f;
    for (std::size_t bin = 1; bin < FftSize / 2; ++bin)
    {
        fftReal_[FftSize - bin] = fftReal_[bin];
        fftImaginary_[FftSize - bin] = -fftImaginary_[bin];
    }

    TransformFft(true);

    for (std::size_t index = 0; index < FftSize; ++index)
    {
        outputAccumulator_[index] +=
            fftReal_[index] * window_[index] * OverlapAddScale;
    }

    for (std::size_t index = 0; index < HopSize; ++index)
    {
        outputFifo_[index] = outputAccumulator_[index];
    }

    for (std::size_t index = 0; index < FftSize - HopSize; ++index)
    {
        outputAccumulator_[index] =
            outputAccumulator_[index + HopSize];
        inputFifo_[index] = inputFifo_[index + HopSize];
    }
    for (std::size_t index = FftSize - HopSize;
        index < FftSize;
        ++index)
    {
        outputAccumulator_[index] = 0.0f;
        inputFifo_[index] = 0.0f;
    }
}

void VoiceEffectsProcessor::AssignPhaseLockPeaks(
    const float maximumMagnitude
) noexcept
{
    constexpr float FrequencyPerBin =
        static_cast<float>(ProcessingSampleRate) /
        static_cast<float>(FftSize);
    const std::size_t maximumPeakBin = std::min(
        static_cast<std::size_t>(PhaseLockMaximumHz / FrequencyPerBin),
        BinCount - 2
    );

    spectralPeak_.fill(0);
    nearestPeak_.fill(0);
    if (maximumMagnitude <= MinimumEnvelopeMagnitude)
    {
        for (std::size_t bin = 0; bin < BinCount; ++bin)
        {
            nearestPeak_[bin] = static_cast<std::uint16_t>(bin);
        }
        return;
    }

    bool hasPeak = false;
    for (std::size_t bin = 1; bin <= maximumPeakBin; ++bin)
    {
        if (analysisMagnitude_[bin] >=
                maximumMagnitude * PhaseLockPeakThreshold &&
            analysisMagnitude_[bin] >= analysisMagnitude_[bin - 1] &&
            analysisMagnitude_[bin] >= analysisMagnitude_[bin + 1])
        {
            spectralPeak_[bin] = 1;
            hasPeak = true;
        }
    }
    if (!hasPeak)
    {
        for (std::size_t bin = 0; bin < BinCount; ++bin)
        {
            nearestPeak_[bin] = static_cast<std::uint16_t>(bin);
        }
        return;
    }

    std::size_t nearestLeft = BinCount;
    for (std::size_t bin = 0; bin <= maximumPeakBin; ++bin)
    {
        if (spectralPeak_[bin] != 0U)
        {
            nearestLeft = bin;
        }
        nearestPeak_[bin] = static_cast<std::uint16_t>(nearestLeft);
    }

    std::size_t nearestRight = BinCount;
    for (std::size_t reverse = maximumPeakBin + 1; reverse > 0; --reverse)
    {
        const std::size_t bin = reverse - 1;
        if (spectralPeak_[bin] != 0U)
        {
            nearestRight = bin;
        }

        const std::size_t left = static_cast<std::size_t>(nearestPeak_[bin]);
        if (left >= BinCount ||
            (nearestRight < BinCount && nearestRight - bin < bin - left))
        {
            nearestPeak_[bin] = static_cast<std::uint16_t>(nearestRight);
        }
    }

    for (std::size_t bin = maximumPeakBin + 1; bin < BinCount; ++bin)
    {
        nearestPeak_[bin] = static_cast<std::uint16_t>(bin);
    }
}

void VoiceEffectsProcessor::EstimateSpeechPeriod() noexcept
{
    constexpr std::size_t Decimation = 4;
    constexpr std::size_t MinimumLag = static_cast<std::size_t>(
        SpeechPitchMinimumPeriodSamples
    );
    constexpr std::size_t MaximumLag = static_cast<std::size_t>(
        SpeechPitchMaximumPeriodSamples
    );

    float bestCorrelation = -1.0f;
    std::size_t bestLag = static_cast<std::size_t>(
        std::lround(speechPitchPeriodSamples_)
    );
    for (std::size_t lag = MinimumLag;
        lag <= MaximumLag;
        lag += Decimation)
    {
        double cross = 0.0;
        double firstEnergy = 0.0;
        double secondEnergy = 0.0;
        for (std::size_t index = lag;
            index < FftSize;
            index += Decimation)
        {
            const double first = inputFifo_[index];
            const double second = inputFifo_[index - lag];
            cross += first * second;
            firstEnergy += first * first;
            secondEnergy += second * second;
        }

        const double denominator = std::sqrt(
            std::max(firstEnergy * secondEnergy, 0.000000000001)
        );
        const float correlation = static_cast<float>(cross / denominator);
        const float continuityPenalty = 0.035f * std::abs(std::log2(
            static_cast<float>(lag) /
                std::max(speechPitchPeriodSamples_, 1.0f)
        ));
        const float score = correlation - continuityPenalty;
        if (score > bestCorrelation)
        {
            bestCorrelation = score;
            bestLag = lag;
        }
    }

    const float rawConfidence = std::clamp(
        (bestCorrelation - SpeechPitchVoicingThreshold) /
            (1.0f - SpeechPitchVoicingThreshold),
        0.0f,
        1.0f
    );
    float confidenceTarget = rawConfidence;
    if (rawConfidence >= SpeechVoicingHoldThreshold)
    {
        speechVoicingHoldFrames_ = SpeechVoicingHoldFrameCount;
    }
    else if (speechVoicingHoldFrames_ > 0U)
    {
        confidenceTarget = std::max(
            confidenceTarget,
            speechVoicingConfidence_ * SpeechVoicingHoldDecay
        );
        --speechVoicingHoldFrames_;
    }

    const float confidenceCoefficient =
        confidenceTarget > speechVoicingConfidence_ ? 0.32f : 0.10f;
    speechVoicingConfidence_ += confidenceCoefficient *
        (confidenceTarget - speechVoicingConfidence_);
    if (rawConfidence <= 0.0f)
    {
        return;
    }

    const float detectedPeriod = static_cast<float>(bestLag);
    speechPitchPeriodSamples_ += 0.18f *
        (detectedPeriod - speechPitchPeriodSamples_);

    int cycleCount = static_cast<int>(std::lround(
        SpeechPitchDefaultDelayRangeSamples /
            std::max(speechPitchPeriodSamples_, 1.0f)
    ));
    cycleCount = std::max(cycleCount, 2);
    if ((cycleCount & 1) != 0)
    {
        ++cycleCount;
    }
    const float targetRange = std::clamp(
        speechPitchPeriodSamples_ * static_cast<float>(cycleCount),
        SpeechPitchMinimumDelayRangeSamples,
        SpeechPitchMaximumDelayRangeSamples
    );
    speechPitchDelayRangeSamples_ += 0.16f *
        (targetRange - speechPitchDelayRangeSamples_);
}

void VoiceEffectsProcessor::EstimateSpectralEnvelope() noexcept
{
    // Two box passes over log magnitude form a compact triangular smoother.
    // This removes harmonic detail while retaining the broad vocal envelope.
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        logMagnitude_[bin] = std::log(std::max(
            analysisMagnitude_[bin],
            MinimumEnvelopeMagnitude
        ));
    }

    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const std::size_t first = bin > SpectralEnvelopeRadius
            ? bin - SpectralEnvelopeRadius
            : 0;
        const std::size_t last = std::min(
            BinCount - 1,
            bin + SpectralEnvelopeRadius
        );
        float sum = 0.0f;
        for (std::size_t neighbour = first; neighbour <= last; ++neighbour)
        {
            sum += logMagnitude_[neighbour];
        }
        smoothedLogMagnitude_[bin] = sum / static_cast<float>(
            last - first + 1
        );
    }

    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const std::size_t first = bin > SpectralEnvelopeRadius
            ? bin - SpectralEnvelopeRadius
            : 0;
        const std::size_t last = std::min(
            BinCount - 1,
            bin + SpectralEnvelopeRadius
        );
        float sum = 0.0f;
        for (std::size_t neighbour = first; neighbour <= last; ++neighbour)
        {
            sum += smoothedLogMagnitude_[neighbour];
        }
        spectralEnvelope_[bin] = std::exp(
            sum / static_cast<float>(last - first + 1)
        );
    }
}

float VoiceEffectsProcessor::SampleSpectralEnvelope(
    const float bin
) const noexcept
{
    const float boundedBin = std::clamp(
        bin,
        0.0f,
        static_cast<float>(BinCount - 1)
    );
    const std::size_t lower = static_cast<std::size_t>(boundedBin);
    const std::size_t upper = std::min(lower + 1, BinCount - 1);
    const float fraction = boundedBin - static_cast<float>(lower);
    return std::lerp(
        spectralEnvelope_[lower],
        spectralEnvelope_[upper],
        fraction
    );
}

void VoiceEffectsProcessor::TransformFft(const bool inverse) noexcept
{
    for (std::size_t index = 1, reversed = 0;
        index < FftSize;
        ++index)
    {
        std::size_t bit = FftSize >> 1U;
        while ((reversed & bit) != 0U)
        {
            reversed ^= bit;
            bit >>= 1U;
        }
        reversed ^= bit;

        if (index < reversed)
        {
            std::swap(fftReal_[index], fftReal_[reversed]);
            std::swap(fftImaginary_[index], fftImaginary_[reversed]);
        }
    }

    for (std::size_t length = 2; length <= FftSize; length <<= 1U)
    {
        const float angle = (inverse ? 2.0f : -2.0f) *
            std::numbers::pi_v<float> /
            static_cast<float>(length);
        const float stepReal = std::cos(angle);
        const float stepImaginary = std::sin(angle);
        const std::size_t halfLength = length >> 1U;

        for (std::size_t start = 0; start < FftSize; start += length)
        {
            float twiddleReal = 1.0f;
            float twiddleImaginary = 0.0f;

            for (std::size_t offset = 0;
                offset < halfLength;
                ++offset)
            {
                const std::size_t evenIndex = start + offset;
                const std::size_t oddIndex = evenIndex + halfLength;
                const float oddReal =
                    fftReal_[oddIndex] * twiddleReal -
                    fftImaginary_[oddIndex] * twiddleImaginary;
                const float oddImaginary =
                    fftReal_[oddIndex] * twiddleImaginary +
                    fftImaginary_[oddIndex] * twiddleReal;
                const float evenReal = fftReal_[evenIndex];
                const float evenImaginary = fftImaginary_[evenIndex];

                fftReal_[evenIndex] = evenReal + oddReal;
                fftImaginary_[evenIndex] = evenImaginary + oddImaginary;
                fftReal_[oddIndex] = evenReal - oddReal;
                fftImaginary_[oddIndex] = evenImaginary - oddImaginary;

                const float nextTwiddleReal =
                    twiddleReal * stepReal -
                    twiddleImaginary * stepImaginary;
                twiddleImaginary =
                    twiddleReal * stepImaginary +
                    twiddleImaginary * stepReal;
                twiddleReal = nextTwiddleReal;
            }
        }
    }

    if (inverse)
    {
        const float inverseSize = 1.0f / static_cast<float>(FftSize);
        for (std::size_t index = 0; index < FftSize; ++index)
        {
            fftReal_[index] *= inverseSize;
            fftImaginary_[index] *= inverseSize;
        }
    }
}

float VoiceEffectsProcessor::ReadDryDelay() const noexcept
{
    const std::size_t readIndex = static_cast<std::size_t>(
        dryWriteSequence_ -
        static_cast<std::uint64_t>(ProcessingLatencySamples)
    ) & DryDelayLineMask;
    return dryDelayLine_[readIndex];
}

float VoiceEffectsProcessor::SmoothingCoefficient(
    const float milliseconds
) noexcept
{
    const float samples = milliseconds * 0.001f *
        static_cast<float>(ProcessingSampleRate);
    return 1.0f - std::exp(-1.0f / samples);
}
