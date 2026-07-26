#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

enum class AudioPreviewDeviceRequestKind
{
    Default,
    Disabled,
    Named
};

enum class AudioPreviewDeviceMatchStatus
{
    Found,
    NotFound,
    Ambiguous
};

struct AudioPreviewDeviceMatchResult final
{
    AudioPreviewDeviceMatchStatus status =
        AudioPreviewDeviceMatchStatus::NotFound;
    std::optional<std::size_t> index;
};

[[nodiscard]] std::string NormalizeAudioPreviewDeviceRequest(
    std::string_view request
);

[[nodiscard]] AudioPreviewDeviceRequestKind
ClassifyAudioPreviewDeviceRequest(std::string_view request);

[[nodiscard]] AudioPreviewDeviceMatchResult FindAudioPreviewDeviceMatch(
    std::string_view request,
    std::span<const std::string> deviceNames
);
