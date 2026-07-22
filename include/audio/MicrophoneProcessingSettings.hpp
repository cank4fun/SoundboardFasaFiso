#pragma once

#include <optional>
#include <string_view>

enum class MicrophoneProcessingPreset
{
    Natural,
    Clean,
    Strong,
    Aggressive,
    Custom
};

enum class MicrophoneNoiseSuppressionLevel
{
    Light,
    Balanced,
    Strong
};

namespace MicrophoneProcessingLimits
{
    inline constexpr float MinimumHighPassHz = 20.0f;
    inline constexpr float MaximumHighPassHz = 300.0f;

    inline constexpr float MinimumAgcTargetDbfs = -40.0f;
    inline constexpr float MaximumAgcTargetDbfs = -3.0f;

    inline constexpr float MinimumCompressorThresholdDb = -60.0f;
    inline constexpr float MaximumCompressorThresholdDb = 0.0f;
    inline constexpr float MinimumCompressorRatio = 1.0f;
    inline constexpr float MaximumCompressorRatio = 20.0f;
    inline constexpr float MinimumCompressorAttackMs = 0.1f;
    inline constexpr float MaximumCompressorAttackMs = 200.0f;
    inline constexpr float MinimumCompressorReleaseMs = 5.0f;
    inline constexpr float MaximumCompressorReleaseMs = 2000.0f;
    inline constexpr float MinimumCompressorMakeupDb = -12.0f;
    inline constexpr float MaximumCompressorMakeupDb = 24.0f;

    inline constexpr float MinimumLimiterCeilingDb = -12.0f;
    inline constexpr float MaximumLimiterCeilingDb = 0.0f;
}

struct MicrophoneProcessingSettings
{
    bool enabled = false;
    MicrophoneProcessingPreset preset =
        MicrophoneProcessingPreset::Natural;

    bool highPassEnabled = true;
    float highPassHz = 80.0f;

    bool noiseSuppressionEnabled = false;
    MicrophoneNoiseSuppressionLevel noiseSuppressionLevel =
        MicrophoneNoiseSuppressionLevel::Balanced;

    bool agcEnabled = false;
    float agcTargetDbfs = -18.0f;

    bool compressorEnabled = true;
    float compressorThresholdDb = -24.0f;
    float compressorRatio = 3.0f;
    float compressorAttackMs = 10.0f;
    float compressorReleaseMs = 120.0f;
    float compressorMakeupDb = 0.0f;

    bool limiterEnabled = true;
    float limiterCeilingDb = -1.0f;
};

std::string_view MicrophoneProcessingPresetName(
    MicrophoneProcessingPreset preset
);

std::string_view MicrophoneNoiseSuppressionLevelName(
    MicrophoneNoiseSuppressionLevel level
);

std::optional<MicrophoneProcessingPreset> ParseMicrophoneProcessingPreset(
    std::string_view value
);

std::optional<MicrophoneNoiseSuppressionLevel>
ParseMicrophoneNoiseSuppressionLevel(std::string_view value);

std::optional<MicrophoneProcessingSettings>
BuildMicrophoneProcessingPreset(
    MicrophoneProcessingPreset preset,
    bool enabled
);

bool MicrophoneProcessingSettingsMatchPreset(
    const MicrophoneProcessingSettings& settings,
    MicrophoneProcessingPreset preset
);

bool IsValidMicrophoneProcessingSettings(
    const MicrophoneProcessingSettings& settings
);
