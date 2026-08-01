#include "audio/VoiceEffectPresetCycle.hpp"

#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    int failureCount = 0;

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            ++failureCount;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    VoiceEffectUserPreset BuildUserPreset(
        std::string name,
        const float pitch
    )
    {
        VoiceEffectUserPreset preset;
        preset.name = std::move(name);
        preset.settings = *BuildVoiceEffectPreset(
            VoiceEffectPreset::DeepHeavy,
            false
        );
        preset.settings.preset = VoiceEffectPreset::Custom;
        preset.settings.pitchSemitones = pitch;
        return preset;
    }
}

int main()
{
    const std::vector<VoiceEffectUserPreset> userPresets{
        BuildUserPreset("Stage One", -2.0f),
        BuildUserPreset("Stage Two", 3.0f)
    };

    VoiceEffectSettings current = *BuildVoiceEffectPreset(
        VoiceEffectPreset::DeepHeavy,
        false
    );

    const auto next = CycleVoiceEffectPreset(
        current,
        userPresets,
        VoiceEffectPresetCycleDirection::Next
    );
    Expect(next.has_value(), "next built-in preset is available");
    Expect(
        next.has_value() &&
            next->settings.preset == VoiceEffectPreset::HighNasalRap,
        "next cycles to High/Nasal Rap"
    );
    Expect(
        next.has_value() && !next->settings.enabled &&
            !next->settings.bypassed,
        "cycling preserves disabled and non-bypassed state"
    );

    current = *BuildVoiceEffectPreset(
        VoiceEffectPreset::TinyHighVoice,
        true
    );
    current.bypassed = true;
    const auto firstUser = CycleVoiceEffectPreset(
        current,
        userPresets,
        VoiceEffectPresetCycleDirection::Next
    );
    Expect(
        firstUser.has_value() && firstUser->userPreset &&
            firstUser->displayName == "Stage One",
        "next wraps from built-ins into user presets"
    );
    Expect(
        firstUser.has_value() && firstUser->settings.enabled &&
            firstUser->settings.bypassed,
        "cycling preserves enabled and bypass state"
    );

    current = ApplyVoiceEffectUserPreset(userPresets.back(), true, false);
    const auto wrapNext = CycleVoiceEffectPreset(
        current,
        userPresets,
        VoiceEffectPresetCycleDirection::Next
    );
    Expect(
        wrapNext.has_value() && !wrapNext->userPreset &&
            wrapNext->settings.preset == VoiceEffectPreset::DeepHeavy,
        "next wraps from last user preset to first built-in"
    );

    current = *BuildVoiceEffectPreset(
        VoiceEffectPreset::DeepHeavy,
        true
    );
    const auto wrapPrevious = CycleVoiceEffectPreset(
        current,
        userPresets,
        VoiceEffectPresetCycleDirection::Previous
    );
    Expect(
        wrapPrevious.has_value() && wrapPrevious->userPreset &&
            wrapPrevious->displayName == "Stage Two",
        "previous wraps from first built-in to last user preset"
    );

    current = *BuildVoiceEffectPreset(
        VoiceEffectPreset::DeepHeavy,
        true
    );
    current.parametricEqEnabled = true;
    current.eqLowGainDb = 2.0f;
    const auto polishedNext = CycleVoiceEffectPreset(
        current,
        userPresets,
        VoiceEffectPresetCycleDirection::Next
    );
    Expect(
        polishedNext.has_value() &&
            polishedNext->settings.preset == VoiceEffectPreset::DeepHeavy,
        "6E customization is not mistaken for an untouched built-in"
    );

    current = *BuildVoiceEffectPreset(
        VoiceEffectPreset::DeepHeavy,
        true
    );
    current.rackOrder = {
        VoiceEffectRackModule::Compressor,
        VoiceEffectRackModule::ParametricEq,
        VoiceEffectRackModule::DeEsser,
        VoiceEffectRackModule::Gate
    };
    const auto reorderedNext = CycleVoiceEffectPreset(
        current,
        userPresets,
        VoiceEffectPresetCycleDirection::Next
    );
    Expect(
        reorderedNext.has_value() &&
            reorderedNext->settings.preset == VoiceEffectPreset::DeepHeavy,
        "6F rack customization starts cycling at the first built-in"
    );

    current.preset = VoiceEffectPreset::Custom;
    current.pitchSemitones = 0.25f;
    const auto customNext = CycleVoiceEffectPreset(
        current,
        userPresets,
        VoiceEffectPresetCycleDirection::Next
    );
    Expect(
        customNext.has_value() &&
            customNext->settings.preset == VoiceEffectPreset::DeepHeavy,
        "unknown custom state starts next cycle at first built-in"
    );

    auto invalidPresets = userPresets;
    invalidPresets.front().name.clear();
    Expect(
        !CycleVoiceEffectPreset(
            current,
            invalidPresets,
            VoiceEffectPresetCycleDirection::Next
        ).has_value(),
        "invalid user preset list is rejected"
    );

    return failureCount == 0 ? 0 : 1;
}
