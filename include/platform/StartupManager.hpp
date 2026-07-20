#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <filesystem>

class StartupManager
{
public:
    static LSTATUS SetEnabled(
        bool enabled,
        const std::filesystem::path& executablePath
    );
};
