#pragma once

#include <filesystem>
#include <string>
#include <string_view>

inline std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();

    return {
        reinterpret_cast<const char*>(utf8.data()),
        utf8.size()
    };
}

inline std::filesystem::path PathFromUtf8(const std::string_view utf8)
{
    const std::u8string value{
        reinterpret_cast<const char8_t*>(utf8.data()),
        utf8.size()
    };

    return std::filesystem::path{value};
}
