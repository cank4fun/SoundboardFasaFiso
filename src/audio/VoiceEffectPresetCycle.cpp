#include "audio/VoiceEffectPresetCycle.hpp"

#include <array>
#include <cstddef>

namespace
{
    constexpr std::array BuiltInPresetOrder{
        VoiceEffectPreset::DeepHeavy,
        VoiceEffectPreset::HighNasalRap,
        VoiceEffectPreset::DarkVocal,
        VoiceEffectPreset::Radio,
        VoiceEffectPreset::Robot,
        VoiceEffectPreset::TinyHighVoice
    };

    bool SettingsEqualIgnoringEnableAndBypass(
        const VoiceEffectSettings& left,
        const VoiceEffectSettings& right
    )
    {
        return left.preset == right.preset &&
            left.pitchSemitones == right.pitchSemitones &&
            left.formantSemitones == right.formantSemitones &&
            left.character == right.character &&
            left.body == right.body &&
            left.drive == right.drive &&
            left.dryWet == right.dryWet &&
            left.outputGainDb == right.outputGainDb;
    }

    std::optional<std::size_t> CurrentSelectionIndex(
        const VoiceEffectSettings& currentSettings,
        const std::vector<VoiceEffectUserPreset>& userPresets
    )
    {
        for (std::size_t index = 0; index < BuiltInPresetOrder.size(); ++index)
        {
            const auto settings = BuildVoiceEffectPreset(
                BuiltInPresetOrder[index],
                currentSettings.enabled
            );

            if (settings.has_value() &&
                SettingsEqualIgnoringEnableAndBypass(
                    currentSettings,
                    *settings
                ))
            {
                return index;
            }
        }

        for (std::size_t index = 0; index < userPresets.size(); ++index)
        {
            if (SettingsEqualIgnoringEnableAndBypass(
                    currentSettings,
                    userPresets[index].settings
                ))
            {
                return BuiltInPresetOrder.size() + index;
            }
        }

        return std::nullopt;
    }
}

std::optional<VoiceEffectPresetSelection> CycleVoiceEffectPreset(
    const VoiceEffectSettings& currentSettings,
    const std::vector<VoiceEffectUserPreset>& userPresets,
    const VoiceEffectPresetCycleDirection direction
)
{
    if (!IsValidVoiceEffectSettings(currentSettings))
    {
        return std::nullopt;
    }

    for (const VoiceEffectUserPreset& preset : userPresets)
    {
        if (!IsValidVoiceEffectUserPreset(preset))
        {
            return std::nullopt;
        }
    }

    const std::size_t selectionCount =
        BuiltInPresetOrder.size() + userPresets.size();

    if (selectionCount == 0)
    {
        return std::nullopt;
    }

    const auto currentIndex = CurrentSelectionIndex(
        currentSettings,
        userPresets
    );

    std::size_t nextIndex = 0;

    if (currentIndex.has_value())
    {
        if (direction == VoiceEffectPresetCycleDirection::Next)
        {
            nextIndex = (*currentIndex + 1U) % selectionCount;
        }
        else
        {
            nextIndex = *currentIndex == 0
                ? selectionCount - 1U
                : *currentIndex - 1U;
        }
    }
    else if (direction == VoiceEffectPresetCycleDirection::Previous)
    {
        nextIndex = selectionCount - 1U;
    }

    if (nextIndex < BuiltInPresetOrder.size())
    {
        const VoiceEffectPreset preset = BuiltInPresetOrder[nextIndex];
        auto settings = BuildVoiceEffectPreset(
            preset,
            currentSettings.enabled
        );

        if (!settings.has_value())
        {
            return std::nullopt;
        }

        settings->bypassed = currentSettings.bypassed;

        return VoiceEffectPresetSelection{
            *settings,
            std::string{VoiceEffectPresetName(preset)},
            false
        };
    }

    const VoiceEffectUserPreset& preset =
        userPresets[nextIndex - BuiltInPresetOrder.size()];

    return VoiceEffectPresetSelection{
        ApplyVoiceEffectUserPreset(
            preset,
            currentSettings.enabled,
            currentSettings.bypassed
        ),
        preset.name,
        true
    };
}
