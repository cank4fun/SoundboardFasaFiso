#pragma once

#include "editor/AudioDocument.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct AudioTimeParseResult final
{
    std::size_t frame = 0U;
    bool clamped = false;
};

[[nodiscard]] std::string FormatAudioFrameTime(
    std::size_t frame,
    std::uint32_t sampleRate
);

[[nodiscard]] std::optional<AudioTimeParseResult> ParseAudioFrameTime(
    std::string_view text,
    std::uint32_t sampleRate,
    std::size_t maximumFrame
);

[[nodiscard]] std::size_t SnapAudioFrameToZeroCrossing(
    const AudioDocument& document,
    std::size_t frame,
    std::size_t maximumDistanceFrames
) noexcept;

[[nodiscard]] AudioFrameRange SnapAudioRangeToZeroCrossings(
    const AudioDocument& document,
    AudioFrameRange range,
    std::size_t maximumDistanceFrames
) noexcept;

inline constexpr float DefaultSilenceTrimThresholdDecibels = -50.0f;

[[nodiscard]] std::optional<AudioFrameRange> FindAudibleAudioRange(
    const AudioDocument& document,
    float thresholdDecibels = DefaultSilenceTrimThresholdDecibels,
    std::size_t paddingFrames = 0U
) noexcept;
