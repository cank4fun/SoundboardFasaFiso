#include "platform/DebugConsole.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ResourceIds.h"

#include <Windows.h>

#include <algorithm>
#include <mutex>

namespace
{
    std::mutex ConsoleMutex;
    HANDLE ConsoleOutputHandle = INVALID_HANDLE_VALUE;
    HANDLE ConsoleErrorHandle = INVALID_HANDLE_VALUE;

    HANDLE OpenConsoleOutput()
    {
        return CreateFileW(
            L"CONOUT$",
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
    }

    void ApplyApplicationIcon(const HWND window)
    {
        if (window == nullptr)
        {
            return;
        }

        const HINSTANCE instance = GetModuleHandleW(nullptr);

        if (instance == nullptr)
        {
            return;
        }

        const auto largeIcon = static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_DEFAULTCOLOR | LR_SHARED
        ));

        const auto smallIcon = static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR | LR_SHARED
        ));

        if (largeIcon != nullptr)
        {
            SendMessageW(
                window,
                WM_SETICON,
                ICON_BIG,
                reinterpret_cast<LPARAM>(largeIcon)
            );
        }

        if (smallIcon != nullptr)
        {
            SendMessageW(
                window,
                WM_SETICON,
                ICON_SMALL,
                reinterpret_cast<LPARAM>(smallIcon)
            );
        }
    }
}

bool DebugConsole::EnsureCreated()
{
    std::scoped_lock lock{ConsoleMutex};

    if (GetConsoleWindow() != nullptr)
    {
        return true;
    }

    if (AllocConsole() == FALSE)
    {
        return false;
    }

    SetConsoleTitleW(L"SoundBoardFasaFiso Debug Console");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    ConsoleOutputHandle = OpenConsoleOutput();
    ConsoleErrorHandle = OpenConsoleOutput();

    if (ConsoleOutputHandle != INVALID_HANDLE_VALUE)
    {
        SetStdHandle(STD_OUTPUT_HANDLE, ConsoleOutputHandle);
    }

    if (ConsoleErrorHandle != INVALID_HANDLE_VALUE)
    {
        SetStdHandle(STD_ERROR_HANDLE, ConsoleErrorHandle);
    }

    const HWND consoleWindow = GetConsoleWindow();
    ApplyApplicationIcon(consoleWindow);

    if (consoleWindow != nullptr)
    {
        const HMENU systemMenu = GetSystemMenu(consoleWindow, FALSE);

        if (systemMenu != nullptr)
        {
            DeleteMenu(systemMenu, SC_CLOSE, MF_BYCOMMAND);
            DrawMenuBar(consoleWindow);
        }
    }

    return true;
}

bool DebugConsole::Show()
{
    if (!EnsureCreated())
    {
        return false;
    }

    const HWND consoleWindow = GetConsoleWindow();

    if (consoleWindow == nullptr)
    {
        return false;
    }

    ShowWindow(consoleWindow, SW_SHOW);
    SetForegroundWindow(consoleWindow);
    return true;
}

void DebugConsole::Hide()
{
    const HWND consoleWindow = GetConsoleWindow();

    if (consoleWindow != nullptr)
    {
        ShowWindow(consoleWindow, SW_HIDE);
    }
}

bool DebugConsole::ToggleVisibility()
{
    if (IsVisible())
    {
        Hide();
        return true;
    }

    return Show();
}

bool DebugConsole::IsVisible()
{
    const HWND consoleWindow = GetConsoleWindow();

    return consoleWindow != nullptr &&
        IsWindowVisible(consoleWindow) != FALSE;
}

std::size_t DebugConsole::Write(
    const char* const text,
    const std::size_t length,
    const bool errorStream
)
{
    if (text == nullptr || length == 0)
    {
        return 0;
    }

    std::scoped_lock lock{ConsoleMutex};

    if (GetConsoleWindow() == nullptr)
    {
        return length;
    }

    const HANDLE handle = errorStream
        ? ConsoleErrorHandle
        : ConsoleOutputHandle;

    if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
    {
        return length;
    }

    std::size_t totalWritten = 0;

    while (totalWritten < length)
    {
        const std::size_t remaining = length - totalWritten;
        const DWORD chunkSize = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(MAXDWORD)
        ));

        DWORD written = 0;

        if (WriteFile(
            handle,
            text + totalWritten,
            chunkSize,
            &written,
            nullptr
        ) == FALSE)
        {
            break;
        }

        if (written == 0)
        {
            break;
        }

        totalWritten += written;
    }

    return totalWritten == 0 ? length : totalWritten;
}
