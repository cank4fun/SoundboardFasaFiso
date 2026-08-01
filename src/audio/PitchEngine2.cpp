#include "audio/PitchEngine2.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    constexpr float SemitonesPerOctave = 12.0f;
    constexpr float MinimumActiveSemitones = 0.08f;
    constexpr float FullActiveSemitones = 0.68f;
    constexpr float MaximumVoicedBlend = 0.88f;
    constexpr float MinimumGrainSpanSamples = 480.0f;
    constexpr float DefaultGrainSpanSamples = 768.0f;
    constexpr float MaximumGrainSpanSamples = 1440.0f;
    constexpr float GrainSpanSmoothingMilliseconds = 28.0f;
    constexpr float BlendAttackMilliseconds = 8.0f;
    constexpr float BlendReleaseMilliseconds = 42.0f;
    constexpr float ProtectionAttackMilliseconds = 3.0f;
    constexpr float ProtectionReleaseMilliseconds = 18.0f;
    constexpr float LevelSmoothingMilliseconds = 42.0f;
    constexpr float MinimumLevel = 0.00025f;
    constexpr float MinimumLevelMatchGain = 0.72f;
    constexpr float MaximumLevelMatchGain = 1.32f;
    constexpr float MaximumTransientDryMix = 0.52f;
    constexpr float ReanchorBlendThreshold = 0.045f;
    constexpr float MinimumPitchConfidence = 0.025f;
    constexpr float MinimumPitchPeriodSamples =
        static_cast<float>(PitchEngine2::ProcessingSampleRate) /
        static_cast<float>(
            SpeechAnalysisCore::MaximumFundamentalFrequencyHz
        );
    constexpr float MaximumPitchPeriodSamples =
        static_cast<float>(PitchEngine2::ProcessingSampleRate) /
        static_cast<float>(
            SpeechAnalysisCore::MinimumFundamentalFrequencyHz
        );
    constexpr float MinimumPitchRatio = 0.5f;
    constexpr float MaximumPitchRatio = 2.0f;

    static_assert(MinimumGrainSpanSamples < DefaultGrainSpanSamples);
    static_assert(DefaultGrainSpanSamples < MaximumGrainSpanSamples);
    static_assert(MaximumGrainSpanSamples * 0.5f <
        static_cast<float>(PitchEngine2::ProcessingLatencySamples) - 2.0f);

    float RaisedCosineWeight(const float phase) noexcept
    {
        const float sine = std::sin(std::numbers::pi_v<float> * phase);
        return sine * sine;
    }

    float CatmullRom(
        const float previous,
        const float first,
        const float second,
        const float next,
        const float fraction
    ) noexcept
    {
        const float squared = fraction * fraction;
        const float cubed = squared * fraction;
        return 0.5f * (
            2.0f * first +
            (-previous + second) * fraction +
            (2.0f * previous - 5.0f * first + 4.0f * second - next) *
                squared +
            (-previous + 3.0f * first - 3.0f * second + next) * cubed
        );
    }
}

PitchEngine2::PitchEngine2() noexcept
{
    grainSpanCoefficient_ = SmoothingCoefficient(
        GrainSpanSmoothingMilliseconds
    );
    blendAttackCoefficient_ = SmoothingCoefficient(
        BlendAttackMilliseconds
    );
    blendReleaseCoefficient_ = SmoothingCoefficient(
        BlendReleaseMilliseconds
    );
    protectionAttackCoefficient_ = SmoothingCoefficient(
        ProtectionAttackMilliseconds
    );
    protectionReleaseCoefficient_ = SmoothingCoefficient(
        ProtectionReleaseMilliseconds
    );
    levelSmoothingCoefficient_ = SmoothingCoefficient(
        LevelSmoothingMilliseconds
    );
    Reset();
}

void PitchEngine2::UpdateAnalysis(
    const SpeechAnalysisFrame& analysis
) noexcept
{
    const bool hasValidPitchPeriod =
        std::isfinite(analysis.pitchPeriodSamples) &&
        analysis.pitchPeriodSamples > 0.0f;
    const float pitchPeriod = hasValidPitchPeriod
        ? std::clamp(
            analysis.pitchPeriodSamples,
            MinimumPitchPeriodSamples,
            MaximumPitchPeriodSamples
        )
        : DefaultGrainSpanSamples;
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
    const float transientProbability = std::clamp(
        Sanitize(analysis.transientProbability),
        0.0f,
        1.0f
    );
    const float unvoicedProbability = std::clamp(
        Sanitize(analysis.unvoicedProbability),
        0.0f,
        1.0f
    );

    if (pitchConfidence > MinimumPitchConfidence && hasValidPitchPeriod)
    {
        int periodCount = static_cast<int>(std::lround(
            DefaultGrainSpanSamples / pitchPeriod
        ));
        periodCount = std::max(periodCount, 2);
        if ((periodCount & 1) != 0)
        {
            ++periodCount;
        }

        targetGrainSpanSamples_ = std::clamp(
            pitchPeriod * static_cast<float>(periodCount),
            MinimumGrainSpanSamples,
            MaximumGrainSpanSamples
        );
    }

    const float periodicQuality = std::sqrt(
        std::max(pitchConfidence * voicingConfidence, 0.0f)
    );
    const float consonantPenalty = 1.0f - std::max(
        unvoicedProbability,
        transientProbability * 0.80f
    );
    targetVoicedQuality_ = std::clamp(
        periodicQuality * speechActivity * consonantPenalty,
        0.0f,
        1.0f
    );
    targetTransientDryMix_ = transientProbability * MaximumTransientDryMix;
    targetUnvoicedDryMix_ = unvoicedProbability;

    if (analysis.onset ||
        (transientProbability > 0.62f && unvoicedProbability > 0.28f))
    {
        reanchorPending_ = true;
    }
}

PitchEngine2Result PitchEngine2::ProcessSample(
    const float inputSample,
    const float spectralPitchSample,
    const float pitchRatio,
    const float formantCompatibility
) noexcept
{
    const float safeInput = Sanitize(inputSample);
    const float safeSpectral = Sanitize(spectralPitchSample);
    const float finitePitchRatio =
        std::isfinite(pitchRatio) && pitchRatio > 0.0f
            ? pitchRatio
            : 1.0f;
    const float safePitchRatio = std::clamp(
        finitePitchRatio,
        MinimumPitchRatio,
        MaximumPitchRatio
    );
    const float safeFormantCompatibility = std::clamp(
        Sanitize(formantCompatibility),
        0.0f,
        1.0f
    );

    delayLine_[
        static_cast<std::size_t>(writeSequence_) & DelayLineMask
    ] = safeInput;

    grainSpanSamples_ += grainSpanCoefficient_ *
        (targetGrainSpanSamples_ - grainSpanSamples_);

    const float transientCoefficient = targetTransientDryMix_ >
            transientDryMix_
        ? protectionAttackCoefficient_
        : protectionReleaseCoefficient_;
    transientDryMix_ += transientCoefficient *
        (targetTransientDryMix_ - transientDryMix_);

    const float unvoicedCoefficient = targetUnvoicedDryMix_ >
            unvoicedDryMix_
        ? protectionAttackCoefficient_
        : protectionReleaseCoefficient_;
    unvoicedDryMix_ += unvoicedCoefficient *
        (targetUnvoicedDryMix_ - unvoicedDryMix_);

    const float semitoneDistance = std::abs(
        SemitonesPerOctave * std::log2(safePitchRatio)
    );
    const float pitchActivity = std::clamp(
        (semitoneDistance - MinimumActiveSemitones) /
            (FullActiveSemitones - MinimumActiveSemitones),
        0.0f,
        1.0f
    );
    const float blendBase = std::clamp(
        MaximumVoicedBlend * pitchActivity * targetVoicedQuality_ *
            safeFormantCompatibility,
        0.0f,
        MaximumVoicedBlend
    );
    const float blendTarget = 1.0f -
        (1.0f - blendBase) * (1.0f - blendBase);
    const float blendCoefficient = blendTarget > voicedBlend_
        ? blendAttackCoefficient_
        : blendReleaseCoefficient_;
    voicedBlend_ += blendCoefficient * (blendTarget - voicedBlend_);

    if (reanchorPending_ && voicedBlend_ <= ReanchorBlendThreshold)
    {
        grainPhase_ = 0.25f;
        reanchorPending_ = false;
    }

    const float delayedDry = ReadDelayCubic(
        static_cast<float>(ProcessingLatencySamples)
    );
    const float timeDomainPitch = pitchActivity > 0.0f
        ? RenderPitchShifted(safePitchRatio)
        : delayedDry;

    spectralLevel_ += levelSmoothingCoefficient_ *
        (std::abs(safeSpectral) - spectralLevel_);
    timeDomainLevel_ += levelSmoothingCoefficient_ *
        (std::abs(timeDomainPitch) - timeDomainLevel_);

    float targetLevelMatchGain = 1.0f;
    if (spectralLevel_ > MinimumLevel && timeDomainLevel_ > MinimumLevel)
    {
        targetLevelMatchGain = std::clamp(
            spectralLevel_ / timeDomainLevel_,
            MinimumLevelMatchGain,
            MaximumLevelMatchGain
        );
    }
    levelMatchGain_ += levelSmoothingCoefficient_ *
        (targetLevelMatchGain - levelMatchGain_);

    const float levelMatchedTimeDomain = timeDomainPitch * levelMatchGain_;
    const float hybridPitch = std::lerp(
        safeSpectral,
        levelMatchedTimeDomain,
        std::clamp(voicedBlend_, 0.0f, 0.96f)
    );

    ++writeSequence_;

    return {
        delayedDry,
        timeDomainPitch,
        Sanitize(hybridPitch),
        std::clamp(voicedBlend_, 0.0f, 1.0f),
        std::clamp(transientDryMix_, 0.0f, MaximumTransientDryMix),
        std::clamp(unvoicedDryMix_, 0.0f, 1.0f)
    };
}

float PitchEngine2::CurrentGrainSpanSamples() const noexcept
{
    return grainSpanSamples_;
}

float PitchEngine2::CurrentVoicedBlend() const noexcept
{
    return voicedBlend_;
}

void PitchEngine2::Reset() noexcept
{
    delayLine_.fill(0.0f);
    writeSequence_ = 0;
    grainPhase_ = 0.25f;
    grainSpanSamples_ = DefaultGrainSpanSamples;
    targetGrainSpanSamples_ = DefaultGrainSpanSamples;
    voicedBlend_ = 0.0f;
    targetVoicedQuality_ = 0.0f;
    transientDryMix_ = 0.0f;
    targetTransientDryMix_ = 0.0f;
    unvoicedDryMix_ = 0.0f;
    targetUnvoicedDryMix_ = 0.0f;
    spectralLevel_ = 0.0f;
    timeDomainLevel_ = 0.0f;
    levelMatchGain_ = 1.0f;
    reanchorPending_ = false;
}

float PitchEngine2::ReadDelayCubic(const float delaySamples) const noexcept
{
    const float clampedDelay = std::clamp(
        Sanitize(delaySamples),
        2.0f,
        static_cast<float>(DelayLineSize - 3U)
    );
    const std::uint64_t integerDelay = static_cast<std::uint64_t>(
        clampedDelay
    );
    if (writeSequence_ < integerDelay)
    {
        return 0.0f;
    }

    const float fraction = clampedDelay -
        static_cast<float>(integerDelay);
    const std::uint64_t firstSequence = writeSequence_ - integerDelay;
    const float first = delayLine_[
        static_cast<std::size_t>(firstSequence) & DelayLineMask
    ];
    if (fraction <= 0.000001f)
    {
        return first;
    }

    const float previous = delayLine_[
        static_cast<std::size_t>(firstSequence + 1U) & DelayLineMask
    ];
    const float second = firstSequence >= 1U
        ? delayLine_[
            static_cast<std::size_t>(firstSequence - 1U) & DelayLineMask
        ]
        : 0.0f;
    const float next = firstSequence >= 2U
        ? delayLine_[
            static_cast<std::size_t>(firstSequence - 2U) & DelayLineMask
        ]
        : 0.0f;

    const float interpolated = CatmullRom(
        previous,
        first,
        second,
        next,
        fraction
    );
    const float localMaximum = std::max({
        std::abs(previous),
        std::abs(first),
        std::abs(second),
        std::abs(next)
    });
    return std::clamp(interpolated, -localMaximum, localMaximum);
}

float PitchEngine2::RenderPitchShifted(const float pitchRatio) noexcept
{
    const float grainSpan = std::clamp(
        grainSpanSamples_,
        MinimumGrainSpanSamples,
        MaximumGrainSpanSamples
    );
    grainPhase_ += (1.0f - pitchRatio) / grainSpan;
    grainPhase_ -= std::floor(grainPhase_);

    float secondPhase = grainPhase_ + 0.5f;
    secondPhase -= std::floor(secondPhase);

    const float minimumDelay =
        static_cast<float>(ProcessingLatencySamples) - grainSpan * 0.5f;
    const float firstDelay = minimumDelay + grainPhase_ * grainSpan;
    const float secondDelay = minimumDelay + secondPhase * grainSpan;
    const float firstWeight = RaisedCosineWeight(grainPhase_);
    const float secondWeight = RaisedCosineWeight(secondPhase);

    return ReadDelayCubic(firstDelay) * firstWeight +
        ReadDelayCubic(secondDelay) * secondWeight;
}

float PitchEngine2::SmoothingCoefficient(
    const float milliseconds
) noexcept
{
    const float sampleCount = std::max(
        milliseconds * 0.001f * static_cast<float>(ProcessingSampleRate),
        1.0f
    );
    return 1.0f - std::exp(-1.0f / sampleCount);
}

float PitchEngine2::Sanitize(const float value) noexcept
{
    return std::isfinite(value) ? value : 0.0f;
}
