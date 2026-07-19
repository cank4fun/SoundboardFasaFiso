#include "config/Config.hpp"
#include "sound/SoundFileFormat.hpp"
#include "platform/Utf8Path.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
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
    };

    struct SettingSource
    {
        std::size_t lineNumber = 0;
        std::string originalLine = "<varsayılan değer>";
    };

    struct ControlHotkey
    {
        std::string_view settingName;
        std::string_view keyName;
        std::uint64_t identity = 0;
        const SettingSource* source = nullptr;
        std::string_view example;
    };

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
        std::cerr << "\n[CONFIG HATASI]\n";

        if (lineNumber != 0)
        {
            std::cerr
                << "Satır "
                << lineNumber
                << ": "
                << originalLine
                << '\n';
        }
        else
        {
            std::cerr << "Satır: <dosya geneli>\n";
        }

        std::cerr
            << "Neden: "
            << reason
            << '\n';

        if (!example.empty())
        {
            std::cerr
                << "Doğru örnek: "
                << example
                << '\n';
        }
    }

    std::string_view SettingExample(
        const std::string_view settingName
    )
    {
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

        if (settingName == "AUDIO_SAMPLE_RATE")
        {
            return "audio_sample_rate=48000";
        }

        if (settingName == "AUDIO_BUFFER_MS")
        {
            return "audio_buffer_ms=5";
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

    std::optional<float> ParseVolume(std::string text)
    {
        std::replace(
            text.begin(),
            text.end(),
            ',',
            '.'
        );

        float volume = 0.0f;

        const char* begin = text.data();
        const char* end = text.data() + text.size();

        const auto [pointer, error] =
            std::from_chars(
                begin,
                end,
                volume
            );

        if (error != std::errc{} || pointer != end)
        {
            return std::nullopt;
        }

        if (volume < 0.0f || volume > 1.0f)
        {
            return std::nullopt;
        }

        return volume;
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
                errorReason =
                    "'|' işaretinin yanında boş bir ses ayarı var.";
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
                        "Ses ayarı 'isim=değer' biçiminde olmalı. "
                        "Hatalı bölüm: " + token;
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
                        "Ses ayarının adı veya değeri boş. "
                        "Hatalı bölüm: " + token;
                    correctExample =
                        "F1=example.wav|volume=0.80|mode=restart";
                    return std::nullopt;
                }

                if (optionName == "VOLUME")
                {
                    if (volumeWasSet)
                    {
                        errorReason =
                            "volume ayarı aynı satırda birden fazla yazılmış.";
                        correctExample =
                            "F1=example.wav|volume=0.80|mode=restart";
                        return std::nullopt;
                    }

                    const auto volume =
                        ParseVolume(optionValue);

                    if (!volume.has_value())
                    {
                        errorReason =
                            "volume değeri 0.00 ile 1.00 arasında "
                            "bir sayı olmalı. Girilen değer: " +
                            optionValue;
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
                        errorReason =
                            "mode ayarı aynı satırda birden fazla yazılmış.";
                        correctExample =
                            "F1=example.wav|volume=0.80|mode=restart";
                        return std::nullopt;
                    }

                    const auto mode =
                        ParsePlaybackMode(optionValue);

                    if (!mode.has_value())
                    {
                        errorReason =
                            "Desteklenmeyen mode değeri: " + optionValue +
                            ". Desteklenenler: restart, overlap, toggle, loop.";
                        correctExample =
                            "F1=example.wav|volume=0.80|mode=restart";
                        return std::nullopt;
                    }

                    parsedValue.mode = *mode;
                    modeWasSet = true;
                }
                else
                {
                    errorReason =
                        "Desteklenmeyen ses ayarı: " + optionName +
                        ". Desteklenen ayarlar: volume, mode.";
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
            errorReason = "Ses dosyasının adı boş bırakılamaz.";
            correctExample = "F1=example.wav";
            return std::nullopt;
        }

        if (parsedValue.soundFile.has_root_path())
        {
            errorReason =
                "Ses dosyası yolu sounds klasörüne göre göreli olmalı.";
            correctExample =
                "F1=effects/example.wav|volume=0.80|mode=restart";
            return std::nullopt;
        }

        for (const auto& component : parsedValue.soundFile)
        {
            if (component == "..")
            {
                errorReason =
                    "Ses dosyası yolu sounds klasörünün dışına çıkamaz.";
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
                "Desteklenmeyen ses dosyası uzantısı: " +
                (extension.empty()
                    ? std::string{"<uzantı yok>"}
                    : extension) +
                ". Desteklenen uzantılar: " +
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
            errorReason = "Hotkey boş bırakılamaz.";
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
                errorReason =
                    "'+' işaretinin iki yanında bir tuş adı olmalı.";
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
                        "Aynı modifier birden fazla yazılmış: " + token;
                    return std::nullopt;
                }

                modifiers |= modifier;
            }
            else
            {
                if (baseKey.has_value())
                {
                    errorReason =
                        "Bir hotkey yalnızca bir ana tuş içerebilir. "
                        "F1+F2 gibi iki ana tuş birlikte kullanılamaz.";
                    return std::nullopt;
                }

                baseKey = ParseBaseKey(token);

                if (!baseKey.has_value())
                {
                    errorReason =
                        "Desteklenmeyen tuş adı: " + token +
                        ". F1-F24, A-Z, 0-9, NUMPAD0-NUMPAD9 ve "
                        "desteklenen özel tuşlardan birini kullanın.";
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
            errorReason =
                "Hotkey içinde ana tuş eksik. CTRL veya SHIFT tek başına "
                "hotkey olamaz.";
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
        return settingName == "OUTPUT" ||
            settingName == "OUTPUT_VOLUME" ||
            settingName == "MONITOR" ||
            settingName == "MONITOR_VOLUME" ||
            settingName == "AUDIO_SAMPLE_RATE" ||
            settingName == "AUDIO_BUFFER_MS" ||
            settingName == "STOP" ||
            settingName == "OUTPUT_MUTE" ||
            settingName == "MONITOR_MUTE" ||
            settingName == "RELOAD" ||
            settingName == "EXIT";
    }
}

bool Config::Load(const std::filesystem::path& filePath)
{
    outputDevice_ = "default";
    outputVolume_ = 1.0f;

    monitorDevice_ = "default";
    monitorVolume_ = 0.30f;

    audioSampleRate_ = 48000;
    audioBufferMilliseconds_ = 5;

    stopKeyName_ = "F11";
    stopModifiers_ = 0;
    stopVirtualKey_ = static_cast<unsigned int>(VK_F11);

    outputMuteKeyName_ = "CTRL+SHIFT+F9";
    outputMuteModifiers_ = MOD_CONTROL | MOD_SHIFT;
    outputMuteVirtualKey_ = static_cast<unsigned int>(VK_F9);

    monitorMuteKeyName_ = "CTRL+SHIFT+F10";
    monitorMuteModifiers_ = MOD_CONTROL | MOD_SHIFT;
    monitorMuteVirtualKey_ = static_cast<unsigned int>(VK_F10);

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
            "Config dosyası açılamadı: " + PathToUtf8(filePath),
            "config.txt dosyasını EXE'nin yanına koyun."
        );

        return false;
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

    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(file, line))
    {
        ++lineNumber;

        const std::string originalLine = Trim(line);

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
                "Satırda '=' işareti yok. Her ayar 'sol=sağ' biçiminde yazılmalı.",
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
                "'=' işaretinin sol tarafı boş. Ayar veya hotkey adı yazılmalı.",
                "F1=example.wav"
            );
            continue;
        }

        if (rightSide.empty())
        {
            reportError(
                lineNumber,
                originalLine,
                "'=' işaretinin sağ tarafı boş. Bir değer veya ses dosyası yazılmalı.",
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
    SettingSource reloadSource;
    SettingSource exitSource;

    std::unordered_set<std::string> seenSettings;

    // Önce genel ayarları oku. Böylece config sırası önemli olmaz.
    for (const ParsedLine& parsedLine : parsedLines)
    {
        const std::string settingName =
            ToUpper(parsedLine.leftSide);

        if (!IsKnownSetting(settingName))
        {
            continue;
        }

        const auto [iterator, inserted] =
            seenSettings.insert(settingName);

        static_cast<void>(iterator);

        if (!inserted)
        {
            reportError(
                parsedLine.lineNumber,
                parsedLine.originalLine,
                "Bu ayar config içinde birden fazla yazılmış: " + settingName,
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
                    "output_volume değeri 0.00 ile 1.00 arasında bir sayı olmalı. "
                    "Girilen değer: " + parsedLine.rightSide,
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
                    "monitor_volume değeri 0.00 ile 1.00 arasında bir sayı olmalı. "
                    "Girilen değer: " + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            monitorVolume_ = *volume;
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
                    "audio_sample_rate değeri 0 veya 8000 ile 192000 arasında "
                    "tam sayı olmalı. 0 cihazın doğal örnekleme hızını kullanır. "
                    "Girilen değer: " + parsedLine.rightSide,
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
                    "audio_buffer_ms değeri 0 veya 2 ile 100 arasında tam sayı "
                    "olmalı. 0 miniaudio/Windows varsayılanını kullanır. "
                    "Girilen değer: " + parsedLine.rightSide,
                    SettingExample(settingName)
                );
                continue;
            }

            audioBufferMilliseconds_ = *bufferMilliseconds;
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
            std::string{"Kontrol hotkey'i başka bir kontrolle çakışıyor: "} +
                std::string{errorControl.settingName} + " ve " +
                std::string{otherControl.settingName} + " aynı tuşu kullanıyor (" +
                std::string{errorControl.keyName} + ").",
            errorControl.example
        );
    }

    // Kontrol hotkey'lerini ses atamalarından rezerve et.
    std::unordered_set<std::uint64_t> usedHotkeys;

    for (const ControlHotkey& control : controlHotkeys)
    {
        usedHotkeys.insert(control.identity);
    }

    // Sonra ses hotkey'lerini oku.
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
                "Sol taraf ne desteklenen bir ayar adı ne de geçerli bir hotkey. " +
                    hotkeyError,
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
                "Bu hotkey daha önce kullanılmış veya kontrol tuşlarından biri için "
                "rezerve edilmiş: " + hotkey->canonicalName,
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
                soundValue->mode
            }
        );
    }

    if (errorCount != 0)
    {
        std::cerr
            << "\nConfig yüklenmedi. Toplam hata: "
            << errorCount
            << "\nYukarıdaki satırları düzeltip tekrar reload yapın.\n";

        return false;
    }

    if (bindings_.empty())
    {
        PrintConfigError(
            0,
            {},
            "Config dosyasında hiç geçerli ses ataması yok.",
            "F1=example.wav|volume=1.00|mode=restart"
        );

        return false;
    }

    return true;
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

unsigned int Config::GetAudioSampleRate() const
{
    return audioSampleRate_;
}

unsigned int Config::GetAudioBufferMilliseconds() const
{
    return audioBufferMilliseconds_;
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
