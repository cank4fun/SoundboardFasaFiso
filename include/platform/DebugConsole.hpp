#pragma once

#include <cstddef>

class DebugConsole
{
public:
    DebugConsole() = delete;

    static bool Show();
    static void Hide();
    static bool ToggleVisibility();
    static bool IsVisible();

    static std::size_t Write(
        const char* text,
        std::size_t length,
        bool errorStream
    );

private:
    static bool EnsureCreated();
};
