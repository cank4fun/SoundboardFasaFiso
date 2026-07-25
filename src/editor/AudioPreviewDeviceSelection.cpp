#include "editor/AudioPreviewDeviceSelection.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace
{
    std::string ToLowerAscii(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            }
        );
        return value;
    }
}

std::string NormalizeAudioPreviewDeviceRequest(
    const std::string_view request
)
{
    const std::size_t first = request.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }

    const std::size_t last = request.find_last_not_of(" \t\r\n");
    return ToLowerAscii(std::string{request.substr(first, last - first + 1U)});
}

AudioPreviewDeviceRequestKind ClassifyAudioPreviewDeviceRequest(
    const std::string_view request
)
{
    const std::string normalized =
        NormalizeAudioPreviewDeviceRequest(request);

    if (normalized.empty() || normalized == "default" ||
        normalized == "varsayilan" || normalized == "varsayılan")
    {
        return AudioPreviewDeviceRequestKind::Default;
    }

    if (normalized == "none" || normalized == "off" ||
        normalized == "disabled" || normalized == "kapali" ||
        normalized == "kapalı")
    {
        return AudioPreviewDeviceRequestKind::Disabled;
    }

    return AudioPreviewDeviceRequestKind::Named;
}

AudioPreviewDeviceMatchResult FindAudioPreviewDeviceMatch(
    const std::string_view request,
    const std::span<const std::string> deviceNames
)
{
    const std::string normalizedRequest =
        NormalizeAudioPreviewDeviceRequest(request);
    if (normalizedRequest.empty())
    {
        return {};
    }

    std::vector<std::size_t> exactMatches;
    std::vector<std::size_t> partialMatches;

    for (std::size_t index = 0U; index < deviceNames.size(); ++index)
    {
        const std::string normalizedName =
            ToLowerAscii(deviceNames[index]);
        if (normalizedName == normalizedRequest)
        {
            exactMatches.push_back(index);
        }
        else if (normalizedName.find(normalizedRequest) !=
            std::string::npos)
        {
            partialMatches.push_back(index);
        }
    }

    const std::vector<std::size_t>& matches = exactMatches.empty()
        ? partialMatches
        : exactMatches;

    if (matches.empty())
    {
        return {};
    }

    if (matches.size() != 1U)
    {
        return {
            AudioPreviewDeviceMatchStatus::Ambiguous,
            std::nullopt
        };
    }

    return {
        AudioPreviewDeviceMatchStatus::Found,
        matches.front()
    };
}
