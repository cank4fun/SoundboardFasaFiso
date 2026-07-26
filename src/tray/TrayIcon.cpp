#include "tray/TrayIcon.hpp"

#include "localization/Localization.hpp"
#include "platform/DebugConsole.hpp"
#include "ResourceIds.h"

#include <cwchar>
#include <iostream>

namespace
{
    constexpr wchar_t TrayWindowClassName[] =
        L"SoundBoardFasaFisoTrayWindow";

    HICON LoadApplicationIcon(
        const HINSTANCE instance,
        const int width,
        const int height
    )
    {
        return static_cast<HICON>(
            LoadImageW(
                instance,
                MAKEINTRESOURCEW(IDI_APP_ICON),
                IMAGE_ICON,
                width,
                height,
                LR_DEFAULTCOLOR | LR_SHARED
            )
        );
    }
}

TrayIcon::~TrayIcon()
{
    Shutdown();
}

bool TrayIcon::Initialize(
    const std::wstring& tooltip,
    const TrayCommandIds& commandIds
)
{
    Shutdown();

    instance_ = GetModuleHandleW(nullptr);
    mainThreadId_ = GetCurrentThreadId();
    taskbarCreatedMessage_ = RegisterWindowMessageW(
        L"TaskbarCreated"
    );
    commandIds_ = commandIds;

    if (instance_ == nullptr)
    {
        std::cerr
            << Localization::Text(
                "Tray icon için uygulama modülü alınamadı. Windows hata kodu: ",
                "The application module for the tray icon could not be obtained. Windows error code: "
            )
            << GetLastError()
            << '\n';

        return false;
    }

    const HICON largeIcon = LoadApplicationIcon(
        instance_,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON)
    );

    const HICON smallIcon = LoadApplicationIcon(
        instance_,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON)
    );

    if (largeIcon == nullptr || smallIcon == nullptr)
    {
        std::cerr
            << Localization::Text(
                "FasaFiso uygulama ikonu yüklenemedi. Windows hata kodu: ",
                "The FasaFiso application icon could not be loaded. Windows error code: "
            )
            << GetLastError()
            << '\n';

        return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &TrayIcon::WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = largeIcon;
    windowClass.hIconSm = smallIcon;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = TrayWindowClassName;

    const ATOM classAtom = RegisterClassExW(&windowClass);

    if (classAtom == 0)
    {
        const DWORD errorCode = GetLastError();

        if (errorCode != ERROR_CLASS_ALREADY_EXISTS)
        {
            std::cerr
                << Localization::Text(
                    "Tray icon pencere sınıfı oluşturulamadı. Windows hata kodu: ",
                    "The tray icon window class could not be created. Windows error code: "
                )
                << errorCode
                << '\n';

            return false;
        }
    }
    else
    {
        classRegistered_ = true;
    }

    window_ = CreateWindowExW(
        0,
        TrayWindowClassName,
        L"SoundBoardFasaFiso Tray",
        WS_OVERLAPPED,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance_,
        this
    );

    if (window_ == nullptr)
    {
        std::cerr
            << Localization::Text(
                "Tray icon gizli penceresi oluşturulamadı. Windows hata kodu: ",
                "The hidden tray icon window could not be created. Windows error code: "
            )
            << GetLastError()
            << '\n';

        Shutdown();
        return false;
    }

    iconData_ = {};
    iconData_.cbSize = sizeof(iconData_);
    iconData_.hWnd = window_;
    iconData_.uID = 1;
    iconData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    iconData_.uCallbackMessage = TrayCallbackMessage;
    iconData_.hIcon = smallIcon;

    if (iconData_.hIcon == nullptr)
    {
        std::cerr
            << Localization::Text(
                "Tray icon simgesi yüklenemedi. Windows hata kodu: ",
                "The tray icon image could not be loaded. Windows error code: "
            )
            << GetLastError()
            << '\n';

        Shutdown();
        return false;
    }

    wcsncpy_s(
        iconData_.szTip,
        tooltip.c_str(),
        _TRUNCATE
    );

    if (!AddNotificationIcon())
    {
        std::cerr
            << Localization::Text(
                "Tray icon görev çubuğuna eklenemedi. Windows hata kodu: ",
                "The tray icon could not be added to the taskbar. Windows error code: "
            )
            << GetLastError()
            << '\n';

        Shutdown();
        return false;
    }

    SendMessageW(
        window_,
        WM_SETICON,
        ICON_BIG,
        reinterpret_cast<LPARAM>(largeIcon)
    );

    SendMessageW(
        window_,
        WM_SETICON,
        ICON_SMALL,
        reinterpret_cast<LPARAM>(smallIcon)
    );

    const HWND consoleWindow = GetConsoleWindow();

    if (consoleWindow != nullptr)
    {
        SendMessageW(
            consoleWindow,
            WM_SETICON,
            ICON_BIG,
            reinterpret_cast<LPARAM>(largeIcon)
        );

        SendMessageW(
            consoleWindow,
            WM_SETICON,
            ICON_SMALL,
            reinterpret_cast<LPARAM>(smallIcon)
        );
    }

    return true;
}

void TrayIcon::Shutdown()
{
    if (iconAdded_ && iconData_.hWnd != nullptr)
    {
        Shell_NotifyIconW(NIM_DELETE, &iconData_);
    }

    iconAdded_ = false;

    iconData_ = {};

    if (window_ != nullptr)
    {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    if (classRegistered_ && instance_ != nullptr)
    {
        UnregisterClassW(TrayWindowClassName, instance_);
    }

    classRegistered_ = false;
    instance_ = nullptr;
    mainThreadId_ = 0;
    taskbarCreatedMessage_ = 0;
    commandIds_ = {};
}

bool TrayIcon::IsConsoleVisible() const
{
    return DebugConsole::IsVisible();
}

LRESULT CALLBACK TrayIcon::WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam
)
{
    TrayIcon* trayIcon = reinterpret_cast<TrayIcon*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );

    if (message == WM_NCCREATE)
    {
        const auto* createData =
            reinterpret_cast<const CREATESTRUCTW*>(lParam);

        trayIcon = static_cast<TrayIcon*>(
            createData->lpCreateParams
        );

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(trayIcon)
        );
    }

    if (trayIcon != nullptr)
    {
        return trayIcon->HandleWindowMessage(
            window,
            message,
            wParam,
            lParam
        );
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayIcon::HandleWindowMessage(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam
)
{
    (void)wParam;

    if (
        taskbarCreatedMessage_ != 0 &&
        message == taskbarCreatedMessage_
    )
    {
        AddNotificationIcon();
        return 0;
    }

    if (message == TrayCallbackMessage)
    {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
        {
            ShowContextMenu();
            return 0;
        }

        if (lParam == WM_LBUTTONDBLCLK)
        {
            PostApplicationCommand(commandIds_.controlPanel);
            return 0;
        }
    }

    if (message == WM_DESTROY)
    {
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool TrayIcon::AddNotificationIcon()
{
    if (iconData_.hWnd == nullptr || iconData_.hIcon == nullptr)
    {
        iconAdded_ = false;
        return false;
    }

    iconAdded_ =
        Shell_NotifyIconW(NIM_ADD, &iconData_) != FALSE;

    return iconAdded_;
}

void TrayIcon::ShowContextMenu()
{
    if (window_ == nullptr)
    {
        return;
    }

    HMENU menu = CreatePopupMenu();

    if (menu == nullptr)
    {
        return;
    }

    AppendMenuW(
        menu,
        MF_STRING | MF_DEFAULT,
        MenuControlPanel,
        Localization::Text(L"Kontrol panelini aç", L"Open control panel")
    );

    AppendMenuW(
        menu,
        MF_STRING,
        MenuReload,
        Localization::Text(L"Config'i yeniden yükle", L"Reload config")
    );

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(
        menu,
        MF_STRING,
        MenuStopAll,
        Localization::Text(L"Tüm sesleri durdur", L"Stop all sounds")
    );

    AppendMenuW(
        menu,
        MF_STRING,
        MenuOutputMute,
        Localization::Text(L"Ana çıkışı mute/unmute", L"Mute/unmute main output")
    );

    AppendMenuW(
        menu,
        MF_STRING,
        MenuMonitorMute,
        Localization::Text(L"Monitör çıkışını mute/unmute", L"Mute/unmute monitor output")
    );

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(
        menu,
        MF_STRING,
        MenuToggleConsole,
        IsConsoleVisible()
            ? Localization::Text(L"Hata ayıklama konsolunu gizle", L"Hide debug console")
            : Localization::Text(L"Hata ayıklama konsolunu aç", L"Open debug console")
    );

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(
        menu,
        MF_STRING,
        MenuExit,
        Localization::Text(L"Programı kapat", L"Exit")
    );

    POINT cursorPosition{};

    if (GetCursorPos(&cursorPosition) == FALSE)
    {
        DestroyMenu(menu);
        return;
    }

    SetForegroundWindow(window_);

    const UINT selectedCommand = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        cursorPosition.x,
        cursorPosition.y,
        0,
        window_,
        nullptr
    );

    DestroyMenu(menu);
    PostMessageW(window_, WM_NULL, 0, 0);

    switch (selectedCommand)
    {
        case MenuControlPanel:
            PostApplicationCommand(commandIds_.controlPanel);
            break;

        case MenuReload:
            PostApplicationCommand(commandIds_.reload);
            break;

        case MenuStopAll:
            PostApplicationCommand(commandIds_.stop);
            break;

        case MenuOutputMute:
            PostApplicationCommand(commandIds_.outputMute);
            break;

        case MenuMonitorMute:
            PostApplicationCommand(commandIds_.monitorMute);
            break;

        case MenuToggleConsole:
            PostApplicationCommand(commandIds_.toggleConsole);
            break;

        case MenuExit:
            PostApplicationCommand(commandIds_.exit);
            break;

        default:
            break;
    }
}

void TrayIcon::PostApplicationCommand(const int commandId) const
{
    if (mainThreadId_ == 0 || commandId <= 0)
    {
        return;
    }

    PostThreadMessageW(
        mainThreadId_,
        WM_HOTKEY,
        static_cast<WPARAM>(commandId),
        0
    );
}
