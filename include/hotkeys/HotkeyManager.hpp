#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <vector>

class HotkeyManager
{
public:
    HotkeyManager() = default;
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    bool Register(
        int id,
        unsigned int modifiers,
        unsigned int virtualKey
    );

    // Positive: hotkey ID; zero: timeout; negative: message-loop failure.
    int WaitForPress(
        unsigned int timeoutMilliseconds,
        HWND dialogWindow = nullptr,
        HACCEL acceleratorTable = nullptr
    );

    void UnregisterAll();

private:
    std::vector<int> registeredIds_;
};
