#include "editor/AudioDocumentWav.hpp"

#include "miniaudio/miniaudio.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr std::uint64_t MaximumDecodedBytes = 1ULL << 30U;
    constexpr std::uint32_t MaximumRiffDataBytes =
        std::numeric_limits<std::uint32_t>::max() - 36U;
    constexpr ma_uint64 DecodeChunkFrames = 16384U;

    bool IsCancellationRequested(
        const std::atomic_bool* cancellationRequested
    ) noexcept
    {
        return cancellationRequested != nullptr &&
            cancellationRequested->load(std::memory_order_relaxed);
    }

    struct WaveInspectionResult final
    {
        AudioWavFileError error = AudioWavFileError::None;
        std::string errorMessage;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error == AudioWavFileError::None;
        }
    };

    std::uint16_t ReadU16(
        const std::array<std::byte, 16>& bytes,
        const std::size_t offset
    ) noexcept
    {
        return static_cast<std::uint16_t>(
            std::to_integer<std::uint16_t>(bytes[offset]) |
            (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U)
        );
    }

    std::uint32_t ReadU32(
        const std::array<std::byte, 16>& bytes,
        const std::size_t offset
    ) noexcept
    {
        return
            std::to_integer<std::uint32_t>(bytes[offset]) |
            (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
            (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
            (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
    }

    bool ReadExact(
        std::ifstream& file,
        std::byte* destination,
        const std::size_t byteCount
    )
    {
        file.read(
            reinterpret_cast<char*>(destination),
            static_cast<std::streamsize>(byteCount)
        );
        return file.gcount() == static_cast<std::streamsize>(byteCount);
    }

    WaveInspectionResult InspectWaveContainer(
        const std::filesystem::path& filePath,
        const std::atomic_bool* cancellationRequested
    )
    {
        std::error_code error;
        const std::uintmax_t fileSize =
            std::filesystem::file_size(filePath, error);

        if (error || fileSize < 12U)
        {
            return {
                AudioWavFileError::InvalidContainer,
                "The WAV file is too small."
            };
        }

        std::ifstream file(filePath, std::ios::binary);

        if (!file.is_open())
        {
            return {
                AudioWavFileError::OpenFailed,
                "The WAV file could not be opened."
            };
        }

        std::array<std::byte, 16> bytes{};

        if (!ReadExact(file, bytes.data(), 12U) ||
            std::to_integer<char>(bytes[0]) != 'R' ||
            std::to_integer<char>(bytes[1]) != 'I' ||
            std::to_integer<char>(bytes[2]) != 'F' ||
            std::to_integer<char>(bytes[3]) != 'F' ||
            std::to_integer<char>(bytes[8]) != 'W' ||
            std::to_integer<char>(bytes[9]) != 'A' ||
            std::to_integer<char>(bytes[10]) != 'V' ||
            std::to_integer<char>(bytes[11]) != 'E')
        {
            return {
                AudioWavFileError::InvalidContainer,
                "The file is not a RIFF/WAVE container."
            };
        }

        const std::uint64_t riffEnd =
            static_cast<std::uint64_t>(ReadU32(bytes, 4U)) + 8ULL;

        if (riffEnd < 12U || riffEnd > fileSize)
        {
            return {
                AudioWavFileError::InvalidContainer,
                "The RIFF size is invalid or truncated."
            };
        }

        std::optional<std::uint16_t> blockAlign;
        std::optional<std::uint32_t> dataSize;
        std::uint64_t position = 12U;

        while (position + 8U <= riffEnd)
        {
            if (IsCancellationRequested(cancellationRequested))
            {
                return {
                    AudioWavFileError::Cancelled,
                    "The WAV load was cancelled."
                };
            }
            if (!ReadExact(file, bytes.data(), 8U))
            {
                return {
                    AudioWavFileError::ReadFailed,
                    "A WAV chunk header could not be read."
                };
            }

            const std::uint32_t chunkSize = ReadU32(bytes, 4U);
            const std::uint64_t chunkEnd = position + 8ULL + chunkSize;
            const std::uint64_t paddedChunkEnd =
                chunkEnd + static_cast<std::uint64_t>(chunkSize & 1U);

            if (chunkEnd < position || paddedChunkEnd > riffEnd)
            {
                return {
                    AudioWavFileError::InvalidContainer,
                    "A WAV chunk extends beyond the RIFF container."
                };
            }

            const bool isFormat =
                std::to_integer<char>(bytes[0]) == 'f' &&
                std::to_integer<char>(bytes[1]) == 'm' &&
                std::to_integer<char>(bytes[2]) == 't' &&
                std::to_integer<char>(bytes[3]) == ' ';
            const bool isData =
                std::to_integer<char>(bytes[0]) == 'd' &&
                std::to_integer<char>(bytes[1]) == 'a' &&
                std::to_integer<char>(bytes[2]) == 't' &&
                std::to_integer<char>(bytes[3]) == 'a';

            if (isFormat && !blockAlign.has_value())
            {
                if (chunkSize < 16U ||
                    !ReadExact(file, bytes.data(), 16U))
                {
                    return {
                        AudioWavFileError::InvalidContainer,
                        "The WAV format chunk is invalid."
                    };
                }

                blockAlign = ReadU16(bytes, 12U);

                if (*blockAlign == 0U)
                {
                    return {
                        AudioWavFileError::InvalidAudioData,
                        "The WAV block alignment is zero."
                    };
                }

                file.seekg(
                    static_cast<std::streamoff>(chunkSize - 16U),
                    std::ios::cur
                );
            }
            else
            {
                if (isData && !dataSize.has_value())
                {
                    dataSize = chunkSize;
                }

                file.seekg(
                    static_cast<std::streamoff>(chunkSize),
                    std::ios::cur
                );
            }

            if (!file.good())
            {
                return {
                    AudioWavFileError::ReadFailed,
                    "A WAV chunk could not be read or skipped."
                };
            }

            if ((chunkSize & 1U) != 0U)
            {
                file.seekg(1, std::ios::cur);

                if (!file.good())
                {
                    return {
                        AudioWavFileError::ReadFailed,
                        "The WAV chunk padding could not be skipped."
                    };
                }
            }

            position = paddedChunkEnd;
        }

        if (position != riffEnd || !blockAlign.has_value() ||
            !dataSize.has_value())
        {
            return {
                AudioWavFileError::InvalidContainer,
                "The WAV container is incomplete."
            };
        }

        if (*dataSize % *blockAlign != 0U)
        {
            return {
                AudioWavFileError::InvalidAudioData,
                "The WAV data is not frame-aligned."
            };
        }

        return {};
    }

    AudioWavLoadResult LoadFailure(
        const AudioWavFileError error,
        std::string message
    )
    {
        AudioWavLoadResult result;
        result.error = error;
        result.errorMessage = std::move(message);
        return result;
    }

    AudioWavSaveResult SaveFailure(
        const AudioWavFileError error,
        std::string message
    )
    {
        AudioWavSaveResult result;
        result.error = error;
        result.errorMessage = std::move(message);
        return result;
    }

    AudioWavFileError MapDecoderError(const ma_result result) noexcept
    {
        switch (result)
        {
        case MA_DOES_NOT_EXIST:
            return AudioWavFileError::FileNotFound;
        case MA_ACCESS_DENIED:
            return AudioWavFileError::OpenFailed;
        case MA_FORMAT_NOT_SUPPORTED:
            return AudioWavFileError::UnsupportedFormat;
        case MA_INVALID_FILE:
            return AudioWavFileError::InvalidContainer;
        default:
            return AudioWavFileError::ReadFailed;
        }
    }

    ma_result InitializeDecoder(
        const std::filesystem::path& filePath,
        const ma_decoder_config& config,
        ma_decoder& decoder
    )
    {
#ifdef _WIN32
        return ma_decoder_init_file_w(
            filePath.c_str(),
            &config,
            &decoder
        );
#else
        return ma_decoder_init_file(
            filePath.c_str(),
            &config,
            &decoder
        );
#endif
    }

    ma_result InitializeEncoder(
        const std::filesystem::path& filePath,
        const ma_encoder_config& config,
        ma_encoder& encoder
    )
    {
#ifdef _WIN32
        return ma_encoder_init_file_w(
            filePath.c_str(),
            &config,
            &encoder
        );
#else
        return ma_encoder_init_file(
            filePath.c_str(),
            &config,
            &encoder
        );
#endif
    }

    std::int16_t EncodePcm16(const float sample) noexcept
    {
        const double clamped = std::clamp(
            static_cast<double>(sample),
            -1.0,
            1.0
        );
        const double scaled = clamped < 0.0
            ? clamped * 32768.0
            : clamped * 32767.0;
        const long rounded = std::lround(scaled);
        return static_cast<std::int16_t>(std::clamp<long>(
            rounded,
            -32768L,
            32767L
        ));
    }

    bool FlushFileContents(const std::filesystem::path& path)
    {
#ifdef _WIN32
        const HANDLE handle = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        const bool succeeded = FlushFileBuffers(handle) != FALSE;
        CloseHandle(handle);
        return succeeded;
#else
        const int descriptor = open(path.c_str(), O_WRONLY);

        if (descriptor < 0)
        {
            return false;
        }

        const bool succeeded = fsync(descriptor) == 0;
        close(descriptor);
        return succeeded;
#endif
    }

    bool CommitTemporaryFile(
        const std::filesystem::path& temporaryPath,
        const std::filesystem::path& destinationPath,
        const AudioWavSaveMode saveMode
    )
    {
#ifdef _WIN32
        if (saveMode == AudioWavSaveMode::CreateNew)
        {
            return MoveFileExW(
                temporaryPath.c_str(),
                destinationPath.c_str(),
                MOVEFILE_WRITE_THROUGH
            ) != FALSE;
        }

        if (ReplaceFileW(
                destinationPath.c_str(),
                temporaryPath.c_str(),
                nullptr,
                REPLACEFILE_WRITE_THROUGH,
                nullptr,
                nullptr
            ) != FALSE)
        {
            return true;
        }

        return MoveFileExW(
            temporaryPath.c_str(),
            destinationPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        ) != FALSE;
#else
        if (saveMode == AudioWavSaveMode::CreateNew)
        {
            if (link(temporaryPath.c_str(), destinationPath.c_str()) != 0)
            {
                return false;
            }

            return unlink(temporaryPath.c_str()) == 0;
        }

        return rename(
            temporaryPath.c_str(),
            destinationPath.c_str()
        ) == 0;
#endif
    }

    std::optional<std::filesystem::path> CreateTemporaryPath(
        const std::filesystem::path& destinationPath
    )
    {
        const auto nonce = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();

        for (unsigned int attempt = 0U; attempt < 100U; ++attempt)
        {
            std::filesystem::path temporaryPath = destinationPath;
            temporaryPath += L".saving-" + std::to_wstring(nonce) +
                L"-" + std::to_wstring(attempt) + L".tmp";

            std::error_code error;

            if (!std::filesystem::exists(temporaryPath, error) && !error)
            {
                return temporaryPath;
            }
        }

        return std::nullopt;
    }
}

bool AudioWavLoadResult::Succeeded() const noexcept
{
    return document.has_value() && error == AudioWavFileError::None;
}

bool AudioWavSaveResult::Succeeded() const noexcept
{
    return error == AudioWavFileError::None;
}

AudioWavLoadResult AudioDocumentWav::Load(
    const std::filesystem::path& filePath,
    const std::atomic_bool* cancellationRequested
)
{
    if (IsCancellationRequested(cancellationRequested))
    {
        return LoadFailure(
            AudioWavFileError::Cancelled,
            "The WAV load was cancelled."
        );
    }

    if (filePath.empty())
    {
        return LoadFailure(
            AudioWavFileError::InvalidPath,
            "The WAV file path is empty."
        );
    }

    std::error_code error;

    if (!std::filesystem::exists(filePath, error) || error)
    {
        return LoadFailure(
            AudioWavFileError::FileNotFound,
            "The WAV file does not exist."
        );
    }

    if (!std::filesystem::is_regular_file(filePath, error) || error)
    {
        return LoadFailure(
            AudioWavFileError::InvalidPath,
            "The WAV path is not a regular file."
        );
    }

    const WaveInspectionResult inspection =
        InspectWaveContainer(filePath, cancellationRequested);

    if (!inspection.Succeeded())
    {
        return LoadFailure(
            inspection.error,
            inspection.errorMessage
        );
    }

    if (IsCancellationRequested(cancellationRequested))
    {
        return LoadFailure(
            AudioWavFileError::Cancelled,
            "The WAV load was cancelled."
        );
    }

    ma_decoder decoder{};
    const ma_decoder_config decoderConfig = ma_decoder_config_init(
        ma_format_f32,
        0U,
        0U
    );
    const ma_result initializeResult = InitializeDecoder(
        filePath,
        decoderConfig,
        decoder
    );

    if (initializeResult != MA_SUCCESS)
    {
        return LoadFailure(
            MapDecoderError(initializeResult),
            std::string{"The WAV decoder could not be initialized: "} +
                ma_result_description(initializeResult)
        );
    }

    ma_format sampleFormat = ma_format_unknown;
    ma_uint32 channelCount = 0U;
    ma_uint32 sampleRate = 0U;
    const ma_result formatResult = ma_decoder_get_data_format(
        &decoder,
        &sampleFormat,
        &channelCount,
        &sampleRate,
        nullptr,
        0U
    );

    if (formatResult != MA_SUCCESS ||
        sampleFormat != ma_format_f32 ||
        channelCount == 0U || channelCount > 32U ||
        sampleRate == 0U || sampleRate > 768000U)
    {
        ma_decoder_uninit(&decoder);
        return LoadFailure(
            AudioWavFileError::InvalidAudioData,
            "The decoded WAV format is invalid."
        );
    }

    ma_uint64 frameCount = 0U;
    const ma_result lengthResult =
        ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);

    if (lengthResult != MA_SUCCESS)
    {
        ma_decoder_uninit(&decoder);
        return LoadFailure(
            AudioWavFileError::ReadFailed,
            std::string{"The WAV length could not be read: "} +
                ma_result_description(lengthResult)
        );
    }

    if (frameCount >
        std::numeric_limits<std::uint64_t>::max() / channelCount)
    {
        ma_decoder_uninit(&decoder);
        return LoadFailure(
            AudioWavFileError::FileTooLarge,
            "The decoded WAV sample count overflows."
        );
    }

    const std::uint64_t sampleCount = frameCount * channelCount;

    if (sampleCount >
            static_cast<std::uint64_t>(
                std::vector<float>{}.max_size()
            ) ||
        sampleCount > MaximumDecodedBytes / sizeof(float))
    {
        ma_decoder_uninit(&decoder);
        return LoadFailure(
            AudioWavFileError::FileTooLarge,
            "The decoded WAV is too large for the editor."
        );
    }

    const ma_result seekResult =
        ma_decoder_seek_to_pcm_frame(&decoder, 0U);

    if (seekResult != MA_SUCCESS)
    {
        ma_decoder_uninit(&decoder);
        return LoadFailure(
            AudioWavFileError::ReadFailed,
            std::string{"The WAV decoder could not seek to the start: "} +
                ma_result_description(seekResult)
        );
    }

    std::vector<float> samples(
        static_cast<std::size_t>(sampleCount),
        0.0f
    );
    ma_uint64 totalFramesRead = 0U;
    ma_result readResult = MA_SUCCESS;

    while (totalFramesRead < frameCount)
    {
        if (IsCancellationRequested(cancellationRequested))
        {
            ma_decoder_uninit(&decoder);
            return LoadFailure(
                AudioWavFileError::Cancelled,
                "The WAV load was cancelled."
            );
        }

        const ma_uint64 requestedFrames = std::min(
            DecodeChunkFrames,
            frameCount - totalFramesRead
        );
        ma_uint64 chunkFramesRead = 0U;
        float* destination = samples.data() +
            static_cast<std::size_t>(totalFramesRead) *
                static_cast<std::size_t>(channelCount);

        readResult = ma_decoder_read_pcm_frames(
            &decoder,
            destination,
            requestedFrames,
            &chunkFramesRead
        );
        totalFramesRead += chunkFramesRead;

        if ((readResult != MA_SUCCESS && readResult != MA_AT_END) ||
            chunkFramesRead == 0U)
        {
            break;
        }
    }

    ma_decoder_uninit(&decoder);

    if ((readResult != MA_SUCCESS && readResult != MA_AT_END) ||
        totalFramesRead != frameCount)
    {
        return LoadFailure(
            AudioWavFileError::ReadFailed,
            "The WAV sample data could not be read completely."
        );
    }

    if (IsCancellationRequested(cancellationRequested))
    {
        return LoadFailure(
            AudioWavFileError::Cancelled,
            "The WAV load was cancelled."
        );
    }

    std::string documentError;
    std::optional<AudioDocument> document = AudioDocument::Create(
        sampleRate,
        channelCount,
        std::move(samples),
        documentError
    );

    if (!document.has_value())
    {
        return LoadFailure(
            AudioWavFileError::InvalidAudioData,
            std::move(documentError)
        );
    }

    AudioWavLoadResult result;
    result.document = std::move(document);
    return result;
}

AudioWavSaveResult AudioDocumentWav::SavePcm16(
    const AudioDocument& document,
    const std::filesystem::path& filePath,
    const AudioWavSaveMode saveMode
)
{
    if (filePath.empty())
    {
        return SaveFailure(
            AudioWavFileError::InvalidPath,
            "The destination WAV path is empty."
        );
    }

    const std::filesystem::path parent = filePath.has_parent_path()
        ? filePath.parent_path()
        : std::filesystem::path{"."};
    std::error_code error;

    if (!std::filesystem::is_directory(parent, error) || error)
    {
        return SaveFailure(
            AudioWavFileError::InvalidPath,
            "The destination WAV directory does not exist."
        );
    }

    error.clear();
    const bool destinationExists =
        std::filesystem::exists(filePath, error);

    if (error)
    {
        return SaveFailure(
            AudioWavFileError::OpenFailed,
            "The destination WAV path could not be inspected."
        );
    }

    if (destinationExists)
    {
        error.clear();

        if (!std::filesystem::is_regular_file(filePath, error) || error)
        {
            return SaveFailure(
                AudioWavFileError::InvalidPath,
                "The destination WAV path is not a regular file."
            );
        }

        if (saveMode == AudioWavSaveMode::CreateNew)
        {
            return SaveFailure(
                AudioWavFileError::DestinationExists,
                "The destination WAV file already exists."
            );
        }
    }

    const std::uint64_t sampleCount = document.Samples().size();
    const std::uint64_t dataByteCount = sampleCount * 2ULL;

    if (sampleCount > MaximumRiffDataBytes / 2U ||
        dataByteCount > MaximumRiffDataBytes)
    {
        return SaveFailure(
            AudioWavFileError::FileTooLarge,
            "The PCM16 WAV would exceed the RIFF size limit."
        );
    }

    std::vector<std::int16_t> encodedSamples(
        static_cast<std::size_t>(sampleCount),
        0
    );

    for (std::size_t index = 0U; index < encodedSamples.size(); ++index)
    {
        encodedSamples[index] = EncodePcm16(document.Samples()[index]);
    }

    const auto temporaryPath = CreateTemporaryPath(filePath);

    if (!temporaryPath.has_value())
    {
        return SaveFailure(
            AudioWavFileError::OpenFailed,
            "A unique temporary WAV path could not be created."
        );
    }

    const ma_encoder_config encoderConfig = ma_encoder_config_init(
        ma_encoding_format_wav,
        ma_format_s16,
        document.ChannelCount(),
        document.SampleRate()
    );
    ma_encoder encoder{};
    const ma_result initializeResult = InitializeEncoder(
        *temporaryPath,
        encoderConfig,
        encoder
    );

    if (initializeResult != MA_SUCCESS)
    {
        std::filesystem::remove(*temporaryPath, error);
        return SaveFailure(
            AudioWavFileError::OpenFailed,
            std::string{"The temporary WAV encoder could not be opened: "} +
                ma_result_description(initializeResult)
        );
    }

    ma_uint64 framesWritten = 0U;
    ma_result writeResult = MA_SUCCESS;

    if (document.FrameCount() != 0U)
    {
        writeResult = ma_encoder_write_pcm_frames(
            &encoder,
            encodedSamples.data(),
            static_cast<ma_uint64>(document.FrameCount()),
            &framesWritten
        );
    }

    ma_encoder_uninit(&encoder);

    if (writeResult != MA_SUCCESS ||
        framesWritten != document.FrameCount())
    {
        std::filesystem::remove(*temporaryPath, error);
        return SaveFailure(
            AudioWavFileError::WriteFailed,
            "The temporary WAV file could not be written completely."
        );
    }

    if (!FlushFileContents(*temporaryPath))
    {
        std::filesystem::remove(*temporaryPath, error);
        return SaveFailure(
            AudioWavFileError::WriteFailed,
            "The temporary WAV file could not be flushed to disk."
        );
    }

    if (!CommitTemporaryFile(*temporaryPath, filePath, saveMode))
    {
        std::filesystem::remove(*temporaryPath, error);
        error.clear();
        const bool nowExists = std::filesystem::exists(filePath, error);
        return SaveFailure(
            saveMode == AudioWavSaveMode::CreateNew &&
                nowExists && !error
                ? AudioWavFileError::DestinationExists
                : AudioWavFileError::CommitFailed,
            "The temporary WAV file could not be activated atomically."
        );
    }

    return {};
}
