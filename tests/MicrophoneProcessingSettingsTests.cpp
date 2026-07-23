#include "audio/MicrophoneProcessingSettings.hpp"

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

    void TestDefaults()
    {
        const MicrophoneProcessingSettings settings;

        Expect(!settings.enabled, "processing defaults to disabled");
        Expect(
            settings.preset == MicrophoneProcessingPreset::Natural,
            "the default preset is natural"
        );
        Expect(
            !settings.noiseSuppressionEnabled,
            "noise suppression defaults to disabled"
        );
        Expect(
            !settings.agcEnabled,
            "AGC defaults to disabled"
        );
        Expect(
            IsValidMicrophoneProcessingSettings(settings),
            "default settings are valid"
        );
    }

    void TestStableNamesAndParsing()
    {
        constexpr std::array presetCases{
            std::pair{MicrophoneProcessingPreset::Natural, "natural"},
            std::pair{MicrophoneProcessingPreset::Clean, "clean"},
            std::pair{MicrophoneProcessingPreset::Strong, "strong"},
            std::pair{MicrophoneProcessingPreset::Aggressive, "aggressive"},
            std::pair{MicrophoneProcessingPreset::Custom, "custom"}
        };

        for (const auto& [value, name] : presetCases)
        {
            Expect(
                MicrophoneProcessingPresetName(value) == name,
                "preset name is stable"
            );
            Expect(
                ParseMicrophoneProcessingPreset(name) == value,
                "preset name round-trips"
            );
        }

        Expect(
            ParseMicrophoneProcessingPreset("CLEAN") ==
                MicrophoneProcessingPreset::Clean,
            "preset parsing is ASCII case-insensitive"
        );
        Expect(
            !ParseMicrophoneProcessingPreset("invalid").has_value(),
            "unknown preset is rejected"
        );
        Expect(
            MicrophoneProcessingPresetName(
                static_cast<MicrophoneProcessingPreset>(999)
            ) == "unknown",
            "unknown preset has an explicit name"
        );

        constexpr std::array levelCases{
            std::pair{MicrophoneNoiseSuppressionLevel::Light, "light"},
            std::pair{MicrophoneNoiseSuppressionLevel::Balanced, "balanced"},
            std::pair{MicrophoneNoiseSuppressionLevel::Strong, "strong"}
        };

        for (const auto& [value, name] : levelCases)
        {
            Expect(
                MicrophoneNoiseSuppressionLevelName(value) == name,
                "noise-suppression level name is stable"
            );
            Expect(
                ParseMicrophoneNoiseSuppressionLevel(name) == value,
                "noise-suppression level name round-trips"
            );
        }

        Expect(
            ParseMicrophoneNoiseSuppressionLevel("BALANCED") ==
                MicrophoneNoiseSuppressionLevel::Balanced,
            "noise-suppression parsing is ASCII case-insensitive"
        );
        Expect(
            !ParseMicrophoneNoiseSuppressionLevel("invalid").has_value(),
            "unknown noise-suppression level is rejected"
        );
        Expect(
            MicrophoneNoiseSuppressionLevelName(
                static_cast<MicrophoneNoiseSuppressionLevel>(999)
            ) == "unknown",
            "unknown noise-suppression level has an explicit name"
        );
    }

    void TestPresetSnapshots()
    {
        struct PresetCase
        {
            MicrophoneProcessingPreset preset;
            MicrophoneNoiseSuppressionLevel noiseSuppressionLevel;
            float highPassHz;
            bool agcEnabled;
            float agcTargetDbfs;
            float thresholdDb;
            float ratio;
            float attackMs;
            float releaseMs;
            float makeupDb;
            float limiterCeilingDb;
        };

        constexpr std::array cases{
            PresetCase{MicrophoneProcessingPreset::Natural,
                MicrophoneNoiseSuppressionLevel::Light,
                80.0f, false, -18.0f,
                -24.0f, 3.0f, 10.0f, 120.0f, 0.0f, -1.0f},
            PresetCase{MicrophoneProcessingPreset::Clean,
                MicrophoneNoiseSuppressionLevel::Balanced,
                90.0f, true, -18.0f,
                -27.0f, 3.5f, 8.0f, 140.0f, 2.0f, -1.0f},
            PresetCase{MicrophoneProcessingPreset::Strong,
                MicrophoneNoiseSuppressionLevel::Strong,
                100.0f, true, -17.0f,
                -30.0f, 4.5f, 6.0f, 170.0f, 4.0f, -1.0f},
            PresetCase{MicrophoneProcessingPreset::Aggressive,
                MicrophoneNoiseSuppressionLevel::Strong,
                120.0f, true, -16.0f,
                -34.0f, 6.0f, 4.0f, 200.0f, 6.0f, -1.5f}
        };

        for (const PresetCase& testCase : cases)
        {
            const auto settings = BuildMicrophoneProcessingPreset(
                testCase.preset,
                true
            );

            Expect(settings.has_value(), "named preset is available");
            if (!settings.has_value())
            {
                continue;
            }

            Expect(settings->enabled, "preset preserves requested enabled state");
            Expect(settings->preset == testCase.preset, "preset identity is stored");
            Expect(settings->highPassEnabled, "preset enables high-pass");
            Expect(settings->compressorEnabled, "preset enables compressor");
            Expect(settings->limiterEnabled, "preset enables limiter");
            Expect(settings->noiseSuppressionEnabled,
                "preset enables RNNoise suppression");
            Expect(
                settings->noiseSuppressionLevel ==
                    testCase.noiseSuppressionLevel,
                "preset noise-suppression level is stable"
            );
            Expect(settings->agcEnabled == testCase.agcEnabled,
                "preset AGC state is stable");
            Expect(settings->agcTargetDbfs == testCase.agcTargetDbfs,
                "preset AGC target is stable");
            Expect(settings->highPassHz == testCase.highPassHz,
                "preset high-pass is stable");
            Expect(settings->compressorThresholdDb == testCase.thresholdDb,
                "preset threshold is stable");
            Expect(settings->compressorRatio == testCase.ratio,
                "preset ratio is stable");
            Expect(settings->compressorAttackMs == testCase.attackMs,
                "preset attack is stable");
            Expect(settings->compressorReleaseMs == testCase.releaseMs,
                "preset release is stable");
            Expect(settings->compressorMakeupDb == testCase.makeupDb,
                "preset makeup gain is stable");
            Expect(settings->limiterCeilingDb == testCase.limiterCeilingDb,
                "preset limiter ceiling is stable");
            Expect(IsValidMicrophoneProcessingSettings(*settings),
                "named preset is valid");
            Expect(MicrophoneProcessingSettingsMatchPreset(
                *settings,
                testCase.preset
            ), "preset snapshot matches itself");

            MicrophoneProcessingSettings changed = *settings;
            changed.agcTargetDbfs -= 1.0f;
            Expect(!MicrophoneProcessingSettingsMatchPreset(
                changed,
                testCase.preset
            ), "edited settings no longer match the preset");
        }

        const auto disabled = BuildMicrophoneProcessingPreset(
            MicrophoneProcessingPreset::Natural,
            false
        );
        Expect(disabled.has_value() && !disabled->enabled,
            "preset preserves disabled master switch");
        Expect(!BuildMicrophoneProcessingPreset(
            MicrophoneProcessingPreset::Custom,
            true
        ).has_value(), "custom is not a fixed preset snapshot");
        Expect(!BuildMicrophoneProcessingPreset(
            static_cast<MicrophoneProcessingPreset>(999),
            true
        ).has_value(), "unknown preset has no snapshot");
    }

    void TestRangeValidation()
    {
        using namespace MicrophoneProcessingLimits;

        struct FloatCase
        {
            float MicrophoneProcessingSettings::* member;
            float minimum;
            float maximum;
            std::string_view name;
        };

        constexpr std::array cases{
            FloatCase{&MicrophoneProcessingSettings::highPassHz,
                MinimumHighPassHz, MaximumHighPassHz, "high-pass"},
            FloatCase{&MicrophoneProcessingSettings::agcTargetDbfs,
                MinimumAgcTargetDbfs, MaximumAgcTargetDbfs, "AGC target"},
            FloatCase{&MicrophoneProcessingSettings::compressorThresholdDb,
                MinimumCompressorThresholdDb, MaximumCompressorThresholdDb,
                "compressor threshold"},
            FloatCase{&MicrophoneProcessingSettings::compressorRatio,
                MinimumCompressorRatio, MaximumCompressorRatio,
                "compressor ratio"},
            FloatCase{&MicrophoneProcessingSettings::compressorAttackMs,
                MinimumCompressorAttackMs, MaximumCompressorAttackMs,
                "compressor attack"},
            FloatCase{&MicrophoneProcessingSettings::compressorReleaseMs,
                MinimumCompressorReleaseMs, MaximumCompressorReleaseMs,
                "compressor release"},
            FloatCase{&MicrophoneProcessingSettings::compressorMakeupDb,
                MinimumCompressorMakeupDb, MaximumCompressorMakeupDb,
                "compressor makeup"},
            FloatCase{&MicrophoneProcessingSettings::limiterCeilingDb,
                MinimumLimiterCeilingDb, MaximumLimiterCeilingDb,
                "limiter ceiling"}
        };

        for (const FloatCase& testCase : cases)
        {
            MicrophoneProcessingSettings settings;
            settings.*(testCase.member) = testCase.minimum;
            Expect(
                IsValidMicrophoneProcessingSettings(settings),
                testCase.name
            );

            settings.*(testCase.member) = testCase.maximum;
            Expect(
                IsValidMicrophoneProcessingSettings(settings),
                testCase.name
            );

            settings.*(testCase.member) =
                std::nextafter(
                    testCase.minimum,
                    -std::numeric_limits<float>::infinity()
                );
            Expect(
                !IsValidMicrophoneProcessingSettings(settings),
                testCase.name
            );

            settings.*(testCase.member) =
                std::nextafter(
                    testCase.maximum,
                    std::numeric_limits<float>::infinity()
                );
            Expect(
                !IsValidMicrophoneProcessingSettings(settings),
                testCase.name
            );

            settings.*(testCase.member) =
                std::numeric_limits<float>::quiet_NaN();
            Expect(
                !IsValidMicrophoneProcessingSettings(settings),
                testCase.name
            );

            settings.*(testCase.member) =
                std::numeric_limits<float>::infinity();
            Expect(
                !IsValidMicrophoneProcessingSettings(settings),
                testCase.name
            );
        }
    }

    void TestUnknownEnums()
    {
        MicrophoneProcessingSettings settings;
        settings.preset = static_cast<MicrophoneProcessingPreset>(999);
        Expect(
            !IsValidMicrophoneProcessingSettings(settings),
            "unknown preset is invalid"
        );

        settings = {};
        settings.noiseSuppressionLevel =
            static_cast<MicrophoneNoiseSuppressionLevel>(999);
        Expect(
            !IsValidMicrophoneProcessingSettings(settings),
            "unknown noise-suppression level is invalid"
        );
    }
}

int main()
{
    TestDefaults();
    TestStableNamesAndParsing();
    TestPresetSnapshots();
    TestRangeValidation();
    TestUnknownEnums();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Microphone processing settings tests passed.\n";
    return 0;
}
