#include "audio/MicrophoneProcessingSettings.hpp"

#include <cmath>
#include <cstddef>

namespace
{
    bool IsInRange(
        const float value,
        const float minimum,
        const float maximum
    )
    {
        return std::isfinite(value) &&
            value >= minimum &&
            value <= maximum;
    }

    bool EqualsAsciiIgnoreCase(
        const std::string_view left,
        const std::string_view right
    )
    {
        if (left.size() != right.size())
        {
            return false;
        }

        for (std::size_t index = 0; index < left.size(); ++index)
        {
            const auto toLowerAscii = [](const char character)
            {
                return character >= 'A' && character <= 'Z'
                    ? static_cast<char>(character - 'A' + 'a')
                    : character;
            };

            if (toLowerAscii(left[index]) != toLowerAscii(right[index]))
            {
                return false;
            }
        }

        return true;
    }

    bool IsKnownPreset(const MicrophoneProcessingPreset preset)
    {
        switch (preset)
        {
            case MicrophoneProcessingPreset::Natural:
            case MicrophoneProcessingPreset::Clean:
            case MicrophoneProcessingPreset::Strong:
            case MicrophoneProcessingPreset::Aggressive:
            case MicrophoneProcessingPreset::Custom:
                return true;
        }

        return false;
    }

    bool IsKnownNoiseSuppressionLevel(
        const MicrophoneNoiseSuppressionLevel level
    )
    {
        switch (level)
        {
            case MicrophoneNoiseSuppressionLevel::Light:
            case MicrophoneNoiseSuppressionLevel::Balanced:
            case MicrophoneNoiseSuppressionLevel::Strong:
                return true;
        }

        return false;
    }
}

std::string_view MicrophoneProcessingPresetName(
    const MicrophoneProcessingPreset preset
)
{
    switch (preset)
    {
        case MicrophoneProcessingPreset::Natural:
            return "natural";
        case MicrophoneProcessingPreset::Clean:
            return "clean";
        case MicrophoneProcessingPreset::Strong:
            return "strong";
        case MicrophoneProcessingPreset::Aggressive:
            return "aggressive";
        case MicrophoneProcessingPreset::Custom:
            return "custom";
    }

    return "unknown";
}

std::string_view MicrophoneNoiseSuppressionLevelName(
    const MicrophoneNoiseSuppressionLevel level
)
{
    switch (level)
    {
        case MicrophoneNoiseSuppressionLevel::Light:
            return "light";
        case MicrophoneNoiseSuppressionLevel::Balanced:
            return "balanced";
        case MicrophoneNoiseSuppressionLevel::Strong:
            return "strong";
    }

    return "unknown";
}

std::optional<MicrophoneProcessingPreset> ParseMicrophoneProcessingPreset(
    const std::string_view value
)
{
    if (EqualsAsciiIgnoreCase(value, "natural"))
    {
        return MicrophoneProcessingPreset::Natural;
    }

    if (EqualsAsciiIgnoreCase(value, "clean"))
    {
        return MicrophoneProcessingPreset::Clean;
    }

    if (EqualsAsciiIgnoreCase(value, "strong"))
    {
        return MicrophoneProcessingPreset::Strong;
    }

    if (EqualsAsciiIgnoreCase(value, "aggressive"))
    {
        return MicrophoneProcessingPreset::Aggressive;
    }

    if (EqualsAsciiIgnoreCase(value, "custom"))
    {
        return MicrophoneProcessingPreset::Custom;
    }

    return std::nullopt;
}

std::optional<MicrophoneNoiseSuppressionLevel>
ParseMicrophoneNoiseSuppressionLevel(const std::string_view value)
{
    if (EqualsAsciiIgnoreCase(value, "light"))
    {
        return MicrophoneNoiseSuppressionLevel::Light;
    }

    if (EqualsAsciiIgnoreCase(value, "balanced"))
    {
        return MicrophoneNoiseSuppressionLevel::Balanced;
    }

    if (EqualsAsciiIgnoreCase(value, "strong"))
    {
        return MicrophoneNoiseSuppressionLevel::Strong;
    }

    return std::nullopt;
}

std::optional<MicrophoneProcessingSettings>
BuildMicrophoneProcessingPreset(
    const MicrophoneProcessingPreset preset,
    const bool enabled
)
{
    MicrophoneProcessingSettings settings;
    settings.enabled = enabled;
    settings.preset = preset;
    settings.noiseSuppressionEnabled = true;
    settings.agcEnabled = false;

    // Conservative voice-processing starting points. Live voice-chat A/B
    // testing may tune these values before the v2.1 release.
    switch (preset)
    {
        case MicrophoneProcessingPreset::Natural:
            settings.noiseSuppressionLevel =
                MicrophoneNoiseSuppressionLevel::Light;
            settings.highPassHz = 80.0f;
            settings.agcEnabled = false;
            settings.agcTargetDbfs = -18.0f;
            settings.compressorThresholdDb = -24.0f;
            settings.compressorRatio = 3.0f;
            settings.compressorAttackMs = 10.0f;
            settings.compressorReleaseMs = 120.0f;
            settings.compressorMakeupDb = 0.0f;
            settings.limiterCeilingDb = -1.0f;
            break;

        case MicrophoneProcessingPreset::Clean:
            settings.noiseSuppressionLevel =
                MicrophoneNoiseSuppressionLevel::Balanced;
            settings.highPassHz = 90.0f;
            settings.agcEnabled = true;
            settings.agcTargetDbfs = -18.0f;
            settings.compressorThresholdDb = -27.0f;
            settings.compressorRatio = 3.5f;
            settings.compressorAttackMs = 8.0f;
            settings.compressorReleaseMs = 140.0f;
            settings.compressorMakeupDb = 2.0f;
            settings.limiterCeilingDb = -1.0f;
            break;

        case MicrophoneProcessingPreset::Strong:
            settings.noiseSuppressionLevel =
                MicrophoneNoiseSuppressionLevel::Strong;
            settings.highPassHz = 100.0f;
            settings.agcEnabled = true;
            settings.agcTargetDbfs = -17.0f;
            settings.compressorThresholdDb = -30.0f;
            settings.compressorRatio = 4.5f;
            settings.compressorAttackMs = 6.0f;
            settings.compressorReleaseMs = 170.0f;
            settings.compressorMakeupDb = 4.0f;
            settings.limiterCeilingDb = -1.0f;
            break;

        case MicrophoneProcessingPreset::Aggressive:
            settings.noiseSuppressionLevel =
                MicrophoneNoiseSuppressionLevel::Strong;
            settings.highPassHz = 120.0f;
            settings.agcEnabled = true;
            settings.agcTargetDbfs = -16.0f;
            settings.compressorThresholdDb = -34.0f;
            settings.compressorRatio = 6.0f;
            settings.compressorAttackMs = 4.0f;
            settings.compressorReleaseMs = 200.0f;
            settings.compressorMakeupDb = 6.0f;
            settings.limiterCeilingDb = -1.5f;
            break;

        case MicrophoneProcessingPreset::Custom:
            return std::nullopt;

        default:
            return std::nullopt;
    }

    return settings;
}

bool MicrophoneProcessingSettingsMatchPreset(
    const MicrophoneProcessingSettings& settings,
    const MicrophoneProcessingPreset preset
)
{
    const auto expected = BuildMicrophoneProcessingPreset(
        preset,
        settings.enabled
    );

    return expected.has_value() &&
        settings.enabled == expected->enabled &&
        settings.highPassEnabled == expected->highPassEnabled &&
        settings.highPassHz == expected->highPassHz &&
        settings.noiseSuppressionEnabled ==
            expected->noiseSuppressionEnabled &&
        settings.noiseSuppressionLevel ==
            expected->noiseSuppressionLevel &&
        settings.agcEnabled == expected->agcEnabled &&
        settings.agcTargetDbfs == expected->agcTargetDbfs &&
        settings.compressorEnabled == expected->compressorEnabled &&
        settings.compressorThresholdDb ==
            expected->compressorThresholdDb &&
        settings.compressorRatio == expected->compressorRatio &&
        settings.compressorAttackMs == expected->compressorAttackMs &&
        settings.compressorReleaseMs == expected->compressorReleaseMs &&
        settings.compressorMakeupDb == expected->compressorMakeupDb &&
        settings.limiterEnabled == expected->limiterEnabled &&
        settings.limiterCeilingDb == expected->limiterCeilingDb;
}

bool IsValidMicrophoneProcessingSettings(
    const MicrophoneProcessingSettings& settings
)
{
    using namespace MicrophoneProcessingLimits;

    return IsKnownPreset(settings.preset) &&
        IsKnownNoiseSuppressionLevel(
            settings.noiseSuppressionLevel
        ) &&
        IsInRange(
            settings.highPassHz,
            MinimumHighPassHz,
            MaximumHighPassHz
        ) &&
        IsInRange(
            settings.agcTargetDbfs,
            MinimumAgcTargetDbfs,
            MaximumAgcTargetDbfs
        ) &&
        IsInRange(
            settings.compressorThresholdDb,
            MinimumCompressorThresholdDb,
            MaximumCompressorThresholdDb
        ) &&
        IsInRange(
            settings.compressorRatio,
            MinimumCompressorRatio,
            MaximumCompressorRatio
        ) &&
        IsInRange(
            settings.compressorAttackMs,
            MinimumCompressorAttackMs,
            MaximumCompressorAttackMs
        ) &&
        IsInRange(
            settings.compressorReleaseMs,
            MinimumCompressorReleaseMs,
            MaximumCompressorReleaseMs
        ) &&
        IsInRange(
            settings.compressorMakeupDb,
            MinimumCompressorMakeupDb,
            MaximumCompressorMakeupDb
        ) &&
        IsInRange(
            settings.limiterCeilingDb,
            MinimumLimiterCeilingDb,
            MaximumLimiterCeilingDb
        );
}
