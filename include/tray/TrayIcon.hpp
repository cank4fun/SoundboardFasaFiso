#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <shellapi.h>

#include <string>

struct TrayCommandIds
{
    int controlPanel = 0;
    int stop = 0;
    int outputMute = 0;
    int monitorMute = 0;
    int reload = 0;
    int toggleConsole = 0;
    int exit = 0;
};

class TrayIcon
{
public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Initialize(
        const std::wstring& tooltip,
        const TrayCommandIds& commandIds
    );

    void Shutdown();

    bool ToggleConsoleVisibility();
    bool IsConsoleVisible() const;

private:
    static constexpr UINT TrayCallbackMessage = WM_APP + 1;

    static constexpr UINT MenuControlPanel = 1;
    static constexpr UINT MenuReload = 2;
    static constexpr UINT MenuStopAll = 3;
    static constexpr UINT MenuOutputMute = 4;
    static constexpr UINT MenuMonitorMute = 5;
    static constexpr UINT MenuToggleConsole = 6;
    static constexpr UINT MenuExit = 7;

    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    LRESULT HandleWindowMessage(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    bool AddNotificationIcon();
    void ShowContextMenu();
    void PostApplicationCommand(int commandId) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    NOTIFYICONDATAW iconData_{};
    DWORD mainThreadId_ = 0;
    UINT taskbarCreatedMessage_ = 0;
    TrayCommandIds commandIds_{};
    bool classRegistered_ = false;
    bool iconAdded_ = false;
};
