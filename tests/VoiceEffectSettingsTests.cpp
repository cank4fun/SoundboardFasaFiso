#include "audio/VoiceEffectSettings.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
    int failureCount = 0;

    void Expect(const bool condition, const std::string_view message)
    {
        if (condition)
        {
            return;
        }

        ++failureCount;
        std::cerr << "FAILED: " << message << '\n';
    }

    bool NearlyEqual(const float left, const float right)
    {
        return std::abs(left - right) <= 0.0001f;
    }

    void TestDefaults()
    {
        const VoiceEffectSettings settings;

        Expect(!settings.enabled, "voice effects default to disabled");
        Expect(!settings.bypassed, "the local bypass defaults to off");
        Expect(
            settings.preset == VoiceEffectPreset::DeepHeavy,
            "the default preset is deep-heavy"
        );
        Expect(
            IsValidVoiceEffectSettings(settings),
            "default voice-effect settings are valid"
        );
    }

    void TestStableNamesAndParsing()
    {
        constexpr std::array cases{
            std::pair{VoiceEffectPreset::DeepHeavy, "deep-heavy"},
            std::pair{VoiceEffectPreset::HighNasalRap, "high-nasal-rap"},
            std::pair{VoiceEffectPreset::DarkVocal, "dark-vocal"},
            std::pair{VoiceEffectPreset::Radio, "radio"},
            std::pair{VoiceEffectPreset::Robot, "robot"},
            std::pair{VoiceEffectPreset::TinyHighVoice, "tiny-high-voice"},
            std::pair{VoiceEffectPreset::Custom, "custom"}
        };

        for (const auto& [preset, name] : cases)
        {
            Expect(
                VoiceEffectPresetName(preset) == name,
                "preset name is stable"
            );
            Expect(
                ParseVoiceEffectPreset(name) == preset,
                "preset name round-trips"
            );
        }

        Expect(
            ParseVoiceEffectPreset("HIGH-NASAL-RAP") ==
                VoiceEffectPreset::HighNasalRap,
            "preset parsing is ASCII case-insensitive"
        );
        Expect(
            !ParseVoiceEffectPreset("invalid").has_value(),
            "unknown preset is rejected"
        );
        Expect(
            VoiceEffectPresetName(
                static_cast<VoiceEffectPreset>(999)
            ) == "unknown",
            "unknown preset has an explicit name"
        );
    }

    void TestPresetSnapshots()
    {
        struct PresetCase
        {
            VoiceEffectPreset preset;
            float pitchSemitones;
            float formantSemitones;
            float character;
            float drive;
            float dryWet;
            float outputGainDb;
        };

        constexpr std::array cases{
            PresetCase{VoiceEffectPreset::DeepHeavy,
                -2.5f, -0.8f, 0.38f, 0.04f, 1.0f, 0.0f},
            PresetCase{VoiceEffectPreset::HighNasalRap,
                1.0f, 0.8f, 0.48f, 0.03f, 1.0f, 0.0f},
            PresetCase{VoiceEffectPreset::DarkVocal,
                -0.25f, -1.4f, 0.46f, 0.06f, 1.0f, 0.0f},
            PresetCase{VoiceEffectPreset::Radio,
                0.0f, 0.0f, 0.85f, 0.30f, 1.0f, -1.0f},
            PresetCase{VoiceEffectPreset::Robot,
                0.0f, 0.0f, 0.75f, 0.15f, 1.0f, -1.0f},
            PresetCase{VoiceEffectPreset::TinyHighVoice,
                4.0f, 2.0f, 0.46f, 0.0f, 1.0f, 0.0f}
        };

        for (const PresetCase& testCase : cases)
        {
            const auto settings = BuildVoiceEffectPreset(
                testCase.preset,
                true
            );

            Expect(settings.has_value(), "built-in preset is available");
            if (!settings.has_value())
            {
                continue;
            }

            Expect(settings->enabled, "preset preserves enabled state");
            Expect(!settings->bypassed, "preset clears local bypass");
            Expect(settings->preset == testCase.preset,
                "preset identity is stored");
            Expect(settings->pitchSemitones == testCase.pitchSemitones,
                "preset pitch is stable");
            Expect(settings->formantSemitones == testCase.formantSemitones,
                "preset formant is stable");
            Expect(settings->character == testCase.character,
                "preset character is stable");
            Expect(settings->drive == testCase.drive,
                "preset drive is stable");
            Expect(settings->dryWet == testCase.dryWet,
                "preset dry/wet is stable");
            Expect(settings->outputGainDb == testCase.outputGainDb,
                "preset output gain is stable");
            Expect(IsValidVoiceEffectSettings(*settings),
                "built-in preset is valid");
            Expect(VoiceEffectSettingsMatchPreset(
                *settings,
                testCase.preset
            ), "preset snapshot matches itself");

            VoiceEffectSettings bypassed = *settings;
            bypassed.bypassed = true;
            Expect(VoiceEffectSettingsMatchPreset(
                bypassed,
                testCase.preset
            ), "local bypass does not change preset identity");

            VoiceEffectSettings changed = *settings;
            changed.pitchSemitones += 0.5f;
            Expect(!VoiceEffectSettingsMatchPreset(
                changed,
                testCase.preset
            ), "edited settings no longer match the preset");
        }

        const auto disabled = BuildVoiceEffectPreset(
            VoiceEffectPreset::DeepHeavy,
            false
        );
        Expect(disabled.has_value() && !disabled->enabled,
            "preset preserves disabled master switch");
        Expect(!BuildVoiceEffectPreset(
            VoiceEffectPreset::Custom,
            true
        ).has_value(), "custom is not a fixed preset snapshot");
        Expect(!BuildVoiceEffectPreset(
            static_cast<VoiceEffectPreset>(999),
            true
        ).has_value(), "unknown preset has no snapshot");

        Expect(
            VoiceEffectPresetHasDedicatedStage(VoiceEffectPreset::Radio),
            "radio keeps its dedicated stage while controls are tuned"
        );
        Expect(
            VoiceEffectPresetHasDedicatedStage(VoiceEffectPreset::Robot),
            "robot keeps its dedicated stage while controls are tuned"
        );
        Expect(
            VoiceEffectPresetHasDedicatedStage(
                VoiceEffectPreset::TinyHighVoice
            ),
            "tiny/high keeps its doubler and bit-reduction stage"
        );
        Expect(
            !VoiceEffectPresetHasDedicatedStage(
                VoiceEffectPreset::DeepHeavy
            ),
            "ordinary voice presets become custom when edited"
        );
        Expect(
            !VoiceEffectPresetHasDedicatedStage(VoiceEffectPreset::Custom),
            "custom has no hidden dedicated stage"
        );
    }


    void TestUserPresets()
    {
        VoiceEffectUserPreset preset;
        preset.name = "My Deep";
        preset.settings.enabled = false;
        preset.settings.bypassed = false;
        preset.settings.preset = VoiceEffectPreset::Custom;
        preset.settings.pitchSemitones = -5.0f;
        preset.settings.formantSemitones = -2.0f;
        preset.settings.character = 0.60f;
        preset.settings.drive = 0.25f;
        preset.settings.dryWet = 0.80f;
        preset.settings.outputGainDb = -1.0f;

        Expect(
            IsValidVoiceEffectUserPresetName(preset.name),
            "ordinary user-preset names are valid"
        );
        Expect(
            IsValidVoiceEffectUserPreset(preset),
            "normalized user presets are valid"
        );
        Expect(
            VoiceEffectUserPresetNamesEqual("My Deep", "my deep"),
            "ASCII user-preset names compare case-insensitively"
        );
        Expect(
            !VoiceEffectUserPresetNamesEqual("My Deep", "My Dark"),
            "different user-preset names remain distinct"
        );

        const VoiceEffectSettings applied = ApplyVoiceEffectUserPreset(
            preset,
            true,
            true
        );
        Expect(applied.enabled, "applying a user preset preserves enable");
        Expect(applied.bypassed, "applying a user preset preserves bypass");
        Expect(
            applied.pitchSemitones == preset.settings.pitchSemitones,
            "applying a user preset copies its DSP settings"
        );

        VoiceEffectUserPreset invalid = preset;
        invalid.name = " bad";
        Expect(
            !IsValidVoiceEffectUserPreset(invalid),
            "leading whitespace is rejected in user-preset names"
        );
        invalid = preset;
        invalid.name = "bad|name";
        Expect(
            !IsValidVoiceEffectUserPreset(invalid),
            "config delimiters are rejected in user-preset names"
        );
        invalid = preset;
        invalid.settings.enabled = true;
        Expect(
            !IsValidVoiceEffectUserPreset(invalid),
            "stored user presets do not persist the master switch"
        );
        invalid = preset;
        invalid.settings.bypassed = true;
        Expect(
            !IsValidVoiceEffectUserPreset(invalid),
            "stored user presets do not persist local bypass"
        );
    }

    void TestRangeValidation()
    {
        using namespace VoiceEffectLimits;

        struct FloatCase
        {
            float VoiceEffectSettings::* member;
            float minimum;
            float maximum;
            std::string_view name;
        };

        constexpr std::array cases{
            FloatCase{&VoiceEffectSettings::pitchSemitones,
                MinimumPitchSemitones, MaximumPitchSemitones, "pitch"},
            FloatCase{&VoiceEffectSettings::formantSemitones,
                MinimumFormantSemitones, MaximumFormantSemitones, "formant"},
            FloatCase{&VoiceEffectSettings::character,
                MinimumCharacter, MaximumCharacter, "character"},
            FloatCase{&VoiceEffectSettings::drive,
                MinimumDrive, MaximumDrive, "drive"},
            FloatCase{&VoiceEffectSettings::dryWet,
                MinimumDryWet, MaximumDryWet, "dry/wet"},
            FloatCase{&VoiceEffectSettings::outputGainDb,
                MinimumOutputGainDb, MaximumOutputGainDb, "output gain"}
        };

        for (const FloatCase& testCase : cases)
        {
            VoiceEffectSettings settings;
            settings.*(testCase.member) = testCase.minimum;
            Expect(IsValidVoiceEffectSettings(settings), testCase.name);

            settings.*(testCase.member) = testCase.maximum;
            Expect(IsValidVoiceEffectSettings(settings), testCase.name);

            settings.*(testCase.member) = std::nextafter(
                testCase.minimum,
                -std::numeric_limits<float>::infinity()
            );
            Expect(!IsValidVoiceEffectSettings(settings), testCase.name);

            settings.*(testCase.member) = std::nextafter(
                testCase.maximum,
                std::numeric_limits<float>::infinity()
            );
            Expect(!IsValidVoiceEffectSettings(settings), testCase.name);

            settings.*(testCase.member) =
                std::numeric_limits<float>::quiet_NaN();
            Expect(!IsValidVoiceEffectSettings(settings), testCase.name);
        }

        VoiceEffectSettings unknownPreset;
        unknownPreset.preset = static_cast<VoiceEffectPreset>(999);
        Expect(
            !IsValidVoiceEffectSettings(unknownPreset),
            "unknown preset is invalid"
        );
    }

    void TestLegacyBuiltInPresetMigration()
    {
        VoiceEffectSettings legacy;
        legacy.enabled = true;
        legacy.bypassed = true;
        legacy.preset = VoiceEffectPreset::DeepHeavy;
        legacy.pitchSemitones = -4.0f;
        legacy.formantSemitones = -1.5f;
        legacy.character = 0.55f;
        legacy.drive = 0.20f;
        legacy.dryWet = 1.0f;
        legacy.outputGainDb = 0.0f;

        Expect(
            MigrateLegacyBuiltInVoiceEffectSettings(legacy),
            "untouched preview preset migrates"
        );
        const auto current = BuildVoiceEffectPreset(
            VoiceEffectPreset::DeepHeavy,
            true
        );
        Expect(current.has_value(), "current deep preset exists");
        if (current.has_value())
        {
            Expect(
                NearlyEqual(
                    legacy.pitchSemitones,
                    current->pitchSemitones
                ),
                "migration applies the quality-tuned pitch"
            );
            Expect(
                NearlyEqual(
                    legacy.formantSemitones,
                    current->formantSemitones
                ),
                "migration applies the quality-tuned formant"
            );
        }
        Expect(legacy.enabled, "migration preserves enabled state");
        Expect(legacy.bypassed, "migration preserves bypass state");

        VoiceEffectSettings qualityPass4A;
        qualityPass4A.enabled = true;
        qualityPass4A.bypassed = false;
        qualityPass4A.preset = VoiceEffectPreset::HighNasalRap;
        qualityPass4A.pitchSemitones = 1.5f;
        qualityPass4A.formantSemitones = 1.1f;
        qualityPass4A.character = 0.55f;
        qualityPass4A.drive = 0.05f;
        qualityPass4A.dryWet = 1.0f;
        qualityPass4A.outputGainDb = 0.0f;
        Expect(
            MigrateLegacyBuiltInVoiceEffectSettings(qualityPass4A),
            "untouched 4A preset migrates"
        );
        const auto currentHigh = BuildVoiceEffectPreset(
            VoiceEffectPreset::HighNasalRap,
            true
        );
        Expect(currentHigh.has_value(), "current high preset exists");
        if (currentHigh.has_value())
        {
            Expect(
                NearlyEqual(
                    qualityPass4A.pitchSemitones,
                    currentHigh->pitchSemitones
                ),
                "4A migration applies the natural high pitch"
            );
        }

        VoiceEffectSettings customized = legacy;
        customized.pitchSemitones = -2.25f;
        Expect(
            !MigrateLegacyBuiltInVoiceEffectSettings(customized),
            "customized built-in values are not overwritten"
        );
        Expect(
            NearlyEqual(customized.pitchSemitones, -2.25f),
            "customized pitch remains untouched"
        );
    }
}

int main()
{
    TestDefaults();
    TestStableNamesAndParsing();
    TestPresetSnapshots();
    TestUserPresets();
    TestRangeValidation();
    TestLegacyBuiltInPresetMigration();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Voice-effect settings tests passed.\n";
    return 0;
}
