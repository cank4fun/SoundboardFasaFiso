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
