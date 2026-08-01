#include "audio/VoiceEffectsProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    constexpr float SemitonesPerOctave = 12.0f;
    constexpr float MinimumSpectralBlendSemitones = 0.25f;
    constexpr float MinimumEnvelopeMagnitude = 0.000001f;
    constexpr float OverlapAddScale = 2.0f / 3.0f;
    constexpr float PhaseLockPeakThreshold = 0.012f;
    constexpr float PhaseLockMaximumHz = 8000.0f;
    constexpr float AirPreservationCrossoverHz = 3200.0f;
    constexpr float MaximumAirPreservation = 0.82f;
    constexpr float UnvoicedWholeBandDryMix = 0.30f;
    constexpr float UnvoicedAirPreservation = 0.78f;
    constexpr float CharacterLowCrossoverHz = 400.0f;
    constexpr float CharacterHighCrossoverHz = 3000.0f;
    constexpr float CharacterFormantScaleSemitones = 2.5f;
    constexpr float MaximumDrivePreGain = 9.0f;
    constexpr float RadioLowCutHz = 300.0f;
    constexpr float RadioHighCutHz = 3400.0f;
    constexpr float RobotCarrierHz = 95.0f;
    constexpr float RobotDryAmount = 0.15f;
    constexpr float TinyDoublerBaseDelaySamples = 576.0f;
    constexpr float TinyDoublerDepthSamples = 72.0f;
    constexpr float TinyDoublerRateHz = 0.8f;
    constexpr float TinyDoublerAmount = 0.035f;
    constexpr float SpeechPitchCompensationRangeSemitones = 5.5f;

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
    voicePolishEngine2_.UpdateSettings(settings);
    const CharacterGains characterGains = BuildCharacterGains(settings);
    smoothedPitchSemitones_ = settings.pitchSemitones;
    smoothedFormantSemitones_ = settings.formantSemitones;
    smoothedCharacterLowGain_ = characterGains.low;
    smoothedCharacterMidGain_ = characterGains.mid;
    smoothedCharacterHighGain_ = characterGains.high;
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
    characterLowPassCoefficient_ = OnePoleCoefficient(
        CharacterLowCrossoverHz
    );
    characterHighCutCoefficient_ = OnePoleCoefficient(
        CharacterHighCrossoverHz
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
    voicePolishEngine2_.UpdateSettings(settings);
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

        const float pitchRatio = std::exp2(
            smoothedPitchSemitones_ / SemitonesPerOctave
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
        const PitchEngine2Result pitchResult = pitchEngine2_.ProcessSample(
            inputSample,
            pitchOutput,
            pitchRatio,
            formantCompatibility
        );
        const float delayedDry = pitchResult.delayedDry;
        const float hybridPitchOutput = pitchResult.hybridPitch;
        const float spectralBlend = std::clamp(
            std::max(
                std::abs(smoothedPitchSemitones_),
                std::abs(smoothedFormantSemitones_)
            ) / MinimumSpectralBlendSemitones,
            0.0f,
            1.0f
        );
        const float protectiveDryMix = std::clamp(
            pitchResult.transientDryMix +
                pitchResult.unvoicedDryMix * UnvoicedWholeBandDryMix,
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
                pitchResult.unvoicedDryMix * UnvoicedAirPreservation,
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
        const float bodyWet = vocalWeightEngine2_.ProcessSample(
            characterWet,
            settings_.body
        );
        const float drivenWet = ProcessDrive(bodyWet);
        const float specialWet = ProcessSpecialEffects(drivenWet);
        // Output gain is intentionally deferred to MicrophoneProcessor so
        // AGC and compression see the untrimmed effect signal. The final gain
        // then runs after dynamics and before the limiter.
        const float transformed = std::lerp(
            delayedDry,
            specialWet,
            std::clamp(smoothedDryWet_, 0.0f, 1.0f)
        );
        const float processed = voicePolishEngine2_.ProcessSample(
            transformed
        );

        output[index] = std::lerp(
            inputSample,
            processed,
            std::clamp(smoothedActiveMix_, 0.0f, 1.0f)
        );
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
    speechAnalysisCore_.Reset();
    speechAnalysisFrame_ = {};
    pitchEngine2_.Reset();
    formantEngine2_.Reset();
    vocalWeightEngine2_.Reset();
    voicePolishEngine2_.Reset();
    inputFifo_.fill(0.0f);
    outputFifo_.fill(0.0f);
    outputAccumulator_.fill(0.0f);
    fftReal_.fill(0.0f);
    fftImaginary_.fill(0.0f);
    window_.fill(0.0f);
    lastPhase_.fill(0.0f);
    sumPhase_.fill(0.0f);
    analysisMagnitude_.fill(0.0f);
    analysisPhase_.fill(0.0f);
    analysisFrequency_.fill(0.0f);
    synthesisMagnitude_.fill(0.0f);
    synthesisReal_.fill(0.0f);
    synthesisImaginary_.fill(0.0f);
    nearestPeak_.fill(0);
    spectralPeak_.fill(0);
    tinyDelayLine_.fill(0.0f);
    tinyWriteSequence_ = 0;
    rover_ = ProcessingLatencySamples;
    smoothedPitchSemitones_ = 0.0f;
    smoothedFormantSemitones_ = 0.0f;
    smoothedCharacterLowGain_ = 1.0f;
    smoothedCharacterMidGain_ = 1.0f;
    smoothedCharacterHighGain_ = 1.0f;
    smoothedDrive_ = 0.0f;
    smoothedRadioMix_ = 0.0f;
    smoothedRobotMix_ = 0.0f;
    smoothedTinyMix_ = 0.0f;
    smoothedDryWet_ = 1.0f;
    smoothedActiveMix_ = 0.0f;
    pitchSmoothingCoefficient_ = 1.0f;
    formantSmoothingCoefficient_ = 1.0f;
    characterSmoothingCoefficient_ = 1.0f;
    driveSmoothingCoefficient_ = 1.0f;
    specialEffectSmoothingCoefficient_ = 1.0f;
    mixSmoothingCoefficient_ = 1.0f;
    bypassSmoothingCoefficient_ = 1.0f;
    characterLowPassCoefficient_ = 1.0f;
    characterHighCutCoefficient_ = 1.0f;
    airPreservationCoefficient_ = 1.0f;
    characterLowState_ = 0.0f;
    characterHighCutState_ = 0.0f;
    dryAirLowState_ = 0.0f;
    wetAirLowState_ = 0.0f;
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

    float maximumMagnitude = 0.0f;
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const float real = fftReal_[bin];
        const float imaginary = fftImaginary_[bin];
        const float magnitude = std::hypot(real, imaginary);
        const float phase = std::atan2(imaginary, real);
        analysisMagnitude_[bin] = magnitude;
        analysisPhase_[bin] = phase;
        maximumMagnitude = std::max(maximumMagnitude, magnitude);

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

    if (speechAnalysisCore_.AnalyzeFrame(inputFifo_, analysisMagnitude_))
    {
        speechAnalysisFrame_ = speechAnalysisCore_.LatestFrame();
    }
    else
    {
        speechAnalysisFrame_ = {};
    }
    pitchEngine2_.UpdateAnalysis(speechAnalysisFrame_);
    vocalWeightEngine2_.UpdateAnalysis(speechAnalysisFrame_);
    voicePolishEngine2_.UpdateAnalysis(speechAnalysisFrame_);

    const float transientAmount =
        speechAnalysisFrame_.transientProbability;
    const float unvoicedAmount =
        speechAnalysisFrame_.unvoicedProbability;
    const bool resetTransientPhases = speechAnalysisFrame_.onset ||
        (transientAmount > 0.55f && unvoicedAmount > 0.30f);
    const bool preserveUnvoicedPhase = unvoicedAmount > 0.68f;

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
    const std::span<const float> spectralEnvelope =
        speechAnalysisCore_.SpectralEnvelope();
    const bool formantFramePrepared = formantEngine2_.PrepareFrame(
        analysisMagnitude_,
        spectralEnvelope,
        speechAnalysisFrame_,
        pitchRatio,
        formantRatio
    );
    for (std::size_t bin = 0; bin < BinCount; ++bin)
    {
        const float shiftedBin = static_cast<float>(bin) * pitchRatio;
        const std::size_t lowerBin = static_cast<std::size_t>(shiftedBin);
        if (lowerBin >= BinCount)
        {
            continue;
        }

        const float fraction = shiftedBin - static_cast<float>(lowerBin);
        const float envelopeCorrection = formantFramePrepared
            ? formantEngine2_.CorrectionForBin(bin)
            : 1.0f;
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

float VoiceEffectsProcessor::SmoothingCoefficient(
    const float milliseconds
) noexcept
{
    const float samples = milliseconds * 0.001f *
        static_cast<float>(ProcessingSampleRate);
    return 1.0f - std::exp(-1.0f / samples);
}
