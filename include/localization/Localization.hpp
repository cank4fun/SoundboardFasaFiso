#pragma once

#include <optional>
#include <string_view>

enum class Language
{
    Turkish,
    English
};

namespace Localization
{
    void SetLanguage(Language language) noexcept;
    Language GetLanguage() noexcept;

    const char* Text(
        const char* turkish,
        const char* english
    ) noexcept;

    const wchar_t* Text(
        const wchar_t* turkish,
        const wchar_t* english
    ) noexcept;

    std::optional<Language> ParseLanguage(
        std::string_view value
    );

    std::string_view LanguageCode(Language language) noexcept;
}
