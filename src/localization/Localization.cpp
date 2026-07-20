#include "localization/Localization.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <string>

namespace
{
    std::atomic<Language> currentLanguage{Language::Turkish};

    std::string Normalize(std::string_view value)
    {
        const std::size_t first = value.find_first_not_of(" \t\r\n");

        if (first == std::string_view::npos)
        {
            return {};
        }

        const std::size_t last = value.find_last_not_of(" \t\r\n");
        std::string normalized{value.substr(first, last - first + 1)};

        std::transform(
            normalized.begin(),
            normalized.end(),
            normalized.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            }
        );

        return normalized;
    }
}

void Localization::SetLanguage(const Language language) noexcept
{
    currentLanguage.store(language, std::memory_order_relaxed);
}

Language Localization::GetLanguage() noexcept
{
    return currentLanguage.load(std::memory_order_relaxed);
}

const char* Localization::Text(
    const char* const turkish,
    const char* const english
) noexcept
{
    return GetLanguage() == Language::English
        ? english
        : turkish;
}

const wchar_t* Localization::Text(
    const wchar_t* const turkish,
    const wchar_t* const english
) noexcept
{
    return GetLanguage() == Language::English
        ? english
        : turkish;
}

std::optional<Language> Localization::ParseLanguage(
    const std::string_view value
)
{
    const std::string normalized = Normalize(value);

    if (normalized == "tr" || normalized == "turkish" ||
        normalized == "turkce")
    {
        return Language::Turkish;
    }

    if (normalized == "en" || normalized == "english")
    {
        return Language::English;
    }

    return std::nullopt;
}

std::string_view Localization::LanguageCode(
    const Language language
) noexcept
{
    return language == Language::English ? "en" : "tr";
}
