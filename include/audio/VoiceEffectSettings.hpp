#pragma once

#include <cstddef>
#include <array>
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

enum class VoiceEffectRackModule
{
    ParametricEq,
    DeEsser,
    Gate,
    Compressor
};

inline constexpr std::size_t VoiceEffectRackModuleCount = 4;
using VoiceEffectRackOrder = std::array<
    VoiceEffectRackModule,
    VoiceEffectRackModuleCount
>;

inline constexpr VoiceEffectRackOrder DefaultVoiceEffectRackOrder{
    VoiceEffectRackModule::ParametricEq,
    VoiceEffectRackModule::DeEsser,
    VoiceEffectRackModule::Gate,
    VoiceEffectRackModule::Compressor
};

namespace VoiceEffectLimits
{
    inline constexpr float MinimumPitchSemitones = -12.0f;
    inline constexpr float MaximumPitchSemitones = 12.0f;
    inline constexpr float MinimumFormantSemitones = -6.0f;
    inline constexpr float MaximumFormantSemitones = 6.0f;
    inline constexpr float MinimumCharacter = 0.0f;
    inline constexpr float MaximumCharacter = 1.0f;
    inline constexpr float MinimumBody = 0.0f;
    inline constexpr float MaximumBody = 1.0f;
    inline constexpr float MinimumDrive = 0.0f;
    inline constexpr float MaximumDrive = 1.0f;
    inline constexpr float MinimumDryWet = 0.0f;
    inline constexpr float MaximumDryWet = 1.0f;
    inline constexpr float MinimumOutputGainDb = -24.0f;
    inline constexpr float MaximumOutputGainDb = 12.0f;
    inline constexpr float MinimumEqGainDb = -12.0f;
    inline constexpr float MaximumEqGainDb = 12.0f;
    inline constexpr float MinimumEqLowFrequencyHz = 60.0f;
    inline constexpr float MaximumEqLowFrequencyHz = 400.0f;
    inline constexpr float MinimumEqMidFrequencyHz = 250.0f;
    inline constexpr float MaximumEqMidFrequencyHz = 5000.0f;
    inline constexpr float MinimumEqMidQ = 0.3f;
    inline constexpr float MaximumEqMidQ = 4.0f;
    inline constexpr float MinimumEqHighFrequencyHz = 3000.0f;
    inline constexpr float MaximumEqHighFrequencyHz = 12000.0f;
    inline constexpr float MinimumPolishAmount = 0.0f;
    inline constexpr float MaximumPolishAmount = 1.0f;
    inline constexpr std::size_t MaximumUserPresetCount = 32;
    inline constexpr std::size_t MaximumUserPresetNameBytes = 48;
}

struct VoiceEffectSettings
{
    bool enabled = false;
    bool bypassed = false;
    VoiceEffectPreset preset = VoiceEffectPreset::DeepHeavy;

    float pitchSemitones = -2.5f;
    float formantSemitones = -0.95f;
    float character = 0.34f;
    float body = 0.0f;
    float drive = 0.03f;
    float dryWet = 0.90f;
    float outputGainDb = 3.7f;

    bool parametricEqEnabled = false;
    bool deEsserEnabled = false;
    bool gateEnabled = false;
    bool compressorEnabled = false;
    float eqLowGainDb = 0.0f;
    float eqLowFrequencyHz = 135.0f;
    float eqMidGainDb = 0.0f;
    float eqMidFrequencyHz = 1450.0f;
    float eqMidQ = 0.82f;
    float eqHighGainDb = 0.0f;
    float eqHighFrequencyHz = 6800.0f;
    float deEsserAmount = 0.0f;
    float gateAmount = 0.0f;
    float compressorAmount = 0.0f;
    VoiceEffectRackOrder rackOrder = DefaultVoiceEffectRackOrder;
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

std::string_view VoiceEffectRackModuleName(VoiceEffectRackModule module);

std::optional<VoiceEffectRackModule> ParseVoiceEffectRackModule(
    std::string_view value
);

std::string SerializeVoiceEffectRackOrder(
    const VoiceEffectRackOrder& order
);

std::optional<VoiceEffectRackOrder> ParseVoiceEffectRackOrder(
    std::string_view value
);

bool IsValidVoiceEffectRackOrder(const VoiceEffectRackOrder& order);

bool MoveVoiceEffectRackModule(
    VoiceEffectRackOrder& order,
    std::size_t index,
    int direction
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
