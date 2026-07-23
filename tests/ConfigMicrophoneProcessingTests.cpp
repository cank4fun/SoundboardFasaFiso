#include "config/Config.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

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
        return std::abs(left - right) <= 0.0005f;
    }

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const auto timestamp =
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count();

            path_ = std::filesystem::temp_directory_path() /
                ("SoundBoardFasaFiso-config-tests-" +
                    std::to_string(timestamp));

            std::filesystem::create_directories(path_);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        const std::filesystem::path& Path() const
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    bool WriteText(
        const std::filesystem::path& path,
        const std::string_view text
    )
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file.good();
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>{file},
            std::istreambuf_iterator<char>{}
        };
    }

    bool SettingsEqual(
        const MicrophoneProcessingSettings& left,
        const MicrophoneProcessingSettings& right
    )
    {
        return left.enabled == right.enabled &&
            left.preset == right.preset &&
            left.echoCancellationEnabled ==
                right.echoCancellationEnabled &&
            left.highPassEnabled == right.highPassEnabled &&
            NearlyEqual(left.highPassHz, right.highPassHz) &&
            left.noiseSuppressionEnabled ==
                right.noiseSuppressionEnabled &&
            left.noiseSuppressionLevel ==
                right.noiseSuppressionLevel &&
            left.agcEnabled == right.agcEnabled &&
            NearlyEqual(left.agcTargetDbfs, right.agcTargetDbfs) &&
            left.compressorEnabled == right.compressorEnabled &&
            NearlyEqual(
                left.compressorThresholdDb,
                right.compressorThresholdDb
            ) &&
            NearlyEqual(left.compressorRatio, right.compressorRatio) &&
            NearlyEqual(
                left.compressorAttackMs,
                right.compressorAttackMs
            ) &&
            NearlyEqual(
                left.compressorReleaseMs,
                right.compressorReleaseMs
            ) &&
            NearlyEqual(
                left.compressorMakeupDb,
                right.compressorMakeupDb
            ) &&
            left.limiterEnabled == right.limiterEnabled &&
            NearlyEqual(left.limiterCeilingDb, right.limiterCeilingDb);
    }

    void TestOldConfigUsesSafeDefaults(const TemporaryDirectory& directory)
    {
        const auto path = directory.Path() / "old-config.txt";
        Expect(
            WriteText(path, "language=en\nmicrophone_enabled=false\n"),
            "old config fixture is written"
        );

        Config config;
        Expect(config.Load(path), "old config without v2.1 keys loads");

        const auto& settings = config.GetMicrophoneProcessingSettings();
        Expect(!settings.enabled, "old config keeps processing disabled");
        Expect(
            !settings.echoCancellationEnabled,
            "old config keeps echo cancellation disabled"
        );
        Expect(
            settings.highPassEnabled,
            "native high-pass default remains available"
        );
        Expect(
            !settings.noiseSuppressionEnabled,
            "unimplemented noise suppression remains disabled"
        );
        Expect(
            !settings.agcEnabled,
            "unimplemented AGC remains disabled"
        );
        Expect(
            IsValidMicrophoneProcessingSettings(settings),
            "old-config defaults are valid"
        );
    }

    void TestAllProcessingKeysParse(const TemporaryDirectory& directory)
    {
        const auto path = directory.Path() / "all-settings.txt";
        constexpr std::string_view content =
            "language=en\n"
            "microphone_processing_enabled=on\n"
            "microphone_processing_preset=STRONG\n"
            "microphone_echo_cancellation_enabled=true\n"
            "microphone_high_pass_enabled=false\n"
            "microphone_high_pass_hz=140,5\n"
            "microphone_noise_suppression_enabled=true\n"
            "microphone_noise_suppression_level=LIGHT\n"
            "microphone_agc_enabled=true\n"
            "microphone_agc_target_dbfs=-12.5\n"
            "microphone_compressor_enabled=false\n"
            "microphone_compressor_threshold_db=-30.25\n"
            "microphone_compressor_ratio=4.5\n"
            "microphone_compressor_attack_ms=0.5\n"
            "microphone_compressor_release_ms=500.25\n"
            "microphone_compressor_makeup_db=6.25\n"
            "microphone_limiter_enabled=true\n"
            "microphone_limiter_ceiling_db=-2.5\n";

        Expect(WriteText(path, content), "complete config fixture is written");

        Config config;
        Expect(config.Load(path), "all microphone-processing keys parse");

        const auto& settings = config.GetMicrophoneProcessingSettings();
        Expect(settings.enabled, "processing enabled parses");
        Expect(
            settings.preset == MicrophoneProcessingPreset::Strong,
            "preset parses case-insensitively"
        );
        Expect(
            settings.echoCancellationEnabled,
            "echo-cancellation enabled parses"
        );
        Expect(!settings.highPassEnabled, "high-pass enabled parses");
        Expect(NearlyEqual(settings.highPassHz, 140.5f), "comma float parses");
        Expect(
            settings.noiseSuppressionEnabled,
            "noise-suppression enabled parses"
        );
        Expect(
            settings.noiseSuppressionLevel ==
                MicrophoneNoiseSuppressionLevel::Light,
            "noise-suppression level parses"
        );
        Expect(settings.agcEnabled, "AGC enabled parses");
        Expect(NearlyEqual(settings.agcTargetDbfs, -12.5f), "AGC target parses");
        Expect(!settings.compressorEnabled, "compressor enabled parses");
        Expect(
            NearlyEqual(settings.compressorThresholdDb, -30.25f),
            "compressor threshold parses"
        );
        Expect(NearlyEqual(settings.compressorRatio, 4.5f), "ratio parses");
        Expect(
            NearlyEqual(settings.compressorAttackMs, 0.5f),
            "attack parses"
        );
        Expect(
            NearlyEqual(settings.compressorReleaseMs, 500.25f),
            "release parses"
        );
        Expect(
            NearlyEqual(settings.compressorMakeupDb, 6.25f),
            "makeup gain parses"
        );
        Expect(settings.limiterEnabled, "limiter enabled parses");
        Expect(
            NearlyEqual(settings.limiterCeilingDb, -2.5f),
            "limiter ceiling parses"
        );
    }

    void TestSaveAndLoadRoundTrip(const TemporaryDirectory& directory)
    {
        MicrophoneProcessingSettings expected;
        expected.enabled = true;
        expected.preset = MicrophoneProcessingPreset::Custom;
        expected.echoCancellationEnabled = true;
        expected.highPassEnabled = true;
        expected.highPassHz = 95.125f;
        expected.noiseSuppressionEnabled = false;
        expected.noiseSuppressionLevel =
            MicrophoneNoiseSuppressionLevel::Strong;
        expected.agcEnabled = false;
        expected.agcTargetDbfs = -16.375f;
        expected.compressorEnabled = true;
        expected.compressorThresholdDb = -28.625f;
        expected.compressorRatio = 3.75f;
        expected.compressorAttackMs = 8.125f;
        expected.compressorReleaseMs = 180.375f;
        expected.compressorMakeupDb = 2.25f;
        expected.limiterEnabled = true;
        expected.limiterCeilingDb = -1.5f;

        Config source;
        Expect(
            source.SetMicrophoneProcessingSettings(expected),
            "valid processing settings are accepted"
        );

        const auto path = directory.Path() / "round-trip.txt";
        Expect(source.Save(path), "processing config saves");

        const std::string saved = ReadText(path);
        Expect(
            saved.find("microphone_processing_enabled=true") !=
                std::string::npos,
            "master switch is serialized"
        );
        Expect(
            saved.find("microphone_processing_preset=custom") !=
                std::string::npos,
            "preset is serialized with a stable name"
        );
        Expect(
            saved.find("microphone_echo_cancellation_enabled=true") !=
                std::string::npos,
            "echo-cancellation switch is serialized"
        );

        Config loaded;
        Expect(loaded.Load(path), "saved processing config reloads");
        Expect(
            SettingsEqual(
                expected,
                loaded.GetMicrophoneProcessingSettings()
            ),
            "processing settings round-trip"
        );
    }

    void TestInvalidValuesAreRejected(const TemporaryDirectory& directory)
    {
        struct InvalidCase
        {
            std::string_view line;
            std::string_view message;
        };

        constexpr InvalidCase cases[]{
            {"microphone_processing_enabled=maybe\n", "invalid boolean"},
            {
                "microphone_echo_cancellation_enabled=maybe\n",
                "invalid echo-cancellation boolean"
            },
            {"microphone_processing_preset=studio\n", "invalid preset"},
            {"microphone_high_pass_hz=301\n", "out-of-range high-pass"},
            {"microphone_compressor_ratio=nan\n", "non-finite ratio"},
            {"microphone_noise_suppression_level=maximum\n", "invalid level"}
        };

        std::size_t index = 0;
        for (const InvalidCase& testCase : cases)
        {
            const auto path = directory.Path() /
                ("invalid-" + std::to_string(index++) + ".txt");
            const std::string content =
                "language=en\n" + std::string{testCase.line};

            Expect(WriteText(path, content), "invalid fixture is written");

            Config config;
            Expect(!config.Load(path), testCase.message);
        }
    }

    void TestSetterRejectsInvalidSettings()
    {
        Config config;
        const MicrophoneProcessingSettings original =
            config.GetMicrophoneProcessingSettings();

        MicrophoneProcessingSettings invalid = original;
        invalid.compressorRatio =
            std::numeric_limits<float>::quiet_NaN();

        Expect(
            !config.SetMicrophoneProcessingSettings(invalid),
            "setter rejects invalid settings"
        );
        Expect(
            SettingsEqual(
                original,
                config.GetMicrophoneProcessingSettings()
            ),
            "setter preserves the previous valid settings"
        );
    }

    void TestDefaultSaveIsSafe(const TemporaryDirectory& directory)
    {
        Config config;
        const auto path = directory.Path() / "default-save.txt";
        Expect(config.Save(path), "default config saves");

        const std::string saved = ReadText(path);
        Expect(
            saved.find("microphone_processing_enabled=false") !=
                std::string::npos,
            "saved default keeps processing disabled"
        );
        Expect(
            saved.find("microphone_echo_cancellation_enabled=false") !=
                std::string::npos,
            "saved default keeps echo cancellation disabled"
        );
        Expect(
            saved.find("microphone_noise_suppression_enabled=false") !=
                std::string::npos,
            "saved default does not claim noise suppression is active"
        );
        Expect(
            saved.find("microphone_agc_enabled=false") !=
                std::string::npos,
            "saved default does not claim AGC is active"
        );
    }
}

int main()
{
    const TemporaryDirectory directory;

    TestOldConfigUsesSafeDefaults(directory);
    TestAllProcessingKeysParse(directory);
    TestSaveAndLoadRoundTrip(directory);
    TestInvalidValuesAreRejected(directory);
    TestSetterRejectsInvalidSettings();
    TestDefaultSaveIsSafe(directory);

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Config microphone-processing tests passed.\n";
    return 0;
}
