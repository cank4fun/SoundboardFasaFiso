#include "editor/AudioDocumentWav.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    int failureCount = 0;

    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            const auto nonce = std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();
            path_ = std::filesystem::temp_directory_path() /
                ("SoundBoardFasaFiso-AudioDocumentWavTests-" +
                    std::to_string(nonce));
            std::filesystem::create_directories(path_);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] const std::filesystem::path& Path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    void Expect(const bool condition, const std::string_view message)
    {
        if (condition)
        {
            return;
        }

        ++failureCount;
        std::cerr << "FAILED: " << message << '\n';
    }

    bool NearlyEqual(
        const float left,
        const float right,
        const float tolerance = 0.00005f
    )
    {
        return std::abs(left - right) <= tolerance;
    }

    AudioDocument RequireDocument(
        std::optional<AudioDocument> document,
        const std::string_view message
    )
    {
        if (!document.has_value())
        {
            Expect(false, message);
            std::exit(1);
        }

        return std::move(*document);
    }

    void AppendId(
        std::vector<std::byte>& bytes,
        const std::string_view id
    )
    {
        for (const char character : id)
        {
            bytes.push_back(static_cast<std::byte>(character));
        }
    }

    void AppendU16(
        std::vector<std::byte>& bytes,
        const std::uint16_t value
    )
    {
        bytes.push_back(static_cast<std::byte>(value & 0xFFU));
        bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    }

    void AppendU24(
        std::vector<std::byte>& bytes,
        const std::uint32_t value
    )
    {
        bytes.push_back(static_cast<std::byte>(value & 0xFFU));
        bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
    }

    void AppendU32(
        std::vector<std::byte>& bytes,
        const std::uint32_t value
    )
    {
        bytes.push_back(static_cast<std::byte>(value & 0xFFU));
        bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
        bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
    }

    void SetU32(
        std::vector<std::byte>& bytes,
        const std::size_t offset,
        const std::uint32_t value
    )
    {
        bytes[offset] = static_cast<std::byte>(value & 0xFFU);
        bytes[offset + 1U] =
            static_cast<std::byte>((value >> 8U) & 0xFFU);
        bytes[offset + 2U] =
            static_cast<std::byte>((value >> 16U) & 0xFFU);
        bytes[offset + 3U] =
            static_cast<std::byte>((value >> 24U) & 0xFFU);
    }

    void AppendChunk(
        std::vector<std::byte>& wave,
        const std::string_view id,
        const std::span<const std::byte> payload
    )
    {
        AppendId(wave, id);
        AppendU32(wave, static_cast<std::uint32_t>(payload.size()));
        wave.insert(wave.end(), payload.begin(), payload.end());

        if ((payload.size() & 1U) != 0U)
        {
            wave.push_back(std::byte{0});
        }
    }

    std::vector<std::byte> BuildWave(
        const std::span<const std::byte> format,
        const std::span<const std::byte> data,
        const bool includeOddJunk = false
    )
    {
        std::vector<std::byte> wave;
        AppendId(wave, "RIFF");
        AppendU32(wave, 0U);
        AppendId(wave, "WAVE");

        if (includeOddJunk)
        {
            const std::array<std::byte, 3> junk{
                std::byte{1}, std::byte{2}, std::byte{3}
            };
            AppendChunk(wave, "JUNK", junk);
        }

        AppendChunk(wave, "fmt ", format);
        AppendChunk(wave, "data", data);
        SetU32(
            wave,
            4U,
            static_cast<std::uint32_t>(wave.size() - 8U)
        );
        return wave;
    }

    std::vector<std::byte> PcmFormat(
        const std::uint16_t formatTag,
        const std::uint16_t channels,
        const std::uint32_t sampleRate,
        const std::uint16_t bitsPerSample
    )
    {
        std::vector<std::byte> format;
        const std::uint16_t blockAlign = static_cast<std::uint16_t>(
            channels * static_cast<std::uint16_t>(bitsPerSample / 8U)
        );
        AppendU16(format, formatTag);
        AppendU16(format, channels);
        AppendU32(format, sampleRate);
        AppendU32(
            format,
            sampleRate * static_cast<std::uint32_t>(blockAlign)
        );
        AppendU16(format, blockAlign);
        AppendU16(format, bitsPerSample);
        return format;
    }

    std::vector<std::byte> ExtensibleFloatFormat(
        const std::uint16_t channels,
        const std::uint32_t sampleRate
    )
    {
        std::vector<std::byte> format = PcmFormat(
            0xFFFEU,
            channels,
            sampleRate,
            32U
        );
        AppendU16(format, 22U);
        AppendU16(format, 32U);
        AppendU32(format, 0U);
        AppendU32(format, 0x00000003U);
        AppendU16(format, 0x0000U);
        AppendU16(format, 0x0010U);
        format.push_back(std::byte{0x80});
        format.push_back(std::byte{0x00});
        format.push_back(std::byte{0x00});
        format.push_back(std::byte{0xAA});
        format.push_back(std::byte{0x00});
        format.push_back(std::byte{0x38});
        format.push_back(std::byte{0x9B});
        format.push_back(std::byte{0x71});
        return format;
    }

    bool WriteBytes(
        const std::filesystem::path& path,
        const std::span<const std::byte> bytes
    )
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);

        if (!file.is_open())
        {
            return false;
        }

        file.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        return file.good();
    }

    std::vector<std::byte> ReadBytes(
        const std::filesystem::path& path
    )
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);

        if (!file.is_open())
        {
            return {};
        }

        const std::streamoff size = file.tellg();

        if (size < 0)
        {
            return {};
        }

        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        file.seekg(0, std::ios::beg);
        file.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        return file.good() ? bytes : std::vector<std::byte>{};
    }

    std::size_t TemporarySiblingCount(
        const std::filesystem::path& destination
    )
    {
        const std::wstring prefix = destination.filename().wstring() +
            L".saving-";
        std::size_t count = 0U;

        for (const auto& entry : std::filesystem::directory_iterator(
                 destination.parent_path()
             ))
        {
            if (entry.path().filename().wstring().starts_with(prefix))
            {
                ++count;
            }
        }

        return count;
    }

    void TestPcm16SaveAndLoad()
    {
        TemporaryDirectory directory;
        std::string errorMessage;
        AudioDocument document = RequireDocument(
            AudioDocument::Create(
                48000U,
                2U,
                {
                    -1.0f, -0.5f,
                    0.0f, 0.5f,
                    1.0f, 1.25f,
                    -1.25f, 0.25f
                },
                errorMessage
            ),
            "PCM16 save fixture is valid"
        );
        const auto path = directory.Path() / "round-trip.wav";
        const AudioWavSaveResult saveResult =
            AudioDocumentWav::SavePcm16(document, path);

        Expect(saveResult.Succeeded(), "PCM16 WAV save succeeds");
        Expect(
            saveResult.errorMessage.empty(),
            "successful save has no error message"
        );
        Expect(
            TemporarySiblingCount(path) == 0U,
            "successful save leaves no temporary sibling"
        );

        const std::vector<std::byte> encoded = ReadBytes(path);
        Expect(encoded.size() == 60U, "PCM16 WAV has the expected size");
        Expect(
            encoded.size() >= 44U &&
                std::to_integer<char>(encoded[0]) == 'R' &&
                std::to_integer<char>(encoded[8]) == 'W',
            "PCM16 WAV has RIFF/WAVE identifiers"
        );
        Expect(
            encoded.size() >= 48U &&
                encoded[44] == std::byte{0x00} &&
                encoded[45] == std::byte{0x80} &&
                encoded[46] == std::byte{0x00} &&
                encoded[47] == std::byte{0xC0},
            "PCM16 conversion uses signed little-endian samples"
        );

        AudioWavLoadResult loadResult = AudioDocumentWav::Load(path);
        Expect(loadResult.Succeeded(), "saved PCM16 WAV loads");

        if (!loadResult.document.has_value())
        {
            return;
        }

        const AudioDocument& loaded = *loadResult.document;
        Expect(loaded.SampleRate() == 48000U, "sample rate round-trips");
        Expect(loaded.ChannelCount() == 2U, "channel count round-trips");
        Expect(loaded.FrameCount() == 4U, "frame count round-trips");

        const std::array<float, 8> expected{
            -1.0f, -0.5f,
            0.0f, 0.5f,
            32767.0f / 32768.0f,
            32767.0f / 32768.0f,
            -1.0f,
            8192.0f / 32768.0f
        };

        for (std::size_t index = 0U; index < expected.size(); ++index)
        {
            Expect(
                NearlyEqual(loaded.Samples()[index], expected[index]),
                "PCM16 samples round-trip with clipping and quantization"
            );
        }
    }

    void TestSaveModes()
    {
        TemporaryDirectory directory;
        std::string errorMessage;
        AudioDocument first = RequireDocument(
            AudioDocument::Create(44100U, 1U, {0.25f}, errorMessage),
            "first save fixture is valid"
        );
        AudioDocument second = RequireDocument(
            AudioDocument::Create(22050U, 1U, {-0.75f}, errorMessage),
            "replacement save fixture is valid"
        );
        const auto path = directory.Path() / "replace.wav";

        Expect(
            AudioDocumentWav::SavePcm16(first, path).Succeeded(),
            "initial create-new save succeeds"
        );
        const std::vector<std::byte> original = ReadBytes(path);
        const AudioWavSaveResult collision =
            AudioDocumentWav::SavePcm16(second, path);
        Expect(
            collision.error == AudioWavFileError::DestinationExists,
            "create-new save rejects an existing destination"
        );
        Expect(
            ReadBytes(path) == original,
            "rejected create-new save preserves the existing file"
        );
        Expect(
            TemporarySiblingCount(path) == 0U,
            "rejected create-new save leaves no temporary sibling"
        );

        Expect(
            AudioDocumentWav::SavePcm16(
                second,
                path,
                AudioWavSaveMode::ReplaceExisting
            ).Succeeded(),
            "replace-existing save succeeds"
        );
        AudioWavLoadResult replaced = AudioDocumentWav::Load(path);
        Expect(replaced.Succeeded(), "replacement WAV loads");

        if (replaced.document.has_value())
        {
            Expect(
                replaced.document->SampleRate() == 22050U,
                "replacement atomically publishes the new WAV"
            );
            Expect(
                NearlyEqual(replaced.document->Samples()[0], -0.75f),
                "replacement sample data is retained"
            );
        }

        Expect(
            TemporarySiblingCount(path) == 0U,
            "replacement leaves no temporary sibling"
        );

        const auto createThroughReplacePath =
            directory.Path() / "replace-missing.wav";
        Expect(
            AudioDocumentWav::SavePcm16(
                second,
                createThroughReplacePath,
                AudioWavSaveMode::ReplaceExisting
            ).Succeeded(),
            "replace-existing mode can create a missing destination"
        );
        Expect(
            AudioDocumentWav::Load(createThroughReplacePath).Succeeded(),
            "replace-created WAV loads"
        );
    }

    void TestPcmVariantsAndChunkPadding()
    {
        TemporaryDirectory directory;

        std::vector<std::byte> pcm8Data{
            std::byte{0x00}, std::byte{0x80}, std::byte{0xFF}
        };
        const auto pcm8Path = directory.Path() / "pcm8.wav";
        const auto pcm8Wave = BuildWave(
            PcmFormat(1U, 1U, 8000U, 8U),
            pcm8Data,
            true
        );
        Expect(WriteBytes(pcm8Path, pcm8Wave), "PCM8 fixture is written");
        AudioWavLoadResult pcm8 = AudioDocumentWav::Load(pcm8Path);
        Expect(pcm8.Succeeded(), "PCM8 WAV with odd JUNK chunk loads");

        if (pcm8.document.has_value())
        {
            const auto samples = pcm8.document->Samples();
            Expect(NearlyEqual(samples[0], -1.0f), "PCM8 minimum decodes");
            Expect(
                NearlyEqual(samples[1], 1.0f / 255.0f),
                "PCM8 midpoint decodes"
            );
            Expect(
                NearlyEqual(samples[2], 1.0f),
                "PCM8 maximum decodes"
            );
        }

        std::vector<std::byte> pcm24Data;
        AppendU24(pcm24Data, 0x00800000U);
        AppendU24(pcm24Data, 0x00000000U);
        AppendU24(pcm24Data, 0x007FFFFFU);
        const auto pcm24Path = directory.Path() / "pcm24.wav";
        const auto pcm24Wave = BuildWave(
            PcmFormat(1U, 1U, 48000U, 24U),
            pcm24Data
        );
        Expect(
            WriteBytes(pcm24Path, pcm24Wave),
            "PCM24 fixture is written"
        );
        AudioWavLoadResult pcm24 = AudioDocumentWav::Load(pcm24Path);
        Expect(pcm24.Succeeded(), "PCM24 WAV loads");

        if (pcm24.document.has_value())
        {
            const auto samples = pcm24.document->Samples();
            Expect(NearlyEqual(samples[0], -1.0f), "PCM24 minimum decodes");
            Expect(NearlyEqual(samples[1], 0.0f), "PCM24 zero decodes");
            Expect(
                NearlyEqual(samples[2], 1.0f),
                "PCM24 maximum decodes"
            );
        }
    }

    void TestExtensibleFloat()
    {
        TemporaryDirectory directory;
        std::vector<std::byte> data;

        for (const float sample : std::array<float, 3>{1.25f, -0.5f, 0.0f})
        {
            AppendU32(data, std::bit_cast<std::uint32_t>(sample));
        }

        const auto path = directory.Path() / "float-extensible.wav";
        const auto wave = BuildWave(
            ExtensibleFloatFormat(1U, 96000U),
            data
        );
        Expect(WriteBytes(path, wave), "extensible float fixture is written");
        AudioWavLoadResult result = AudioDocumentWav::Load(path);
        Expect(result.Succeeded(), "extensible IEEE float WAV loads");

        if (result.document.has_value())
        {
            Expect(
                result.document->SampleRate() == 96000U,
                "extensible sample rate is retained"
            );
            Expect(
                NearlyEqual(result.document->Samples()[0], 1.25f),
                "floating-point headroom is retained"
            );
            Expect(
                NearlyEqual(result.document->Samples()[1], -0.5f),
                "negative float sample is retained"
            );
        }
    }

    void TestInvalidInputs()
    {
        TemporaryDirectory directory;
        const auto missing = directory.Path() / "missing.wav";
        Expect(
            AudioDocumentWav::Load(missing).error ==
                AudioWavFileError::FileNotFound,
            "missing WAV is reported"
        );
        Expect(
            AudioDocumentWav::Load(directory.Path()).error ==
                AudioWavFileError::InvalidPath,
            "directory WAV path is rejected"
        );

        const auto shortPath = directory.Path() / "short.wav";
        const std::array<std::byte, 4> shortBytes{
            std::byte{'n'}, std::byte{'o'}, std::byte{'p'}, std::byte{'e'}
        };
        Expect(WriteBytes(shortPath, shortBytes), "short fixture is written");
        Expect(
            AudioDocumentWav::Load(shortPath).error ==
                AudioWavFileError::InvalidContainer,
            "short WAV is rejected"
        );

        std::vector<std::byte> truncated = BuildWave(
            PcmFormat(1U, 1U, 48000U, 16U),
            std::array<std::byte, 2>{std::byte{0}, std::byte{0}}
        );
        SetU32(
            truncated,
            4U,
            static_cast<std::uint32_t>(truncated.size() + 128U)
        );
        const auto truncatedPath = directory.Path() / "truncated.wav";
        Expect(
            WriteBytes(truncatedPath, truncated),
            "truncated fixture is written"
        );
        Expect(
            AudioDocumentWav::Load(truncatedPath).error ==
                AudioWavFileError::InvalidContainer,
            "truncated RIFF size is rejected"
        );

        std::vector<std::byte> unsupportedData{std::byte{0x00}};
        const auto unsupportedPath = directory.Path() / "unsupported.wav";
        const auto unsupportedWave = BuildWave(
            PcmFormat(0x7777U, 1U, 8000U, 8U),
            unsupportedData
        );
        Expect(
            WriteBytes(unsupportedPath, unsupportedWave),
            "unsupported fixture is written"
        );
        Expect(
            !AudioDocumentWav::Load(unsupportedPath).Succeeded(),
            "unsupported WAV encoding is rejected"
        );

        std::vector<std::byte> misalignedData{
            std::byte{0x00}, std::byte{0x00}
        };
        const auto misalignedPath = directory.Path() / "misaligned.wav";
        const auto misalignedWave = BuildWave(
            PcmFormat(1U, 2U, 48000U, 16U),
            misalignedData
        );
        Expect(
            WriteBytes(misalignedPath, misalignedWave),
            "misaligned fixture is written"
        );
        Expect(
            AudioDocumentWav::Load(misalignedPath).error ==
                AudioWavFileError::InvalidAudioData,
            "partial PCM frame is rejected"
        );

        std::vector<std::byte> nanData;
        AppendU32(
            nanData,
            std::bit_cast<std::uint32_t>(
                std::numeric_limits<float>::quiet_NaN()
            )
        );
        const auto nanPath = directory.Path() / "nan.wav";
        const auto nanWave = BuildWave(
            PcmFormat(3U, 1U, 48000U, 32U),
            nanData
        );
        Expect(WriteBytes(nanPath, nanWave), "NaN fixture is written");
        Expect(
            AudioDocumentWav::Load(nanPath).error ==
                AudioWavFileError::InvalidAudioData,
            "non-finite floating-point WAV is rejected"
        );
    }

    void TestEmptyDocumentAndInvalidSavePaths()
    {
        TemporaryDirectory directory;
        std::string errorMessage;
        AudioDocument empty = RequireDocument(
            AudioDocument::Create(48000U, 2U, {}, errorMessage),
            "empty document fixture is valid"
        );
        const auto emptyPath = directory.Path() / "empty.wav";
        Expect(
            AudioDocumentWav::SavePcm16(empty, emptyPath).Succeeded(),
            "empty WAV saves"
        );
        AudioWavLoadResult loaded = AudioDocumentWav::Load(emptyPath);
        Expect(loaded.Succeeded(), "empty WAV loads");
        Expect(
            loaded.document.has_value() && loaded.document->Empty(),
            "empty WAV retains zero frames"
        );

        Expect(
            AudioDocumentWav::SavePcm16(empty, {}).error ==
                AudioWavFileError::InvalidPath,
            "empty destination path is rejected"
        );
        Expect(
            AudioDocumentWav::SavePcm16(
                empty,
                directory.Path() / "missing" / "file.wav"
            ).error == AudioWavFileError::InvalidPath,
            "missing destination directory is rejected"
        );
        Expect(
            AudioDocumentWav::SavePcm16(
                empty,
                directory.Path(),
                AudioWavSaveMode::ReplaceExisting
            ).error == AudioWavFileError::InvalidPath,
            "destination directory cannot be replaced as a WAV"
        );
    }
}

int main()
{
    TestPcm16SaveAndLoad();
    TestSaveModes();
    TestPcmVariantsAndChunkPadding();
    TestExtensibleFloat();
    TestInvalidInputs();
    TestEmptyDocumentAndInvalidSavePaths();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Audio document WAV tests passed.\n";
    return 0;
}
