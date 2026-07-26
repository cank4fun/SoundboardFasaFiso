#include "editor/AudioSelectionTools.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] std::string_view TrimAscii(
        std::string_view text
    ) noexcept
    {
        while (!text.empty() &&
            (text.front() == ' ' || text.front() == '\t' ||
             text.front() == '\r' || text.front() == '\n'))
        {
            text.remove_prefix(1U);
        }

        while (!text.empty() &&
            (text.back() == ' ' || text.back() == '\t' ||
             text.back() == '\r' || text.back() == '\n'))
        {
            text.remove_suffix(1U);
        }

        return text;
    }

    [[nodiscard]] bool ParseUnsigned(
        const std::string_view text,
        std::uint64_t& value
    ) noexcept
    {
        if (text.empty())
        {
            return false;
        }

        const char* const begin = text.data();
        const char* const end = begin + text.size();
        const auto result = std::from_chars(begin, end, value, 10);
        return result.ec == std::errc{} && result.ptr == end;
    }

    [[nodiscard]] bool ParseSecondsComponent(
        std::string_view text,
        double& seconds
    ) noexcept
    {
        text = TrimAscii(text);
        if (text.empty())
        {
            return false;
        }

        std::string normalized{text};
        std::replace(normalized.begin(), normalized.end(), ',', '.');

        const char* const begin = normalized.data();
        const char* const end = begin + normalized.size();
        const auto result = std::from_chars(
            begin,
            end,
            seconds,
            std::chars_format::general
        );
        return result.ec == std::errc{} && result.ptr == end &&
            std::isfinite(seconds) && seconds >= 0.0;
    }

    struct BoundaryCandidate final
    {
        bool crossing = false;
        double amplitude = std::numeric_limits<double>::infinity();
    };

    [[nodiscard]] BoundaryCandidate EvaluateBoundary(
        const AudioDocument& document,
        const std::size_t boundaryFrame
    ) noexcept
    {
        const std::size_t channelCount =
            static_cast<std::size_t>(document.ChannelCount());

        if (boundaryFrame == 0U ||
            boundaryFrame == document.FrameCount())
        {
            return BoundaryCandidate{
                true,
                static_cast<double>(channelCount) * 2.0
            };
        }

        if (boundaryFrame > document.FrameCount())
        {
            return {};
        }

        const std::span<const float> samples = document.Samples();
        const std::size_t previousOffset =
            (boundaryFrame - 1U) * channelCount;
        const std::size_t currentOffset = boundaryFrame * channelCount;

        double amplitude = 0.0;
        bool hasCrossing = false;

        for (std::size_t channel = 0U;
             channel < channelCount;
             ++channel)
        {
            const float previous = samples[previousOffset + channel];
            const float current = samples[currentOffset + channel];
            const bool crossing = previous == 0.0f || current == 0.0f ||
                ((previous < 0.0f) != (current < 0.0f));
            hasCrossing = hasCrossing || crossing;
            amplitude += static_cast<double>(std::abs(previous)) +
                static_cast<double>(std::abs(current));
        }

        return BoundaryCandidate{hasCrossing, amplitude};
    }

}

std::optional<AudioFrameRange> SetAudioSelectionBoundary(
    const std::optional<AudioFrameRange>& selection,
    const std::size_t playheadFrame,
    const std::size_t frameCount,
    const AudioSelectionBoundary boundary
) noexcept
{
    if (frameCount == 0U)
    {
        return std::nullopt;
    }

    const std::size_t frame = std::min(playheadFrame, frameCount);
    const bool hasValidSelection = selection.has_value() &&
        selection->beginFrame < selection->endFrame &&
        selection->endFrame <= frameCount;

    if (boundary == AudioSelectionBoundary::Begin)
    {
        if (frame >= frameCount)
        {
            return std::nullopt;
        }

        const std::size_t endFrame = hasValidSelection &&
            selection->endFrame > frame
            ? selection->endFrame
            : frameCount;
        return AudioFrameRange{frame, endFrame};
    }

    if (frame == 0U)
    {
        return std::nullopt;
    }

    const std::size_t beginFrame = hasValidSelection &&
        selection->beginFrame < frame
        ? selection->beginFrame
        : 0U;
    return AudioFrameRange{beginFrame, frame};
}

std::string FormatAudioFrameTime(
    const std::size_t frame,
    const std::uint32_t sampleRate
)
{
    if (sampleRate == 0U)
    {
        return "00:00.000";
    }

    const std::uint64_t milliseconds = static_cast<std::uint64_t>(
        std::llround(
            static_cast<long double>(frame) * 1000.0L /
            static_cast<long double>(sampleRate)
        )
    );
    const std::uint64_t hours = milliseconds / 3600000U;
    const std::uint64_t minutes = (milliseconds / 60000U) % 60U;
    const std::uint64_t seconds = (milliseconds / 1000U) % 60U;
    const std::uint64_t remainder = milliseconds % 1000U;

    std::string result;
    result.reserve(16U);

    const auto appendTwoDigits = [&result](const std::uint64_t value)
    {
        result.push_back(static_cast<char>('0' + (value / 10U) % 10U));
        result.push_back(static_cast<char>('0' + value % 10U));
    };

    if (hours > 0U)
    {
        result.append(std::to_string(hours));
        result.push_back(':');
    }

    appendTwoDigits(minutes);
    result.push_back(':');
    appendTwoDigits(seconds);
    result.push_back('.');
    result.push_back(static_cast<char>('0' + (remainder / 100U) % 10U));
    result.push_back(static_cast<char>('0' + (remainder / 10U) % 10U));
    result.push_back(static_cast<char>('0' + remainder % 10U));
    return result;
}

std::optional<AudioTimeParseResult> ParseAudioFrameTime(
    std::string_view text,
    const std::uint32_t sampleRate,
    const std::size_t maximumFrame
)
{
    text = TrimAscii(text);
    if (text.empty() || sampleRate == 0U)
    {
        return std::nullopt;
    }

    std::vector<std::string_view> components;
    std::size_t begin = 0U;
    while (begin <= text.size())
    {
        const std::size_t separator = text.find(':', begin);
        const std::size_t end = separator == std::string_view::npos
            ? text.size()
            : separator;
        components.push_back(text.substr(begin, end - begin));
        if (separator == std::string_view::npos)
        {
            break;
        }
        begin = separator + 1U;
    }

    if (components.empty() || components.size() > 3U)
    {
        return std::nullopt;
    }

    double secondsComponent = 0.0;
    if (!ParseSecondsComponent(components.back(), secondsComponent) ||
        (secondsComponent >= 60.0 && components.size() > 1U))
    {
        return std::nullopt;
    }

    std::uint64_t minutes = 0U;
    std::uint64_t hours = 0U;

    if (components.size() >= 2U)
    {
        if (!ParseUnsigned(TrimAscii(components[components.size() - 2U]), minutes) ||
            (components.size() == 3U && minutes >= 60U))
        {
            return std::nullopt;
        }
    }

    if (components.size() == 3U &&
        !ParseUnsigned(TrimAscii(components.front()), hours))
    {
        return std::nullopt;
    }

    const long double totalSeconds =
        static_cast<long double>(hours) * 3600.0L +
        static_cast<long double>(minutes) * 60.0L +
        static_cast<long double>(secondsComponent);

    if (!std::isfinite(static_cast<double>(totalSeconds)) ||
        totalSeconds < 0.0L)
    {
        return std::nullopt;
    }

    const long double rawFrame = totalSeconds *
        static_cast<long double>(sampleRate);
    if (rawFrame > static_cast<long double>(
            std::numeric_limits<std::size_t>::max()))
    {
        return AudioTimeParseResult{maximumFrame, true};
    }

    const std::size_t parsedFrame = static_cast<std::size_t>(
        std::llround(rawFrame)
    );
    return AudioTimeParseResult{
        std::min(parsedFrame, maximumFrame),
        parsedFrame > maximumFrame
    };
}

std::size_t SnapAudioFrameToZeroCrossing(
    const AudioDocument& document,
    const std::size_t frame,
    const std::size_t maximumDistanceFrames
) noexcept
{
    const std::size_t frameCount = document.FrameCount();
    if (frameCount == 0U)
    {
        return 0U;
    }

    const std::size_t clampedFrame = std::min(frame, frameCount);
    const std::size_t begin = clampedFrame > maximumDistanceFrames
        ? clampedFrame - maximumDistanceFrames
        : 0U;
    const std::size_t end = maximumDistanceFrames >
        frameCount - clampedFrame
        ? frameCount
        : clampedFrame + maximumDistanceFrames;

    std::size_t bestFrame = clampedFrame;
    BoundaryCandidate best = EvaluateBoundary(document, clampedFrame);
    std::size_t bestDistance = 0U;

    for (std::size_t candidate = begin;
         candidate <= end;
         ++candidate)
    {
        const BoundaryCandidate evaluated = EvaluateBoundary(
            document,
            candidate
        );
        const std::size_t distance = candidate > clampedFrame
            ? candidate - clampedFrame
            : clampedFrame - candidate;

        const bool betterCrossing = evaluated.crossing && !best.crossing;
        const bool sameCrossingClass = evaluated.crossing == best.crossing;
        const bool closer = distance < bestDistance;
        const bool sameDistance = distance == bestDistance;
        const bool quieter = evaluated.amplitude < best.amplitude;

        if (betterCrossing ||
            (sameCrossingClass && closer) ||
            (sameCrossingClass && sameDistance && quieter) ||
            (sameCrossingClass && sameDistance &&
             evaluated.amplitude == best.amplitude && candidate < bestFrame))
        {
            best = evaluated;
            bestDistance = distance;
            bestFrame = candidate;
        }
    }

    return bestFrame;
}

AudioFrameRange SnapAudioRangeToZeroCrossings(
    const AudioDocument& document,
    AudioFrameRange range,
    const std::size_t maximumDistanceFrames
) noexcept
{
    if (!document.IsValidRange(range))
    {
        return range;
    }

    range.beginFrame = SnapAudioFrameToZeroCrossing(
        document,
        range.beginFrame,
        maximumDistanceFrames
    );
    range.endFrame = SnapAudioFrameToZeroCrossing(
        document,
        range.endFrame,
        maximumDistanceFrames
    );

    if (range.beginFrame > range.endFrame)
    {
        std::swap(range.beginFrame, range.endFrame);
    }

    return range;
}


std::optional<AudioFrameRange> FindAudibleAudioRange(
    const AudioDocument& document,
    const float thresholdDecibels,
    const std::size_t paddingFrames
) noexcept
{
    if (document.Empty() || !std::isfinite(thresholdDecibels) ||
        thresholdDecibels > 0.0f || thresholdDecibels < -160.0f)
    {
        return std::nullopt;
    }

    const double threshold = std::pow(
        10.0,
        static_cast<double>(thresholdDecibels) / 20.0
    );
    const std::size_t channelCount = static_cast<std::size_t>(
        document.ChannelCount()
    );
    const std::size_t frameCount = document.FrameCount();
    const std::span<const float> samples = document.Samples();

    const auto frameIsAudible = [&](const std::size_t frame) noexcept
    {
        const std::size_t offset = frame * channelCount;
        for (std::size_t channel = 0U; channel < channelCount; ++channel)
        {
            if (static_cast<double>(std::abs(samples[offset + channel])) >
                threshold)
            {
                return true;
            }
        }
        return false;
    };

    std::size_t begin = 0U;
    while (begin < frameCount && !frameIsAudible(begin))
    {
        ++begin;
    }
    if (begin == frameCount)
    {
        return std::nullopt;
    }

    std::size_t end = frameCount;
    while (end > begin && !frameIsAudible(end - 1U))
    {
        --end;
    }

    begin = begin > paddingFrames ? begin - paddingFrames : 0U;
    end = paddingFrames > frameCount - end
        ? frameCount
        : end + paddingFrames;
    return AudioFrameRange{begin, end};
}
