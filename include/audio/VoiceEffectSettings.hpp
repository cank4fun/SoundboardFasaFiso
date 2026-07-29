#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

enum class VoiceEffectPreset
{
    DeepHeavy,
    HighNasalRap,
    DarkVocal,
    Radio,
    Robot,
    TinyHighVoice,
    Custom
};

namespace VoiceEffectLimits
{
    inline constexpr float MinimumPitchSemitones = -12.0f;
    inline constexpr float MaximumPitchSemitones = 12.0f;
    inline constexpr float MinimumFormantSemitones = -6.0f;
    inline constexpr float MaximumFormantSemitones = 6.0f;
    inline constexpr float MinimumCharacter = 0.0f;
    inline constexpr float MaximumCharacter = 1.0f;
    inline constexpr float MinimumDrive = 0.0f;
    inline constexpr float MaximumDrive = 1.0f;
    inline constexpr float MinimumDryWet = 0.0f;
    inline constexpr float MaximumDryWet = 1.0f;
    inline constexpr float MinimumOutputGainDb = -24.0f;
    inline constexpr float MaximumOutputGainDb = 12.0f;
    inline constexpr std::size_t MaximumUserPresetCount = 32;
    inline constexpr std::size_t MaximumUserPresetNameBytes = 48;
}

struct VoiceEffectSettings
{
    bool enabled = false;
    bool bypassed = false;
    VoiceEffectPreset preset = VoiceEffectPreset::DeepHeavy;

    float pitchSemitones = -2.5f;
    float formantSemitones = -0.8f;
    float character = 0.38f;
    float drive = 0.04f;
    float dryWet = 1.0f;
    float outputGainDb = 0.0f;
};

struct VoiceEffectUserPreset
{
    std::string name;
    VoiceEffectSettings settings;
};

std::string_view VoiceEffectPresetName(VoiceEffectPreset preset);

std::optional<VoiceEffectPreset> ParseVoiceEffectPreset(
    std::string_view value
);

std::optional<VoiceEffectSettings> BuildVoiceEffectPreset(
    VoiceEffectPreset preset,
    bool enabled
);

bool VoiceEffectSettingsMatchPreset(
    const VoiceEffectSettings& settings,
    VoiceEffectPreset preset
);

bool VoiceEffectPresetHasDedicatedStage(VoiceEffectPreset preset);

bool MigrateLegacyBuiltInVoiceEffectSettings(
    VoiceEffectSettings& settings
);

bool IsValidVoiceEffectSettings(const VoiceEffectSettings& settings);

bool IsValidVoiceEffectUserPresetName(std::string_view name);

bool VoiceEffectUserPresetNamesEqual(
    std::string_view left,
    std::string_view right
);

bool IsValidVoiceEffectUserPreset(const VoiceEffectUserPreset& preset);

VoiceEffectSettings ApplyVoiceEffectUserPreset(
    const VoiceEffectUserPreset& preset,
    bool enabled,
    bool bypassed
);
