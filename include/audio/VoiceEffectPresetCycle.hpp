#pragma once

#include "audio/VoiceEffectSettings.hpp"

#include <optional>
#include <string>
#include <vector>

enum class VoiceEffectPresetCycleDirection
{
    Previous,
    Next
};

struct VoiceEffectPresetSelection
{
    VoiceEffectSettings settings;
    std::string displayName;
    bool userPreset = false;
};

std::optional<VoiceEffectPresetSelection> CycleVoiceEffectPreset(
    const VoiceEffectSettings& currentSettings,
    const std::vector<VoiceEffectUserPreset>& userPresets,
    VoiceEffectPresetCycleDirection direction
);
