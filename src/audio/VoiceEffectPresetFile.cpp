#include "audio/VoiceEffectPresetFile.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>

namespace
{
    constexpr std::size_t MaximumPresetFileBytes = 16U * 1024U;
    constexpr std::string_view FormatHeader = "SBFF_VOICE_PRESET=1";
    constexpr std::uint64_t FnvOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t FnvPrime = 1099511628211ULL;

    std::uint64_t Fnv1a64(const std::string_view value) noexcept
    {
        std::uint64_t hash = FnvOffsetBasis;
        for (const unsigned char byte : value)
        {
            hash ^= byte;
            hash *= FnvPrime;
        }
        return hash;
    }

    bool IsValidUtf8(const std::string_view value) noexcept
    {
        std::size_t index = 0;
        while (index < value.size())
        {
            const unsigned char first = static_cast<unsigned char>(value[index]);
            if (first <= 0x7FU)
            {
                ++index;
                continue;
            }

            std::size_t length = 0;
            std::uint32_t codePoint = 0;
            if ((first & 0xE0U) == 0xC0U)
            {
                length = 2;
                codePoint = first & 0x1FU;
            }
            else if ((first & 0xF0U) == 0xE0U)
            {
                length = 3;
                codePoint = first & 0x0FU;
            }
            else if ((first & 0xF8U) == 0xF0U)
            {
                length = 4;
                codePoint = first & 0x07U;
            }
            else
            {
                return false;
            }

            if (index + length > value.size())
            {
                return false;
            }

            for (std::size_t offset = 1; offset < length; ++offset)
            {
                const unsigned char continuation =
                    static_cast<unsigned char>(value[index + offset]);
                if ((continuation & 0xC0U) != 0x80U)
                {
                    return false;
                }
                codePoint = (codePoint << 6U) | (continuation & 0x3FU);
            }

            const std::uint32_t minimum = length == 2 ? 0x80U
                : length == 3 ? 0x800U
                : 0x10000U;
            if (codePoint < minimum || codePoint > 0x10FFFFU ||
                (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
            {
                return false;
            }
            index += length;
        }
        return true;
    }

    std::string FloatText(const float value)
    {
        std::array<char, 64> buffer{};
        const auto [end, error] = std::to_chars(
            buffer.data(),
            buffer.data() + buffer.size(),
            value,
            std::chars_format::general,
            std::numeric_limits<float>::max_digits10
        );
        if (error != std::errc{})
        {
            return "0";
        }
        return std::string(buffer.data(), end);
    }

    void AppendField(
        std::string& output,
        const std::string_view name,
        const std::string_view value
    )
    {
        output.append(name);
        output.push_back('=');
        output.append(value);
        output.push_back('\n');
    }

    std::string SerializePresetPayload(const VoiceEffectUserPreset& preset)
    {
        const VoiceEffectSettings& settings = preset.settings;
        std::string output;
        output.reserve(1024);
        output.append(FormatHeader);
        output.push_back('\n');
        AppendField(output, "name", preset.name);
        AppendField(output, "preset", VoiceEffectPresetName(settings.preset));
        AppendField(output, "pitch_semitones", FloatText(settings.pitchSemitones));
        AppendField(output, "formant_semitones", FloatText(settings.formantSemitones));
        AppendField(output, "character", FloatText(settings.character));
        AppendField(output, "body", FloatText(settings.body));
        AppendField(output, "drive", FloatText(settings.drive));
        AppendField(output, "dry_wet", FloatText(settings.dryWet));
        AppendField(output, "output_gain_db", FloatText(settings.outputGainDb));
        AppendField(output, "eq_enabled", settings.parametricEqEnabled ? "1" : "0");
        AppendField(output, "eq_low_gain_db", FloatText(settings.eqLowGainDb));
        AppendField(output, "eq_low_frequency_hz", FloatText(settings.eqLowFrequencyHz));
        AppendField(output, "eq_mid_gain_db", FloatText(settings.eqMidGainDb));
        AppendField(output, "eq_mid_frequency_hz", FloatText(settings.eqMidFrequencyHz));
        AppendField(output, "eq_mid_q", FloatText(settings.eqMidQ));
        AppendField(output, "eq_high_gain_db", FloatText(settings.eqHighGainDb));
        AppendField(output, "eq_high_frequency_hz", FloatText(settings.eqHighFrequencyHz));
        AppendField(output, "de_esser_enabled", settings.deEsserEnabled ? "1" : "0");
        AppendField(output, "de_esser_amount", FloatText(settings.deEsserAmount));
        AppendField(output, "gate_enabled", settings.gateEnabled ? "1" : "0");
        AppendField(output, "gate_amount", FloatText(settings.gateAmount));
        AppendField(output, "compressor_enabled", settings.compressorEnabled ? "1" : "0");
        AppendField(output, "compressor_amount", FloatText(settings.compressorAmount));
        AppendField(output, "rack_order", SerializeVoiceEffectRackOrder(settings.rackOrder));
        return output;
    }

    std::string Hex64(const std::uint64_t value)
    {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0') << std::setw(16) << value;
        return stream.str();
    }

    std::optional<std::uint64_t> ParseHex64(const std::string_view value)
    {
        if (value.size() != 16U)
        {
            return std::nullopt;
        }
        std::uint64_t result = 0;
        const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), result, 16
        );
        if (error != std::errc{} || end != value.data() + value.size())
        {
            return std::nullopt;
        }
        return result;
    }

    std::optional<float> ParseFloat(const std::string_view value)
    {
        float result = 0.0f;
        const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), result,
            std::chars_format::general
        );
        if (error != std::errc{} || end != value.data() + value.size() ||
            !std::isfinite(result))
        {
            return std::nullopt;
        }
        return result;
    }

    std::optional<bool> ParseBoolean(const std::string_view value)
    {
        if (value == "0")
        {
            return false;
        }
        if (value == "1")
        {
            return true;
        }
        return std::nullopt;
    }

    std::string LowerAscii(std::string value)
    {
        for (char& character : value)
        {
            if (character >= 'A' && character <= 'Z')
            {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        return value;
    }

    std::string SafeFilePrefix(const std::string_view name)
    {
        std::string result;
        result.reserve(24);
        for (const unsigned char byte : name)
        {
            if ((byte >= 'A' && byte <= 'Z') ||
                (byte >= 'a' && byte <= 'z') ||
                (byte >= '0' && byte <= '9') || byte == '-' || byte == '_')
            {
                result.push_back(static_cast<char>(byte));
            }
            else if ((byte == ' ' || byte == '.') &&
                !result.empty() && result.back() != '_')
            {
                result.push_back('_');
            }

            if (result.size() >= 24U)
            {
                break;
            }
        }

        while (!result.empty() && result.back() == '_')
        {
            result.pop_back();
        }
        return result.empty() ? "voice-preset" : result;
    }

    bool ReplaceFileTransactionally(
        const std::filesystem::path& temporaryPath,
        const std::filesystem::path& destinationPath,
        std::string& errorMessage
    )
    {
        std::error_code error;
        std::filesystem::path backupPath = destinationPath;
        backupPath += L".bak";
        std::filesystem::remove(backupPath, error);
        error.clear();

        const bool destinationExists =
            std::filesystem::exists(destinationPath, error) && !error;
        if (error)
        {
            errorMessage = "The destination file could not be inspected.";
            return false;
        }

        if (destinationExists)
        {
            if (!std::filesystem::is_regular_file(destinationPath, error) ||
                error)
            {
                errorMessage = "The destination is not a regular file.";
                return false;
            }
            std::filesystem::rename(destinationPath, backupPath, error);
            if (error)
            {
                errorMessage = "The previous preset file could not be backed up.";
                return false;
            }
        }

        error.clear();
        std::filesystem::rename(temporaryPath, destinationPath, error);
        if (error)
        {
            if (destinationExists)
            {
                std::error_code restoreError;
                std::filesystem::rename(backupPath, destinationPath, restoreError);
            }
            errorMessage = "The new preset file could not replace the destination.";
            return false;
        }

        if (destinationExists)
        {
            error.clear();
            std::filesystem::remove(backupPath, error);
        }
        return true;
    }

    bool ExtensionMatches(const std::filesystem::path& path)
    {
        std::wstring extension = path.extension().wstring();
        std::transform(
            extension.begin(), extension.end(), extension.begin(),
            [](const wchar_t character)
            {
                return character >= L'A' && character <= L'Z'
                    ? static_cast<wchar_t>(character - L'A' + L'a')
                    : character;
            }
        );
        return extension == VoiceEffectPresetFileExtension;
    }
}

std::filesystem::path BuildVoiceEffectPresetFilePath(
    const std::filesystem::path& folder,
    const VoiceEffectUserPreset& preset
)
{
    const std::uint64_t nameHash = Fnv1a64(LowerAscii(preset.name));
    const std::string fileName = SafeFilePrefix(preset.name) + "-" +
        Hex64(nameHash).substr(0, 8) + ".sbffvoice";
    return folder / std::filesystem::path(fileName);
}

bool SaveVoiceEffectPresetFile(
    const std::filesystem::path& path,
    const VoiceEffectUserPreset& preset,
    std::string& errorMessage
)
{
    errorMessage.clear();
    if (!IsValidVoiceEffectUserPreset(preset))
    {
        errorMessage = "The preset is invalid.";
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        errorMessage = "The preset folder could not be created.";
        return false;
    }

    std::string payload = SerializePresetPayload(preset);
    AppendField(payload, "checksum", Hex64(Fnv1a64(payload)));
    if (payload.size() > MaximumPresetFileBytes)
    {
        errorMessage = "The serialized preset exceeds the size limit.";
        return false;
    }

    std::filesystem::path temporaryPath = path;
    temporaryPath += L".tmp";
    std::filesystem::remove(temporaryPath, error);
    error.clear();

    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        errorMessage = "The temporary preset file could not be opened.";
        return false;
    }
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    output.flush();
    if (!output)
    {
        output.close();
        std::filesystem::remove(temporaryPath, error);
        errorMessage = "The preset file could not be written completely.";
        return false;
    }
    output.close();

    if (!ReplaceFileTransactionally(temporaryPath, path, errorMessage))
    {
        std::filesystem::remove(temporaryPath, error);
        return false;
    }
    return true;
}

VoiceEffectPresetFileLoadResult LoadVoiceEffectPresetFile(
    const std::filesystem::path& path
)
{
    VoiceEffectPresetFileLoadResult result;
    std::error_code error;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, error);
    if (error || fileSize == 0U || fileSize > MaximumPresetFileBytes)
    {
        result.errorMessage = "The preset file size is invalid.";
        return result;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        result.errorMessage = "The preset file could not be opened.";
        return result;
    }
    std::string text(static_cast<std::size_t>(fileSize), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input || text.find('\0') != std::string::npos || !IsValidUtf8(text))
    {
        result.errorMessage = "The preset file is not valid UTF-8 text.";
        return result;
    }

    if (text.size() >= 3U &&
        static_cast<unsigned char>(text[0]) == 0xEFU &&
        static_cast<unsigned char>(text[1]) == 0xBBU &&
        static_cast<unsigned char>(text[2]) == 0xBFU)
    {
        text.erase(0, 3);
    }

    const std::size_t checksumLineStart = text.rfind("checksum=");
    if (checksumLineStart == std::string::npos ||
        (checksumLineStart != 0U && text[checksumLineStart - 1U] != '\n'))
    {
        result.errorMessage = "The preset checksum is missing.";
        return result;
    }
    const std::size_t checksumValueStart = checksumLineStart + 9U;
    std::size_t checksumValueEnd = text.find('\n', checksumValueStart);
    if (checksumValueEnd == std::string::npos)
    {
        checksumValueEnd = text.size();
    }
    if (checksumValueEnd != text.size() - (text.back() == '\n' ? 1U : 0U))
    {
        result.errorMessage = "The checksum must be the final field.";
        return result;
    }

    const auto expectedChecksum = ParseHex64(std::string_view{text}.substr(
        checksumValueStart, checksumValueEnd - checksumValueStart
    ));
    const std::string_view payload{text.data(), checksumLineStart};
    if (!expectedChecksum.has_value() || Fnv1a64(payload) != *expectedChecksum)
    {
        result.errorMessage = "The preset checksum does not match.";
        return result;
    }

    VoiceEffectUserPreset preset;
    preset.settings.enabled = false;
    preset.settings.bypassed = false;
    std::array<bool, 24> seen{};
    bool headerSeen = false;
    std::size_t lineStart = 0;
    std::size_t fieldCount = 0;

    const auto markField = [&](const std::size_t index)
    {
        if (index >= seen.size() || seen[index])
        {
            return false;
        }
        seen[index] = true;
        ++fieldCount;
        return true;
    };

    while (lineStart < payload.size())
    {
        std::size_t lineEnd = payload.find('\n', lineStart);
        if (lineEnd == std::string_view::npos)
        {
            lineEnd = payload.size();
        }
        std::string_view line = payload.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        lineStart = lineEnd + (lineEnd < payload.size() ? 1U : 0U);

        if (!headerSeen)
        {
            if (line != FormatHeader)
            {
                result.errorMessage = "The preset format header is invalid.";
                return result;
            }
            headerSeen = true;
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || equals == 0U)
        {
            result.errorMessage = "A preset field is malformed.";
            return result;
        }
        const std::string_view key = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1U);

        float* floatDestination = nullptr;
        bool* boolDestination = nullptr;
        std::size_t fieldIndex = 0;
        if (key == "name")
        {
            fieldIndex = 0;
            if (!markField(fieldIndex))
            {
                result.errorMessage = "A preset field is duplicated.";
                return result;
            }
            preset.name.assign(value);
            continue;
        }
        if (key == "preset")
        {
            fieldIndex = 1;
            if (!markField(fieldIndex))
            {
                result.errorMessage = "A preset field is duplicated.";
                return result;
            }
            const auto parsed = ParseVoiceEffectPreset(value);
            if (!parsed.has_value())
            {
                result.errorMessage = "The built-in preset identifier is invalid.";
                return result;
            }
            preset.settings.preset = *parsed;
            continue;
        }
        if (key == "rack_order")
        {
            fieldIndex = 23;
            if (!markField(fieldIndex))
            {
                result.errorMessage = "A preset field is duplicated.";
                return result;
            }
            const auto parsed = ParseVoiceEffectRackOrder(value);
            if (!parsed.has_value())
            {
                result.errorMessage = "The rack order is invalid.";
                return result;
            }
            preset.settings.rackOrder = *parsed;
            continue;
        }

#define SBFF_FLOAT_FIELD(fieldKey, fieldNumber, member) \
        if (key == fieldKey) \
        { \
            fieldIndex = fieldNumber; \
            floatDestination = &preset.settings.member; \
        } \
        else
#define SBFF_BOOL_FIELD(fieldKey, fieldNumber, member) \
        if (key == fieldKey) \
        { \
            fieldIndex = fieldNumber; \
            boolDestination = &preset.settings.member; \
        } \
        else
        SBFF_FLOAT_FIELD("pitch_semitones", 2, pitchSemitones)
        SBFF_FLOAT_FIELD("formant_semitones", 3, formantSemitones)
        SBFF_FLOAT_FIELD("character", 4, character)
        SBFF_FLOAT_FIELD("body", 5, body)
        SBFF_FLOAT_FIELD("drive", 6, drive)
        SBFF_FLOAT_FIELD("dry_wet", 7, dryWet)
        SBFF_FLOAT_FIELD("output_gain_db", 8, outputGainDb)
        SBFF_BOOL_FIELD("eq_enabled", 9, parametricEqEnabled)
        SBFF_FLOAT_FIELD("eq_low_gain_db", 10, eqLowGainDb)
        SBFF_FLOAT_FIELD("eq_low_frequency_hz", 11, eqLowFrequencyHz)
        SBFF_FLOAT_FIELD("eq_mid_gain_db", 12, eqMidGainDb)
        SBFF_FLOAT_FIELD("eq_mid_frequency_hz", 13, eqMidFrequencyHz)
        SBFF_FLOAT_FIELD("eq_mid_q", 14, eqMidQ)
        SBFF_FLOAT_FIELD("eq_high_gain_db", 15, eqHighGainDb)
        SBFF_FLOAT_FIELD("eq_high_frequency_hz", 16, eqHighFrequencyHz)
        SBFF_BOOL_FIELD("de_esser_enabled", 17, deEsserEnabled)
        SBFF_FLOAT_FIELD("de_esser_amount", 18, deEsserAmount)
        SBFF_BOOL_FIELD("gate_enabled", 19, gateEnabled)
        SBFF_FLOAT_FIELD("gate_amount", 20, gateAmount)
        SBFF_BOOL_FIELD("compressor_enabled", 21, compressorEnabled)
        SBFF_FLOAT_FIELD("compressor_amount", 22, compressorAmount)
        {
            result.errorMessage = "The preset contains an unsupported field.";
            return result;
        }
#undef SBFF_FLOAT_FIELD
#undef SBFF_BOOL_FIELD

        if (!markField(fieldIndex))
        {
            result.errorMessage = "A preset field is duplicated.";
            return result;
        }
        if (floatDestination != nullptr)
        {
            const auto parsed = ParseFloat(value);
            if (!parsed.has_value())
            {
                result.errorMessage = "A numeric preset field is invalid.";
                return result;
            }
            *floatDestination = *parsed;
        }
        else if (boolDestination != nullptr)
        {
            const auto parsed = ParseBoolean(value);
            if (!parsed.has_value())
            {
                result.errorMessage = "A boolean preset field must be 0 or 1.";
                return result;
            }
            *boolDestination = *parsed;
        }
    }

    if (!headerSeen || fieldCount != seen.size() ||
        !std::all_of(seen.begin(), seen.end(), [](const bool value)
        {
            return value;
        }))
    {
        result.errorMessage = "The preset file is missing a required field.";
        return result;
    }
    if (!IsValidVoiceEffectUserPreset(preset))
    {
        result.errorMessage = "A preset value is outside the supported range.";
        return result;
    }

    result.preset = std::move(preset);
    return result;
}

std::vector<std::filesystem::path> DiscoverVoiceEffectPresetFiles(
    const std::filesystem::path& folder
)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error) || error)
    {
        return files;
    }

    std::filesystem::directory_iterator iterator(
        folder,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end)
    {
        const std::filesystem::directory_entry entry = *iterator;
        std::error_code entryError;
        if (entry.is_regular_file(entryError) && !entryError &&
            ExtensionMatches(entry.path()))
        {
            files.push_back(entry.path());
        }
        iterator.increment(error);
    }

    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right)
    {
        return left.filename().wstring() < right.filename().wstring();
    });
    return files;
}
