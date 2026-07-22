#include "audio/MicrophoneProcessingSettings.hpp"

#include <cmath>

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

    return "natural";
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

    return "balanced";
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
