#include "update/UpdateChecker.hpp"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr wchar_t GitHubHost[] = L"api.github.com";
    constexpr wchar_t LatestReleasePath[] =
        L"/repos/cank4fun/SoundboardFasaFiso/releases/latest";
    constexpr std::size_t MaximumResponseBytes = 1024U * 1024U;
    constexpr std::size_t MaximumReleaseUrlCharacters = 2048U;
    constexpr std::string_view OfficialReleaseUrlPrefix =
        "https://github.com/cank4fun/SoundboardFasaFiso/releases/";

    class InternetHandle
    {
    public:
        InternetHandle() = default;
        explicit InternetHandle(HINTERNET handle) noexcept
            : handle_(handle)
        {
        }

        ~InternetHandle()
        {
            Reset();
        }

        InternetHandle(const InternetHandle&) = delete;
        InternetHandle& operator=(const InternetHandle&) = delete;

        InternetHandle(InternetHandle&& other) noexcept
            : handle_(std::exchange(other.handle_, nullptr))
        {
        }

        InternetHandle& operator=(InternetHandle&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                handle_ = std::exchange(other.handle_, nullptr);
            }

            return *this;
        }

        HINTERNET Get() const noexcept
        {
            return handle_;
        }

        explicit operator bool() const noexcept
        {
            return handle_ != nullptr;
        }

    private:
        void Reset() noexcept
        {
            if (handle_ != nullptr)
            {
                WinHttpCloseHandle(handle_);
                handle_ = nullptr;
            }
        }

        HINTERNET handle_ = nullptr;
    };

    struct ParsedVersion
    {
        std::array<unsigned int, 3> core{};
        std::vector<std::string> prerelease;
    };

    std::string Trim(std::string_view value)
    {
        const std::size_t first = value.find_first_not_of(" \t\r\n");

        if (first == std::string_view::npos)
        {
            return {};
        }

        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return std::string{value.substr(first, last - first + 1)};
    }

    bool IsNumeric(std::string_view value)
    {
        return !value.empty() && std::all_of(
            value.begin(),
            value.end(),
            [](const unsigned char character)
            {
                return std::isdigit(character) != 0;
            }
        );
    }

    bool IsOfficialReleaseUrl(const std::string_view value)
    {
        return value.size() <= MaximumReleaseUrlCharacters &&
            value.starts_with(OfficialReleaseUrlPrefix) &&
            std::all_of(
                value.begin(),
                value.end(),
                [](const unsigned char character)
                {
                    return character > 0x20U && character < 0x7FU;
                }
            );
    }

    std::optional<unsigned int> ParseUnsigned(std::string_view value)
    {
        if (!IsNumeric(value))
        {
            return std::nullopt;
        }

        unsigned int number = 0;
        const auto [pointer, error] = std::from_chars(
            value.data(),
            value.data() + value.size(),
            number
        );

        if (error != std::errc{} || pointer != value.data() + value.size())
        {
            return std::nullopt;
        }

        return number;
    }

    std::optional<ParsedVersion> ParseVersion(std::string_view rawVersion)
    {
        std::string version = Trim(rawVersion);

        if (version.empty())
        {
            return std::nullopt;
        }

        if (version.front() == 'v' || version.front() == 'V')
        {
            version.erase(version.begin());
        }

        const std::size_t metadataPosition = version.find('+');

        if (metadataPosition != std::string::npos)
        {
            version.erase(metadataPosition);
        }

        std::string prereleaseText;
        const std::size_t prereleasePosition = version.find('-');

        if (prereleasePosition != std::string::npos)
        {
            prereleaseText = version.substr(prereleasePosition + 1);
            version.erase(prereleasePosition);
        }

        if (version.empty())
        {
            return std::nullopt;
        }

        ParsedVersion parsed;
        std::size_t componentIndex = 0;
        std::size_t componentStart = 0;

        while (componentStart <= version.size())
        {
            if (componentIndex >= parsed.core.size())
            {
                return std::nullopt;
            }

            const std::size_t separator = version.find('.', componentStart);
            const std::size_t length = separator == std::string::npos
                ? version.size() - componentStart
                : separator - componentStart;

            if (length == 0)
            {
                return std::nullopt;
            }

            const auto component = ParseUnsigned(
                std::string_view{version}.substr(componentStart, length)
            );

            if (!component.has_value())
            {
                return std::nullopt;
            }

            parsed.core[componentIndex++] = *component;

            if (separator == std::string::npos)
            {
                break;
            }

            componentStart = separator + 1;
        }

        if (!prereleaseText.empty())
        {
            std::size_t identifierStart = 0;

            while (identifierStart <= prereleaseText.size())
            {
                const std::size_t separator = prereleaseText.find(
                    '.',
                    identifierStart
                );
                const std::size_t length = separator == std::string::npos
                    ? prereleaseText.size() - identifierStart
                    : separator - identifierStart;

                if (length == 0)
                {
                    return std::nullopt;
                }

                parsed.prerelease.push_back(
                    prereleaseText.substr(identifierStart, length)
                );

                if (separator == std::string::npos)
                {
                    break;
                }

                identifierStart = separator + 1;
            }
        }

        return parsed;
    }

    int CompareVersions(
        const ParsedVersion& first,
        const ParsedVersion& second
    )
    {
        for (std::size_t index = 0; index < first.core.size(); ++index)
        {
            if (first.core[index] != second.core[index])
            {
                return first.core[index] > second.core[index] ? 1 : -1;
            }
        }

        if (first.prerelease.empty() != second.prerelease.empty())
        {
            return first.prerelease.empty() ? 1 : -1;
        }

        const std::size_t commonCount = std::min(
            first.prerelease.size(),
            second.prerelease.size()
        );

        for (std::size_t index = 0; index < commonCount; ++index)
        {
            const std::string_view firstPart = first.prerelease[index];
            const std::string_view secondPart = second.prerelease[index];
            const bool firstNumeric = IsNumeric(firstPart);
            const bool secondNumeric = IsNumeric(secondPart);

            if (firstNumeric && secondNumeric)
            {
                const auto firstNumber = ParseUnsigned(firstPart);
                const auto secondNumber = ParseUnsigned(secondPart);

                if (firstNumber != secondNumber)
                {
                    return *firstNumber > *secondNumber ? 1 : -1;
                }
            }
            else if (firstNumeric != secondNumeric)
            {
                return firstNumeric ? -1 : 1;
            }
            else if (firstPart != secondPart)
            {
                return firstPart > secondPart ? 1 : -1;
            }
        }

        if (first.prerelease.size() == second.prerelease.size())
        {
            return 0;
        }

        return first.prerelease.size() > second.prerelease.size() ? 1 : -1;
    }

    std::optional<std::string> ExtractJsonString(
        const std::string& json,
        const std::string_view key
    )
    {
        const std::string quotedKey = '"' + std::string{key} + '"';
        const std::size_t keyPosition = json.find(quotedKey);

        if (keyPosition == std::string::npos)
        {
            return std::nullopt;
        }

        const std::size_t colonPosition = json.find(
            ':',
            keyPosition + quotedKey.size()
        );

        if (colonPosition == std::string::npos)
        {
            return std::nullopt;
        }

        const std::size_t quotePosition = json.find('"', colonPosition + 1);

        if (quotePosition == std::string::npos)
        {
            return std::nullopt;
        }

        std::string value;
        bool escaped = false;

        for (std::size_t index = quotePosition + 1; index < json.size(); ++index)
        {
            const char character = json[index];

            if (escaped)
            {
                switch (character)
                {
                    case '"': value.push_back('"'); break;
                    case '\\': value.push_back('\\'); break;
                    case '/': value.push_back('/'); break;
                    case 'b': value.push_back('\b'); break;
                    case 'f': value.push_back('\f'); break;
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    default: return std::nullopt;
                }

                escaped = false;
                continue;
            }

            if (character == '\\')
            {
                escaped = true;
                continue;
            }

            if (character == '"')
            {
                return value;
            }

            value.push_back(character);
        }

        return std::nullopt;
    }

    std::string WindowsErrorMessage(const DWORD errorCode)
    {
        return "Windows error " + std::to_string(errorCode);
    }

    UpdateCheckResult Failure(std::string message)
    {
        UpdateCheckResult result;
        result.status = UpdateCheckStatus::Failed;
        result.errorMessage = std::move(message);
        return result;
    }
}

UpdateCheckResult UpdateChecker::CheckLatestRelease(
    const std::string_view currentVersion
)
{
    std::wstring userAgent = L"SoundBoardFasaFiso/";

    for (const char character : currentVersion)
    {
        userAgent.push_back(static_cast<wchar_t>(
            static_cast<unsigned char>(character)
        ));
    }

    InternetHandle session{WinHttpOpen(
        userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    )};

    if (!session)
    {
        return Failure(WindowsErrorMessage(GetLastError()));
    }

    WinHttpSetTimeouts(session.Get(), 5000, 5000, 5000, 5000);

    InternetHandle connection{WinHttpConnect(
        session.Get(),
        GitHubHost,
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    )};

    if (!connection)
    {
        return Failure(WindowsErrorMessage(GetLastError()));
    }

    InternetHandle request{WinHttpOpenRequest(
        connection.Get(),
        L"GET",
        LatestReleasePath,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    )};

    if (!request)
    {
        return Failure(WindowsErrorMessage(GetLastError()));
    }

    constexpr wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2026-03-10\r\n";

    if (WinHttpSendRequest(
            request.Get(),
            headers,
            static_cast<DWORD>(-1L),
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0
        ) == FALSE)
    {
        return Failure(WindowsErrorMessage(GetLastError()));
    }

    if (WinHttpReceiveResponse(request.Get(), nullptr) == FALSE)
    {
        return Failure(WindowsErrorMessage(GetLastError()));
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);

    if (WinHttpQueryHeaders(
            request.Get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX
        ) == FALSE)
    {
        return Failure(WindowsErrorMessage(GetLastError()));
    }

    if (statusCode != HTTP_STATUS_OK)
    {
        return Failure("GitHub API returned HTTP " + std::to_string(statusCode));
    }

    std::string response;

    while (true)
    {
        DWORD availableBytes = 0;

        if (WinHttpQueryDataAvailable(
                request.Get(),
                &availableBytes
            ) == FALSE)
        {
            return Failure(WindowsErrorMessage(GetLastError()));
        }

        if (availableBytes == 0)
        {
            break;
        }

        if (response.size() + availableBytes > MaximumResponseBytes)
        {
            return Failure("GitHub API response exceeded the safety limit");
        }

        const std::size_t oldSize = response.size();
        response.resize(oldSize + availableBytes);

        DWORD bytesRead = 0;

        if (WinHttpReadData(
                request.Get(),
                response.data() + oldSize,
                availableBytes,
                &bytesRead
            ) == FALSE)
        {
            return Failure(WindowsErrorMessage(GetLastError()));
        }

        response.resize(oldSize + bytesRead);
    }

    const auto latestVersion = ExtractJsonString(response, "tag_name");
    const auto releaseUrl = ExtractJsonString(response, "html_url");

    if (!latestVersion.has_value() || !releaseUrl.has_value())
    {
        return Failure("GitHub API response did not contain release metadata");
    }

    const auto parsedLatestVersion = ParseVersion(*latestVersion);
    const auto parsedCurrentVersion = ParseVersion(currentVersion);

    if (!parsedLatestVersion.has_value() ||
        !parsedCurrentVersion.has_value())
    {
        return Failure("GitHub release metadata contained an invalid version");
    }

    if (!IsOfficialReleaseUrl(*releaseUrl))
    {
        return Failure("GitHub release metadata contained an unexpected URL");
    }

    UpdateCheckResult result;
    result.latestVersion = *latestVersion;
    result.releaseUrl = *releaseUrl;
    result.status =
        CompareVersions(*parsedLatestVersion, *parsedCurrentVersion) > 0
            ? UpdateCheckStatus::UpdateAvailable
            : UpdateCheckStatus::UpToDate;
    return result;
}
