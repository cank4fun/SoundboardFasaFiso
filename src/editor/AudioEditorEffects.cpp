#include "editor/AudioEditorEffects.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <span>

namespace
{
    constexpr std::size_t MaximumGainTextLength = 32U;

    [[nodiscard]] bool IsAsciiWhitespace(const char value) noexcept
    {
        return value == ' ' || value == '\t' || value == '\r' ||
            value == '\n' || value == '\f' || value == '\v';
    }
}

std::optional<float> ParseAudioEffectGainDecibels(
    const std::string_view text
) noexcept
{
    const auto first = std::find_if_not(
        text.begin(),
        text.end(),
        IsAsciiWhitespace
    );
    const auto last = std::find_if_not(
        text.rbegin(),
        text.rend(),
        IsAsciiWhitespace
    ).base();

    if (first >= last)
    {
        return std::nullopt;
    }

    const std::size_t length = static_cast<std::size_t>(last - first);
    if (length > MaximumGainTextLength)
    {
        return std::nullopt;
    }

    std::array<char, MaximumGainTextLength + 1U> normalized{};
    std::size_t outputLength = 0U;
    std::size_t separatorCount = 0U;
    char separator = '\0';

    for (auto iterator = first; iterator != last; ++iterator)
    {
        char value = *iterator;
        if (value == ',' || value == '.')
        {
            if (separator == '\0')
            {
                separator = value;
            }
            else if (separator != value)
            {
                return std::nullopt;
            }

            ++separatorCount;
            if (separatorCount > 1U)
            {
                return std::nullopt;
            }
            value = '.';
        }

        if (outputLength == 0U && value == '+')
        {
            continue;
        }

        normalized[outputLength++] = value;
    }

    if (outputLength == 0U)
    {
        return std::nullopt;
    }

    float decibels = 0.0f;
    const char* const begin = normalized.data();
    const char* const end = begin + outputLength;
    const auto result = std::from_chars(
        begin,
        end,
        decibels,
        std::chars_format::general
    );

    if (result.ec != std::errc{} || result.ptr != end ||
        !std::isfinite(decibels) ||
        decibels < MinimumAudioEffectGainDecibels ||
        decibels > MaximumAudioEffectGainDecibels)
    {
        return std::nullopt;
    }

    return decibels;
}

std::optional<AudioFrameRange> ResolveAudioEffectRange(
    const std::size_t frameCount,
    const std::optional<AudioFrameRange>& selection,
    const AudioEffectScope scope
) noexcept
{
    if (frameCount == 0U)
    {
        return std::nullopt;
    }

    if (scope == AudioEffectScope::Selection && selection.has_value() &&
        selection->beginFrame <= selection->endFrame &&
        selection->endFrame <= frameCount && !selection->IsEmpty())
    {
        return selection;
    }

    return AudioFrameRange{0U, frameCount};
}


bool WouldAudioEffectGainExceedUnitPeak(
    const AudioDocument& document,
    const AudioFrameRange range,
    const float decibels
) noexcept
{
    if (!document.IsValidRange(range) || range.IsEmpty() ||
        !std::isfinite(decibels))
    {
        return false;
    }

    const double scale = std::pow(
        10.0,
        static_cast<double>(decibels) / 20.0
    );
    if (!std::isfinite(scale))
    {
        return true;
    }

    const std::size_t channelCount = static_cast<std::size_t>(
        document.ChannelCount()
    );
    const std::size_t beginOffset = range.beginFrame * channelCount;
    const std::size_t endOffset = range.endFrame * channelCount;
    const std::span<const float> samples = document.Samples();

    for (std::size_t index = beginOffset; index < endOffset; ++index)
    {
        if (static_cast<double>(std::abs(samples[index])) * scale > 1.0)
        {
            return true;
        }
    }

    return false;
}
