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
            "unimplemented noise suppression defaults to disabled"
        );
        Expect(
            !settings.agcEnabled,
            "unimplemented AGC defaults to disabled"
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
