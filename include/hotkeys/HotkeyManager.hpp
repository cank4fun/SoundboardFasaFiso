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

    // Sonuc:
    //  > 0 : Basılan hotkey ID'si
    //    0 : Sure doldu, hotkey basilmedi
    //   -1 : Windows mesaj dongusu kapandi veya hata verdi
    int WaitForPress(
        unsigned int timeoutMilliseconds,
        HWND dialogWindow = nullptr,
        HACCEL acceleratorTable = nullptr
    );

    void UnregisterAll();

private:
    std::vector<int> registeredIds_;
};
