#include "config/Config.hpp"
#include "platform/Utf8Path.hpp"
#include "sound/SoundFileFormat.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    struct ParsedLine
    {
        std::string leftSide;
        std::string rightSide;
        std::string originalLine;
        std::size_t lineNumber = 0;
    };

    bool FlushFileContents(
        const std::filesystem::path& path,
        DWORD& errorCode
    )
    {
        const HANDLE fileHandle = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (fileHandle == INVALID_HANDLE_VALUE)
        {
            errorCode = GetLastError();
            return false;
        }

        const BOOL flushed = FlushFileBuffers(fileHandle);
        errorCode = flushed ? ERROR_SUCCESS : GetLastError();
        CloseHandle(fileHandle);
        return flushed != FALSE;
    }

    bool CommitTemporaryFile(
        const std::filesystem::path& temporaryPath,
        const std::filesystem::path& destinationPath,
        const std::filesystem::path& backupPath,
        const bool destinationExists,
        DWORD& errorCode
    )
    {
        if (destinationExists)
        {
            DeleteFileW(backupPath.c_str());

            if (ReplaceFileW(
                    destinationPath.c_str(),
                    temporaryPath.c_str(),
                    backupPath.c_str(),
                    REPLACEFILE_WRITE_THROUGH,
                    nullptr,
                    nullptr
                ) != FALSE)
            {
                errorCode = ERROR_SUCCESS;
                return true;
            }

            errorCode = GetLastError();
            return false;
        }

        if (MoveFileExW(
                temporaryPath.c_str(),
                destinationPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
            ) != FALSE)
        {
            errorCode = ERROR_SUCCESS;
            return true;
        }

        errorCode = GetLastError();
        return false;
    }

    struct ParsedBaseKey
    {
        std::string canonicalName;
        unsigned int virtualKey = 0;
    };

    struct ParsedHotkey
    {
        std::string canonicalName;
        unsigned int modifiers = 0;
        unsigned int virtualKey = 0;
    };

    struct ParsedSoundValue
    {
        std::filesystem::path soundFile;
        float volume = 1.0f;
        PlaybackMode mode = PlaybackMode::Restart;
        unsigned int fadeInMilliseconds = 0;
        unsigned int fadeOutMilliseconds = 0;
    };

    struct SettingSource
    {
        std::size_t lineNumber = 0;
        std::string originalLine = "<default>";
    };

    struct ControlHotkey
    {
        std::string_view settingName;
        std::string_view keyName;
        std::uint64_t identity = 0;
        const SettingSource* source = nullptr;
        std::string_view example;
    };

    struct ProcessingBooleanSetting
    {
        std::string_view name;
        bool MicrophoneProcessingSettings::* member = nullptr;
        std::string_view example;
    };

    struct ProcessingFloatSetting
    {
        std::string_view name;
        float MicrophoneProcessingSettings::* member = nullptr;
        float minimum = 0.0f;
        float maximum = 0.0f;
        std::string_view rangeTurkish;
        std::string_view rangeEnglish;
        std::string_view example;
    };

    struct VoiceEffectBooleanSetting
    {
        std::string_view name;
        bool VoiceEffectSettings::* member = nullptr;
        std::string_view example;
    };

    struct VoiceEffectFloatSetting
    {
        std::string_view name;
        float VoiceEffectSettings::* member = nullptr;
        float minimum = 0.0f;
        float maximum = 0.0f;
        std::string_view rangeTurkish;
        std::string_view rangeEnglish;
        std::string_view example;
    };

    constexpr std::array ProcessingBooleanSettings{
        ProcessingBooleanSetting{
            "MICROPHONE_PROCESSING_ENABLED",
            &MicrophoneProcessingSettings::enabled,
            "microphone_processing_enabled=false"
        },
        ProcessingBooleanSetting{
            "MICROPHONE_ECHO_CANCELLATION_ENABLED",
            &MicrophoneProcessingSettings::echoCancellationEnabled,
            "microphone_echo_cancellation_enabled=false"
        },
        ProcessingBooleanSetting{
            "MICROPHONE_HIGH_PASS_ENABLED",
            &MicrophoneProcessingSettings::highPassEnabled,
            "microphone_high_pass_enabled=true"
        },
        ProcessingBooleanSetting{
            "MICROPHONE_NOISE_SUPPRESSION_ENABLED",
            &MicrophoneProcessingSettings::noiseSuppressionEnabled,
            "microphone_noise_suppression_enabled=false"
        },
        ProcessingBooleanSetting{
            "MICROPHONE_AGC_ENABLED",
            &MicrophoneProcessingSettings::agcEnabled,
            "microphone_agc_enabled=false"
        },
        ProcessingBooleanSetting{
            "MICROPHONE_COMPRESSOR_ENABLED",
            &MicrophoneProcessingSettings::compressorEnabled,
            "microphone_compressor_enabled=true"
        },
        ProcessingBooleanSetting{
            "MICROPHONE_LIMITER_ENABLED",
            &MicrophoneProcessingSettings::limiterEnabled,
            "microphone_limiter_enabled=true"
        }
    };

    constexpr std::array ProcessingFloatSettings{
        ProcessingFloatSetting{
            "MICROPHONE_HIGH_PASS_HZ",
            &MicrophoneProcessingSettings::highPassHz,
            MicrophoneProcessingLimits::MinimumHighPassHz,
            MicrophoneProcessingLimits::MaximumHighPassHz,
            "20 ile 300",
            "between 20 and 300",
            "microphone_high_pass_hz=80.0"
        },
        ProcessingFloatSetting{
            "MICROPHONE_AGC_TARGET_DBFS",
            &MicrophoneProcessingSettings::agcTargetDbfs,
            MicrophoneProcessingLimits::MinimumAgcTargetDbfs,
            MicrophoneProcessingLimits::MaximumAgcTargetDbfs,
            "-40 ile -3",
            "between -40 and -3",
            "microphone_agc_target_dbfs=-18.0"
        },
        ProcessingFloatSetting{
            "MICROPHONE_COMPRESSOR_THRESHOLD_DB",
            &MicrophoneProcessingSettings::compressorThresholdDb,
            MicrophoneProcessingLimits::MinimumCompressorThresholdDb,
            MicrophoneProcessingLimits::MaximumCompressorThresholdDb,
            "-60 ile 0",
            "between -60 and 0",
            "microphone_compressor_threshold_db=-24.0"
        },
        ProcessingFloatSetting{
            "MICROPHONE_COMPRESSOR_RATIO",
            &MicrophoneProcessingSettings::compressorRatio,
            MicrophoneProcessingLimits::MinimumCompressorRatio,
            MicrophoneProcessingLimits::MaximumCompressorRatio,
            "1 ile 20",
            "between 1 and 20",
            "microphone_compressor_ratio=3.0"
        },
        ProcessingFloatSetting{
            "MICROPHONE_COMPRESSOR_ATTACK_MS",
            &MicrophoneProcessingSettings::compressorAttackMs,
            MicrophoneProcessingLimits::MinimumCompressorAttackMs,
            MicrophoneProcessingLimits::MaximumCompressorAttackMs,
            "0.1 ile 200",
            "between 0.1 and 200",
            "microphone_compressor_attack_ms=10.0"
        },
        ProcessingFloatSetting{
            "MICROPHONE_COMPRESSOR_RELEASE_MS",
            &MicrophoneProcessingSettings::compressorReleaseMs,
            MicrophoneProcessingLimits::MinimumCompressorReleaseMs,
            MicrophoneProcessingLimits::MaximumCompressorReleaseMs,
            "5 ile 2000",
            "between 5 and 2000",
            "microphone_compressor_release_ms=120.0"
        },
        ProcessingFloatSetting{
            "MICROPHONE_COMPRESSOR_MAKEUP_DB",
            &MicrophoneProcessingSettings::compressorMakeupDb,
            MicrophoneProcessingLimits::MinimumCompressorMakeupDb,
            MicrophoneProcessingLimits::MaximumCompressorMakeupDb,
            "-12 ile 24",
            "between -12 and 24",
            "microphone_compressor_makeup_db=0.0"
        },
        ProcessingFloatSetting{
            "MICROPHONE_LIMITER_CEILING_DB",
            &MicrophoneProcessingSettings::limiterCeilingDb,
            MicrophoneProcessingLimits::MinimumLimiterCeilingDb,
            MicrophoneProcessingLimits::MaximumLimiterCeilingDb,
            "-12 ile 0",
            "between -12 and 0",
            "microphone_limiter_ceiling_db=-1.0"
        }
    };

    constexpr std::array VoiceEffectBooleanSettings{
        VoiceEffectBooleanSetting{
            "VOICE_EFFECTS_ENABLED",
            &VoiceEffectSettings::enabled,
            "voice_effects_enabled=false"
        },
        VoiceEffectBooleanSetting{
            "VOICE_EFFECTS_BYPASSED",
            &VoiceEffectSettings::bypassed,
            "voice_effects_bypassed=false"
        }
    };

    constexpr std::array VoiceEffectFloatSettings{
        VoiceEffectFloatSetting{
            "VOICE_EFFECTS_PITCH_SEMITONES",
            &VoiceEffectSettings::pitchSemitones,
            VoiceEffectLimits::MinimumPitchSemitones,
            VoiceEffectLimits::MaximumPitchSemitones,
            "-12 ile 12",
            "between -12 and 12",
            "voice_effects_pitch_semitones=-3.0"
        },
        VoiceEffectFloatSetting{
            "VOICE_EFFECTS_FORMANT_SEMITONES",
            &VoiceEffectSettings::formantSemitones,
            VoiceEffectLimits::MinimumFormantSemitones,
            VoiceEffectLimits::MaximumFormantSemitones,
            "-6 ile 6",
            "between -6 and 6",
            "voice_effects_formant_semitones=-1.0"
        },
        VoiceEffectFloatSetting{
            "VOICE_EFFECTS_CHARACTER",
            &VoiceEffectSettings::character,
            VoiceEffectLimits::MinimumCharacter,
            VoiceEffectLimits::MaximumCharacter,
            "0 ile 1",
            "between 0 and 1",
            "voice_effects_character=0.42"
        },
        VoiceEffectFloatSetting{
            "VOICE_EFFECTS_BODY",
            &VoiceEffectSettings::body,
            VoiceEffectLimits::MinimumBody,
            VoiceEffectLimits::MaximumBody,
            "0 ile 1",
            "between 0 and 1",
            "voice_effects_body=0.0"
        },
        VoiceEffectFloatSetting{
            "VOICE_EFFECTS_DRIVE",
            &VoiceEffectSettings::drive,
            VoiceEffectLimits::MinimumDrive,
            VoiceEffectLimits::MaximumDrive,
            "0 ile 1",
            "between 0 and 1",
            "voice_effects_drive=0.08"
        },
        VoiceEffectFloatSetting{
            "VOICE_EFFECTS_DRY_WET",
            &VoiceEffectSettings::dryWet,
            VoiceEffectLimits::MinimumDryWet,
            VoiceEffectLimits::MaximumDryWet,
            "0 ile 1",
            "between 0 and 1",
            "voice_effects_dry_wet=1.0"
        },
        VoiceEffectFloatSetting{
            "VOICE_EFFECTS_OUTPUT_GAIN_DB",
            &VoiceEffectSettings::outputGainDb,
            VoiceEffectLimits::MinimumOutputGainDb,
            VoiceEffectLimits::MaximumOutputGainDb,
            "-24 ile 12",
            "between -24 and 12",
            "voice_effects_output_gain_db=0.0"
        }
    };

    const ProcessingBooleanSetting* FindProcessingBooleanSetting(
        const std::string_view name
    )
    {
        const auto iterator = std::find_if(
            ProcessingBooleanSettings.begin(),
            ProcessingBooleanSettings.end(),
            [name](const ProcessingBooleanSetting& setting)
            {
                return setting.name == name;
            }
        );

        return iterator == ProcessingBooleanSettings.end()
            ? nullptr
            : &*iterator;
    }

    const ProcessingFloatSetting* FindProcessingFloatSetting(
        const std::string_view name
    )
    {
        const auto iterator = std::find_if(
            ProcessingFloatSettings.begin(),
            ProcessingFloatSettings.end(),
            [name](const ProcessingFloatSetting& setting)
            {
                return setting.name == name;
            }
        );

        return iterator == ProcessingFloatSettings.end()
            ? nullptr
            : &*iterator;
    }

    const VoiceEffectBooleanSetting* FindVoiceEffectBooleanSetting(
        const std::string_view name
    )
    {
        const auto iterator = std::find_if(
            VoiceEffectBooleanSettings.begin(),
            VoiceEffectBooleanSettings.end(),
            [name](const VoiceEffectBooleanSetting& setting)
            {
                return setting.name == name;
            }
        );

        return iterator == VoiceEffectBooleanSettings.end()
            ? nullptr
            : &*iterator;
    }

    const VoiceEffectFloatSetting* FindVoiceEffectFloatSetting(
        const std::string_view name
    )
    {
        const auto iterator = std::find_if(
            VoiceEffectFloatSettings.begin(),
            VoiceEffectFloatSettings.end(),
            [name](const VoiceEffectFloatSetting& setting)
            {
                return setting.name == name;
            }
        );

        return iterator == VoiceEffectFloatSettings.end()
            ? nullptr
            : &*iterator;
    }

    std::string Trim(const std::string& text)
    {
        const std::size_t first =
            text.find_first_not_of(" \t\r\n");

        if (first == std::string::npos)
        {
            return {};
        }

        const std::size_t last =
            text.find_last_not_of(" \t\r\n");

        return text.substr(first, last - first + 1);
    }

    std::string ToUpper(std::string text)
    {
        std::transform(
            text.begin(),
            text.end(),
            text.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(
                    std::toupper(character)
                );
            }
        );

        return text;
    }

    void PrintConfigError(
        const std::size_t lineNumber,
        const std::string_view originalLine,
        const std::string_view reason,
        const std::string_view example
    )
    {
        std::cerr
            << Localization::Text(
                "\n[CONFIG HATASI]\n",
                "\n[CONFIG ERROR]\n"
            );

        if (lineNumber != 0)
        {
            std::cerr
                << Localization::Text("Satır ", "Line ")
                << lineNumber
                << ": "
                << originalLine
                << '\n';
        }
        else
        {
            std::cerr
                << Localization::Text(
                    "Satır: <dosya geneli>\n",
                    "Line: <whole file>\n"
                );
        }

        std::cerr
            << Localization::Text("Neden: ", "Reason: ")
            << reason
            << '\n';

        if (!example.empty())
        {
            std::cerr
                << Localization::Text(
                    "Doğru örnek: ",
                    "Correct example: "
                )
                << example
                << '\n';
        }
    }

    std::string_view SettingExample(
        const std::string_view settingName
    )
    {
        if (const auto* setting =
                FindProcessingBooleanSetting(settingName))
        {
            return setting->example;
        }

        if (const auto* setting =
                FindProcessingFloatSetting(settingName))
        {
            return setting->example;
        }

        if (const auto* setting =
                FindVoiceEffectBooleanSetting(settingName))
        {
            return setting->example;
        }

        if (const auto* setting =
                FindVoiceEffectFloatSetting(settingName))
        {
            return setting->example;
        }

        if (settingName == "MICROPHONE_PROCESSING_PRESET")
        {
            return "microphone_processing_preset=natural";
        }

        if (settingName == "MICROPHONE_NOISE_SUPPRESSION_LEVEL")
        {
            return "microphone_noise_suppression_level=balanced";
        }

        if (settingName == "VOICE_EFFECTS_PRESET")
        {
            return "voice_effects_preset=deep-heavy";
        }

        if (settingName == "VOICE_EFFECTS_USER_PRESET")
        {
            return "voice_effects_user_preset=My Deep|preset=custom|pitch=-4.0|formant=-1.5|character=0.55|body=0.40|drive=0.20|dry_wet=1.0|output_gain_db=0.0";
        }

        if (settingName == "LANGUAGE")
        {
            return "language=tr";
        }

        if (settingName == "THEME")
        {
            return "theme=dark";
        }

        if (settingName == "OUTPUT")
        {
            return "output=CABLE Input";
        }

        if (settingName == "OUTPUT_VOLUME")
        {
            return "output_volume=1.00";
        }

        if (settingName == "MONITOR")
        {
            return "monitor=default";
        }

        if (settingName == "MONITOR_VOLUME")
        {
            return "monitor_volume=0.30";
        }

        if (settingName == "MICROPHONE_ENABLED")
        {
            return "microphone_enabled=true";
        }

        if (settingName == "MICROPHONE")
        {
            return "microphone=default";
        }

        if (settingName == "MICROPHONE_VOLUME")
        {
            return "microphone_volume=1.00";
        }

        if (settingName == "MICROPHONE_TO_OUTPUT")
        {
            return "microphone_to_output=true";
        }

        if (settingName == "MICROPHONE_TO_MONITOR")
        {
            return "microphone_to_monitor=false";
        }

        if (settingName == "AUDIO_SAMPLE_RATE")
        {
            return "audio_sample_rate=48000";
        }

        if (settingName == "AUDIO_BUFFER_MS")
        {
            return "audio_buffer_ms=5";
        }

        if (settingName == "START_WITH_WINDOWS")
        {
            return "start_with_windows=false";
        }

        if (settingName == "SHOW_CONSOLE_ON_START")
        {
            return "show_console_on_start=false";
        }

        if (settingName == "CHECK_UPDATES_ON_START")
        {
            return "check_updates_on_start=true";
        }

        if (settingName == "STOP")
        {
            return "stop=F11";
        }

        if (settingName == "OUTPUT_MUTE")
        {
            return "output_mute=CTRL+SHIFT+F9";
        }

        if (settingName == "MONITOR_MUTE")
        {
            return "monitor_mute=CTRL+SHIFT+F10";
        }

        if (settingName == "VOICE_EFFECTS_PREVIOUS_PRESET")
        {
            return "voice_effects_previous_preset=CTRL+ALT+F21";
        }

        if (settingName == "VOICE_EFFECTS_NEXT_PRESET")
        {
            return "voice_effects_next_preset=CTRL+ALT+F22";
        }

        if (settingName == "VOICE_EFFECTS_BYPASS")
        {
            return "voice_effects_bypass=CTRL+ALT+F23";
        }

        if (settingName == "RELOAD")
        {
            return "reload=CTRL+SHIFT+F11";
        }

        if (settingName == "EXIT")
        {
            return "exit=CTRL+SHIFT+F12";
        }

        return "F1=example.wav|volume=0.80|mode=restart";
    }

    std::optional<float> ParseFiniteFloat(std::string text)
    {
        std::replace(
            text.begin(),
            text.end(),
            ',',
            '.'
        );

        float value = 0.0f;

        const char* begin = text.data();
        const char* end = text.data() + text.size();

        const auto [pointer, error] =
            std::from_chars(begin, end, value);

        if (error != std::errc{} || pointer != end ||
            !std::isfinite(value))
        {
            return std::nullopt;
        }

        return value;
    }

    std::optional<VoiceEffectUserPreset>
        ParseVoiceEffectUserPresetValue(
            const std::string& text,
            std::string& errorReason
        )
    {
        constexpr std::size_t LegacyPartCount = 8;
        constexpr std::size_t CurrentPartCount = 9;
        std::array<std::string, CurrentPartCount> parts;
        std::size_t partCount = 0;
        std::size_t start = 0;
        bool hasTrailingPart = false;

        while (start <= text.size() && partCount < parts.size())
        {
            const std::size_t separator = text.find('|', start);
            parts[partCount++] = Trim(text.substr(
                start,
                separator == std::string::npos
                    ? std::string::npos
                    : separator - start
            ));

            if (separator == std::string::npos)
            {
                break;
            }

            start = separator + 1;
            if (partCount == parts.size())
            {
                hasTrailingPart = true;
            }
        }

        if ((partCount != LegacyPartCount &&
                partCount != CurrentPartCount) ||
            hasTrailingPart)
        {
            errorReason = Localization::Text(
                "Kullanıcı preset'i ad ve yedi veya sekiz alan içermeli.",
                "A user preset must contain a name and seven or eight fields."
            );
            return std::nullopt;
        }

        VoiceEffectUserPreset result;
        result.name = parts[0];
        result.settings.enabled = false;
        result.settings.bypassed = false;

        if (!IsValidVoiceEffectUserPresetName(result.name))
        {
            errorReason = Localization::Text(
                "Preset adı 1-48 bayt olmalı; başta/sonda boşluk ve '|', '=' veya kontrol karakteri içeremez.",
                "The preset name must be 1-48 bytes and cannot have leading/trailing whitespace or contain '|', '=', or control characters."
            );
            return std::nullopt;
        }

        bool hasPreset = false;
        bool hasPitch = false;
        bool hasFormant = false;
        bool hasCharacter = false;
        bool hasBody = false;
        bool hasDrive = false;
        bool hasDryWet = false;
        bool hasOutputGain = false;

        for (std::size_t index = 1; index < partCount; ++index)
        {
            const std::size_t equalsPosition = parts[index].find('=');
            if (equalsPosition == std::string::npos)
            {
                errorReason = Localization::Text(
                    "Kullanıcı preset alanı 'ad=değer' biçiminde olmalı.",
                    "A user-preset field must use the 'name=value' format."
                );
                return std::nullopt;
            }

            const std::string fieldName = ToUpper(Trim(
                parts[index].substr(0, equalsPosition)
            ));
            const std::string fieldValue = Trim(
                parts[index].substr(equalsPosition + 1)
            );

            if (fieldName == "PRESET")
            {
                if (hasPreset)
                {
                    errorReason = Localization::Text(
                        "Kullanıcı preset'inde preset alanı tekrarlanmış.",
                        "The preset field is duplicated in the user preset."
                    );
                    return std::nullopt;
                }

                const auto preset = ParseVoiceEffectPreset(fieldValue);
                if (!preset.has_value())
                {
                    errorReason = Localization::Text(
                        "Kullanıcı preset'indeki preset türü geçersiz.",
                        "The preset type in the user preset is invalid."
                    );
                    return std::nullopt;
                }

                result.settings.preset = *preset;
                hasPreset = true;
                continue;
            }

            const auto value = ParseFiniteFloat(fieldValue);
            if (!value.has_value())
            {
                errorReason = Localization::Text(
                    "Kullanıcı preset'indeki sayısal alan sonlu bir sayı olmalı.",
                    "A numeric field in the user preset must be a finite number."
                );
                return std::nullopt;
            }

            bool* seen = nullptr;
            float* destination = nullptr;

            if (fieldName == "PITCH")
            {
                seen = &hasPitch;
                destination = &result.settings.pitchSemitones;
            }
            else if (fieldName == "FORMANT")
            {
                seen = &hasFormant;
                destination = &result.settings.formantSemitones;
            }
            else if (fieldName == "CHARACTER")
            {
                seen = &hasCharacter;
                destination = &result.settings.character;
            }
            else if (fieldName == "BODY")
            {
                seen = &hasBody;
                destination = &result.settings.body;
            }
            else if (fieldName == "DRIVE")
            {
                seen = &hasDrive;
                destination = &result.settings.drive;
            }
            else if (fieldName == "DRY_WET")
            {
                seen = &hasDryWet;
                destination = &result.settings.dryWet;
            }
            else if (fieldName == "OUTPUT_GAIN_DB")
            {
                seen = &hasOutputGain;
                destination = &result.settings.outputGainDb;
            }
            else
            {
                errorReason = Localization::Text(
                    "Kullanıcı preset'inde desteklenmeyen alan var: ",
                    "The user preset contains an unsupported field: "
                ) + fieldName;
                return std::nullopt;
            }

            if (*seen)
            {
                errorReason = Localization::Text(
                    "Kullanıcı preset'inde alan tekrarlanmış: ",
                    "A field is duplicated in the user preset: "
                ) + fieldName;
                return std::nullopt;
            }

            *seen = true;
            *destination = *value;
        }

        // BODY was introduced after v2.2.0. Missing BODY therefore keeps
        // the struct default of zero so existing saved user presets load.
        if (!hasPreset || !hasPitch || !hasFormant ||
            !hasCharacter || !hasDrive || !hasDryWet ||
            !hasOutputGain)
        {
            errorReason = Localization::Text(
                "Kullanıcı preset'inde gerekli alanlardan biri eksik.",
                "The user preset is missing one or more required fields."
            );
            return std::nullopt;
        }

        if (!IsValidVoiceEffectUserPreset(result))
        {
            errorReason = Localization::Text(
                "Kullanıcı preset'indeki bir değer izin verilen aralığın dışında.",
                "A value in the user preset is outside the allowed range."
            );
            return std::nullopt;
        }

        return result;
    }

    std::optional<float> ParseVolume(const std::string& text)
    {
        const auto volume = ParseFiniteFloat(text);

        if (!volume.has_value() ||
            *volume < 0.0f || *volume > 1.0f)
        {
            return std::nullopt;
        }

        return volume;
    }

    std::optional<bool> ParseBoolean(const std::string& text)
    {
        const std::string value = ToUpper(Trim(text));

        if (value == "TRUE" || value == "YES" ||
            value == "ON" || value == "1")
        {
            return true;
        }

        if (value == "FALSE" || value == "NO" ||
            value == "OFF" || value == "0")
        {
            return false;
        }

        return std::nullopt;
    }

    std::optional<unsigned int> ParseUnsignedInteger(
        const std::string& text
    )
    {
        unsigned int value = 0;

        const char* begin = text.data();
        const char* end = text.data() + text.size();

        const auto [pointer, error] =
            std::from_chars(begin, end, value);

        if (error != std::errc{} || pointer != end)
        {
            return std::nullopt;
        }

        return value;
    }

    std::optional<PlaybackMode> ParsePlaybackMode(
        const std::string& text
    )
    {
        const std::string modeName =
            ToUpper(Trim(text));

        if (modeName == "RESTART")
        {
            return PlaybackMode::Restart;
        }

        if (modeName == "OVERLAP")
        {
            return PlaybackMode::Overlap;
        }

        if (modeName == "TOGGLE")
        {
            return PlaybackMode::Toggle;
        }

        if (modeName == "LOOP")
        {
            return PlaybackMode::Loop;
        }

        if (modeName == "IGNORE")
        {
            return PlaybackMode::Ignore;
        }

        return std::nullopt;
    }

    std::optional<ParsedSoundValue> ParseSoundValue(
        const std::string& originalValue,
        std::string& errorReason,
        std::string& correctExample
    )
    {
        ParsedSoundValue parsedValue;

        bool volumeWasSet = false;
        bool modeWasSet = false;
        bool fadeInWasSet = false;
        bool fadeOutWasSet = false;

        std::size_t tokenStart = 0;
        std::size_t tokenIndex = 0;

        while (tokenStart <= originalValue.size())
        {
            const std::size_t separatorPosition =
                originalValue.find('|', tokenStart);

            const std::size_t tokenLength =
                separatorPosition == std::string::npos
                    ? originalValue.size() - tokenStart
                    : separatorPosition - tokenStart;

            const std::string token =
                Trim(originalValue.substr(
                    tokenStart,
                    tokenLength
                ));

            if (token.empty())
            {
                errorReason = Localization::Text(
                    "'|' işaretinin yanında boş bir ses ayarı var.",
                    "There is an empty sound option next to the '|' character."
                );
                correctExample =
                    "F1=example.wav|volume=0.80|mode=restart";
                return std::nullopt;
            }

            if (tokenIndex == 0)
            {
                parsedValue.soundFile = PathFromUtf8(token);
            }
            else
            {
                const std::size_t equalsPosition =
                    token.find('=');

                if (equalsPosition == std::string::npos ||
                    token.find('=', equalsPosition + 1) !=
                        std::string::npos)
                {
                    errorReason =
                        std::string{Localization::Text(
                            "Ses ayarı 'isim=değer' biçiminde olmalı. Hatalı bölüm: ",
                            "A sound option must use the 'name=value' format. Invalid section: "
                        )} + token;
                    correctExample =
                        "F1=example.wav|volume=0.80|mode=restart";
                    return std::nullopt;
                }

                const std::string optionName =
                    ToUpper(Trim(token.substr(
                        0,
                        equalsPosition
                    )));

                const std::string optionValue =
                    Trim(token.substr(
                        equalsPosition + 1
                    ));

                if (optionName.empty() || optionValue.empty())
                {
                    errorReason =
                        std::string{Localization::Text(
                            "Ses ayarının adı veya değeri boş. Hatalı bölüm: ",
                            "The sound option name or value is empty. Invalid section: "
                        )} + token;
                    correctExample =
                        "F1=example.wav|volume=0.80|mode=restart";
                    return std::nullopt;
                }

                if (optionName == "VOLUME")
                {
                    if (volumeWasSet)
                    {
                        errorReason = Localization::Text(
                            "volume ayarı aynı satırda birden fazla yazılmış.",
                            "The volume option is specified more than once on the same line."
                        );
                        correctExample =
                            "F1=example.wav|volume=0.80|mode=restart";
                        return std::nullopt;
                    }

                    const auto volume =
                        ParseVolume(optionValue);

                    if (!volume.has_value())
                    {
                        errorReason =
                            std::string{Localization::Text(
                                "volume değeri 0.00 ile 1.00 arasında bir sayı olmalı. Girilen değer: ",
                                "The volume value must be a number between 0.00 and 1.00. Entered value: "
                            )} + optionValue;
                        correctExample =
                            "F1=example.wav|volume=0.80|mode=restart";
                        return std::nullopt;
                    }

                    parsedValue.volume = *volume;
                    volumeWasSet = true;
                }
                else if (optionName == "MODE")
                {
                    if (modeWasSet)
                    {
                        errorReason = Localization::Text(
                            "mode ayarı aynı satırda birden fazla yazılmış.",
                            "The mode option is specified more than once on the same line."
                        );
                        correctExample =
                            "F1=example.wav|volume=0.80|mode=restart";
                        return std::nullopt;
                    }

                    const auto mode =
                        ParsePlaybackMode(optionValue);

                    if (!mode.has_value())
                    {
                        errorReason =
                            std::string{Localization::Text(
                                "Desteklenmeyen mode değeri: ",
                                "Unsupported mode value: "
                            )} + optionValue + Localization::Text(
                                ". Desteklenenler: restart, overlap, toggle, loop, ignore.",
                                ". Supported values: restart, overlap, toggle, loop, ignore."
                            );
                        correctExample =
                            "F1=example.wav|volume=0.80|mode=restart";
                        return std::nullopt;
                    }

                    parsedValue.mode = *mode;
                    modeWasSet = true;
                }
                else if (optionName == "FADE_IN_MS" ||
                    optionName == "FADE_OUT_MS")
                {
                    bool& wasSet = optionName == "FADE_IN_MS"
                        ? fadeInWasSet
                        : fadeOutWasSet;

                    if (wasSet)
                    {
                        errorReason = optionName +
                            Localization::Text(
                                " ayarı aynı satırda birden fazla yazılmış.",
                                " is specified more than once on the same line."
                            );
                        correctExample =
                            "F1=example.wav|volume=0.80|mode=restart|fade_in_ms=50|fade_out_ms=100";
                        return std::nullopt;
                    }

                    const auto milliseconds =
                        ParseUnsignedInteger(optionValue);

                    if (!milliseconds.has_value() ||
                        *milliseconds > 10000)
                    {
                        errorReason = optionName +
                            std::string{Localization::Text(
                                " değeri 0 ile 10000 arasında tam sayı olmalı. Girilen değer: ",
                                " must be an integer between 0 and 10000. Entered value: "
                            )} + optionValue;
                        correctExample =
                            "F1=example.wav|volume=0.80|mode=restart|fade_in_ms=50|fade_out_ms=100";
                        return std::nullopt;
                    }

                    if (optionName == "FADE_IN_MS")
                    {
                        parsedValue.fadeInMilliseconds = *milliseconds;
                    }
                    else
                    {
                        parsedValue.fadeOutMilliseconds = *milliseconds;
                    }

                    wasSet = true;
                }
                else
                {
                    errorReason =
                        std::string{Localization::Text(
                            "Desteklenmeyen ses ayarı: ",
                            "Unsupported sound option: "
                        )} + optionName + Localization::Text(
                            ". Desteklenen ayarlar: volume, mode, fade_in_ms, fade_out_ms.",
                            ". Supported options: volume, mode, fade_in_ms, fade_out_ms."
                        );
                    correctExample =
                        "F1=example.wav|volume=0.80|mode=restart";
                    return std::nullopt;
                }
            }

            ++tokenIndex;

            if (separatorPosition == std::string::npos)
            {
                break;
            }

            tokenStart = separatorPosition + 1;
        }

        if (parsedValue.soundFile.empty())
        {
            errorReason = Localization::Text(
                "Ses dosyasının adı boş bırakılamaz.",
                "The sound file name cannot be empty."
            );
            correctExample = "F1=example.wav";
            return std::nullopt;
        }

        if (parsedValue.soundFile.has_root_path())
        {
            errorReason = Localization::Text(
                "Ses dosyası yolu sounds klasörüne göre göreli olmalı.",
                "The sound file path must be relative to the sounds folder."
            );
            correctExample =
                "F1=effects/example.wav|volume=0.80|mode=restart";
            return std::nullopt;
        }

        for (const auto& component : parsedValue.soundFile)
        {
            if (component == "..")
            {
                errorReason = Localization::Text(
                    "Ses dosyası yolu sounds klasörünün dışına çıkamaz.",
                    "The sound file path cannot leave the sounds folder."
                );
                correctExample =
                    "F1=effects/example.wav|volume=0.80|mode=restart";
                return std::nullopt;
            }
        }

        if (!SoundFileFormat::IsSupported(
            parsedValue.soundFile
        ))
        {
            const std::string extension =
                SoundFileFormat::NormalizedExtension(
                    parsedValue.soundFile
                );

            errorReason =
                std::string{Localization::Text(
                    "Desteklenmeyen ses dosyası uzantısı: ",
                    "Unsupported sound file extension: "
                )} +
                (extension.empty()
                    ? std::string{Localization::Text(
                        "<uzantı yok>",
                        "<no extension>"
                    )}
                    : extension) +
                Localization::Text(
                    ". Desteklenen uzantılar: ",
                    ". Supported extensions: "
                ) +
                std::string{SoundFileFormat::SupportedExtensions()} + ".";
            correctExample =
                "F1=example.mp3|volume=0.80|mode=restart";
            return std::nullopt;
        }

        return parsedValue;
    }

    std::optional<ParsedBaseKey> ParseBaseKey(
        const std::string& originalKeyName
    )
    {
        const std::string keyName =
            ToUpper(Trim(originalKeyName));

        if (keyName.empty())
        {
            return std::nullopt;
        }

        if (keyName.size() >= 2 && keyName.front() == 'F')
        {
            int functionKeyNumber = 0;

            const char* begin = keyName.data() + 1;
            const char* end = keyName.data() + keyName.size();

            const auto [pointer, error] =
                std::from_chars(
                    begin,
                    end,
                    functionKeyNumber
                );

            if (error == std::errc{} &&
                pointer == end &&
                functionKeyNumber >= 1 &&
                functionKeyNumber <= 24)
            {
                return ParsedBaseKey{
                    "F" + std::to_string(functionKeyNumber),
                    static_cast<unsigned int>(
                        VK_F1 + functionKeyNumber - 1
                    )
                };
            }
        }

        constexpr std::string_view numpadPrefix = "NUMPAD";

        if (keyName.starts_with(numpadPrefix) &&
            keyName.size() == numpadPrefix.size() + 1 &&
            keyName.back() >= '0' &&
            keyName.back() <= '9')
        {
            const int number = keyName.back() - '0';

            return ParsedBaseKey{
                "NUMPAD" + std::to_string(number),
                static_cast<unsigned int>(VK_NUMPAD0 + number)
            };
        }

        if (keyName.size() == 1)
        {
            const unsigned char character =
                static_cast<unsigned char>(keyName.front());

            if ((character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9'))
            {
                return ParsedBaseKey{
                    keyName,
                    static_cast<unsigned int>(character)
                };
            }
        }

        const std::pair<std::string_view, unsigned int> namedKeys[] = {
            {"SPACE", VK_SPACE},
            {"TAB", VK_TAB},
            {"ENTER", VK_RETURN},
            {"RETURN", VK_RETURN},
            {"ESC", VK_ESCAPE},
            {"ESCAPE", VK_ESCAPE},
            {"BACKSPACE", VK_BACK},
            {"INSERT", VK_INSERT},
            {"DELETE", VK_DELETE},
            {"HOME", VK_HOME},
            {"END", VK_END},
            {"PAGEUP", VK_PRIOR},
            {"PGUP", VK_PRIOR},
            {"PAGEDOWN", VK_NEXT},
            {"PGDN", VK_NEXT},
            {"UP", VK_UP},
            {"DOWN", VK_DOWN},
            {"LEFT", VK_LEFT},
            {"RIGHT", VK_RIGHT},
            {"NUMPAD_ADD", VK_ADD},
            {"NUMPAD_SUBTRACT", VK_SUBTRACT},
            {"NUMPAD_MULTIPLY", VK_MULTIPLY},
            {"NUMPAD_DIVIDE", VK_DIVIDE},
            {"NUMPAD_DECIMAL", VK_DECIMAL}
        };

        for (const auto& [name, virtualKey] : namedKeys)
        {
            if (keyName == name)
            {
                std::string canonicalName{name};

                if (keyName == "RETURN")
                {
                    canonicalName = "ENTER";
                }
                else if (keyName == "ESCAPE")
                {
                    canonicalName = "ESC";
                }
                else if (keyName == "PGUP")
                {
                    canonicalName = "PAGEUP";
                }
                else if (keyName == "PGDN")
                {
                    canonicalName = "PAGEDOWN";
                }

                return ParsedBaseKey{
                    std::move(canonicalName),
                    virtualKey
                };
            }
        }

        return std::nullopt;
    }

    std::optional<ParsedHotkey> ParseHotkey(
        const std::string& originalHotkey,
        std::string& errorReason
    )
    {
        const std::string hotkeyText = Trim(originalHotkey);

        if (hotkeyText.empty())
        {
            errorReason = Localization::Text(
                "Hotkey boş bırakılamaz.",
                "The hotkey cannot be empty."
            );
            return std::nullopt;
        }

        unsigned int modifiers = 0;
        std::optional<ParsedBaseKey> baseKey;

        std::size_t tokenStart = 0;

        while (tokenStart <= hotkeyText.size())
        {
            const std::size_t separatorPosition =
                hotkeyText.find('+', tokenStart);

            const std::size_t tokenLength =
                separatorPosition == std::string::npos
                    ? hotkeyText.size() - tokenStart
                    : separatorPosition - tokenStart;

            const std::string token =
                ToUpper(Trim(hotkeyText.substr(
                    tokenStart,
                    tokenLength
                )));

            if (token.empty())
            {
                errorReason = Localization::Text(
                    "'+' işaretinin iki yanında bir tuş adı olmalı.",
                    "A key name must appear on both sides of the '+' character."
                );
                return std::nullopt;
            }

            unsigned int modifier = 0;

            if (token == "CTRL" || token == "CONTROL")
            {
                modifier = MOD_CONTROL;
            }
            else if (token == "SHIFT")
            {
                modifier = MOD_SHIFT;
            }
            else if (token == "ALT")
            {
                modifier = MOD_ALT;
            }
            else if (token == "WIN" || token == "WINDOWS")
            {
                modifier = MOD_WIN;
            }

            if (modifier != 0)
            {
                if ((modifiers & modifier) != 0)
                {
                    errorReason =
                        std::string{Localization::Text(
                            "Aynı modifier birden fazla yazılmış: ",
                            "The same modifier is specified more than once: "
                        )} + token;
                    return std::nullopt;
                }

                modifiers |= modifier;
            }
            else
            {
                if (baseKey.has_value())
                {
                    errorReason = Localization::Text(
                        "Bir hotkey yalnızca bir ana tuş içerebilir. F1+F2 gibi iki ana tuş birlikte kullanılamaz.",
                        "A hotkey can contain only one primary key. Two primary keys such as F1+F2 cannot be used together."
                    );
                    return std::nullopt;
                }

                baseKey = ParseBaseKey(token);

                if (!baseKey.has_value())
                {
                    errorReason =
                        std::string{Localization::Text(
                            "Desteklenmeyen tuş adı: ",
                            "Unsupported key name: "
                        )} + token + Localization::Text(
                            ". F1-F24, A-Z, 0-9, NUMPAD0-NUMPAD9 ve desteklenen özel tuşlardan birini kullanın.",
                            ". Use F1-F24, A-Z, 0-9, NUMPAD0-NUMPAD9, or one of the supported special keys."
                        );
                    return std::nullopt;
                }
            }

            if (separatorPosition == std::string::npos)
            {
                break;
            }

            tokenStart = separatorPosition + 1;
        }

        if (!baseKey.has_value())
        {
            errorReason = Localization::Text(
                "Hotkey içinde ana tuş eksik. CTRL veya SHIFT tek başına hotkey olamaz.",
                "The hotkey is missing a primary key. CTRL or SHIFT cannot be used as a hotkey by itself."
            );
            return std::nullopt;
        }

        std::string canonicalName;

        const auto appendPart =
            [&canonicalName](const std::string_view part)
            {
                if (!canonicalName.empty())
                {
                    canonicalName += '+';
                }

                canonicalName += part;
            };

        if ((modifiers & MOD_CONTROL) != 0)
        {
            appendPart("CTRL");
        }

        if ((modifiers & MOD_SHIFT) != 0)
        {
            appendPart("SHIFT");
        }

        if ((modifiers & MOD_ALT) != 0)
        {
            appendPart("ALT");
        }

        if ((modifiers & MOD_WIN) != 0)
        {
            appendPart("WIN");
        }

        appendPart(baseKey->canonicalName);

        return ParsedHotkey{
            std::move(canonicalName),
            modifiers,
            baseKey->virtualKey
        };
    }

    std::uint64_t MakeHotkeyIdentity(
        const unsigned int modifiers,
        const unsigned int virtualKey
    )
    {
        return
            (static_cast<std::uint64_t>(modifiers) << 32U) |
            static_cast<std::uint64_t>(virtualKey);
    }

    bool IsKnownSetting(const std::string& settingName)
    {
        if (FindProcessingBooleanSetting(settingName) != nullptr ||
            FindProcessingFloatSetting(settingName) != nullptr ||
            FindVoiceEffectBooleanSetting(settingName) != nullptr ||
            FindVoiceEffectFloatSetting(settingName) != nullptr ||
            settingName == "MICROPHONE_PROCESSING_PRESET" ||
            settingName == "MICROPHONE_NOISE_SUPPRESSION_LEVEL" ||
            settingName == "VOICE_EFFECTS_PRESET" ||
            settingName == "VOICE_EFFECTS_USER_PRESET")
        {
            return true;
        }

        return settingName == "LANGUAGE" ||
            settingName == "THEME" ||
            settingName == "OUTPUT" ||
            settingName == "OUTPUT_VOLUME" ||
            settingName == "MONITOR" ||
            settingName == "MONITOR_VOLUME" ||
            settingName == "MICROPHONE_ENABLED" ||
            settingName == "MICROPHONE" ||
            settingName == "MICROPHONE_VOLUME" ||
            settingName == "MICROPHONE_TO_OUTPUT" ||
            settingName == "MICROPHONE_TO_MONITOR" ||
            settingName == "AUDIO_SAMPLE_RATE" ||
            settingName == "AUDIO_BUFFER_MS" ||
            settingName == "START_WITH_WINDOWS" ||
            settingName == "SHOW_CONSOLE_ON_START" ||
            settingName == "CHECK_UPDATES_ON_START" ||
            settingName == "STOP" ||
            settingName == "OUTPUT_MUTE" ||
            settingName == "MONITOR_MUTE" ||
            settingName == "VOICE_EFFECTS_PREVIOUS_PRESET" ||
            settingName == "VOICE_EFFECTS_NEXT_PRESET" ||
            settingName == "VOICE_EFFECTS_BYPASS" ||
            settingName == "RELOAD" ||
            settingName == "EXIT";
    }
}

bool Config::Load(const std::filesystem::path& filePath)
{
    language_ = Language::Turkish;
    Localization::SetLanguage(language_);
    theme_ = AppTheme::Dark;

    outputDevice_ = "default";
    outputVolume_ = 1.0f;

    monitorDevice_ = "default";
    monitorVolume_ = 0.30f;

    microphoneEnabled_ = false;
    microphoneDevice_ = "default";
    microphoneVolume_ = 1.0f;
    microphoneToOutput_ = true;
    microphoneToMonitor_ = false;
    microphoneProcessingSettings_ = {};
    voiceEffectSettings_ = {};
    voiceEffectUserPresets_.clear();

    audioSampleRate_ = 48000;
    audioBufferMilliseconds_ = 5;

    startWithWindows_ = false;
    showConsoleOnStart_ = false;
    checkUpdatesOnStart_ = true;

    stopKeyName_ = "F11";
    stopModifiers_ = 0;
    stopVirtualKey_ = static_cast<unsigned int>(VK_F11);

    outputMuteKeyName_ = "CTRL+SHIFT+F9";
    outputMuteModifiers_ = MOD_CONTROL | MOD_SHIFT;
    outputMuteVirtualKey_ = static_cast<unsigned int>(VK_F9);

    monitorMuteKeyName_ = "CTRL+SHIFT+F10";
    monitorMuteModifiers_ = MOD_CONTROL | MOD_SHIFT;
    monitorMuteVirtualKey_ = static_cast<unsigned int>(VK_F10);

    voiceEffectsPreviousPresetKeyName_ = "CTRL+ALT+F21";
    voiceEffectsPreviousPresetModifiers_ = MOD_CONTROL | MOD_ALT;
    voiceEffectsPreviousPresetVirtualKey_ =
        static_cast<unsigned int>(VK_F1 + 20);

    voiceEffectsNextPresetKeyName_ = "CTRL+ALT+F22";
    voiceEffectsNextPresetModifiers_ = MOD_CONTROL | MOD_ALT;
    voiceEffectsNextPresetVirtualKey_ =
        static_cast<unsigned int>(VK_F1 + 21);

    voiceEffectsBypassKeyName_ = "CTRL+ALT+F23";
    voiceEffectsBypassModifiers_ = MOD_CONTROL | MOD_ALT;
    voiceEffectsBypassVirtualKey_ = static_cast<unsigned int>(VK_F1 + 22);

    reloadKeyName_ = "CTRL+SHIFT+F11";
    reloadModifiers_ = MOD_CONTROL | MOD_SHIFT;
    reloadVirtualKey_ = static_cast<unsigned int>(VK_F11);

    exitKeyName_ = "CTRL+SHIFT+F12";
    exitModifiers_ = MOD_CONTROL | MOD_SHIFT;
    exitVirtualKey_ = static_cast<unsigned int>(VK_F12);

    bindings_.clear();

    std::ifstream file(filePath);

    if (!file.is_open())
    {
        PrintConfigError(
            0,
            {},
            std::string{Localization::Text(
                "Config dosyası açılamadı: ",
                "The config file could not be opened: "
            )} + PathToUtf8(filePath),
            Localization::Text(
                "config.txt dosyasını EXE'nin yanına koyun.",
                "Place config.txt next to the executable."
            )
        );

        return false;
    }

    std::vector<std::string> rawLines;
    std::string line;

    while (std::getline(file, line))
    {
        rawLines.push_back(line);
    }

    for (const std::string& rawLine : rawLines)
    {
        const std::string candidate = Trim(rawLine);
        const std::size_t equalsPosition = candidate.find('=');

        if (equalsPosition == std::string::npos)
        {
            continue;
        }

        const std::string settingName = ToUpper(
            Trim(candidate.substr(0, equalsPosition))
        );

        if (settingName != "LANGUAGE")
        {
            continue;
        }

        const auto detectedLanguage = Localization::ParseLanguage(
            Trim(candidate.substr(equalsPosition + 1))
        );

        if (detectedLanguage.has_value())
        {
            language_ = *detectedLanguage;
            Localization::SetLanguage(language_);
        }

        break;
    }

    std::size_t errorCount = 0;

    const auto reportError =
        [&errorCount](
            const std::size_t lineNumber,
            const std::string_view originalLine,
            const std::string_view reason,
            const std::string_view example
        )
        {
            ++errorCount;
            PrintConfigError(
                lineNumber,
                originalLine,
                reason,
                example
            );
        };

    std::vector<ParsedLine> parsedLines;
    std::size_t lineNumber = 0;

    for (const std::string& rawLine : rawLines)
    {
        ++lineNumber;

        const std::string originalLine = Trim(rawLine);

        if (originalLine.empty() || originalLine.front() == '#')
        {
            continue;
        }

        const std::size_t equalsPosition =
            originalLine.find('=');

        if (equalsPosition == std::string::npos)
        {
            reportError(
                lineNumber,
                originalLine,
                Localization::Text(
                    "Satırda '=' işareti yok. Her ayar 'sol=sağ' biçiminde yazılmalı.",
                    "The line has no '=' character. Every setting must use the 'left=right' format."
                ),
                "F1=example.wav|volume=0.80|mode=restart"
            );
            continue;
        }

        const std::string leftSide =
            Trim(originalLine.substr(0, equalsPosition));

        const std::string rightSide =
            Trim(originalLine.substr(equalsPosition + 1));

        if (leftSide.empty())
        {
            reportError(
                lineNumber,
                originalLine,
                Localization::Text(
                    "'=' işaretinin sol tarafı boş. Ayar veya hotkey adı yazılmalı.",
                    "The left side of '=' is empty. Enter a setting or hotkey name."
                ),
                "F1=example.wav"
            );
            continue;
        }

        if (rightSide.empty())
        {
            reportError(
                lineNumber,
                originalLine,
                Localization::Text(
                    "'=' işaretinin sağ tarafı boş. Bir değer veya ses dosyası yazılmalı.",
                    "The right side of '=' is empty. Enter a value or sound file."
                ),
                "F1=example.wav"
            );
            continue;
        }

        parsedLines.push_back(
            ParsedLine{
                leftSide,
                rightSide,
                originalLine,
                lineNumber
            }
        );
    }

    SettingSource stopSource;
    SettingSource outputMuteSource;
    SettingSource monitorMuteSource;
    SettingSource voiceEffectsPreviousPresetSource;
    SettingSource voiceEffectsNextPresetSource;
    SettingSource voiceEffectsBypassSource;
    SettingSource reloadSource;
    SettingSource exitSource;

    std::unordered_set<std::string> seenSettings;
    std::vector<std::string> seenVoiceEffectUserPresetNames;

    // Parse general settings first so config order does not matter.
    for (const ParsedLine& parsedLine : parsedLines)
    {
        const std::string settingName =
            ToUpper(parsedLine.leftSide);

        if (!IsKnownSetting(settingName))
        {
            continue;
        }

        if (settingName != "VOICE_EFFECTS_USER_PRESET")
        {
            const auto [iterator, inserted] =
                seenSettings.insert(settingName);

            static_cast<void>(iterator);

            if (!inserted)
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "Bu ayar config içinde birden fazla yazılmış: ",
                        "This setting is specified more than once in the config: "
                    )} + settingName,
                    SettingExample(settingName)
                );
                continue;
            }
        }

        if (settingName == "LANGUAGE")
        {
            const auto language = Localization::ParseLanguage(
                parsedLine.rightSide
            );

            if (!language.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "language değeri 'tr' veya 'en' olmalı. Girilen değer: ",
                        "language must be 'tr' or 'en'. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            language_ = *language;
            Localization::SetLanguage(language_);
            continue;
        }

        if (settingName == "THEME")
        {
            const std::string normalizedTheme =
                ToUpper(parsedLine.rightSide);

            if (normalizedTheme == "LIGHT")
            {
                theme_ = AppTheme::Light;
                continue;
            }

            if (normalizedTheme == "DARK")
            {
                theme_ = AppTheme::Dark;
                continue;
            }

            reportError(
                parsedLine.lineNumber,
                parsedLine.originalLine,
                std::string{Localization::Text(
                    "theme değeri 'light' veya 'dark' olmalı. Girilen değer: ",
                    "theme must be 'light' or 'dark'. Entered value: "
                )} + parsedLine.rightSide,
                SettingExample(settingName)
            );
            continue;
        }

        if (settingName == "OUTPUT")
        {
            outputDevice_ = parsedLine.rightSide;
            continue;
        }

        if (settingName == "OUTPUT_VOLUME")
        {
            const auto volume =
                ParseVolume(parsedLine.rightSide);

            if (!volume.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "output_volume değeri 0.00 ile 1.00 arasında bir sayı olmalı. Girilen değer: ",
                        "output_volume must be a number between 0.00 and 1.00. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            outputVolume_ = *volume;
            continue;
        }

        if (settingName == "MONITOR")
        {
            monitorDevice_ = parsedLine.rightSide;
            continue;
        }

        if (settingName == "MONITOR_VOLUME")
        {
            const auto volume =
                ParseVolume(parsedLine.rightSide);

            if (!volume.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "monitor_volume değeri 0.00 ile 1.00 arasında bir sayı olmalı. Girilen değer: ",
                        "monitor_volume must be a number between 0.00 and 1.00. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            monitorVolume_ = *volume;
            continue;
        }

        if (settingName == "MICROPHONE_ENABLED")
        {
            const auto enabled = ParseBoolean(parsedLine.rightSide);

            if (!enabled.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "microphone_enabled değeri true veya false olmalı. Girilen değer: ",
                        "microphone_enabled must be true or false. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            microphoneEnabled_ = *enabled;
            continue;
        }

        if (settingName == "MICROPHONE")
        {
            microphoneDevice_ = parsedLine.rightSide;
            continue;
        }

        if (settingName == "MICROPHONE_VOLUME")
        {
            const auto volume = ParseVolume(parsedLine.rightSide);

            if (!volume.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "microphone_volume değeri 0.00 ile 1.00 arasında bir sayı olmalı. Girilen değer: ",
                        "microphone_volume must be a number between 0.00 and 1.00. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            microphoneVolume_ = *volume;
            continue;
        }

        if (settingName == "MICROPHONE_TO_OUTPUT")
        {
            const auto enabled = ParseBoolean(parsedLine.rightSide);

            if (!enabled.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "microphone_to_output değeri true veya false olmalı. Girilen değer: ",
                        "microphone_to_output must be true or false. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            microphoneToOutput_ = *enabled;
            continue;
        }

        if (settingName == "MICROPHONE_TO_MONITOR")
        {
            const auto enabled = ParseBoolean(parsedLine.rightSide);

            if (!enabled.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "microphone_to_monitor değeri true veya false olmalı. Girilen değer: ",
                        "microphone_to_monitor must be true or false. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            microphoneToMonitor_ = *enabled;
            continue;
        }

        if (const auto* processingSetting =
                FindProcessingBooleanSetting(settingName))
        {
            const auto enabled = ParseBoolean(parsedLine.rightSide);

            if (!enabled.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "Mikrofon işleme ayarı true veya false olmalı: ",
                        "The microphone-processing setting must be true or false: "
                    )} + settingName +
                        Localization::Text(
                            ". Girilen değer: ",
                            ". Entered value: "
                        ) + parsedLine.rightSide,
                    processingSetting->example
                );
                continue;
            }

            microphoneProcessingSettings_.*(processingSetting->member) =
                *enabled;
            continue;
        }

        if (const auto* processingSetting =
                FindProcessingFloatSetting(settingName))
        {
            const auto value = ParseFiniteFloat(parsedLine.rightSide);
            const bool valid = value.has_value() &&
                *value >= processingSetting->minimum &&
                *value <= processingSetting->maximum;

            if (!valid)
            {
                const std::string_view range =
                    Localization::GetLanguage() == Language::English
                        ? processingSetting->rangeEnglish
                        : processingSetting->rangeTurkish;

                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "Mikrofon işleme ayarı sonlu bir sayı olmalı ve şu aralıkta kalmalı: ",
                        "The microphone-processing setting must be a finite number in this range: "
                    )} + settingName + " (" +
                        std::string{range} + ")." +
                        Localization::Text(
                            " Girilen değer: ",
                            " Entered value: "
                        ) + parsedLine.rightSide,
                    processingSetting->example
                );
                continue;
            }

            microphoneProcessingSettings_.*(processingSetting->member) =
                *value;
            continue;
        }

        if (settingName == "MICROPHONE_PROCESSING_PRESET")
        {
            const auto preset = ParseMicrophoneProcessingPreset(
                parsedLine.rightSide
            );

            if (!preset.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "microphone_processing_preset natural, clean, strong, aggressive veya custom olmalı. Girilen değer: ",
                        "microphone_processing_preset must be natural, clean, strong, aggressive or custom. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            microphoneProcessingSettings_.preset = *preset;
            continue;
        }

        if (settingName == "MICROPHONE_NOISE_SUPPRESSION_LEVEL")
        {
            const auto level = ParseMicrophoneNoiseSuppressionLevel(
                parsedLine.rightSide
            );

            if (!level.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "microphone_noise_suppression_level light, balanced veya strong olmalı. Girilen değer: ",
                        "microphone_noise_suppression_level must be light, balanced or strong. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            microphoneProcessingSettings_.noiseSuppressionLevel = *level;
            continue;
        }

        if (const auto* voiceEffectSetting =
                FindVoiceEffectBooleanSetting(settingName))
        {
            const auto enabled = ParseBoolean(parsedLine.rightSide);

            if (!enabled.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "Voice Effects ayarı true veya false olmalı: ",
                        "The Voice Effects setting must be true or false: "
                    )} + settingName +
                        Localization::Text(
                            ". Girilen değer: ",
                            ". Entered value: "
                        ) + parsedLine.rightSide,
                    voiceEffectSetting->example
                );
                continue;
            }

            voiceEffectSettings_.*(voiceEffectSetting->member) = *enabled;
            continue;
        }

        if (const auto* voiceEffectSetting =
                FindVoiceEffectFloatSetting(settingName))
        {
            const auto value = ParseFiniteFloat(parsedLine.rightSide);
            const bool valid = value.has_value() &&
                *value >= voiceEffectSetting->minimum &&
                *value <= voiceEffectSetting->maximum;

            if (!valid)
            {
                const std::string_view range =
                    Localization::GetLanguage() == Language::English
                        ? voiceEffectSetting->rangeEnglish
                        : voiceEffectSetting->rangeTurkish;

                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "Voice Effects ayarı sonlu bir sayı olmalı ve şu aralıkta kalmalı: ",
                        "The Voice Effects setting must be a finite number in this range: "
                    )} + settingName + " (" +
                        std::string{range} + ")." +
                        Localization::Text(
                            " Girilen değer: ",
                            " Entered value: "
                        ) + parsedLine.rightSide,
                    voiceEffectSetting->example
                );
                continue;
            }

            voiceEffectSettings_.*(voiceEffectSetting->member) = *value;
            continue;
        }

        if (settingName == "VOICE_EFFECTS_PRESET")
        {
            const auto preset = ParseVoiceEffectPreset(
                parsedLine.rightSide
            );

            if (!preset.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "voice_effects_preset deep-heavy, high-nasal-rap, dark-vocal, radio, robot, tiny-high-voice veya custom olmalı. Girilen değer: ",
                        "voice_effects_preset must be deep-heavy, high-nasal-rap, dark-vocal, radio, robot, tiny-high-voice or custom. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            voiceEffectSettings_.preset = *preset;
            continue;
        }

        if (settingName == "VOICE_EFFECTS_USER_PRESET")
        {
            std::string errorReason;
            auto preset = ParseVoiceEffectUserPresetValue(
                parsedLine.rightSide,
                errorReason
            );

            if (!preset.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    errorReason,
                    SettingExample(settingName)
                );
                continue;
            }

            if (voiceEffectUserPresets_.size() >=
                VoiceEffectLimits::MaximumUserPresetCount)
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    Localization::Text(
                        "En fazla 32 Voice Effects kullanıcı preset'i kaydedilebilir.",
                        "A maximum of 32 Voice Effects user presets can be saved."
                    ),
                    SettingExample(settingName)
                );
                continue;
            }

            const bool duplicateName = std::any_of(
                seenVoiceEffectUserPresetNames.begin(),
                seenVoiceEffectUserPresetNames.end(),
                [&preset](const std::string& existingName)
                {
                    return VoiceEffectUserPresetNamesEqual(
                        existingName,
                        preset->name
                    );
                }
            );

            if (duplicateName)
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "Kullanıcı preset adı birden fazla kullanılmış: ",
                        "A user-preset name is used more than once: "
                    )} + preset->name,
                    SettingExample(settingName)
                );
                continue;
            }

            seenVoiceEffectUserPresetNames.push_back(preset->name);
            voiceEffectUserPresets_.push_back(std::move(*preset));
            continue;
        }

        if (settingName == "AUDIO_SAMPLE_RATE")
        {
            const auto sampleRate =
                ParseUnsignedInteger(parsedLine.rightSide);

            const bool isValid =
                sampleRate.has_value() &&
                (*sampleRate == 0 ||
                    (*sampleRate >= 8000 && *sampleRate <= 192000));

            if (!isValid)
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "audio_sample_rate değeri 0 veya 8000 ile 192000 arasında tam sayı olmalı. 0 cihazın doğal örnekleme hızını kullanır. Girilen değer: ",
                        "audio_sample_rate must be 0 or an integer between 8000 and 192000. 0 uses the device's native sample rate. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            audioSampleRate_ = *sampleRate;
            continue;
        }

        if (settingName == "AUDIO_BUFFER_MS")
        {
            const auto bufferMilliseconds =
                ParseUnsignedInteger(parsedLine.rightSide);

            const bool isValid =
                bufferMilliseconds.has_value() &&
                (*bufferMilliseconds == 0 ||
                    (*bufferMilliseconds >= 2 &&
                        *bufferMilliseconds <= 100));

            if (!isValid)
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "audio_buffer_ms değeri 0 veya 2 ile 100 arasında tam sayı olmalı. 0 miniaudio/Windows varsayılanını kullanır. Girilen değer: ",
                        "audio_buffer_ms must be 0 or an integer between 2 and 100. 0 uses the miniaudio/Windows default. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            audioBufferMilliseconds_ = *bufferMilliseconds;
            continue;
        }

        if (settingName == "START_WITH_WINDOWS")
        {
            const auto enabled = ParseBoolean(parsedLine.rightSide);

            if (!enabled.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "start_with_windows değeri true veya false olmalı. Girilen değer: ",
                        "start_with_windows must be true or false. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            startWithWindows_ = *enabled;
            continue;
        }

        if (settingName == "SHOW_CONSOLE_ON_START")
        {
            const auto enabled = ParseBoolean(parsedLine.rightSide);

            if (!enabled.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "show_console_on_start değeri true veya false olmalı. Girilen değer: ",
                        "show_console_on_start must be true or false. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            showConsoleOnStart_ = *enabled;
            continue;
        }

        if (settingName == "CHECK_UPDATES_ON_START")
        {
            const auto enabled = ParseBoolean(parsedLine.rightSide);

            if (!enabled.has_value())
            {
                reportError(
                    parsedLine.lineNumber,
                    parsedLine.originalLine,
                    std::string{Localization::Text(
                        "check_updates_on_start değeri true veya false olmalı. Girilen değer: ",
                        "check_updates_on_start must be true or false. Entered value: "
                    )} + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            checkUpdatesOnStart_ = *enabled;
            continue;
        }

        std::string hotkeyError;
        const auto hotkey =
            ParseHotkey(parsedLine.rightSide, hotkeyError);

        if (!hotkey.has_value())
        {
            reportError(
                parsedLine.lineNumber,
                parsedLine.originalLine,
                hotkeyError,
                SettingExample(settingName)
            );
            continue;
        }

        const SettingSource source{
            parsedLine.lineNumber,
            parsedLine.originalLine
        };

        if (settingName == "STOP")
        {
            stopKeyName_ = hotkey->canonicalName;
            stopModifiers_ = hotkey->modifiers;
            stopVirtualKey_ = hotkey->virtualKey;
            stopSource = source;
            continue;
        }

        if (settingName == "OUTPUT_MUTE")
        {
            outputMuteKeyName_ = hotkey->canonicalName;
            outputMuteModifiers_ = hotkey->modifiers;
            outputMuteVirtualKey_ = hotkey->virtualKey;
            outputMuteSource = source;
            continue;
        }

        if (settingName == "MONITOR_MUTE")
        {
            monitorMuteKeyName_ = hotkey->canonicalName;
            monitorMuteModifiers_ = hotkey->modifiers;
            monitorMuteVirtualKey_ = hotkey->virtualKey;
            monitorMuteSource = source;
            continue;
        }

        if (settingName == "VOICE_EFFECTS_PREVIOUS_PRESET")
        {
            voiceEffectsPreviousPresetKeyName_ = hotkey->canonicalName;
            voiceEffectsPreviousPresetModifiers_ = hotkey->modifiers;
            voiceEffectsPreviousPresetVirtualKey_ = hotkey->virtualKey;
            voiceEffectsPreviousPresetSource = source;
            continue;
        }

        if (settingName == "VOICE_EFFECTS_NEXT_PRESET")
        {
            voiceEffectsNextPresetKeyName_ = hotkey->canonicalName;
            voiceEffectsNextPresetModifiers_ = hotkey->modifiers;
            voiceEffectsNextPresetVirtualKey_ = hotkey->virtualKey;
            voiceEffectsNextPresetSource = source;
            continue;
        }

        if (settingName == "VOICE_EFFECTS_BYPASS")
        {
            voiceEffectsBypassKeyName_ = hotkey->canonicalName;
            voiceEffectsBypassModifiers_ = hotkey->modifiers;
            voiceEffectsBypassVirtualKey_ = hotkey->virtualKey;
            voiceEffectsBypassSource = source;
            continue;
        }

        if (settingName == "RELOAD")
        {
            reloadKeyName_ = hotkey->canonicalName;
            reloadModifiers_ = hotkey->modifiers;
            reloadVirtualKey_ = hotkey->virtualKey;
            reloadSource = source;
            continue;
        }

        if (settingName == "EXIT")
        {
            exitKeyName_ = hotkey->canonicalName;
            exitModifiers_ = hotkey->modifiers;
            exitVirtualKey_ = hotkey->virtualKey;
            exitSource = source;
        }
    }

    const std::array controlHotkeys{
        ControlHotkey{
            "stop",
            stopKeyName_,
            MakeHotkeyIdentity(stopModifiers_, stopVirtualKey_),
            &stopSource,
            "stop=F11"
        },
        ControlHotkey{
            "output_mute",
            outputMuteKeyName_,
            MakeHotkeyIdentity(
                outputMuteModifiers_,
                outputMuteVirtualKey_
            ),
            &outputMuteSource,
            "output_mute=CTRL+SHIFT+F9"
        },
        ControlHotkey{
            "monitor_mute",
            monitorMuteKeyName_,
            MakeHotkeyIdentity(
                monitorMuteModifiers_,
                monitorMuteVirtualKey_
            ),
            &monitorMuteSource,
            "monitor_mute=CTRL+SHIFT+F10"
        },
        ControlHotkey{
            "voice_effects_previous_preset",
            voiceEffectsPreviousPresetKeyName_,
            MakeHotkeyIdentity(
                voiceEffectsPreviousPresetModifiers_,
                voiceEffectsPreviousPresetVirtualKey_
            ),
            &voiceEffectsPreviousPresetSource,
            "voice_effects_previous_preset=CTRL+ALT+F21"
        },
        ControlHotkey{
            "voice_effects_next_preset",
            voiceEffectsNextPresetKeyName_,
            MakeHotkeyIdentity(
                voiceEffectsNextPresetModifiers_,
                voiceEffectsNextPresetVirtualKey_
            ),
            &voiceEffectsNextPresetSource,
            "voice_effects_next_preset=CTRL+ALT+F22"
        },
        ControlHotkey{
            "voice_effects_bypass",
            voiceEffectsBypassKeyName_,
            MakeHotkeyIdentity(
                voiceEffectsBypassModifiers_,
                voiceEffectsBypassVirtualKey_
            ),
            &voiceEffectsBypassSource,
            "voice_effects_bypass=CTRL+ALT+F23"
        },
        ControlHotkey{
            "reload",
            reloadKeyName_,
            MakeHotkeyIdentity(reloadModifiers_, reloadVirtualKey_),
            &reloadSource,
            "reload=CTRL+SHIFT+F11"
        },
        ControlHotkey{
            "exit",
            exitKeyName_,
            MakeHotkeyIdentity(exitModifiers_, exitVirtualKey_),
            &exitSource,
            "exit=CTRL+SHIFT+F12"
        }
    };

    std::unordered_map<std::uint64_t, std::size_t>
        controlIdentityOwners;

    for (std::size_t index = 0;
         index < controlHotkeys.size();
         ++index)
    {
        const ControlHotkey& current = controlHotkeys[index];

        const auto [iterator, inserted] =
            controlIdentityOwners.emplace(
                current.identity,
                index
            );

        if (inserted)
        {
            continue;
        }

        const ControlHotkey& previous =
            controlHotkeys[iterator->second];

        const bool currentWasConfigured =
            current.source->lineNumber != 0;

        const bool previousWasConfigured =
            previous.source->lineNumber != 0;

        const ControlHotkey& errorControl =
            currentWasConfigured || !previousWasConfigured
                ? current
                : previous;

        const ControlHotkey& otherControl =
            &errorControl == &current
                ? previous
                : current;

        reportError(
            errorControl.source->lineNumber,
            errorControl.source->originalLine,
            std::string{Localization::Text(
                "Kontrol hotkey'i başka bir kontrolle çakışıyor: ",
                "A control hotkey conflicts with another control: "
            )} + std::string{errorControl.settingName} +
                Localization::Text(" ve ", " and ") +
                std::string{otherControl.settingName} +
                Localization::Text(
                    " aynı tuşu kullanıyor (",
                    " use the same key ("
                ) + std::string{errorControl.keyName} + ").",
            errorControl.example
        );
    }

    // Reserve control hotkeys before validating sound bindings.
    std::unordered_set<std::uint64_t> usedHotkeys;

    for (const ControlHotkey& control : controlHotkeys)
    {
        usedHotkeys.insert(control.identity);
    }

    // Parse sound bindings after the control hotkeys are known.
    for (const ParsedLine& parsedLine : parsedLines)
    {
        const std::string settingName =
            ToUpper(parsedLine.leftSide);

        if (IsKnownSetting(settingName))
        {
            continue;
        }

        std::string hotkeyError;
        const auto hotkey =
            ParseHotkey(parsedLine.leftSide, hotkeyError);

        if (!hotkey.has_value())
        {
            reportError(
                parsedLine.lineNumber,
                parsedLine.originalLine,
                std::string{Localization::Text(
                    "Sol taraf ne desteklenen bir ayar adı ne de geçerli bir hotkey. ",
                    "The left side is neither a supported setting name nor a valid hotkey. "
                )} + hotkeyError,
                "CTRL+F3=example.mp3|volume=0.80|mode=restart"
            );
            continue;
        }

        const std::uint64_t identity =
            MakeHotkeyIdentity(
                hotkey->modifiers,
                hotkey->virtualKey
            );

        if (usedHotkeys.contains(identity))
        {
            reportError(
                parsedLine.lineNumber,
                parsedLine.originalLine,
                std::string{Localization::Text(
                    "Bu hotkey daha önce kullanılmış veya kontrol tuşlarından biri için rezerve edilmiş: ",
                    "This hotkey is already used or reserved for a control: "
                )} + hotkey->canonicalName,
                "CTRL+F3=example.mp3|volume=0.80|mode=restart"
            );
            continue;
        }

        std::string soundError;
        std::string soundExample;

        const auto soundValue =
            ParseSoundValue(
                parsedLine.rightSide,
                soundError,
                soundExample
            );

        if (!soundValue.has_value())
        {
            reportError(
                parsedLine.lineNumber,
                parsedLine.originalLine,
                soundError,
                soundExample
            );
            continue;
        }

        usedHotkeys.insert(identity);

        bindings_.push_back(
            SoundBinding{
                hotkey->canonicalName,
                hotkey->modifiers,
                hotkey->virtualKey,
                soundValue->soundFile,
                soundValue->volume,
                soundValue->mode,
                soundValue->fadeInMilliseconds,
                soundValue->fadeOutMilliseconds
            }
        );
    }

    if (microphoneEnabled_ && microphoneDevice_.empty())
    {
        reportError(
            0,
            {},
            Localization::Text(
                "Mikrofon etkin ancak microphone cihaz adı boş.",
                "The microphone is enabled but the microphone device name is empty."
            ),
            "microphone=default"
        );
    }

    if (microphoneEnabled_ &&
        !microphoneToOutput_ &&
        !microphoneToMonitor_)
    {
        reportError(
            0,
            {},
            Localization::Text(
                "Mikrofon etkin olduğunda en az bir yönlendirme açık olmalı.",
                "At least one microphone route must be enabled when the microphone is active."
            ),
            "microphone_to_output=true"
        );
    }

    if (errorCount != 0)
    {
        std::cerr
            << Localization::Text(
                "\nConfig yüklenmedi. Toplam hata: ",
                "\nConfig was not loaded. Total errors: "
            )
            << errorCount
            << Localization::Text(
                "\nYukarıdaki satırları düzeltip tekrar reload yapın.\n",
                "\nFix the lines above and reload again.\n"
            );

        return false;
    }

    // Refresh only untouched v2.2 preview presets. Custom edits and user
    // presets remain byte-for-byte under user control.
    MigrateLegacyBuiltInVoiceEffectSettings(voiceEffectSettings_);

    return true;
}


bool Config::Save(const std::filesystem::path& filePath) const
{
    if (filePath.empty())
    {
        return false;
    }

    std::filesystem::path temporaryPath = filePath;
    temporaryPath += L".tmp";

    std::filesystem::path backupPath = filePath;
    backupPath += L".bak";

    std::error_code error;
    std::filesystem::remove(temporaryPath, error);
    error.clear();

    std::ofstream file(
        temporaryPath,
        std::ios::binary | std::ios::trunc
    );

    if (!file.is_open())
    {
        std::cerr
            << Localization::Text(
                "Config geçici dosyası oluşturulamadı: ",
                "The temporary config file could not be created: "
            )
            << PathToUtf8(temporaryPath)
            << '\n';
        return false;
    }

    file
        << "# SoundBoardFasaFiso configuration\n"
        << "# This file can be edited manually or from the control panel.\n\n"
        << "# DİL VE GÖRÜNÜM / LANGUAGE AND APPEARANCE\n"
        << "language=" << Localization::LanguageCode(language_) << '\n'
        << "theme="
        << (theme_ == AppTheme::Dark ? "dark" : "light") << "\n\n"
        << "# SES ÇIKIŞLARI / AUDIO OUTPUTS\n"
        << "output=" << outputDevice_ << '\n'
        << std::fixed << std::setprecision(2)
        << "output_volume=" << outputVolume_ << '\n'
        << "monitor=" << monitorDevice_ << '\n'
        << "monitor_volume=" << monitorVolume_ << "\n\n"
        << "# MİKROFON MİKSERİ / MICROPHONE MIXER\n"
        << "microphone_enabled="
        << (microphoneEnabled_ ? "true" : "false") << '\n'
        << "microphone=" << microphoneDevice_ << '\n'
        << "microphone_volume=" << microphoneVolume_ << '\n'
        << "microphone_to_output="
        << (microphoneToOutput_ ? "true" : "false") << '\n'
        << "microphone_to_monitor="
        << (microphoneToMonitor_ ? "true" : "false") << "\n\n"
        << "# MİKROFON İŞLEME / MICROPHONE PROCESSING\n"
        << "microphone_processing_enabled="
        << (microphoneProcessingSettings_.enabled ? "true" : "false")
        << '\n'
        << "microphone_processing_preset="
        << MicrophoneProcessingPresetName(
            microphoneProcessingSettings_.preset
        ) << '\n'
        << "microphone_echo_cancellation_enabled="
        << (microphoneProcessingSettings_.echoCancellationEnabled
            ? "true" : "false") << '\n'
        << "microphone_high_pass_enabled="
        << (microphoneProcessingSettings_.highPassEnabled
            ? "true" : "false") << '\n'
        << std::setprecision(3)
        << "microphone_high_pass_hz="
        << microphoneProcessingSettings_.highPassHz << '\n'
        << "microphone_noise_suppression_enabled="
        << (microphoneProcessingSettings_.noiseSuppressionEnabled
            ? "true" : "false") << '\n'
        << "microphone_noise_suppression_level="
        << MicrophoneNoiseSuppressionLevelName(
            microphoneProcessingSettings_.noiseSuppressionLevel
        ) << '\n'
        << "microphone_agc_enabled="
        << (microphoneProcessingSettings_.agcEnabled
            ? "true" : "false") << '\n'
        << "microphone_agc_target_dbfs="
        << microphoneProcessingSettings_.agcTargetDbfs << '\n'
        << "microphone_compressor_enabled="
        << (microphoneProcessingSettings_.compressorEnabled
            ? "true" : "false") << '\n'
        << "microphone_compressor_threshold_db="
        << microphoneProcessingSettings_.compressorThresholdDb << '\n'
        << "microphone_compressor_ratio="
        << microphoneProcessingSettings_.compressorRatio << '\n'
        << "microphone_compressor_attack_ms="
        << microphoneProcessingSettings_.compressorAttackMs << '\n'
        << "microphone_compressor_release_ms="
        << microphoneProcessingSettings_.compressorReleaseMs << '\n'
        << "microphone_compressor_makeup_db="
        << microphoneProcessingSettings_.compressorMakeupDb << '\n'
        << "microphone_limiter_enabled="
        << (microphoneProcessingSettings_.limiterEnabled
            ? "true" : "false") << '\n'
        << "microphone_limiter_ceiling_db="
        << microphoneProcessingSettings_.limiterCeilingDb << "\n\n"
        << "# SES EFEKTLERİ / VOICE EFFECTS\n"
        << "voice_effects_enabled="
        << (voiceEffectSettings_.enabled ? "true" : "false") << '\n'
        << "voice_effects_bypassed="
        << (voiceEffectSettings_.bypassed ? "true" : "false") << '\n'
        << "voice_effects_preset="
        << VoiceEffectPresetName(voiceEffectSettings_.preset) << '\n'
        << std::setprecision(3)
        << "voice_effects_pitch_semitones="
        << voiceEffectSettings_.pitchSemitones << '\n'
        << "voice_effects_formant_semitones="
        << voiceEffectSettings_.formantSemitones << '\n'
        << "voice_effects_character="
        << voiceEffectSettings_.character << '\n'
        << "voice_effects_body="
        << voiceEffectSettings_.body << '\n'
        << "voice_effects_drive="
        << voiceEffectSettings_.drive << '\n'
        << "voice_effects_dry_wet="
        << voiceEffectSettings_.dryWet << '\n'
        << "voice_effects_output_gain_db="
        << voiceEffectSettings_.outputGainDb << '\n';

    for (const VoiceEffectUserPreset& preset : voiceEffectUserPresets_)
    {
        file
            << "voice_effects_user_preset=" << preset.name
            << "|preset=" << VoiceEffectPresetName(preset.settings.preset)
            << "|pitch=" << preset.settings.pitchSemitones
            << "|formant=" << preset.settings.formantSemitones
            << "|character=" << preset.settings.character
            << "|body=" << preset.settings.body
            << "|drive=" << preset.settings.drive
            << "|dry_wet=" << preset.settings.dryWet
            << "|output_gain_db=" << preset.settings.outputGainDb
            << '\n';
    }

    file
        << '\n'
        << std::setprecision(2)
        << "# AUDIO GECİKME AYARLARI / AUDIO LATENCY SETTINGS\n"
        << "audio_sample_rate=" << audioSampleRate_ << '\n'
        << "audio_buffer_ms=" << audioBufferMilliseconds_ << "\n\n"
        << "# UYGULAMA / APPLICATION\n"
        << "start_with_windows="
        << (startWithWindows_ ? "true" : "false") << '\n'
        << "show_console_on_start="
        << (showConsoleOnStart_ ? "true" : "false") << '\n'
        << "check_updates_on_start="
        << (checkUpdatesOnStart_ ? "true" : "false") << "\n\n"
        << "# KONTROLLER / CONTROLS\n"
        << "stop=" << stopKeyName_ << '\n'
        << "output_mute=" << outputMuteKeyName_ << '\n'
        << "monitor_mute=" << monitorMuteKeyName_ << '\n'
        << "voice_effects_previous_preset="
        << voiceEffectsPreviousPresetKeyName_ << '\n'
        << "voice_effects_next_preset="
        << voiceEffectsNextPresetKeyName_ << '\n'
        << "voice_effects_bypass=" << voiceEffectsBypassKeyName_ << '\n'
        << "reload=" << reloadKeyName_ << '\n'
        << "exit=" << exitKeyName_ << "\n\n"
        << "# TUŞ ATAMALARI / SOUND BINDINGS\n";

    for (const SoundBinding& binding : bindings_)
    {
        file
            << binding.keyName
            << '='
            << PathToUtf8(binding.soundFile)
            << "|volume="
            << std::fixed
            << std::setprecision(2)
            << binding.volume
            << "|mode="
            << PlaybackModeName(binding.mode);

        if (binding.fadeInMilliseconds > 0)
        {
            file << "|fade_in_ms=" << binding.fadeInMilliseconds;
        }

        if (binding.fadeOutMilliseconds > 0)
        {
            file << "|fade_out_ms=" << binding.fadeOutMilliseconds;
        }

        file << '\n';
    }

    file.flush();

    if (!file.good())
    {
        file.close();
        std::filesystem::remove(temporaryPath, error);

        std::cerr
            << Localization::Text(
                "Config geçici dosyasına yazılamadı: ",
                "The temporary config file could not be written: "
            )
            << PathToUtf8(temporaryPath)
            << '\n';
        return false;
    }

    file.close();

    DWORD systemError = ERROR_SUCCESS;

    if (!FlushFileContents(temporaryPath, systemError))
    {
        std::filesystem::remove(temporaryPath, error);

        std::cerr
            << Localization::Text(
                "Config geçici dosyası diske yazılamadı. Hata kodu: ",
                "The temporary config file could not be flushed to disk. Error code: "
            )
            << systemError
            << '\n';
        return false;
    }

    error.clear();
    const bool destinationExists =
        std::filesystem::exists(filePath, error) && !error;

    if (!CommitTemporaryFile(
            temporaryPath,
            filePath,
            backupPath,
            destinationExists,
            systemError
        ))
    {
        error.clear();
        std::filesystem::remove(temporaryPath, error);

        std::cerr
            << Localization::Text(
                "Yeni config atomik olarak etkinleştirilemedi. Önceki dosya korundu. Hata kodu: ",
                "The new config could not be activated atomically. The previous file was preserved. Error code: "
            )
            << systemError
            << '\n';
        return false;
    }

    return true;
}

void Config::SetLanguage(const Language language)
{
    language_ = language;
}

void Config::SetTheme(const AppTheme theme)
{
    theme_ = theme;
}

void Config::SetOutputDevice(std::string deviceName)
{
    outputDevice_ = std::move(deviceName);
}

bool Config::SetOutputVolume(const float volume)
{
    if (volume < 0.0f || volume > 1.0f)
    {
        return false;
    }

    outputVolume_ = volume;
    return true;
}

void Config::SetMonitorDevice(std::string deviceName)
{
    monitorDevice_ = std::move(deviceName);
}

bool Config::SetMonitorVolume(const float volume)
{
    if (volume < 0.0f || volume > 1.0f)
    {
        return false;
    }

    monitorVolume_ = volume;
    return true;
}

void Config::SetMicrophoneEnabled(const bool enabled)
{
    microphoneEnabled_ = enabled;
}

void Config::SetMicrophoneDevice(std::string deviceName)
{
    microphoneDevice_ = std::move(deviceName);
}

bool Config::SetMicrophoneVolume(const float volume)
{
    if (volume < 0.0f || volume > 1.0f)
    {
        return false;
    }

    microphoneVolume_ = volume;
    return true;
}

void Config::SetMicrophoneToOutput(const bool enabled)
{
    microphoneToOutput_ = enabled;
}

void Config::SetMicrophoneToMonitor(const bool enabled)
{
    microphoneToMonitor_ = enabled;
}

bool Config::SetMicrophoneProcessingSettings(
    MicrophoneProcessingSettings settings
)
{
    if (!IsValidMicrophoneProcessingSettings(settings))
    {
        return false;
    }

    microphoneProcessingSettings_ = std::move(settings);
    return true;
}

bool Config::SetVoiceEffectSettings(VoiceEffectSettings settings)
{
    if (!IsValidVoiceEffectSettings(settings))
    {
        return false;
    }

    voiceEffectSettings_ = std::move(settings);
    return true;
}

bool Config::SetVoiceEffectUserPresets(
    std::vector<VoiceEffectUserPreset> presets
)
{
    if (presets.size() > VoiceEffectLimits::MaximumUserPresetCount)
    {
        return false;
    }

    for (std::size_t index = 0; index < presets.size(); ++index)
    {
        if (!IsValidVoiceEffectUserPreset(presets[index]))
        {
            return false;
        }

        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (VoiceEffectUserPresetNamesEqual(
                    presets[previous].name,
                    presets[index].name
                ))
            {
                return false;
            }
        }
    }

    voiceEffectUserPresets_ = std::move(presets);
    return true;
}

bool Config::AddOrUpdateVoiceEffectUserPreset(
    VoiceEffectUserPreset preset
)
{
    if (!IsValidVoiceEffectUserPreset(preset))
    {
        return false;
    }

    const auto iterator = std::find_if(
        voiceEffectUserPresets_.begin(),
        voiceEffectUserPresets_.end(),
        [&preset](const VoiceEffectUserPreset& existing)
        {
            return VoiceEffectUserPresetNamesEqual(
                existing.name,
                preset.name
            );
        }
    );

    if (iterator != voiceEffectUserPresets_.end())
    {
        *iterator = std::move(preset);
        return true;
    }

    if (voiceEffectUserPresets_.size() >=
        VoiceEffectLimits::MaximumUserPresetCount)
    {
        return false;
    }

    voiceEffectUserPresets_.push_back(std::move(preset));
    return true;
}

bool Config::RemoveVoiceEffectUserPreset(const std::string_view name)
{
    const auto iterator = std::find_if(
        voiceEffectUserPresets_.begin(),
        voiceEffectUserPresets_.end(),
        [name](const VoiceEffectUserPreset& preset)
        {
            return VoiceEffectUserPresetNamesEqual(preset.name, name);
        }
    );

    if (iterator == voiceEffectUserPresets_.end())
    {
        return false;
    }

    voiceEffectUserPresets_.erase(iterator);
    return true;
}

bool Config::SetAudioSampleRate(const unsigned int sampleRate)
{
    if (sampleRate != 0 &&
        (sampleRate < 8000 || sampleRate > 192000))
    {
        return false;
    }

    audioSampleRate_ = sampleRate;
    return true;
}

bool Config::SetAudioBufferMilliseconds(
    const unsigned int bufferMilliseconds
)
{
    if (bufferMilliseconds != 0 &&
        (bufferMilliseconds < 2 || bufferMilliseconds > 100))
    {
        return false;
    }

    audioBufferMilliseconds_ = bufferMilliseconds;
    return true;
}

void Config::SetStartWithWindows(const bool enabled)
{
    startWithWindows_ = enabled;
}

void Config::SetShowConsoleOnStart(const bool enabled)
{
    showConsoleOnStart_ = enabled;
}

void Config::SetCheckUpdatesOnStart(const bool enabled)
{
    checkUpdatesOnStart_ = enabled;
}

bool Config::SetControlHotkeys(
    std::string stopKeyName,
    std::string outputMuteKeyName,
    std::string monitorMuteKeyName,
    std::string voiceEffectsPreviousPresetKeyName,
    std::string voiceEffectsNextPresetKeyName,
    std::string voiceEffectsBypassKeyName,
    std::string reloadKeyName,
    std::string exitKeyName
)
{
    std::string errorReason;

    const auto stop = ParseHotkey(stopKeyName, errorReason);
    const auto outputMute = ParseHotkey(outputMuteKeyName, errorReason);
    const auto monitorMute = ParseHotkey(monitorMuteKeyName, errorReason);
    const auto voiceEffectsPreviousPreset = ParseHotkey(
        voiceEffectsPreviousPresetKeyName,
        errorReason
    );
    const auto voiceEffectsNextPreset = ParseHotkey(
        voiceEffectsNextPresetKeyName,
        errorReason
    );
    const auto voiceEffectsBypass = ParseHotkey(
        voiceEffectsBypassKeyName,
        errorReason
    );
    const auto reload = ParseHotkey(reloadKeyName, errorReason);
    const auto exit = ParseHotkey(exitKeyName, errorReason);

    if (!stop.has_value() || !outputMute.has_value() ||
        !monitorMute.has_value() ||
        !voiceEffectsPreviousPreset.has_value() ||
        !voiceEffectsNextPreset.has_value() ||
        !voiceEffectsBypass.has_value() ||
        !reload.has_value() || !exit.has_value())
    {
        return false;
    }

    const ParsedHotkey* hotkeys[] = {
        &*stop,
        &*outputMute,
        &*monitorMute,
        &*voiceEffectsPreviousPreset,
        &*voiceEffectsNextPreset,
        &*voiceEffectsBypass,
        &*reload,
        &*exit
    };

    std::unordered_set<std::uint64_t> identities;

    for (const ParsedHotkey* hotkey : hotkeys)
    {
        if (!identities.insert(
                MakeHotkeyIdentity(
                    hotkey->modifiers,
                    hotkey->virtualKey
                )
            ).second)
        {
            return false;
        }
    }

    stopKeyName_ = stop->canonicalName;
    stopModifiers_ = stop->modifiers;
    stopVirtualKey_ = stop->virtualKey;

    outputMuteKeyName_ = outputMute->canonicalName;
    outputMuteModifiers_ = outputMute->modifiers;
    outputMuteVirtualKey_ = outputMute->virtualKey;

    monitorMuteKeyName_ = monitorMute->canonicalName;
    monitorMuteModifiers_ = monitorMute->modifiers;
    monitorMuteVirtualKey_ = monitorMute->virtualKey;

    voiceEffectsPreviousPresetKeyName_ =
        voiceEffectsPreviousPreset->canonicalName;
    voiceEffectsPreviousPresetModifiers_ =
        voiceEffectsPreviousPreset->modifiers;
    voiceEffectsPreviousPresetVirtualKey_ =
        voiceEffectsPreviousPreset->virtualKey;

    voiceEffectsNextPresetKeyName_ =
        voiceEffectsNextPreset->canonicalName;
    voiceEffectsNextPresetModifiers_ =
        voiceEffectsNextPreset->modifiers;
    voiceEffectsNextPresetVirtualKey_ =
        voiceEffectsNextPreset->virtualKey;

    voiceEffectsBypassKeyName_ = voiceEffectsBypass->canonicalName;
    voiceEffectsBypassModifiers_ = voiceEffectsBypass->modifiers;
    voiceEffectsBypassVirtualKey_ = voiceEffectsBypass->virtualKey;

    reloadKeyName_ = reload->canonicalName;
    reloadModifiers_ = reload->modifiers;
    reloadVirtualKey_ = reload->virtualKey;

    exitKeyName_ = exit->canonicalName;
    exitModifiers_ = exit->modifiers;
    exitVirtualKey_ = exit->virtualKey;

    return true;
}

bool Config::SetBindings(std::vector<SoundBinding> bindings)
{
    std::unordered_set<std::uint64_t> usedHotkeys{
        MakeHotkeyIdentity(stopModifiers_, stopVirtualKey_),
        MakeHotkeyIdentity(outputMuteModifiers_, outputMuteVirtualKey_),
        MakeHotkeyIdentity(
            monitorMuteModifiers_,
            monitorMuteVirtualKey_
        ),
        MakeHotkeyIdentity(
            voiceEffectsPreviousPresetModifiers_,
            voiceEffectsPreviousPresetVirtualKey_
        ),
        MakeHotkeyIdentity(
            voiceEffectsNextPresetModifiers_,
            voiceEffectsNextPresetVirtualKey_
        ),
        MakeHotkeyIdentity(
            voiceEffectsBypassModifiers_,
            voiceEffectsBypassVirtualKey_
        ),
        MakeHotkeyIdentity(reloadModifiers_, reloadVirtualKey_),
        MakeHotkeyIdentity(exitModifiers_, exitVirtualKey_)
    };

    for (SoundBinding& binding : bindings)
    {
        if (binding.volume < 0.0f || binding.volume > 1.0f ||
            binding.fadeInMilliseconds > 10000 ||
            binding.fadeOutMilliseconds > 10000 ||
            binding.soundFile.empty() ||
            binding.soundFile.has_root_path() ||
            !SoundFileFormat::IsSupported(binding.soundFile))
        {
            return false;
        }

        for (const auto& component : binding.soundFile)
        {
            if (component == "..")
            {
                return false;
            }
        }

        std::string errorReason;
        const auto hotkey = ParseHotkey(binding.keyName, errorReason);

        if (!hotkey.has_value())
        {
            return false;
        }

        const std::uint64_t identity = MakeHotkeyIdentity(
            hotkey->modifiers,
            hotkey->virtualKey
        );

        if (!usedHotkeys.insert(identity).second)
        {
            return false;
        }

        binding.keyName = hotkey->canonicalName;
        binding.modifiers = hotkey->modifiers;
        binding.virtualKey = hotkey->virtualKey;
        binding.soundFile = binding.soundFile.lexically_normal();
    }

    bindings_ = std::move(bindings);
    return true;
}

Language Config::GetLanguage() const
{
    return language_;
}

AppTheme Config::GetTheme() const
{
    return theme_;
}

const std::string& Config::GetOutputDevice() const
{
    return outputDevice_;
}

float Config::GetOutputVolume() const
{
    return outputVolume_;
}

const std::string& Config::GetMonitorDevice() const
{
    return monitorDevice_;
}

float Config::GetMonitorVolume() const
{
    return monitorVolume_;
}

bool Config::GetMicrophoneEnabled() const
{
    return microphoneEnabled_;
}

const std::string& Config::GetMicrophoneDevice() const
{
    return microphoneDevice_;
}

float Config::GetMicrophoneVolume() const
{
    return microphoneVolume_;
}

bool Config::GetMicrophoneToOutput() const
{
    return microphoneToOutput_;
}

bool Config::GetMicrophoneToMonitor() const
{
    return microphoneToMonitor_;
}

const MicrophoneProcessingSettings&
Config::GetMicrophoneProcessingSettings() const
{
    return microphoneProcessingSettings_;
}

const VoiceEffectSettings& Config::GetVoiceEffectSettings() const
{
    return voiceEffectSettings_;
}

const std::vector<VoiceEffectUserPreset>&
    Config::GetVoiceEffectUserPresets() const
{
    return voiceEffectUserPresets_;
}

unsigned int Config::GetAudioSampleRate() const
{
    return audioSampleRate_;
}

unsigned int Config::GetAudioBufferMilliseconds() const
{
    return audioBufferMilliseconds_;
}

bool Config::GetStartWithWindows() const
{
    return startWithWindows_;
}

bool Config::GetCheckUpdatesOnStart() const
{
    return checkUpdatesOnStart_;
}

const std::string& Config::GetStopKeyName() const
{
    return stopKeyName_;
}

unsigned int Config::GetStopModifiers() const
{
    return stopModifiers_;
}

unsigned int Config::GetStopVirtualKey() const
{
    return stopVirtualKey_;
}

const std::string& Config::GetOutputMuteKeyName() const
{
    return outputMuteKeyName_;
}

unsigned int Config::GetOutputMuteModifiers() const
{
    return outputMuteModifiers_;
}

unsigned int Config::GetOutputMuteVirtualKey() const
{
    return outputMuteVirtualKey_;
}

const std::string& Config::GetMonitorMuteKeyName() const
{
    return monitorMuteKeyName_;
}

unsigned int Config::GetMonitorMuteModifiers() const
{
    return monitorMuteModifiers_;
}

unsigned int Config::GetMonitorMuteVirtualKey() const
{
    return monitorMuteVirtualKey_;
}

const std::string& Config::GetVoiceEffectsPreviousPresetKeyName() const
{
    return voiceEffectsPreviousPresetKeyName_;
}

unsigned int Config::GetVoiceEffectsPreviousPresetModifiers() const
{
    return voiceEffectsPreviousPresetModifiers_;
}

unsigned int Config::GetVoiceEffectsPreviousPresetVirtualKey() const
{
    return voiceEffectsPreviousPresetVirtualKey_;
}

const std::string& Config::GetVoiceEffectsNextPresetKeyName() const
{
    return voiceEffectsNextPresetKeyName_;
}

unsigned int Config::GetVoiceEffectsNextPresetModifiers() const
{
    return voiceEffectsNextPresetModifiers_;
}

unsigned int Config::GetVoiceEffectsNextPresetVirtualKey() const
{
    return voiceEffectsNextPresetVirtualKey_;
}

const std::string& Config::GetVoiceEffectsBypassKeyName() const
{
    return voiceEffectsBypassKeyName_;
}

unsigned int Config::GetVoiceEffectsBypassModifiers() const
{
    return voiceEffectsBypassModifiers_;
}

unsigned int Config::GetVoiceEffectsBypassVirtualKey() const
{
    return voiceEffectsBypassVirtualKey_;
}

const std::string& Config::GetReloadKeyName() const
{
    return reloadKeyName_;
}

unsigned int Config::GetReloadModifiers() const
{
    return reloadModifiers_;
}

unsigned int Config::GetReloadVirtualKey() const
{
    return reloadVirtualKey_;
}

const std::string& Config::GetExitKeyName() const
{
    return exitKeyName_;
}

unsigned int Config::GetExitModifiers() const
{
    return exitModifiers_;
}

unsigned int Config::GetExitVirtualKey() const
{
    return exitVirtualKey_;
}

const std::vector<SoundBinding>& Config::GetBindings() const
{
    return bindings_;
}
