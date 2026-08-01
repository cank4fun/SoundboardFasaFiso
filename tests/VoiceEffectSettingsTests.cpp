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
        Expect(NearlyEqual(settings.body, 0.0f),
            "the Body control defaults to neutral");
        Expect(!settings.parametricEqEnabled && !settings.deEsserEnabled &&
            !settings.gateEnabled && !settings.compressorEnabled,
            "6E polish modules default to bypassed");
        Expect(NearlyEqual(settings.eqLowGainDb, 0.0f) &&
            NearlyEqual(settings.eqLowFrequencyHz, 135.0f) &&
            NearlyEqual(settings.eqMidGainDb, 0.0f) &&
            NearlyEqual(settings.eqMidFrequencyHz, 1450.0f) &&
            NearlyEqual(settings.eqMidQ, 0.82f) &&
            NearlyEqual(settings.eqHighGainDb, 0.0f) &&
            NearlyEqual(settings.eqHighFrequencyHz, 6800.0f),
            "parametric EQ defaults to a flat speech-focused response");
        Expect(
            settings.preset == VoiceEffectPreset::DeepHeavy,
            "the default preset is deep-heavy"
        );
        Expect(
            settings.rackOrder == DefaultVoiceEffectRackOrder,
            "the 6F rack defaults to the stable polish order"
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

    void TestRackOrderHelpers()
    {
        constexpr VoiceEffectRackOrder customOrder{
            VoiceEffectRackModule::Gate,
            VoiceEffectRackModule::ParametricEq,
            VoiceEffectRackModule::Compressor,
            VoiceEffectRackModule::DeEsser
        };

        Expect(
            IsValidVoiceEffectRackOrder(customOrder),
            "a complete unique rack order is valid"
        );
        Expect(
            SerializeVoiceEffectRackOrder(customOrder) ==
                "gate,parametric-eq,compressor,de-esser",
            "rack order has a stable serialized form"
        );
        Expect(
            ParseVoiceEffectRackOrder(
                "GATE,PARAMETRIC-EQ,COMPRESSOR,DE-ESSER"
            ) == customOrder,
            "rack order parses case-insensitively"
        );
        Expect(
            ParseVoiceEffectRackModule("de-esser") ==
                VoiceEffectRackModule::DeEsser,
            "individual rack module names parse"
        );
        Expect(
            VoiceEffectRackModuleName(VoiceEffectRackModule::Compressor) ==
                "compressor",
            "individual rack module names are stable"
        );

        VoiceEffectRackOrder duplicate = customOrder;
        duplicate[3] = VoiceEffectRackModule::Gate;
        Expect(
            !IsValidVoiceEffectRackOrder(duplicate),
            "duplicate rack modules are rejected"
        );
        Expect(
            !ParseVoiceEffectRackOrder(
                "gate,parametric-eq,compressor,gate"
            ).has_value(),
            "serialized duplicate rack modules are rejected"
        );
        Expect(
            !ParseVoiceEffectRackOrder(
                "gate,parametric-eq,compressor"
            ).has_value(),
            "incomplete rack orders are rejected"
        );
        Expect(
            !ParseVoiceEffectRackOrder(
                "gate,parametric-eq,compressor,de-esser,"
            ).has_value(),
            "rack orders with a trailing delimiter are rejected"
        );

        VoiceEffectRackOrder moved = DefaultVoiceEffectRackOrder;
        Expect(
            MoveVoiceEffectRackModule(moved, 2U, -1),
            "rack module moves upward"
        );
        Expect(
            moved[1] == VoiceEffectRackModule::Gate &&
                moved[2] == VoiceEffectRackModule::DeEsser,
            "rack move swaps exactly one adjacent module"
        );
        Expect(
            !MoveVoiceEffectRackModule(moved, 0U, -1) &&
                !MoveVoiceEffectRackModule(moved, moved.size() - 1U, 1),
            "rack move rejects out-of-range directions"
        );

        VoiceEffectSettings invalid;
        invalid.rackOrder = duplicate;
        Expect(
            !IsValidVoiceEffectSettings(invalid),
            "settings validation rejects an invalid rack order"
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
            float body;
            float drive;
            float dryWet;
            float outputGainDb;
        };

        constexpr std::array cases{
            PresetCase{VoiceEffectPreset::DeepHeavy,
                -2.5f, -0.95f, 0.34f, 0.0f, 0.03f, 0.90f, 3.7f},
            PresetCase{VoiceEffectPreset::HighNasalRap,
                1.0f, 0.70f, 0.42f, 0.0f, 0.015f, 0.88f, 3.4f},
            PresetCase{VoiceEffectPreset::DarkVocal,
                -0.25f, -1.35f, 0.43f, 0.0f, 0.045f, 0.93f, 1.8f},
            PresetCase{VoiceEffectPreset::Radio,
                0.0f, 0.0f, 0.85f, 0.0f, 0.30f, 1.0f, 3.8f},
            PresetCase{VoiceEffectPreset::Robot,
                0.0f, 0.0f, 0.75f, 0.0f, 0.15f, 1.0f, 5.5f},
            PresetCase{VoiceEffectPreset::TinyHighVoice,
                4.0f, 1.65f, 0.39f, 0.0f, 0.0f, 0.84f, 4.0f}
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
            Expect(settings->body == testCase.body,
                "preset body is stable");
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
            "tiny/high keeps its compact doubler stage"
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
        preset.settings.body = 0.70f;
        preset.settings.drive = 0.25f;
        preset.settings.dryWet = 0.80f;
        preset.settings.outputGainDb = -1.0f;
        preset.settings.parametricEqEnabled = true;
        preset.settings.deEsserEnabled = true;
        preset.settings.gateEnabled = true;
        preset.settings.compressorEnabled = true;
        preset.settings.eqLowGainDb = 2.0f;
        preset.settings.eqLowFrequencyHz = 165.0f;
        preset.settings.eqMidGainDb = -1.5f;
        preset.settings.eqMidFrequencyHz = 1850.0f;
        preset.settings.eqMidQ = 1.35f;
        preset.settings.eqHighGainDb = 1.0f;
        preset.settings.eqHighFrequencyHz = 7600.0f;
        preset.settings.deEsserAmount = 0.45f;
        preset.settings.gateAmount = 0.30f;
        preset.settings.compressorAmount = 0.55f;

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
            applied.pitchSemitones == preset.settings.pitchSemitones &&
                applied.body == preset.settings.body &&
                applied.parametricEqEnabled && applied.compressorEnabled &&
                applied.eqMidGainDb == preset.settings.eqMidGainDb &&
                applied.eqLowFrequencyHz ==
                    preset.settings.eqLowFrequencyHz &&
                applied.eqMidFrequencyHz ==
                    preset.settings.eqMidFrequencyHz &&
                applied.eqMidQ == preset.settings.eqMidQ &&
                applied.eqHighFrequencyHz ==
                    preset.settings.eqHighFrequencyHz,
            "applying a user preset copies all DSP settings"
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
            FloatCase{&VoiceEffectSettings::body,
                MinimumBody, MaximumBody, "body"},
            FloatCase{&VoiceEffectSettings::drive,
                MinimumDrive, MaximumDrive, "drive"},
            FloatCase{&VoiceEffectSettings::dryWet,
                MinimumDryWet, MaximumDryWet, "dry/wet"},
            FloatCase{&VoiceEffectSettings::outputGainDb,
                MinimumOutputGainDb, MaximumOutputGainDb, "output gain"},
            FloatCase{&VoiceEffectSettings::eqLowGainDb,
                MinimumEqGainDb, MaximumEqGainDb, "EQ low gain"},
            FloatCase{&VoiceEffectSettings::eqLowFrequencyHz,
                MinimumEqLowFrequencyHz, MaximumEqLowFrequencyHz,
                "EQ low frequency"},
            FloatCase{&VoiceEffectSettings::eqMidGainDb,
                MinimumEqGainDb, MaximumEqGainDb, "EQ mid gain"},
            FloatCase{&VoiceEffectSettings::eqMidFrequencyHz,
                MinimumEqMidFrequencyHz, MaximumEqMidFrequencyHz,
                "EQ mid frequency"},
            FloatCase{&VoiceEffectSettings::eqMidQ,
                MinimumEqMidQ, MaximumEqMidQ, "EQ mid Q"},
            FloatCase{&VoiceEffectSettings::eqHighGainDb,
                MinimumEqGainDb, MaximumEqGainDb, "EQ high gain"},
            FloatCase{&VoiceEffectSettings::eqHighFrequencyHz,
                MinimumEqHighFrequencyHz, MaximumEqHighFrequencyHz,
                "EQ high frequency"},
            FloatCase{&VoiceEffectSettings::deEsserAmount,
                MinimumPolishAmount, MaximumPolishAmount, "de-esser amount"},
            FloatCase{&VoiceEffectSettings::gateAmount,
                MinimumPolishAmount, MaximumPolishAmount, "gate amount"},
            FloatCase{&VoiceEffectSettings::compressorAmount,
                MinimumPolishAmount, MaximumPolishAmount,
                "compressor amount"}
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

        VoiceEffectSettings qualityPass4B;
        qualityPass4B.enabled = true;
        qualityPass4B.bypassed = true;
        qualityPass4B.preset = VoiceEffectPreset::Radio;
        qualityPass4B.pitchSemitones = 0.0f;
        qualityPass4B.formantSemitones = 0.0f;
        qualityPass4B.character = 0.85f;
        qualityPass4B.drive = 0.30f;
        qualityPass4B.dryWet = 1.0f;
        qualityPass4B.outputGainDb = -1.0f;
        Expect(
            MigrateLegacyBuiltInVoiceEffectSettings(qualityPass4B),
            "untouched 4B preset migrates"
        );
        const auto currentRadio = BuildVoiceEffectPreset(
            VoiceEffectPreset::Radio,
            true
        );
        Expect(currentRadio.has_value(), "current radio preset exists");
        if (currentRadio.has_value())
        {
            Expect(
                NearlyEqual(
                    qualityPass4B.outputGainDb,
                    currentRadio->outputGainDb
                ),
                "4B migration applies the final radio level"
            );
        }
        Expect(qualityPass4B.bypassed,
            "4B migration preserves bypass state");

        VoiceEffectSettings qualityPass4D;
        qualityPass4D.enabled = true;
        qualityPass4D.bypassed = true;
        qualityPass4D.preset = VoiceEffectPreset::TinyHighVoice;
        qualityPass4D.pitchSemitones = 4.0f;
        qualityPass4D.formantSemitones = 1.8f;
        qualityPass4D.character = 0.42f;
        qualityPass4D.drive = 0.0f;
        qualityPass4D.dryWet = 0.88f;
        qualityPass4D.outputGainDb = 0.7f;
        Expect(
            MigrateLegacyBuiltInVoiceEffectSettings(qualityPass4D),
            "untouched 4D preset migrates"
        );
        const auto currentTiny = BuildVoiceEffectPreset(
            VoiceEffectPreset::TinyHighVoice,
            true
        );
        Expect(currentTiny.has_value(), "current tiny preset exists");
        if (currentTiny.has_value())
        {
            Expect(
                NearlyEqual(
                    qualityPass4D.outputGainDb,
                    currentTiny->outputGainDb
                ),
                "4D migration applies the final tiny level"
            );
            Expect(
                NearlyEqual(
                    qualityPass4D.formantSemitones,
                    currentTiny->formantSemitones
                ),
                "4D migration applies the final tiny tone"
            );
        }
        Expect(qualityPass4D.bypassed,
            "4D migration preserves bypass state");

        VoiceEffectSettings qualityPass4E;
        qualityPass4E.enabled = true;
        qualityPass4E.bypassed = true;
        qualityPass4E.preset = VoiceEffectPreset::Robot;
        qualityPass4E.pitchSemitones = 0.0f;
        qualityPass4E.formantSemitones = 0.0f;
        qualityPass4E.character = 0.75f;
        qualityPass4E.drive = 0.15f;
        qualityPass4E.dryWet = 1.0f;
        qualityPass4E.outputGainDb = 2.0f;
        Expect(
            MigrateLegacyBuiltInVoiceEffectSettings(qualityPass4E),
            "untouched 4E preset migrates"
        );
        const auto currentRobot = BuildVoiceEffectPreset(
            VoiceEffectPreset::Robot,
            true
        );
        Expect(currentRobot.has_value(), "current robot preset exists");
        if (currentRobot.has_value())
        {
            Expect(
                NearlyEqual(
                    qualityPass4E.outputGainDb,
                    currentRobot->outputGainDb
                ),
                "4E migration applies the loudness-matched robot level"
            );
        }
        Expect(qualityPass4E.bypassed,
            "4E migration preserves bypass state");

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

        VoiceEffectSettings eqCustomized = legacy;
        eqCustomized.eqMidFrequencyHz = 2200.0f;
        Expect(
            !MigrateLegacyBuiltInVoiceEffectSettings(eqCustomized),
            "custom parametric EQ values prevent legacy migration"
        );
        Expect(
            NearlyEqual(eqCustomized.eqMidFrequencyHz, 2200.0f),
            "custom EQ frequency remains untouched"
        );
    }
}

int main()
{
    TestDefaults();
    TestStableNamesAndParsing();
    TestRackOrderHelpers();
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
