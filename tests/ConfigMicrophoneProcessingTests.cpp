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

    bool VoiceSettingsEqual(
        const VoiceEffectSettings& left,
        const VoiceEffectSettings& right
    )
    {
        return left.enabled == right.enabled &&
            left.bypassed == right.bypassed &&
            left.preset == right.preset &&
            NearlyEqual(left.pitchSemitones, right.pitchSemitones) &&
            NearlyEqual(left.formantSemitones, right.formantSemitones) &&
            NearlyEqual(left.character, right.character) &&
            NearlyEqual(left.body, right.body) &&
            NearlyEqual(left.drive, right.drive) &&
            NearlyEqual(left.dryWet, right.dryWet) &&
            NearlyEqual(left.outputGainDb, right.outputGainDb);
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

        const auto& voiceSettings = config.GetVoiceEffectSettings();
        Expect(
            !voiceSettings.enabled,
            "old config keeps Voice Effects disabled"
        );
        Expect(
            !voiceSettings.bypassed,
            "old config uses the non-bypassed default"
        );
        Expect(
            voiceSettings.preset == VoiceEffectPreset::DeepHeavy,
            "old config uses the safe built-in Voice Effects preset"
        );
        Expect(
            IsValidVoiceEffectSettings(voiceSettings),
            "old-config Voice Effects defaults are valid"
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

    void TestAllVoiceEffectKeysParse(
        const TemporaryDirectory& directory
    )
    {
        const auto path = directory.Path() / "all-voice-effects.txt";
        constexpr std::string_view content =
            "language=en\n"
            "voice_effects_enabled=on\n"
            "voice_effects_bypassed=true\n"
            "voice_effects_preset=DARK-VOCAL\n"
            "voice_effects_pitch_semitones=-3,5\n"
            "voice_effects_formant_semitones=-2.25\n"
            "voice_effects_character=0.75\n"
            "voice_effects_body=0.55\n"
            "voice_effects_drive=0.40\n"
            "voice_effects_dry_wet=0.65\n"
            "voice_effects_output_gain_db=-1.25\n";

        Expect(
            WriteText(path, content),
            "complete Voice Effects config fixture is written"
        );

        Config config;
        Expect(config.Load(path), "all Voice Effects keys parse");

        const auto& settings = config.GetVoiceEffectSettings();
        Expect(settings.enabled, "Voice Effects enabled parses");
        Expect(settings.bypassed, "Voice Effects bypass parses");
        Expect(
            settings.preset == VoiceEffectPreset::DarkVocal,
            "Voice Effects preset parses case-insensitively"
        );
        Expect(
            NearlyEqual(settings.pitchSemitones, -3.5f),
            "Voice Effects comma pitch parses"
        );
        Expect(
            NearlyEqual(settings.formantSemitones, -2.25f),
            "Voice Effects formant parses"
        );
        Expect(
            NearlyEqual(settings.character, 0.75f),
            "Voice Effects character parses"
        );
        Expect(
            NearlyEqual(settings.body, 0.55f),
            "Voice Effects body parses"
        );
        Expect(
            NearlyEqual(settings.drive, 0.40f),
            "Voice Effects drive parses"
        );
        Expect(
            NearlyEqual(settings.dryWet, 0.65f),
            "Voice Effects dry/wet parses"
        );
        Expect(
            NearlyEqual(settings.outputGainDb, -1.25f),
            "Voice Effects output gain parses"
        );
        Expect(
            IsValidVoiceEffectSettings(settings),
            "parsed Voice Effects settings are valid"
        );
    }

    void TestLegacyUserPresetWithoutBodyLoads(
        const TemporaryDirectory& directory
    )
    {
        const auto path = directory.Path() / "legacy-user-preset.txt";
        constexpr std::string_view content =
            "voice_effects_user_preset=Legacy|preset=custom|pitch=0|"
            "formant=0|character=0.25|drive=0|dry_wet=1|"
            "output_gain_db=0\n";

        Expect(
            WriteText(path, content),
            "legacy user-preset fixture is written"
        );

        Config config;
        Expect(
            config.Load(path),
            "v2.2.0 user preset without Body still loads"
        );
        const auto& presets = config.GetVoiceEffectUserPresets();
        Expect(presets.size() == 1, "legacy user preset is retained");
        if (presets.size() == 1)
        {
            Expect(
                NearlyEqual(presets[0].settings.body, 0.0f),
                "missing legacy Body defaults to zero"
            );
        }
    }

    void TestLegacyVoiceEffectPreviewPresetMigrates(
        const TemporaryDirectory& directory
    )
    {
        const auto path = directory.Path() / "legacy-voice-effects.txt";
        constexpr std::string_view content =
            "voice_effects_enabled=true\n"
            "voice_effects_bypassed=true\n"
            "voice_effects_preset=deep-heavy\n"
            "voice_effects_pitch_semitones=-4.0\n"
            "voice_effects_formant_semitones=-1.5\n"
            "voice_effects_character=0.55\n"
            "voice_effects_drive=0.20\n"
            "voice_effects_dry_wet=1.0\n"
            "voice_effects_output_gain_db=0.0\n";

        Expect(
            WriteText(path, content),
            "legacy Voice Effects fixture is written"
        );

        Config config;
        Expect(config.Load(path), "legacy Voice Effects config loads");
        const auto expected = BuildVoiceEffectPreset(
            VoiceEffectPreset::DeepHeavy,
            true
        );
        Expect(expected.has_value(), "quality-tuned deep preset exists");

        const auto& actual = config.GetVoiceEffectSettings();
        if (expected.has_value())
        {
            VoiceEffectSettings expectedWithBypass = *expected;
            expectedWithBypass.bypassed = true;
            Expect(
                VoiceSettingsEqual(actual, expectedWithBypass),
                "untouched preview preset migrates during config load"
            );
        }
    }

    void TestVoiceEffectsSaveAndLoadRoundTrip(
        const TemporaryDirectory& directory
    )
    {
        VoiceEffectSettings expected;
        expected.enabled = true;
        expected.bypassed = true;
        expected.preset = VoiceEffectPreset::Custom;
        expected.pitchSemitones = -5.125f;
        expected.formantSemitones = 2.375f;
        expected.character = 0.625f;
        expected.body = 0.575f;
        expected.drive = 0.375f;
        expected.dryWet = 0.725f;
        expected.outputGainDb = -2.875f;

        Config source;
        Expect(
            source.SetVoiceEffectSettings(expected),
            "valid Voice Effects settings are accepted"
        );

        const auto path = directory.Path() / "voice-effects-round-trip.txt";
        Expect(source.Save(path), "Voice Effects config saves");

        const std::string saved = ReadText(path);
        Expect(
            saved.find("voice_effects_enabled=true") != std::string::npos,
            "Voice Effects master switch is serialized"
        );
        Expect(
            saved.find("voice_effects_bypassed=true") != std::string::npos,
            "Voice Effects bypass is serialized"
        );
        Expect(
            saved.find("voice_effects_preset=custom") != std::string::npos,
            "Voice Effects preset is serialized with a stable name"
        );
        Expect(
            saved.find("voice_effects_pitch_semitones=-5.125") !=
                std::string::npos,
            "Voice Effects pitch is serialized"
        );
        Expect(
            saved.find("voice_effects_body=0.575") != std::string::npos,
            "Voice Effects body is serialized"
        );

        Config loaded;
        Expect(loaded.Load(path), "saved Voice Effects config reloads");
        Expect(
            VoiceSettingsEqual(expected, loaded.GetVoiceEffectSettings()),
            "Voice Effects settings round-trip"
        );
    }

    void TestVoiceEffectUserPresetPersistence(
        const TemporaryDirectory& directory
    )
    {
        VoiceEffectUserPreset deep;
        deep.name = "My Deep";
        deep.settings.enabled = false;
        deep.settings.bypassed = false;
        deep.settings.preset = VoiceEffectPreset::Custom;
        deep.settings.pitchSemitones = -5.0f;
        deep.settings.formantSemitones = -2.0f;
        deep.settings.character = 0.60f;
        deep.settings.body = 0.70f;
        deep.settings.drive = 0.25f;
        deep.settings.dryWet = 0.80f;
        deep.settings.outputGainDb = -1.5f;

        VoiceEffectUserPreset radio;
        radio.name = "Desk Radio";
        radio.settings.enabled = false;
        radio.settings.bypassed = false;
        radio.settings.preset = VoiceEffectPreset::Radio;
        radio.settings.pitchSemitones = 0.0f;
        radio.settings.formantSemitones = 0.0f;
        radio.settings.character = 0.90f;
        radio.settings.body = 0.20f;
        radio.settings.drive = 0.35f;
        radio.settings.dryWet = 0.75f;
        radio.settings.outputGainDb = -2.0f;

        Config source;
        Expect(
            source.AddOrUpdateVoiceEffectUserPreset(deep),
            "first user preset is accepted"
        );
        Expect(
            source.AddOrUpdateVoiceEffectUserPreset(radio),
            "second user preset is accepted"
        );

        VoiceEffectUserPreset updated = deep;
        updated.name = "my deep";
        updated.settings.pitchSemitones = -6.0f;
        Expect(
            source.AddOrUpdateVoiceEffectUserPreset(updated),
            "ASCII case-insensitive names update an existing preset"
        );
        Expect(
            source.GetVoiceEffectUserPresets().size() == 2,
            "updating a preset does not append a duplicate"
        );

        const auto path = directory.Path() / "voice-user-presets.txt";
        Expect(source.Save(path), "user presets save with the config");

        const std::string saved = ReadText(path);
        Expect(
            saved.find(
                "voice_effects_user_preset=my deep|preset=custom|pitch=-6"
            ) != std::string::npos,
            "custom user preset is serialized"
        );
        Expect(
            saved.find(
                "voice_effects_user_preset=Desk Radio|preset=radio"
            ) != std::string::npos,
            "dedicated-stage user preset is serialized"
        );

        Config loaded;
        Expect(loaded.Load(path), "saved user presets reload");
        const auto& presets = loaded.GetVoiceEffectUserPresets();
        Expect(presets.size() == 2, "all user presets round-trip");
        if (presets.size() == 2)
        {
            Expect(
                presets[0].name == "my deep",
                "updated user-preset name round-trips"
            );
            Expect(
                NearlyEqual(presets[0].settings.pitchSemitones, -6.0f) &&
                    NearlyEqual(presets[0].settings.body, 0.70f),
                "updated user-preset settings round-trip"
            );
            Expect(
                presets[1].settings.preset == VoiceEffectPreset::Radio,
                "dedicated-stage identity round-trips"
            );
        }

        Expect(
            loaded.RemoveVoiceEffectUserPreset("MY DEEP"),
            "user presets can be removed case-insensitively"
        );
        Expect(
            loaded.GetVoiceEffectUserPresets().size() == 1,
            "removed user preset leaves the remaining entries intact"
        );
        Expect(
            !loaded.RemoveVoiceEffectUserPreset("missing"),
            "removing a missing user preset is reported"
        );

        VoiceEffectUserPreset invalid = deep;
        invalid.name = "bad|name";
        Expect(
            !loaded.AddOrUpdateVoiceEffectUserPreset(invalid),
            "invalid user-preset names are rejected"
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

    void TestPlaybackPoliciesRoundTrip(
        const TemporaryDirectory& directory
    )
    {
        const auto sourcePath =
            directory.Path() / "playback-policies.txt";

        constexpr std::string_view content =
            "language=en\n"
            "F1=example.wav|volume=0.80|mode=restart\n"
            "F2=example.wav|volume=0.80|mode=overlap\n"
            "F3=example.wav|volume=0.80|mode=toggle\n"
            "F4=example.wav|volume=0.80|mode=loop\n"
            "F5=example.wav|volume=0.80|mode=ignore|fade_in_ms=75|fade_out_ms=125\n";

        Expect(
            WriteText(sourcePath, content),
            "playback-policy fixture is written"
        );

        Config loaded;
        Expect(
            loaded.Load(sourcePath),
            "all playback policies parse"
        );

        const auto& bindings = loaded.GetBindings();
        Expect(bindings.size() == 5, "all playback-policy bindings load");

        if (bindings.size() == 5)
        {
            Expect(
                bindings[0].mode == PlaybackMode::Restart,
                "restart policy parses"
            );
            Expect(
                bindings[1].mode == PlaybackMode::Overlap,
                "overlap policy parses"
            );
            Expect(
                bindings[2].mode == PlaybackMode::Toggle,
                "toggle policy parses"
            );
            Expect(
                bindings[3].mode == PlaybackMode::Loop,
                "loop policy parses"
            );
            Expect(
                bindings[4].mode == PlaybackMode::Ignore,
                "ignore policy parses"
            );
            Expect(
                bindings[4].fadeInMilliseconds == 75,
                "fade-in duration parses"
            );
            Expect(
                bindings[4].fadeOutMilliseconds == 125,
                "fade-out duration parses"
            );
        }

        const auto savedPath =
            directory.Path() / "playback-policies-saved.txt";

        Expect(loaded.Save(savedPath), "playback policies save");

        const std::string saved = ReadText(savedPath);
        Expect(
            saved.find("|mode=ignore|fade_in_ms=75|fade_out_ms=125") !=
                std::string::npos,
            "ignore policy and fade durations serialize"
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
            {"microphone_noise_suppression_level=maximum\n", "invalid level"},
            {"voice_effects_enabled=maybe\n", "invalid Voice Effects boolean"},
            {"voice_effects_preset=monster\n", "invalid Voice Effects preset"},
            {
                "voice_effects_pitch_semitones=12.01\n",
                "out-of-range Voice Effects pitch"
            },
            {
                "voice_effects_formant_semitones=nan\n",
                "non-finite Voice Effects formant"
            },
            {
                "voice_effects_body=1.01\n",
                "out-of-range Voice Effects body"
            },
            {
                "voice_effects_dry_wet=-0.01\n",
                "out-of-range Voice Effects dry/wet"
            },
            {
                "voice_effects_user_preset=bad|preset=custom|pitch=99|formant=0|character=0|drive=0|dry_wet=1|output_gain_db=0\n",
                "invalid Voice Effects user preset"
            },
            {
                "F8=example.wav|fade_in_ms=10001\n",
                "out-of-range fade in"
            },
            {
                "F9=example.wav|fade_out_ms=-1\n",
                "negative fade out"
            }
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

        const VoiceEffectSettings originalVoice =
            config.GetVoiceEffectSettings();
        VoiceEffectSettings invalidVoice = originalVoice;
        invalidVoice.outputGainDb =
            std::numeric_limits<float>::infinity();

        Expect(
            !config.SetVoiceEffectSettings(invalidVoice),
            "Voice Effects setter rejects invalid settings"
        );
        Expect(
            VoiceSettingsEqual(
                originalVoice,
                config.GetVoiceEffectSettings()
            ),
            "Voice Effects setter preserves the previous valid settings"
        );
    }

    void TestVoiceEffectHotkeys(const TemporaryDirectory& directory)
    {
        const auto path = directory.Path() / "voice-effect-hotkeys.txt";
        constexpr std::string_view content =
            "language=en\n"
            "voice_effects_previous_preset=CTRL+ALT+HOME\n"
            "voice_effects_next_preset=CTRL+ALT+END\n"
            "voice_effects_bypass=CTRL+ALT+V\n";

        Expect(WriteText(path, content), "Voice Effects hotkey fixture is written");

        Config config;
        Expect(config.Load(path), "Voice Effects hotkeys parse");
        Expect(
            config.GetVoiceEffectsPreviousPresetKeyName() ==
                "CTRL+ALT+HOME",
            "previous-preset hotkey parses"
        );
        Expect(
            config.GetVoiceEffectsNextPresetKeyName() ==
                "CTRL+ALT+END",
            "next-preset hotkey parses"
        );
        Expect(
            config.GetVoiceEffectsBypassKeyName() == "CTRL+ALT+V",
            "bypass hotkey parses"
        );

        const auto savedPath = directory.Path() /
            "voice-effect-hotkeys-saved.txt";
        Expect(config.Save(savedPath), "Voice Effects hotkeys save");

        const std::string saved = ReadText(savedPath);
        Expect(
            saved.find(
                "voice_effects_previous_preset=CTRL+ALT+HOME"
            ) != std::string::npos,
            "previous-preset hotkey is persisted"
        );
        Expect(
            saved.find(
                "voice_effects_next_preset=CTRL+ALT+END"
            ) != std::string::npos,
            "next-preset hotkey is persisted"
        );
        Expect(
            saved.find("voice_effects_bypass=CTRL+ALT+V") !=
                std::string::npos,
            "bypass hotkey is persisted"
        );

        Config duplicate;
        Expect(
            !duplicate.SetControlHotkeys(
                "F11",
                "CTRL+SHIFT+F9",
                "CTRL+SHIFT+F10",
                "CTRL+ALT+F21",
                "CTRL+ALT+F22",
                "CTRL+ALT+F21",
                "CTRL+SHIFT+F11",
                "CTRL+SHIFT+F12"
            ),
            "Voice Effects control hotkey conflicts are rejected"
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
        Expect(
            saved.find("voice_effects_enabled=false") !=
                std::string::npos,
            "saved default keeps Voice Effects disabled"
        );
        Expect(
            saved.find("voice_effects_bypassed=false") !=
                std::string::npos,
            "saved default does not persist an active bypass"
        );
        Expect(
            saved.find("voice_effects_preset=deep-heavy") !=
                std::string::npos,
            "saved default uses the stable Voice Effects preset name"
        );
        Expect(
            saved.find(
                "voice_effects_previous_preset=CTRL+ALT+F21"
            ) != std::string::npos,
            "saved default includes the previous-preset hotkey"
        );
        Expect(
            saved.find(
                "voice_effects_next_preset=CTRL+ALT+F22"
            ) != std::string::npos,
            "saved default includes the next-preset hotkey"
        );
        Expect(
            saved.find("voice_effects_bypass=CTRL+ALT+F23") !=
                std::string::npos,
            "saved default includes the Voice Effects bypass hotkey"
        );
    }

    void TestSaveKeepsLastGoodBackup(const TemporaryDirectory& directory)
    {
        const auto path = directory.Path() / "atomic-save.txt";
        std::filesystem::path backupPath = path;
        backupPath += L".bak";
        std::filesystem::path temporaryPath = path;
        temporaryPath += L".tmp";

        Config first;
        Expect(first.SetOutputVolume(0.25f), "first output volume is valid");
        Expect(first.Save(path), "first atomic config save succeeds");
        const std::string firstContents = ReadText(path);

        Config second;
        Expect(second.SetOutputVolume(0.75f), "second output volume is valid");
        Expect(second.Save(path), "replacement atomic config save succeeds");

        Expect(
            std::filesystem::exists(backupPath),
            "replacement save keeps a last-good backup"
        );
        Expect(
            ReadText(backupPath) == firstContents,
            "backup contains the previous complete config"
        );
        Expect(
            ReadText(path).find("output_volume=0.75") != std::string::npos,
            "destination contains the new config"
        );
        Expect(
            !std::filesystem::exists(temporaryPath),
            "successful save leaves no temporary file"
        );
    }
}

int main()
{
    const TemporaryDirectory directory;

    TestOldConfigUsesSafeDefaults(directory);
    TestAllProcessingKeysParse(directory);
    TestAllVoiceEffectKeysParse(directory);
    TestLegacyUserPresetWithoutBodyLoads(directory);
    TestLegacyVoiceEffectPreviewPresetMigrates(directory);
    TestVoiceEffectsSaveAndLoadRoundTrip(directory);
    TestVoiceEffectUserPresetPersistence(directory);
    TestSaveAndLoadRoundTrip(directory);
    TestPlaybackPoliciesRoundTrip(directory);
    TestInvalidValuesAreRejected(directory);
    TestSetterRejectsInvalidSettings();
    TestVoiceEffectHotkeys(directory);
    TestDefaultSaveIsSafe(directory);
    TestSaveKeepsLastGoodBackup(directory);

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Config microphone-processing tests passed.\n";
    return 0;
}
