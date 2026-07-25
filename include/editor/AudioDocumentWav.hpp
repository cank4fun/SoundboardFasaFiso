#pragma once

#include "editor/AudioDocument.hpp"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>

enum class AudioWavFileError
{
    None,
    Cancelled,
    InvalidPath,
    FileNotFound,
    OpenFailed,
    ReadFailed,
    InvalidContainer,
    UnsupportedFormat,
    InvalidAudioData,
    FileTooLarge,
    DestinationExists,
    WriteFailed,
    CommitFailed
};

enum class AudioWavSaveMode
{
    CreateNew,
    ReplaceExisting
};

struct AudioWavLoadResult final
{
    std::optional<AudioDocument> document;
    AudioWavFileError error = AudioWavFileError::None;
    std::string errorMessage;

    [[nodiscard]] bool Succeeded() const noexcept;
};

struct AudioWavSaveResult final
{
    AudioWavFileError error = AudioWavFileError::None;
    std::string errorMessage;

    [[nodiscard]] bool Succeeded() const noexcept;
};

class AudioDocumentWav final
{
public:
    static AudioWavLoadResult Load(
        const std::filesystem::path& filePath,
        const std::atomic_bool* cancellationRequested = nullptr
    );

    static AudioWavSaveResult SavePcm16(
        const AudioDocument& document,
        const std::filesystem::path& filePath,
        AudioWavSaveMode saveMode = AudioWavSaveMode::CreateNew
    );
};
