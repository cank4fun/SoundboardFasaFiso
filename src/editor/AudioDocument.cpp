#include "editor/AudioDocument.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
    constexpr std::uint32_t MaximumChannelCount = 32U;
    constexpr std::uint32_t MaximumSampleRate = 768000U;

    float ScaleSample(
        const float sample,
        const double scale
    ) noexcept
    {
        return static_cast<float>(
            static_cast<double>(sample) * scale
        );
    }
}

std::size_t AudioFrameRange::FrameCount() const noexcept
{
    return endFrame >= beginFrame
        ? endFrame - beginFrame
        : 0U;
}

bool AudioFrameRange::IsEmpty() const noexcept
{
    return beginFrame == endFrame;
}

AudioDocument::AudioDocument(
    const std::uint32_t sampleRate,
    const std::uint32_t channelCount,
    std::vector<float> interleavedSamples
)
    : sampleRate_(sampleRate),
      channelCount_(channelCount),
      samples_(std::move(interleavedSamples))
{
}

std::optional<AudioDocument> AudioDocument::Create(
    const std::uint32_t sampleRate,
    const std::uint32_t channelCount,
    std::vector<float> interleavedSamples,
    std::string& errorMessage
)
{
    errorMessage.clear();

    if (sampleRate == 0U || sampleRate > MaximumSampleRate)
    {
        errorMessage = "The audio sample rate is invalid.";
        return std::nullopt;
    }

    if (channelCount == 0U || channelCount > MaximumChannelCount)
    {
        errorMessage = "The audio channel count is invalid.";
        return std::nullopt;
    }

    if (interleavedSamples.size() % channelCount != 0U)
    {
        errorMessage =
            "The interleaved audio sample count is not frame-aligned.";
        return std::nullopt;
    }

    for (const float sample : interleavedSamples)
    {
        if (!std::isfinite(sample))
        {
            errorMessage =
                "The audio contains a non-finite sample.";
            return std::nullopt;
        }
    }

    return AudioDocument{
        sampleRate,
        channelCount,
        std::move(interleavedSamples)
    };
}

std::uint32_t AudioDocument::SampleRate() const noexcept
{
    return sampleRate_;
}

std::uint32_t AudioDocument::ChannelCount() const noexcept
{
    return channelCount_;
}

std::size_t AudioDocument::FrameCount() const noexcept
{
    return samples_.size() /
        static_cast<std::size_t>(channelCount_);
}

double AudioDocument::DurationSeconds() const noexcept
{
    return static_cast<double>(FrameCount()) /
        static_cast<double>(sampleRate_);
}

bool AudioDocument::Empty() const noexcept
{
    return samples_.empty();
}

std::span<const float> AudioDocument::Samples() const noexcept
{
    return samples_;
}

bool AudioDocument::IsValidRange(
    const AudioFrameRange range
) const noexcept
{
    return range.beginFrame <= range.endFrame &&
        range.endFrame <= FrameCount();
}

AudioEditResult AudioDocument::CropTo(
    const AudioFrameRange range
)
{
    if (!IsValidRange(range) || range.IsEmpty())
    {
        return AudioEditResult::InvalidRange;
    }

    if (range.beginFrame == 0U && range.endFrame == FrameCount())
    {
        return AudioEditResult::NoChange;
    }

    const std::size_t beginOffset = SampleOffset(range.beginFrame);
    const std::size_t endOffset = SampleOffset(range.endFrame);

    std::vector<float> cropped{
        samples_.begin() + static_cast<std::ptrdiff_t>(beginOffset),
        samples_.begin() + static_cast<std::ptrdiff_t>(endOffset)
    };

    samples_ = std::move(cropped);
    return AudioEditResult::Applied;
}

AudioEditResult AudioDocument::Delete(
    const AudioFrameRange range
)
{
    if (!IsValidRange(range))
    {
        return AudioEditResult::InvalidRange;
    }

    if (range.IsEmpty())
    {
        return AudioEditResult::NoChange;
    }

    const std::size_t beginOffset = SampleOffset(range.beginFrame);
    const std::size_t endOffset = SampleOffset(range.endFrame);

    samples_.erase(
        samples_.begin() + static_cast<std::ptrdiff_t>(beginOffset),
        samples_.begin() + static_cast<std::ptrdiff_t>(endOffset)
    );

    return AudioEditResult::Applied;
}

AudioEditResult AudioDocument::ApplyGainDecibels(
    const AudioFrameRange range,
    const float decibels
)
{
    if (!IsValidRange(range))
    {
        return AudioEditResult::InvalidRange;
    }

    if (!std::isfinite(decibels))
    {
        return AudioEditResult::InvalidValue;
    }

    if (range.IsEmpty() || decibels == 0.0f)
    {
        return AudioEditResult::NoChange;
    }

    const double scale = std::pow(
        10.0,
        static_cast<double>(decibels) / 20.0
    );

    if (!std::isfinite(scale))
    {
        return AudioEditResult::InvalidValue;
    }

    const std::size_t beginOffset = SampleOffset(range.beginFrame);
    const std::size_t endOffset = SampleOffset(range.endFrame);
    float peak = 0.0f;

    for (std::size_t index = beginOffset; index < endOffset; ++index)
    {
        peak = std::max(peak, std::abs(samples_[index]));
    }

    if (peak == 0.0f)
    {
        return AudioEditResult::NoChange;
    }

    const double scaledPeak =
        static_cast<double>(peak) * scale;

    if (!std::isfinite(scaledPeak) ||
        scaledPeak > static_cast<double>(
            std::numeric_limits<float>::max()
        ))
    {
        return AudioEditResult::InvalidValue;
    }

    for (std::size_t index = beginOffset; index < endOffset; ++index)
    {
        samples_[index] = ScaleSample(samples_[index], scale);
    }

    return AudioEditResult::Applied;
}

AudioEditResult AudioDocument::NormalizePeak(
    const AudioFrameRange range,
    const float targetPeak
)
{
    if (!IsValidRange(range))
    {
        return AudioEditResult::InvalidRange;
    }

    if (!std::isfinite(targetPeak) ||
        targetPeak <= 0.0f || targetPeak > 1.0f)
    {
        return AudioEditResult::InvalidValue;
    }

    if (range.IsEmpty())
    {
        return AudioEditResult::NoChange;
    }

    const std::size_t beginOffset = SampleOffset(range.beginFrame);
    const std::size_t endOffset = SampleOffset(range.endFrame);
    float peak = 0.0f;

    for (std::size_t index = beginOffset; index < endOffset; ++index)
    {
        peak = std::max(peak, std::abs(samples_[index]));
    }

    if (peak == 0.0f)
    {
        return AudioEditResult::NoChange;
    }

    if (peak == targetPeak)
    {
        return AudioEditResult::NoChange;
    }

    const double scale =
        static_cast<double>(targetPeak) /
        static_cast<double>(peak);

    for (std::size_t index = beginOffset; index < endOffset; ++index)
    {
        samples_[index] = ScaleSample(samples_[index], scale);
    }

    return AudioEditResult::Applied;
}

AudioEditResult AudioDocument::FadeIn(
    const AudioFrameRange range
)
{
    return ApplyFade(range, true);
}

AudioEditResult AudioDocument::FadeOut(
    const AudioFrameRange range
)
{
    return ApplyFade(range, false);
}

AudioEditResult AudioDocument::ConvertToMono()
{
    if (channelCount_ == 1U)
    {
        return AudioEditResult::NoChange;
    }

    const std::size_t frameCount = FrameCount();
    std::vector<float> monoSamples(frameCount, 0.0f);
    const double divisor = static_cast<double>(channelCount_);

    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
        const std::size_t offset = SampleOffset(frame);
        double sum = 0.0;

        for (std::uint32_t channel = 0U;
             channel < channelCount_;
             ++channel)
        {
            sum += samples_[
                offset + static_cast<std::size_t>(channel)
            ];
        }

        monoSamples[frame] = static_cast<float>(sum / divisor);
    }

    samples_ = std::move(monoSamples);
    channelCount_ = 1U;
    return AudioEditResult::Applied;
}

std::size_t AudioDocument::SampleOffset(
    const std::size_t frame
) const noexcept
{
    return frame * static_cast<std::size_t>(channelCount_);
}

AudioEditResult AudioDocument::ApplyFade(
    const AudioFrameRange range,
    const bool fadeIn
)
{
    if (!IsValidRange(range))
    {
        return AudioEditResult::InvalidRange;
    }

    const std::size_t frameCount = range.FrameCount();

    if (frameCount == 0U)
    {
        return AudioEditResult::NoChange;
    }

    const double denominator = frameCount > 1U
        ? static_cast<double>(frameCount - 1U)
        : 1.0;

    for (std::size_t relativeFrame = 0U;
         relativeFrame < frameCount;
         ++relativeFrame)
    {
        double scale = 0.0;

        if (frameCount > 1U)
        {
            scale = fadeIn
                ? static_cast<double>(relativeFrame) / denominator
                : static_cast<double>(
                    frameCount - 1U - relativeFrame
                ) / denominator;
        }

        const std::size_t offset = SampleOffset(
            range.beginFrame + relativeFrame
        );

        for (std::uint32_t channel = 0U;
             channel < channelCount_;
             ++channel)
        {
            const std::size_t index =
                offset + static_cast<std::size_t>(channel);
            samples_[index] = ScaleSample(samples_[index], scale);
        }
    }

    return AudioEditResult::Applied;
}
