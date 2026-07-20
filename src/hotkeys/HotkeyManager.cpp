#include "hotkeys/HotkeyManager.hpp"
#include "localization/Localization.hpp"

#include <Windows.h>

#include <iostream>

HotkeyManager::~HotkeyManager()
{
    UnregisterAll();
}

bool HotkeyManager::Register(
    const int id,
    const unsigned int modifiers,
    const unsigned int virtualKey
)
{
    const BOOL success = RegisterHotKey(
        nullptr,
        id,
        static_cast<UINT>(modifiers) | MOD_NOREPEAT,
        static_cast<UINT>(virtualKey)
    );

    if (success == FALSE)
    {
        const DWORD errorCode = GetLastError();

        std::cerr
            << Localization::Text(
                "Hotkey kaydedilemedi. ID: ",
                "Hotkey could not be registered. ID: "
            )
            << id
            << Localization::Text(
                ", Windows hata kodu: ",
                ", Windows error code: "
            )
            << errorCode
            << '\n';

        if (errorCode == ERROR_HOTKEY_ALREADY_REGISTERED)
        {
            std::cerr
                << Localization::Text(
                    "Bu tuş kombinasyonu başka bir program veya hotkey tarafından kullanılıyor.\n",
                    "This key combination is already used by another program or hotkey.\n"
                );
        }

        return false;
    }

    registeredIds_.push_back(id);
    return true;
}

int HotkeyManager::WaitForPress(
    const unsigned int timeoutMilliseconds
)
{
    const DWORD waitResult = MsgWaitForMultipleObjectsEx(
        0,
        nullptr,
        static_cast<DWORD>(timeoutMilliseconds),
        QS_ALLINPUT,
        MWMO_INPUTAVAILABLE
    );

    if (waitResult == WAIT_TIMEOUT)
    {
        return 0;
    }

    if (waitResult == WAIT_FAILED)
    {
        std::cerr
            << Localization::Text(
                "Windows mesaj bekleme işlemi hata verdi: ",
                "Waiting for Windows messages failed: "
            )
            << GetLastError()
            << '\n';

        return -1;
    }

    MSG message{};

    while (PeekMessageW(
        &message,
        nullptr,
        0,
        0,
        PM_REMOVE
    ))
    {
        if (message.message == WM_QUIT)
        {
            return -1;
        }

        if (message.message == WM_HOTKEY)
        {
            return static_cast<int>(message.wParam);
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return 0;
}

void HotkeyManager::UnregisterAll()
{
    for (const int id : registeredIds_)
    {
        UnregisterHotKey(nullptr, id);
    }

    registeredIds_.clear();
}
