#include "audio/VoiceEffectSettings.hpp"

#include <cmath>
#include <cstddef>
#include <utility>

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

    std::optional<VoiceEffectSettings> BuildQualityPass4BSnapshot(
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
        }

        return settings;
    }


    std::optional<VoiceEffectSettings> BuildQualityPass4DSnapshot(
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
                settings.pitchSemitones = -2.5f;
                settings.formantSemitones = -0.9f;
                settings.character = 0.36f;
                settings.drive = 0.035f;
                settings.dryWet = 0.92f;
                settings.outputGainDb = 0.4f;
                break;

            case VoiceEffectPreset::HighNasalRap:
                settings.pitchSemitones = 1.0f;
                settings.formantSemitones = 0.75f;
                settings.character = 0.44f;
                settings.drive = 0.02f;
                settings.dryWet = 0.90f;
                settings.outputGainDb = 0.4f;
                break;

            case VoiceEffectPreset::DarkVocal:
                settings.pitchSemitones = -0.25f;
                settings.formantSemitones = -1.35f;
                settings.character = 0.44f;
                settings.drive = 0.05f;
                settings.dryWet = 0.94f;
                break;

            case VoiceEffectPreset::Radio:
                settings.pitchSemitones = 0.0f;
                settings.formantSemitones = 0.0f;
                settings.character = 0.85f;
                settings.drive = 0.30f;
                settings.dryWet = 1.0f;
                settings.outputGainDb = -0.5f;
                break;

            case VoiceEffectPreset::Robot:
                settings.pitchSemitones = 0.0f;
                settings.formantSemitones = 0.0f;
                settings.character = 0.75f;
                settings.drive = 0.15f;
                settings.dryWet = 1.0f;
                settings.outputGainDb = 0.0f;
                break;

            case VoiceEffectPreset::TinyHighVoice:
                settings.pitchSemitones = 4.0f;
                settings.formantSemitones = 1.8f;
                settings.character = 0.42f;
                settings.drive = 0.0f;
                settings.dryWet = 0.88f;
                settings.outputGainDb = 0.7f;
                break;

            case VoiceEffectPreset::Custom:
                return std::nullopt;
        }

        return settings;
    }

    std::optional<VoiceEffectSettings> BuildQualityPass4ESnapshot(
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
                settings.pitchSemitones = -2.5f;
                settings.formantSemitones = -0.95f;
                settings.character = 0.34f;
                settings.drive = 0.03f;
                settings.dryWet = 0.90f;
                settings.outputGainDb = 1.5f;
                break;

            case VoiceEffectPreset::HighNasalRap:
                settings.pitchSemitones = 1.0f;
                settings.formantSemitones = 0.70f;
                settings.character = 0.42f;
                settings.drive = 0.015f;
                settings.dryWet = 0.88f;
                settings.outputGainDb = 1.5f;
                break;

            case VoiceEffectPreset::DarkVocal:
                settings.pitchSemitones = -0.25f;
                settings.formantSemitones = -1.35f;
                settings.character = 0.43f;
                settings.drive = 0.045f;
                settings.dryWet = 0.93f;
                settings.outputGainDb = 0.4f;
                break;

            case VoiceEffectPreset::Radio:
                settings.pitchSemitones = 0.0f;
                settings.formantSemitones = 0.0f;
                settings.character = 0.85f;
                settings.drive = 0.30f;
                settings.dryWet = 1.0f;
                settings.outputGainDb = 0.9f;
                break;

            case VoiceEffectPreset::Robot:
                settings.pitchSemitones = 0.0f;
                settings.formantSemitones = 0.0f;
                settings.character = 0.75f;
                settings.drive = 0.15f;
                settings.dryWet = 1.0f;
                settings.outputGainDb = 2.0f;
                break;

            case VoiceEffectPreset::TinyHighVoice:
                settings.pitchSemitones = 4.0f;
                settings.formantSemitones = 1.65f;
                settings.character = 0.39f;
                settings.drive = 0.0f;
                settings.dryWet = 0.84f;
                settings.outputGainDb = 1.9f;
                break;

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
            NearlyEqual(settings.body, snapshot.body) &&
            NearlyEqual(settings.drive, snapshot.drive) &&
            NearlyEqual(settings.dryWet, snapshot.dryWet) &&
            NearlyEqual(settings.outputGainDb, snapshot.outputGainDb) &&
            settings.parametricEqEnabled == snapshot.parametricEqEnabled &&
            settings.deEsserEnabled == snapshot.deEsserEnabled &&
            settings.gateEnabled == snapshot.gateEnabled &&
            settings.compressorEnabled == snapshot.compressorEnabled &&
            NearlyEqual(settings.eqLowGainDb, snapshot.eqLowGainDb) &&
            NearlyEqual(
                settings.eqLowFrequencyHz,
                snapshot.eqLowFrequencyHz
            ) &&
            NearlyEqual(settings.eqMidGainDb, snapshot.eqMidGainDb) &&
            NearlyEqual(
                settings.eqMidFrequencyHz,
                snapshot.eqMidFrequencyHz
            ) &&
            NearlyEqual(settings.eqMidQ, snapshot.eqMidQ) &&
            NearlyEqual(settings.eqHighGainDb, snapshot.eqHighGainDb) &&
            NearlyEqual(
                settings.eqHighFrequencyHz,
                snapshot.eqHighFrequencyHz
            ) &&
            NearlyEqual(settings.deEsserAmount, snapshot.deEsserAmount) &&
            NearlyEqual(settings.gateAmount, snapshot.gateAmount) &&
            NearlyEqual(
                settings.compressorAmount,
                snapshot.compressorAmount
            ) &&
            settings.rackOrder == snapshot.rackOrder;
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

std::string_view VoiceEffectRackModuleName(
    const VoiceEffectRackModule module
)
{
    switch (module)
    {
        case VoiceEffectRackModule::ParametricEq:
            return "parametric-eq";
        case VoiceEffectRackModule::DeEsser:
            return "de-esser";
        case VoiceEffectRackModule::Gate:
            return "gate";
        case VoiceEffectRackModule::Compressor:
            return "compressor";
    }

    return "unknown";
}

std::optional<VoiceEffectRackModule> ParseVoiceEffectRackModule(
    const std::string_view value
)
{
    if (EqualsAsciiIgnoreCase(value, "parametric-eq") ||
        EqualsAsciiIgnoreCase(value, "eq"))
    {
        return VoiceEffectRackModule::ParametricEq;
    }

    if (EqualsAsciiIgnoreCase(value, "de-esser") ||
        EqualsAsciiIgnoreCase(value, "de_esser"))
    {
        return VoiceEffectRackModule::DeEsser;
    }

    if (EqualsAsciiIgnoreCase(value, "gate"))
    {
        return VoiceEffectRackModule::Gate;
    }

    if (EqualsAsciiIgnoreCase(value, "compressor"))
    {
        return VoiceEffectRackModule::Compressor;
    }

    return std::nullopt;
}

std::string SerializeVoiceEffectRackOrder(
    const VoiceEffectRackOrder& order
)
{
    std::string result;
    for (std::size_t index = 0; index < order.size(); ++index)
    {
        if (index != 0)
        {
            result.push_back(',');
        }
        result.append(VoiceEffectRackModuleName(order[index]));
    }
    return result;
}

std::optional<VoiceEffectRackOrder> ParseVoiceEffectRackOrder(
    const std::string_view value
)
{
    VoiceEffectRackOrder order{};
    std::size_t start = 0;

    for (std::size_t index = 0; index < order.size(); ++index)
    {
        const std::size_t separator = value.find(',', start);
        const bool finalModule = index + 1U == order.size();
        if ((!finalModule && separator == std::string_view::npos) ||
            (finalModule && separator != std::string_view::npos))
        {
            return std::nullopt;
        }

        const std::size_t end = separator == std::string_view::npos
            ? value.size()
            : separator;
        const std::string_view token = value.substr(start, end - start);
        const auto module = ParseVoiceEffectRackModule(token);
        if (!module.has_value())
        {
            return std::nullopt;
        }

        order[index] = *module;
        start = end + (separator == std::string_view::npos ? 0U : 1U);
    }

    if (start < value.size() || !IsValidVoiceEffectRackOrder(order))
    {
        return std::nullopt;
    }

    return order;
}

bool IsValidVoiceEffectRackOrder(const VoiceEffectRackOrder& order)
{
    std::array<bool, VoiceEffectRackModuleCount> seen{};
    for (const VoiceEffectRackModule module : order)
    {
        const std::size_t index = static_cast<std::size_t>(module);
        if (index >= seen.size() || seen[index])
        {
            return false;
        }
        seen[index] = true;
    }
    return true;
}

bool MoveVoiceEffectRackModule(
    VoiceEffectRackOrder& order,
    const std::size_t index,
    const int direction
)
{
    if (!IsValidVoiceEffectRackOrder(order) ||
        index >= order.size() ||
        (direction != -1 && direction != 1))
    {
        return false;
    }

    if ((direction < 0 && index == 0) ||
        (direction > 0 && index + 1U >= order.size()))
    {
        return false;
    }

    const std::size_t destination = direction < 0
        ? index - 1U
        : index + 1U;
    std::swap(order[index], order[destination]);
    return true;
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

    // Final v2.2 built-in tuning. Radio, robot, and tiny/high keep their
    // dedicated character stages while the shared controls remain available
    // for user-specific microphone adjustment.
    switch (preset)
    {
        case VoiceEffectPreset::DeepHeavy:
            settings.pitchSemitones = -2.5f;
            settings.formantSemitones = -0.95f;
            settings.character = 0.34f;
            settings.drive = 0.03f;
            settings.dryWet = 0.90f;
            settings.outputGainDb = 3.7f;
            break;

        case VoiceEffectPreset::HighNasalRap:
            settings.pitchSemitones = 1.0f;
            settings.formantSemitones = 0.70f;
            settings.character = 0.42f;
            settings.drive = 0.015f;
            settings.dryWet = 0.88f;
            settings.outputGainDb = 3.4f;
            break;

        case VoiceEffectPreset::DarkVocal:
            settings.pitchSemitones = -0.25f;
            settings.formantSemitones = -1.35f;
            settings.character = 0.43f;
            settings.drive = 0.045f;
            settings.dryWet = 0.93f;
            settings.outputGainDb = 1.8f;
            break;

        case VoiceEffectPreset::Radio:
            settings.pitchSemitones = 0.0f;
            settings.formantSemitones = 0.0f;
            settings.character = 0.85f;
            settings.drive = 0.30f;
            settings.dryWet = 1.0f;
            settings.outputGainDb = 3.8f;
            break;

        case VoiceEffectPreset::Robot:
            settings.pitchSemitones = 0.0f;
            settings.formantSemitones = 0.0f;
            settings.character = 0.75f;
            settings.drive = 0.15f;
            settings.dryWet = 1.0f;
            settings.outputGainDb = 5.5f;
            break;

        case VoiceEffectPreset::TinyHighVoice:
            settings.pitchSemitones = 4.0f;
            settings.formantSemitones = 1.65f;
            settings.character = 0.39f;
            settings.drive = 0.0f;
            settings.dryWet = 0.84f;
            settings.outputGainDb = 4.0f;
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
        settings.body == expected->body &&
        settings.drive == expected->drive &&
        settings.dryWet == expected->dryWet &&
        settings.outputGainDb == expected->outputGainDb &&
        settings.parametricEqEnabled == expected->parametricEqEnabled &&
        settings.deEsserEnabled == expected->deEsserEnabled &&
        settings.gateEnabled == expected->gateEnabled &&
        settings.compressorEnabled == expected->compressorEnabled &&
        settings.eqLowGainDb == expected->eqLowGainDb &&
        settings.eqLowFrequencyHz == expected->eqLowFrequencyHz &&
        settings.eqMidGainDb == expected->eqMidGainDb &&
        settings.eqMidFrequencyHz == expected->eqMidFrequencyHz &&
        settings.eqMidQ == expected->eqMidQ &&
        settings.eqHighGainDb == expected->eqHighGainDb &&
        settings.eqHighFrequencyHz == expected->eqHighFrequencyHz &&
        settings.deEsserAmount == expected->deEsserAmount &&
        settings.gateAmount == expected->gateAmount &&
        settings.compressorAmount == expected->compressorAmount &&
        settings.rackOrder == expected->rackOrder;
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
    const auto qualityPass4B = BuildQualityPass4BSnapshot(
        settings.preset,
        settings.enabled
    );
    const auto qualityPass4D = BuildQualityPass4DSnapshot(
        settings.preset,
        settings.enabled
    );
    const auto qualityPass4E = BuildQualityPass4ESnapshot(
        settings.preset,
        settings.enabled
    );
    const bool matchesLegacy = legacy.has_value() &&
        SettingsMatchSnapshot(settings, *legacy);
    const bool matchesQualityPass4A = qualityPass4A.has_value() &&
        SettingsMatchSnapshot(settings, *qualityPass4A);
    const bool matchesQualityPass4B = qualityPass4B.has_value() &&
        SettingsMatchSnapshot(settings, *qualityPass4B);
    const bool matchesQualityPass4D = qualityPass4D.has_value() &&
        SettingsMatchSnapshot(settings, *qualityPass4D);
    const bool matchesQualityPass4E = qualityPass4E.has_value() &&
        SettingsMatchSnapshot(settings, *qualityPass4E);
    if (!matchesLegacy && !matchesQualityPass4A &&
        !matchesQualityPass4B && !matchesQualityPass4D &&
        !matchesQualityPass4E)
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
            settings.body,
            MinimumBody,
            MaximumBody
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
        ) &&
        IsInRange(
            settings.eqLowGainDb,
            MinimumEqGainDb,
            MaximumEqGainDb
        ) &&
        IsInRange(
            settings.eqLowFrequencyHz,
            MinimumEqLowFrequencyHz,
            MaximumEqLowFrequencyHz
        ) &&
        IsInRange(
            settings.eqMidGainDb,
            MinimumEqGainDb,
            MaximumEqGainDb
        ) &&
        IsInRange(
            settings.eqMidFrequencyHz,
            MinimumEqMidFrequencyHz,
            MaximumEqMidFrequencyHz
        ) &&
        IsInRange(
            settings.eqMidQ,
            MinimumEqMidQ,
            MaximumEqMidQ
        ) &&
        IsInRange(
            settings.eqHighGainDb,
            MinimumEqGainDb,
            MaximumEqGainDb
        ) &&
        IsInRange(
            settings.eqHighFrequencyHz,
            MinimumEqHighFrequencyHz,
            MaximumEqHighFrequencyHz
        ) &&
        IsInRange(
            settings.deEsserAmount,
            MinimumPolishAmount,
            MaximumPolishAmount
        ) &&
        IsInRange(
            settings.gateAmount,
            MinimumPolishAmount,
            MaximumPolishAmount
        ) &&
        IsInRange(
            settings.compressorAmount,
            MinimumPolishAmount,
            MaximumPolishAmount
        ) &&
        IsValidVoiceEffectRackOrder(settings.rackOrder);
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
