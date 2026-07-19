#pragma once

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
    int WaitForPress(unsigned int timeoutMilliseconds);

    void UnregisterAll();

private:
    std::vector<int> registeredIds_;
};
