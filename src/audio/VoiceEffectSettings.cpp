#include "audio/VoiceEffectSettings.hpp"

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

    bool IsKnownPreset(const VoiceEffectPreset preset)
    {
        switch (preset)
        {
            case VoiceEffectPreset::DeepHeavy:
            case VoiceEffectPreset::HighNasalRap:
            case VoiceEffectPreset::DarkVocal:
            case VoiceEffectPreset::Radio:
            case VoiceEffectPreset::Robot:
            case VoiceEffectPreset::TinyHighVoice:
            case VoiceEffectPreset::Custom:
                return true;
        }

        return false;
    }

    bool NearlyEqual(const float left, const float right)
    {
        return std::abs(left - right) <= 0.0001f;
    }

    std::optional<VoiceEffectSettings> BuildLegacyPresetSnapshot(
        const VoiceEffectPreset preset,
        const bool enabled
    )
    {
        VoiceEffectSettings settings;
        settings.enabled = enabled;
        settings.bypassed = false;
        settings.preset = preset;
        settings.outputGainDb = 0.0f;

        switch (preset)
        {
            case VoiceEffectPreset::DeepHeavy:
                settings.pitchSemitones = -4.0f;
                settings.formantSemitones = -1.5f;
                settings.character = 0.55f;
                settings.drive = 0.20f;
                settings.dryWet = 1.0f;
                break;

            case VoiceEffectPreset::HighNasalRap:
                settings.pitchSemitones = 2.0f;
                settings.formantSemitones = 1.5f;
                settings.character = 0.70f;
                settings.drive = 0.12f;
                settings.dryWet = 1.0f;
                break;

            case VoiceEffectPreset::DarkVocal:
                settings.pitchSemitones = -1.0f;
                settings.formantSemitones = -2.5f;
                settings.character = 0.65f;
                settings.drive = 0.25f;
                settings.dryWet = 1.0f;
                break;

            case VoiceEffectPreset::TinyHighVoice:
                settings.pitchSemitones = 7.0f;
                settings.formantSemitones = 4.0f;
                settings.character = 0.68f;
                settings.drive = 0.03f;
                settings.dryWet = 1.0f;
                break;

            case VoiceEffectPreset::Radio:
            case VoiceEffectPreset::Robot:
            case VoiceEffectPreset::Custom:
                return std::nullopt;
        }

        return settings;
    }

    std::optional<VoiceEffectSettings> BuildQualityPass4ASnapshot(
        const VoiceEffectPreset preset,
        const bool enabled
    )
    {
        VoiceEffectSettings settings;
        settings.enabled = enabled;
        settings.bypassed = false;
        settings.preset = preset;
        settings.outputGainDb = 0.0f;

        switch (preset)
        {
            case VoiceEffectPreset::DeepHeavy:
                settings.pitchSemitones = -3.0f;
                settings.formantSemitones = -1.0f;
                settings.character = 0.42f;
                settings.drive = 0.08f;
                settings.dryWet = 1.0f;
                break;

            case VoiceEffectPreset::HighNasalRap:
                settings.pitchSemitones = 1.5f;
                settings.formantSemitones = 1.1f;
                settings.character = 0.55f;
                settings.drive = 0.05f;
                settings.dryWet = 1.0f;
                break;

            case VoiceEffectPreset::DarkVocal:
                settings.pitchSemitones = -0.5f;
                settings.formantSemitones = -1.8f;
                settings.character = 0.50f;
                settings.drive = 0.10f;
                settings.dryWet = 1.0f;
                break;

            case VoiceEffectPreset::TinyHighVoice:
                settings.pitchSemitones = 5.0f;
                settings.formantSemitones = 2.5f;
                settings.character = 0.52f;
                settings.drive = 0.0f;
                settings.dryWet = 1.0f;
                break;

            case VoiceEffectPreset::Radio:
            case VoiceEffectPreset::Robot:
            case VoiceEffectPreset::Custom:
                return std::nullopt;
        }

        return settings;
    }

    bool SettingsMatchSnapshot(
        const VoiceEffectSettings& settings,
        const VoiceEffectSettings& snapshot
    )
    {
        return settings.preset == snapshot.preset &&
            NearlyEqual(
                settings.pitchSemitones,
                snapshot.pitchSemitones
            ) &&
            NearlyEqual(
                settings.formantSemitones,
                snapshot.formantSemitones
            ) &&
            NearlyEqual(settings.character, snapshot.character) &&
            NearlyEqual(settings.drive, snapshot.drive) &&
            NearlyEqual(settings.dryWet, snapshot.dryWet) &&
            NearlyEqual(settings.outputGainDb, snapshot.outputGainDb);
    }
}

std::string_view VoiceEffectPresetName(const VoiceEffectPreset preset)
{
    switch (preset)
    {
        case VoiceEffectPreset::DeepHeavy:
            return "deep-heavy";
        case VoiceEffectPreset::HighNasalRap:
            return "high-nasal-rap";
        case VoiceEffectPreset::DarkVocal:
            return "dark-vocal";
        case VoiceEffectPreset::Radio:
            return "radio";
        case VoiceEffectPreset::Robot:
            return "robot";
        case VoiceEffectPreset::TinyHighVoice:
            return "tiny-high-voice";
        case VoiceEffectPreset::Custom:
            return "custom";
    }

    return "unknown";
}

std::optional<VoiceEffectPreset> ParseVoiceEffectPreset(
    const std::string_view value
)
{
    if (EqualsAsciiIgnoreCase(value, "deep-heavy"))
    {
        return VoiceEffectPreset::DeepHeavy;
    }

    if (EqualsAsciiIgnoreCase(value, "high-nasal-rap"))
    {
        return VoiceEffectPreset::HighNasalRap;
    }

    if (EqualsAsciiIgnoreCase(value, "dark-vocal"))
    {
        return VoiceEffectPreset::DarkVocal;
    }

    if (EqualsAsciiIgnoreCase(value, "radio"))
    {
        return VoiceEffectPreset::Radio;
    }

    if (EqualsAsciiIgnoreCase(value, "robot"))
    {
        return VoiceEffectPreset::Robot;
    }

    if (EqualsAsciiIgnoreCase(value, "tiny-high-voice"))
    {
        return VoiceEffectPreset::TinyHighVoice;
    }

    if (EqualsAsciiIgnoreCase(value, "custom"))
    {
        return VoiceEffectPreset::Custom;
    }

    return std::nullopt;
}

std::optional<VoiceEffectSettings> BuildVoiceEffectPreset(
    const VoiceEffectPreset preset,
    const bool enabled
)
{
    VoiceEffectSettings settings;
    settings.enabled = enabled;
    settings.bypassed = false;
    settings.preset = preset;
    settings.outputGainDb = 0.0f;

    // Conservative starting points. Radio, robot, and tiny/high keep their
    // dedicated character stages while the shared controls remain available
    // for microphone A/B tuning before the v2.2 release.
    switch (preset)
    {
        case VoiceEffectPreset::DeepHeavy:
            settings.pitchSemitones = -2.5f;
            settings.formantSemitones = -0.8f;
            settings.character = 0.38f;
            settings.drive = 0.04f;
            settings.dryWet = 1.0f;
            break;

        case VoiceEffectPreset::HighNasalRap:
            settings.pitchSemitones = 1.0f;
            settings.formantSemitones = 0.8f;
            settings.character = 0.48f;
            settings.drive = 0.03f;
            settings.dryWet = 1.0f;
            break;

        case VoiceEffectPreset::DarkVocal:
            settings.pitchSemitones = -0.25f;
            settings.formantSemitones = -1.4f;
            settings.character = 0.46f;
            settings.drive = 0.06f;
            settings.dryWet = 1.0f;
            break;

        case VoiceEffectPreset::Radio:
            settings.pitchSemitones = 0.0f;
            settings.formantSemitones = 0.0f;
            settings.character = 0.85f;
            settings.drive = 0.30f;
            settings.dryWet = 1.0f;
            settings.outputGainDb = -1.0f;
            break;

        case VoiceEffectPreset::Robot:
            settings.pitchSemitones = 0.0f;
            settings.formantSemitones = 0.0f;
            settings.character = 0.75f;
            settings.drive = 0.15f;
            settings.dryWet = 1.0f;
            settings.outputGainDb = -1.0f;
            break;

        case VoiceEffectPreset::TinyHighVoice:
            settings.pitchSemitones = 4.0f;
            settings.formantSemitones = 2.0f;
            settings.character = 0.46f;
            settings.drive = 0.0f;
            settings.dryWet = 1.0f;
            break;

        case VoiceEffectPreset::Custom:
            return std::nullopt;

        default:
            return std::nullopt;
    }

    return settings;
}

bool VoiceEffectSettingsMatchPreset(
    const VoiceEffectSettings& settings,
    const VoiceEffectPreset preset
)
{
    const auto expected = BuildVoiceEffectPreset(
        preset,
        settings.enabled
    );

    return expected.has_value() &&
        settings.enabled == expected->enabled &&
        settings.pitchSemitones == expected->pitchSemitones &&
        settings.formantSemitones == expected->formantSemitones &&
        settings.character == expected->character &&
        settings.drive == expected->drive &&
        settings.dryWet == expected->dryWet &&
        settings.outputGainDb == expected->outputGainDb;
}

bool VoiceEffectPresetHasDedicatedStage(const VoiceEffectPreset preset)
{
    return preset == VoiceEffectPreset::Radio ||
        preset == VoiceEffectPreset::Robot ||
        preset == VoiceEffectPreset::TinyHighVoice;
}

bool MigrateLegacyBuiltInVoiceEffectSettings(
    VoiceEffectSettings& settings
)
{
    const auto legacy = BuildLegacyPresetSnapshot(
        settings.preset,
        settings.enabled
    );
    const auto qualityPass4A = BuildQualityPass4ASnapshot(
        settings.preset,
        settings.enabled
    );
    const bool matchesLegacy = legacy.has_value() &&
        SettingsMatchSnapshot(settings, *legacy);
    const bool matchesQualityPass4A = qualityPass4A.has_value() &&
        SettingsMatchSnapshot(settings, *qualityPass4A);
    if (!matchesLegacy && !matchesQualityPass4A)
    {
        return false;
    }

    const auto current = BuildVoiceEffectPreset(
        settings.preset,
        settings.enabled
    );
    if (!current.has_value())
    {
        return false;
    }

    const bool bypassed = settings.bypassed;
    settings = *current;
    settings.bypassed = bypassed;
    return true;
}

bool IsValidVoiceEffectSettings(const VoiceEffectSettings& settings)
{
    using namespace VoiceEffectLimits;

    return IsKnownPreset(settings.preset) &&
        IsInRange(
            settings.pitchSemitones,
            MinimumPitchSemitones,
            MaximumPitchSemitones
        ) &&
        IsInRange(
            settings.formantSemitones,
            MinimumFormantSemitones,
            MaximumFormantSemitones
        ) &&
        IsInRange(
            settings.character,
            MinimumCharacter,
            MaximumCharacter
        ) &&
        IsInRange(
            settings.drive,
            MinimumDrive,
            MaximumDrive
        ) &&
        IsInRange(
            settings.dryWet,
            MinimumDryWet,
            MaximumDryWet
        ) &&
        IsInRange(
            settings.outputGainDb,
            MinimumOutputGainDb,
            MaximumOutputGainDb
        );
}

bool IsValidVoiceEffectUserPresetName(const std::string_view name)
{
    using namespace VoiceEffectLimits;

    if (name.empty() || name.size() > MaximumUserPresetNameBytes ||
        name.front() == ' ' || name.front() == '\t' ||
        name.back() == ' ' || name.back() == '\t')
    {
        return false;
    }

    for (const unsigned char character : name)
    {
        if (character < 0x20U || character == 0x7FU ||
            character == static_cast<unsigned char>('|') ||
            character == static_cast<unsigned char>('='))
        {
            return false;
        }
    }

    return true;
}

bool VoiceEffectUserPresetNamesEqual(
    const std::string_view left,
    const std::string_view right
)
{
    return EqualsAsciiIgnoreCase(left, right);
}

bool IsValidVoiceEffectUserPreset(const VoiceEffectUserPreset& preset)
{
    return IsValidVoiceEffectUserPresetName(preset.name) &&
        !preset.settings.enabled &&
        !preset.settings.bypassed &&
        IsValidVoiceEffectSettings(preset.settings);
}

VoiceEffectSettings ApplyVoiceEffectUserPreset(
    const VoiceEffectUserPreset& preset,
    const bool enabled,
    const bool bypassed
)
{
    VoiceEffectSettings settings = preset.settings;
    settings.enabled = enabled;
    settings.bypassed = bypassed;
    return settings;
}
