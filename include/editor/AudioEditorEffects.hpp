#pragma once

#include "editor/AudioDocument.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

enum class AudioEffectScope
{
    WholeDocument,
    Selection
};

inline constexpr float MinimumAudioEffectGainDecibels = -60.0f;
inline constexpr float MaximumAudioEffectGainDecibels = 24.0f;

[[nodiscard]] std::optional<float> ParseAudioEffectGainDecibels(
    std::string_view text
) noexcept;

[[nodiscard]] std::optional<AudioFrameRange> ResolveAudioEffectRange(
    std::size_t frameCount,
    const std::optional<AudioFrameRange>& selection,
    AudioEffectScope scope
) noexcept;

[[nodiscard]] bool WouldAudioEffectGainExceedUnitPeak(
    const AudioDocument& document,
    AudioFrameRange range,
    float decibels
) noexcept;
