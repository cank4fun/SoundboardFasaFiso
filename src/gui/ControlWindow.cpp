#include "gui/ControlWindow.hpp"

#include "app/Version.hpp"
#include "config/Config.hpp"
#include "localization/Localization.hpp"
#include "ResourceIds.h"

#include <shellapi.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>

namespace
{
    constexpr wchar_t ControlWindowClassName[] =
        L"SoundBoardFasaFiso.ControlWindow";

    constexpr int BaseMargin = 16;
    constexpr int HeaderHeight = 28;
    constexpr int StatusHeight = 24;
    constexpr int AudioGroupHeight = 132;
    constexpr int ButtonHeight = 34;
    constexpr int ButtonGap = 8;

    std::wstring BuildVolumeText(const float volume)
    {
        return std::to_wstring(
            static_cast<int>(volume * 100.0f)
        ) + L"%";
    }
}

ControlWindow::~ControlWindow()
{
    Shutdown();
}

bool ControlWindow::Initialize(
    const Config& config,
    const std::filesystem::path& configPath,
    const std::filesystem::path& soundsFolder,
    const ControlWindowCommandIds& commandIds
)
{
    Shutdown();

    instance_ = GetModuleHandleW(nullptr);
    mainThreadId_ = GetCurrentThreadId();
    commandIds_ = commandIds;
    configPath_ = configPath;
    soundsFolder_ = soundsFolder;

    if (instance_ == nullptr)
    {
        return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &ControlWindow::WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR
    ));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR
    ));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = ControlWindowClassName;

    const ATOM classAtom = RegisterClassExW(&windowClass);

    if (classAtom == 0)
    {
        const DWORD errorCode = GetLastError();

        if (errorCode != ERROR_CLASS_ALREADY_EXISTS)
        {
            Shutdown();
            return false;
        }
    }
    else
    {
        classRegistered_ = true;
    }

    RECT windowRectangle{
        0,
        0,
        MinimumClientWidth,
        MinimumClientHeight
    };

    AdjustWindowRectEx(
        &windowRectangle,
        WS_OVERLAPPEDWINDOW,
        FALSE,
        0
    );

    window_ = CreateWindowExW(
        0,
        ControlWindowClassName,
        L"SoundBoardFasaFiso",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRectangle.right - windowRectangle.left,
        windowRectangle.bottom - windowRectangle.top,
        nullptr,
        nullptr,
        instance_,
        this
    );

    if (window_ == nullptr)
    {
        Shutdown();
        return false;
    }

    if (!CreateControls())
    {
        Shutdown();
        return false;
    }

    UpdateConfig(config);
    SetStatus(Localization::Text(
        L"Soundboard hazır.",
        L"Soundboard ready."
    ));

    RECT clientRectangle{};
    GetClientRect(window_, &clientRectangle);
    LayoutControls(
        clientRectangle.right - clientRectangle.left,
        clientRectangle.bottom - clientRectangle.top
    );

    return true;
}

void ControlWindow::Shutdown()
{
    if (window_ != nullptr)
    {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    if (classRegistered_ && instance_ != nullptr)
    {
        UnregisterClassW(ControlWindowClassName, instance_);
    }

    classRegistered_ = false;
    instance_ = nullptr;
    mainThreadId_ = 0;
    commandIds_ = {};
    configPath_.clear();
    soundsFolder_.clear();

    headerLabel_ = nullptr;
    statusCaption_ = nullptr;
    statusValue_ = nullptr;
    audioGroup_ = nullptr;
    outputLabel_ = nullptr;
    monitorLabel_ = nullptr;
    audioSettingsLabel_ = nullptr;
    languageLabel_ = nullptr;
    bindingsGroup_ = nullptr;
    bindingsList_ = nullptr;
    reloadButton_ = nullptr;
    stopButton_ = nullptr;
    outputMuteButton_ = nullptr;
    monitorMuteButton_ = nullptr;
    openConfigButton_ = nullptr;
    openSoundsButton_ = nullptr;
    consoleButton_ = nullptr;
    exitButton_ = nullptr;
}

void ControlWindow::Show()
{
    if (window_ == nullptr)
    {
        return;
    }

    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
}

void ControlWindow::Hide()
{
    if (window_ != nullptr)
    {
        ShowWindow(window_, SW_HIDE);
    }
}

bool ControlWindow::ToggleVisibility()
{
    if (window_ == nullptr)
    {
        return false;
    }

    if (IsVisible())
    {
        Hide();
    }
    else
    {
        Show();
    }

    return true;
}

bool ControlWindow::IsVisible() const
{
    return window_ != nullptr &&
        IsWindowVisible(window_) != FALSE;
}

void ControlWindow::UpdateConfig(const Config& config)
{
    if (window_ == nullptr)
    {
        return;
    }

    RefreshLocalizedText();

    SetControlText(
        outputLabel_,
        std::wstring{Localization::Text(
            L"Ana çıkış: ",
            L"Main output: "
        )} + Utf8ToWide(config.GetOutputDevice()) +
        L" (" + BuildVolumeText(config.GetOutputVolume()) + L")"
    );

    SetControlText(
        monitorLabel_,
        std::wstring{Localization::Text(
            L"Monitör çıkışı: ",
            L"Monitor output: "
        )} + Utf8ToWide(config.GetMonitorDevice()) +
        L" (" + BuildVolumeText(config.GetMonitorVolume()) + L")"
    );

    const std::wstring sampleRate =
        config.GetAudioSampleRate() == 0
            ? std::wstring{Localization::Text(
                L"cihaz doğal hızı",
                L"device native rate"
            )}
            : std::to_wstring(config.GetAudioSampleRate()) + L" Hz";

    const std::wstring buffer =
        config.GetAudioBufferMilliseconds() == 0
            ? std::wstring{Localization::Text(
                L"varsayılan",
                L"default"
            )}
            : std::to_wstring(
                config.GetAudioBufferMilliseconds()
            ) + L" ms";

    SetControlText(
        audioSettingsLabel_,
        std::wstring{Localization::Text(
            L"Ses ayarları: ",
            L"Audio settings: "
        )} + sampleRate + L", " + buffer
    );

    SetControlText(
        languageLabel_,
        std::wstring{Localization::Text(
            L"Dil: Türkçe",
            L"Language: English"
        )}
    );

    PopulateBindings(config);
}

void ControlWindow::SetStatus(const std::wstring& status)
{
    SetControlText(statusValue_, status);
}

LRESULT CALLBACK ControlWindow::WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam
)
{
    ControlWindow* controlWindow = reinterpret_cast<ControlWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );

    if (message == WM_NCCREATE)
    {
        const auto* createData =
            reinterpret_cast<const CREATESTRUCTW*>(lParam);

        controlWindow = static_cast<ControlWindow*>(
            createData->lpCreateParams
        );

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(controlWindow)
        );
    }

    if (controlWindow != nullptr)
    {
        return controlWindow->HandleWindowMessage(
            window,
            message,
            wParam,
            lParam
        );
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT ControlWindow::HandleWindowMessage(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam
)
{
    switch (message)
    {
        case WM_COMMAND:
        {
            if (HIWORD(wParam) != BN_CLICKED)
            {
                break;
            }

            switch (LOWORD(wParam))
            {
                case IdReload:
                    SetStatus(Localization::Text(
                        L"Config yeniden yükleniyor...",
                        L"Reloading config..."
                    ));
                    PostApplicationCommand(commandIds_.reload);
                    return 0;

                case IdStopAll:
                    PostApplicationCommand(commandIds_.stop);
                    return 0;

                case IdOutputMute:
                    PostApplicationCommand(commandIds_.outputMute);
                    return 0;

                case IdMonitorMute:
                    PostApplicationCommand(commandIds_.monitorMute);
                    return 0;

                case IdOpenConfig:
                    OpenPath(configPath_);
                    return 0;

                case IdOpenSounds:
                    OpenPath(soundsFolder_);
                    return 0;

                case IdToggleConsole:
                    PostApplicationCommand(commandIds_.toggleConsole);
                    return 0;

                case IdExit:
                    PostApplicationCommand(commandIds_.exit);
                    return 0;

                default:
                    break;
            }

            break;
        }

        case WM_SIZE:
            LayoutControls(
                LOWORD(lParam),
                HIWORD(lParam)
            );
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* minimumMaximum =
                reinterpret_cast<MINMAXINFO*>(lParam);

            RECT rectangle{
                0,
                0,
                MinimumClientWidth,
                MinimumClientHeight
            };

            AdjustWindowRectEx(
                &rectangle,
                static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)),
                FALSE,
                static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE))
            );

            minimumMaximum->ptMinTrackSize.x =
                rectangle.right - rectangle.left;
            minimumMaximum->ptMinTrackSize.y =
                rectangle.bottom - rectangle.top;
            return 0;
        }

        case WM_CLOSE:
            Hide();
            return 0;

        case WM_DESTROY:
            return 0;

        default:
            break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool ControlWindow::CreateControls()
{
    if (window_ == nullptr)
    {
        return false;
    }

    const auto createControl = [this](
        const wchar_t* className,
        const wchar_t* text,
        const DWORD style,
        const int id
    )
    {
        HWND control = CreateWindowExW(
            0,
            className,
            text,
            WS_CHILD | WS_VISIBLE | style,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(id)
            ),
            instance_,
            nullptr
        );

        ApplyDefaultFont(control);
        return control;
    };

    headerLabel_ = createControl(
        L"STATIC",
        L"SoundBoardFasaFiso",
        SS_LEFT,
        0
    );

    statusCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    statusValue_ = createControl(L"STATIC", L"", SS_LEFT, 0);

    audioGroup_ = createControl(
        L"BUTTON",
        L"",
        BS_GROUPBOX,
        0
    );

    outputLabel_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    monitorLabel_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    audioSettingsLabel_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    languageLabel_ = createControl(L"STATIC", L"", SS_LEFT, 0);

    bindingsGroup_ = createControl(
        L"BUTTON",
        L"",
        BS_GROUPBOX,
        0
    );

    bindingsList_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            WS_HSCROLL | LBS_NOINTEGRALHEIGHT,
        0,
        0,
        0,
        0,
        window_,
        nullptr,
        instance_,
        nullptr
    );
    ApplyDefaultFont(bindingsList_);

    reloadButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON,
        IdReload
    );
    stopButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON,
        IdStopAll
    );
    outputMuteButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON,
        IdOutputMute
    );
    monitorMuteButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON,
        IdMonitorMute
    );
    openConfigButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON,
        IdOpenConfig
    );
    openSoundsButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON,
        IdOpenSounds
    );
    consoleButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON,
        IdToggleConsole
    );
    exitButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON,
        IdExit
    );

    const HWND controls[] = {
        headerLabel_,
        statusCaption_,
        statusValue_,
        audioGroup_,
        outputLabel_,
        monitorLabel_,
        audioSettingsLabel_,
        languageLabel_,
        bindingsGroup_,
        bindingsList_,
        reloadButton_,
        stopButton_,
        outputMuteButton_,
        monitorMuteButton_,
        openConfigButton_,
        openSoundsButton_,
        consoleButton_,
        exitButton_
    };

    return std::all_of(
        std::begin(controls),
        std::end(controls),
        [](const HWND control)
        {
            return control != nullptr;
        }
    );
}

void ControlWindow::LayoutControls(
    const int clientWidth,
    const int clientHeight
)
{
    if (window_ == nullptr || clientWidth <= 0 || clientHeight <= 0)
    {
        return;
    }

    const int contentWidth = clientWidth - BaseMargin * 2;
    const int buttonWidth =
        (contentWidth - ButtonGap * 3) / 4;

    int y = BaseMargin;

    MoveWindow(
        headerLabel_,
        BaseMargin,
        y,
        contentWidth,
        HeaderHeight,
        TRUE
    );
    y += HeaderHeight + 4;

    MoveWindow(
        statusCaption_,
        BaseMargin,
        y,
        72,
        StatusHeight,
        TRUE
    );
    MoveWindow(
        statusValue_,
        BaseMargin + 72,
        y,
        contentWidth - 72,
        StatusHeight,
        TRUE
    );
    y += StatusHeight + 8;

    MoveWindow(
        audioGroup_,
        BaseMargin,
        y,
        contentWidth,
        AudioGroupHeight,
        TRUE
    );

    const int audioTextX = BaseMargin + 14;
    int audioTextY = y + 24;
    const int audioTextWidth = contentWidth - 28;

    MoveWindow(
        outputLabel_,
        audioTextX,
        audioTextY,
        audioTextWidth,
        22,
        TRUE
    );
    audioTextY += 24;

    MoveWindow(
        monitorLabel_,
        audioTextX,
        audioTextY,
        audioTextWidth,
        22,
        TRUE
    );
    audioTextY += 24;

    MoveWindow(
        audioSettingsLabel_,
        audioTextX,
        audioTextY,
        audioTextWidth,
        22,
        TRUE
    );
    audioTextY += 24;

    MoveWindow(
        languageLabel_,
        audioTextX,
        audioTextY,
        audioTextWidth,
        22,
        TRUE
    );

    y += AudioGroupHeight + 8;

    const int buttonsAreaHeight =
        ButtonHeight * 2 + ButtonGap + BaseMargin;

    const int bindingsHeight = std::max(
        120,
        clientHeight - y - buttonsAreaHeight
    );

    MoveWindow(
        bindingsGroup_,
        BaseMargin,
        y,
        contentWidth,
        bindingsHeight,
        TRUE
    );

    MoveWindow(
        bindingsList_,
        BaseMargin + 12,
        y + 24,
        contentWidth - 24,
        bindingsHeight - 36,
        TRUE
    );

    y += bindingsHeight + 8;

    const HWND firstRow[] = {
        reloadButton_,
        stopButton_,
        outputMuteButton_,
        monitorMuteButton_
    };

    const HWND secondRow[] = {
        openConfigButton_,
        openSoundsButton_,
        consoleButton_,
        exitButton_
    };

    for (std::size_t index = 0; index < 4; ++index)
    {
        const int x = BaseMargin +
            static_cast<int>(index) * (buttonWidth + ButtonGap);

        MoveWindow(
            firstRow[index],
            x,
            y,
            buttonWidth,
            ButtonHeight,
            TRUE
        );
    }

    y += ButtonHeight + ButtonGap;

    for (std::size_t index = 0; index < 4; ++index)
    {
        const int x = BaseMargin +
            static_cast<int>(index) * (buttonWidth + ButtonGap);

        MoveWindow(
            secondRow[index],
            x,
            y,
            buttonWidth,
            ButtonHeight,
            TRUE
        );
    }
}

void ControlWindow::RefreshLocalizedText()
{
    if (window_ == nullptr)
    {
        return;
    }

    SetWindowTextW(
        window_,
        Localization::Text(
            L"SoundBoardFasaFiso Kontrol Paneli",
            L"SoundBoardFasaFiso Control Panel"
        )
    );

    SetControlText(
        headerLabel_,
        std::wstring{L"SoundBoardFasaFiso v"} +
            Utf8ToWide(std::string{AppVersion::String})
    );

    SetControlText(
        statusCaption_,
        Localization::Text(L"Durum:", L"Status:")
    );

    SetControlText(
        audioGroup_,
        Localization::Text(L"Ses ve dil", L"Audio and language")
    );

    SetControlText(
        bindingsGroup_,
        Localization::Text(L"Ses atamaları", L"Sound bindings")
    );

    SetControlText(
        reloadButton_,
        Localization::Text(L"Config'i yenile", L"Reload config")
    );
    SetControlText(
        stopButton_,
        Localization::Text(L"Tümünü durdur", L"Stop all")
    );
    SetControlText(
        outputMuteButton_,
        Localization::Text(L"Ana çıkışı sustur/aç", L"Toggle main mute")
    );
    SetControlText(
        monitorMuteButton_,
        Localization::Text(L"Monitörü sustur/aç", L"Toggle monitor mute")
    );
    SetControlText(
        openConfigButton_,
        Localization::Text(L"Config'i aç", L"Open config")
    );
    SetControlText(
        openSoundsButton_,
        Localization::Text(L"Ses klasörünü aç", L"Open sounds folder")
    );
    SetControlText(
        consoleButton_,
        Localization::Text(L"Konsolu göster/gizle", L"Show/hide console")
    );
    SetControlText(
        exitButton_,
        Localization::Text(L"Programı kapat", L"Exit")
    );
}

void ControlWindow::PopulateBindings(const Config& config)
{
    if (bindingsList_ == nullptr)
    {
        return;
    }

    SendMessageW(bindingsList_, LB_RESETCONTENT, 0, 0);

    int maximumWidth = 0;
    HDC deviceContext = GetDC(bindingsList_);

    for (const SoundBinding& binding : config.GetBindings())
    {
        const std::wstring entry =
            Utf8ToWide(binding.keyName) + L"  ->  " +
            binding.soundFile.wstring() + L"  |  " +
            BuildVolumeText(binding.volume) + L"  |  " +
            Utf8ToWide(std::string{PlaybackModeName(binding.mode)});

        SendMessageW(
            bindingsList_,
            LB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(entry.c_str())
        );

        if (deviceContext != nullptr)
        {
            SIZE textSize{};

            if (GetTextExtentPoint32W(
                deviceContext,
                entry.c_str(),
                static_cast<int>(entry.size()),
                &textSize
            ) != FALSE)
            {
                maximumWidth = std::max(
                    maximumWidth,
                    static_cast<int>(textSize.cx) + 24
                );
            }
        }
    }

    if (deviceContext != nullptr)
    {
        ReleaseDC(bindingsList_, deviceContext);
    }

    SendMessageW(
        bindingsList_,
        LB_SETHORIZONTALEXTENT,
        static_cast<WPARAM>(maximumWidth),
        0
    );
}

void ControlWindow::PostApplicationCommand(
    const int commandId
) const
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

void ControlWindow::OpenPath(
    const std::filesystem::path& path
) const
{
    if (window_ == nullptr || path.empty())
    {
        return;
    }

    const std::wstring nativePath = path.wstring();

    const HINSTANCE result = ShellExecuteW(
        window_,
        L"open",
        nativePath.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );

    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Dosya veya klasör açılamadı.",
                L"The file or folder could not be opened."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
    }
}

std::wstring ControlWindow::Utf8ToWide(
    const std::string& value
)
{
    if (value.empty())
    {
        return {};
    }

    const int requiredCharacters = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );

    if (requiredCharacters <= 0)
    {
        return std::wstring(value.begin(), value.end());
    }

    std::wstring converted(
        static_cast<std::size_t>(requiredCharacters),
        L'\0'
    );

    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        converted.data(),
        requiredCharacters
    );

    return converted;
}

void ControlWindow::SetControlText(
    const HWND control,
    const std::wstring& text
)
{
    if (control != nullptr)
    {
        SetWindowTextW(control, text.c_str());
    }
}

void ControlWindow::ApplyDefaultFont(const HWND control)
{
    if (control != nullptr)
    {
        SendMessageW(
            control,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(
                GetStockObject(DEFAULT_GUI_FONT)
            ),
            TRUE
        );
    }
}
