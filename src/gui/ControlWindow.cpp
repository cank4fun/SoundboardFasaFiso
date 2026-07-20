#include "gui/ControlWindow.hpp"

#include "app/Version.hpp"
#include "audio/Audio.hpp"
#include "localization/Localization.hpp"
#include "ResourceIds.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    constexpr wchar_t ControlWindowClassName[] =
        L"SoundBoardFasaFiso.ControlWindow";

    constexpr int BaseMargin = 16;
    constexpr int HeaderHeight = 28;
    constexpr int StatusHeight = 24;
    constexpr int SettingsGroupHeight = 242;
    constexpr int ButtonHeight = 34;
    constexpr int ButtonGap = 8;
    constexpr int RowHeight = 30;

    std::wstring BuildVolumeText(const float volume)
    {
        return std::to_wstring(
            static_cast<int>(volume * 100.0f + 0.5f)
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
    const std::vector<std::string>& playbackDevices,
    const ControlWindowCommandIds& commandIds
)
{
    Shutdown();

    instance_ = GetModuleHandleW(nullptr);
    mainThreadId_ = GetCurrentThreadId();
    commandIds_ = commandIds;
    configPath_ = configPath;
    pendingConfigPath_ = configPath;
    pendingConfigPath_ += L".pending";
    soundsFolder_ = soundsFolder;
    currentConfig_ = config;
    playbackDevices_ = playbackDevices;

    if (instance_ == nullptr)
    {
        return false;
    }

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_BAR_CLASSES;

    if (InitCommonControlsEx(&commonControls) == FALSE)
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
    pendingConfigPath_.clear();
    soundsFolder_.clear();
    playbackDevices_.clear();

    headerLabel_ = nullptr;
    statusCaption_ = nullptr;
    statusValue_ = nullptr;
    settingsGroup_ = nullptr;
    outputCaption_ = nullptr;
    outputCombo_ = nullptr;
    outputVolumeCaption_ = nullptr;
    outputVolumeSlider_ = nullptr;
    outputVolumeValue_ = nullptr;
    monitorCaption_ = nullptr;
    monitorCombo_ = nullptr;
    monitorVolumeCaption_ = nullptr;
    monitorVolumeSlider_ = nullptr;
    monitorVolumeValue_ = nullptr;
    sampleRateCaption_ = nullptr;
    sampleRateCombo_ = nullptr;
    bufferCaption_ = nullptr;
    bufferCombo_ = nullptr;
    languageCaption_ = nullptr;
    languageCombo_ = nullptr;
    refreshDevicesButton_ = nullptr;
    applySettingsButton_ = nullptr;
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

    currentConfig_ = config;
    RefreshLocalizedText();
    PopulateDeviceCombos();
    PopulateNumericCombos();
    PopulateEditorControls();
    PopulateBindings(config);
}

void ControlWindow::SetPlaybackDevices(
    const std::vector<std::string>& playbackDevices
)
{
    playbackDevices_ = playbackDevices;

    if (window_ != nullptr)
    {
        PopulateDeviceCombos();
        PopulateEditorControls();
    }
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
                case IdApplySettings:
                    SavePendingSettings();
                    return 0;

                case IdRefreshDevices:
                {
                    SetStatus(Localization::Text(
                        L"Ses cihazları yenileniyor...",
                        L"Refreshing audio devices..."
                    ));

                    const std::vector<std::string> devices =
                        Audio::EnumeratePlaybackDevices();

                    SetPlaybackDevices(devices);

                    SetStatus(
                        devices.empty()
                            ? Localization::Text(
                                L"Ses cihazı listesi alınamadı.",
                                L"The audio device list could not be read."
                            )
                            : Localization::Text(
                                L"Ses cihazı listesi yenilendi.",
                                L"Audio device list refreshed."
                            )
                    );
                    return 0;
                }

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

        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == outputVolumeSlider_ ||
                reinterpret_cast<HWND>(lParam) == monitorVolumeSlider_)
            {
                UpdateVolumeLabels();
                return 0;
            }
            break;

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
        const int id,
        const DWORD extendedStyle = 0
    )
    {
        HWND control = CreateWindowExW(
            extendedStyle,
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

    settingsGroup_ = createControl(
        L"BUTTON",
        L"",
        BS_GROUPBOX,
        0
    );

    outputCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    outputCombo_ = createControl(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        0,
        WS_EX_CLIENTEDGE
    );
    outputVolumeCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    outputVolumeSlider_ = createControl(
        TRACKBAR_CLASSW,
        L"",
        TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
        IdOutputVolumeSlider
    );
    outputVolumeValue_ = createControl(L"STATIC", L"", SS_RIGHT, 0);

    monitorCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    monitorCombo_ = createControl(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        0,
        WS_EX_CLIENTEDGE
    );
    monitorVolumeCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    monitorVolumeSlider_ = createControl(
        TRACKBAR_CLASSW,
        L"",
        TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
        IdMonitorVolumeSlider
    );
    monitorVolumeValue_ = createControl(L"STATIC", L"", SS_RIGHT, 0);

    sampleRateCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    sampleRateCombo_ = createControl(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWN | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        0,
        WS_EX_CLIENTEDGE
    );
    bufferCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    bufferCombo_ = createControl(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWN | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        0,
        WS_EX_CLIENTEDGE
    );

    languageCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    languageCombo_ = createControl(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        0,
        WS_EX_CLIENTEDGE
    );

    refreshDevicesButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON | WS_TABSTOP,
        IdRefreshDevices
    );
    applySettingsButton_ = createControl(
        L"BUTTON",
        L"",
        BS_DEFPUSHBUTTON | WS_TABSTOP,
        IdApplySettings
    );

    SendMessageW(outputVolumeSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(monitorVolumeSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(outputVolumeSlider_, TBM_SETPAGESIZE, 0, 5);
    SendMessageW(monitorVolumeSlider_, TBM_SETPAGESIZE, 0, 5);

    bindingsGroup_ = createControl(
        L"BUTTON",
        L"",
        BS_GROUPBOX,
        0
    );

    bindingsList_ = createControl(
        L"LISTBOX",
        L"",
        WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT,
        0,
        WS_EX_CLIENTEDGE
    );

    reloadButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON | WS_TABSTOP,
        IdReload
    );
    stopButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON | WS_TABSTOP,
        IdStopAll
    );
    outputMuteButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON | WS_TABSTOP,
        IdOutputMute
    );
    monitorMuteButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON | WS_TABSTOP,
        IdMonitorMute
    );
    openConfigButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON | WS_TABSTOP,
        IdOpenConfig
    );
    openSoundsButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON | WS_TABSTOP,
        IdOpenSounds
    );
    consoleButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON | WS_TABSTOP,
        IdToggleConsole
    );
    exitButton_ = createControl(
        L"BUTTON",
        L"",
        BS_PUSHBUTTON | WS_TABSTOP,
        IdExit
    );

    const HWND controls[] = {
        headerLabel_,
        statusCaption_,
        statusValue_,
        settingsGroup_,
        outputCaption_,
        outputCombo_,
        outputVolumeCaption_,
        outputVolumeSlider_,
        outputVolumeValue_,
        monitorCaption_,
        monitorCombo_,
        monitorVolumeCaption_,
        monitorVolumeSlider_,
        monitorVolumeValue_,
        sampleRateCaption_,
        sampleRateCombo_,
        bufferCaption_,
        bufferCombo_,
        languageCaption_,
        languageCombo_,
        refreshDevicesButton_,
        applySettingsButton_,
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
        settingsGroup_,
        BaseMargin,
        y,
        contentWidth,
        SettingsGroupHeight,
        TRUE
    );

    const int innerX = BaseMargin + 14;
    const int innerWidth = contentWidth - 28;
    const int labelWidth = 118;
    const int comboWidth = std::max(280, innerWidth * 38 / 100);
    const int volumeCaptionWidth = 72;
    const int volumeValueWidth = 46;
    const int fieldGap = 8;
    const int sectionGap = 16;
    const int volumeSliderWidth = std::max(
        120,
        innerWidth - labelWidth - comboWidth - volumeCaptionWidth -
            volumeValueWidth - fieldGap * 3 - sectionGap
    );

    int rowY = y + 28;

    MoveWindow(outputCaption_, innerX, rowY + 5, labelWidth, 22, TRUE);
    MoveWindow(
        outputCombo_,
        innerX + labelWidth,
        rowY,
        comboWidth,
        220,
        TRUE
    );

    int volumeX = innerX + labelWidth + comboWidth + sectionGap;
    MoveWindow(
        outputVolumeCaption_,
        volumeX,
        rowY + 5,
        volumeCaptionWidth,
        22,
        TRUE
    );
    volumeX += volumeCaptionWidth + fieldGap;
    MoveWindow(
        outputVolumeSlider_,
        volumeX,
        rowY,
        volumeSliderWidth,
        28,
        TRUE
    );
    MoveWindow(
        outputVolumeValue_,
        volumeX + volumeSliderWidth + fieldGap,
        rowY + 5,
        volumeValueWidth,
        22,
        TRUE
    );

    rowY += RowHeight + 8;

    MoveWindow(monitorCaption_, innerX, rowY + 5, labelWidth, 22, TRUE);
    MoveWindow(
        monitorCombo_,
        innerX + labelWidth,
        rowY,
        comboWidth,
        220,
        TRUE
    );

    volumeX = innerX + labelWidth + comboWidth + sectionGap;
    MoveWindow(
        monitorVolumeCaption_,
        volumeX,
        rowY + 5,
        volumeCaptionWidth,
        22,
        TRUE
    );
    volumeX += volumeCaptionWidth + fieldGap;
    MoveWindow(
        monitorVolumeSlider_,
        volumeX,
        rowY,
        volumeSliderWidth,
        28,
        TRUE
    );
    MoveWindow(
        monitorVolumeValue_,
        volumeX + volumeSliderWidth + fieldGap,
        rowY + 5,
        volumeValueWidth,
        22,
        TRUE
    );

    rowY += RowHeight + 12;

    const int halfWidth = (innerWidth - sectionGap) / 2;
    const int smallLabelWidth = 150;
    const int smallControlWidth = halfWidth - smallLabelWidth;

    MoveWindow(sampleRateCaption_, innerX, rowY + 5, smallLabelWidth, 22, TRUE);
    MoveWindow(
        sampleRateCombo_,
        innerX + smallLabelWidth,
        rowY,
        smallControlWidth,
        180,
        TRUE
    );

    const int rightColumnX = innerX + halfWidth + sectionGap;
    MoveWindow(bufferCaption_, rightColumnX, rowY + 5, smallLabelWidth, 22, TRUE);
    MoveWindow(
        bufferCombo_,
        rightColumnX + smallLabelWidth,
        rowY,
        smallControlWidth,
        180,
        TRUE
    );

    rowY += RowHeight + 12;

    MoveWindow(languageCaption_, innerX, rowY + 5, labelWidth, 22, TRUE);
    MoveWindow(
        languageCombo_,
        innerX + labelWidth,
        rowY,
        190,
        150,
        TRUE
    );

    const int actionButtonWidth = 190;
    const int applyX = innerX + innerWidth - actionButtonWidth;
    MoveWindow(
        applySettingsButton_,
        applyX,
        rowY,
        actionButtonWidth,
        ButtonHeight,
        TRUE
    );
    MoveWindow(
        refreshDevicesButton_,
        applyX - actionButtonWidth - ButtonGap,
        rowY,
        actionButtonWidth,
        ButtonHeight,
        TRUE
    );

    y += SettingsGroupHeight + 8;

    const int buttonsAreaHeight =
        ButtonHeight * 2 + ButtonGap + BaseMargin;

    const int bindingsHeight = std::max(
        110,
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

    SetControlText(statusCaption_, Localization::Text(L"Durum:", L"Status:"));
    SetControlText(settingsGroup_, Localization::Text(L"Ayarlar", L"Settings"));
    SetControlText(outputCaption_, Localization::Text(L"Ana çıkış:", L"Main output:"));
    SetControlText(monitorCaption_, Localization::Text(L"Monitör çıkışı:", L"Monitor output:"));
    SetControlText(outputVolumeCaption_, Localization::Text(L"Ses:", L"Volume:"));
    SetControlText(monitorVolumeCaption_, Localization::Text(L"Ses:", L"Volume:"));
    SetControlText(
        sampleRateCaption_,
        Localization::Text(
            L"Örnekleme (Hz, 0=oto):",
            L"Sample rate (Hz, 0=auto):"
        )
    );
    SetControlText(
        bufferCaption_,
        Localization::Text(
            L"Buffer (ms, 0=oto):",
            L"Buffer (ms, 0=auto):"
        )
    );
    SetControlText(languageCaption_, Localization::Text(L"Dil:", L"Language:"));
    SetControlText(
        refreshDevicesButton_,
        Localization::Text(L"Cihazları yenile", L"Refresh devices")
    );
    SetControlText(
        applySettingsButton_,
        Localization::Text(L"Kaydet ve uygula", L"Save and apply")
    );

    SetControlText(bindingsGroup_, Localization::Text(L"Ses atamaları", L"Sound bindings"));
    SetControlText(reloadButton_, Localization::Text(L"Config'i yenile", L"Reload config"));
    SetControlText(stopButton_, Localization::Text(L"Tümünü durdur", L"Stop all"));
    SetControlText(outputMuteButton_, Localization::Text(L"Ana çıkışı sustur/aç", L"Toggle main mute"));
    SetControlText(monitorMuteButton_, Localization::Text(L"Monitörü sustur/aç", L"Toggle monitor mute"));
    SetControlText(openConfigButton_, Localization::Text(L"Config'i aç", L"Open config"));
    SetControlText(openSoundsButton_, Localization::Text(L"Ses klasörünü aç", L"Open sounds folder"));
    SetControlText(consoleButton_, Localization::Text(L"Konsolu göster/gizle", L"Show/hide console"));
    SetControlText(exitButton_, Localization::Text(L"Programı kapat", L"Exit"));
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

void ControlWindow::PopulateEditorControls()
{
    if (window_ == nullptr)
    {
        return;
    }

    SelectComboText(
        outputCombo_,
        Utf8ToWide(currentConfig_.GetOutputDevice())
    );
    SelectComboText(
        monitorCombo_,
        Utf8ToWide(currentConfig_.GetMonitorDevice())
    );

    SendMessageW(
        outputVolumeSlider_,
        TBM_SETPOS,
        TRUE,
        static_cast<LPARAM>(
            currentConfig_.GetOutputVolume() * 100.0f + 0.5f
        )
    );
    SendMessageW(
        monitorVolumeSlider_,
        TBM_SETPOS,
        TRUE,
        static_cast<LPARAM>(
            currentConfig_.GetMonitorVolume() * 100.0f + 0.5f
        )
    );

    SetControlText(
        sampleRateCombo_,
        std::to_wstring(currentConfig_.GetAudioSampleRate())
    );
    SetControlText(
        bufferCombo_,
        std::to_wstring(currentConfig_.GetAudioBufferMilliseconds())
    );

    SendMessageW(
        languageCombo_,
        CB_SETCURSEL,
        currentConfig_.GetLanguage() == Language::English ? 1 : 0,
        0
    );

    UpdateVolumeLabels();
}

void ControlWindow::PopulateDeviceCombos()
{
    if (outputCombo_ == nullptr || monitorCombo_ == nullptr)
    {
        return;
    }

    SendMessageW(outputCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(monitorCombo_, CB_RESETCONTENT, 0, 0);

    AddComboItem(outputCombo_, L"default");
    AddComboItem(monitorCombo_, L"none");
    AddComboItem(monitorCombo_, L"default");

    const std::wstring configuredOutput =
        Utf8ToWide(currentConfig_.GetOutputDevice());
    const std::wstring configuredMonitor =
        Utf8ToWide(currentConfig_.GetMonitorDevice());

    AddComboItem(outputCombo_, configuredOutput);
    AddComboItem(monitorCombo_, configuredMonitor);

    for (const std::string& device : playbackDevices_)
    {
        const std::wstring deviceName = Utf8ToWide(device);
        AddComboItem(outputCombo_, deviceName);
        AddComboItem(monitorCombo_, deviceName);
    }
}

void ControlWindow::PopulateNumericCombos()
{
    if (sampleRateCombo_ == nullptr || bufferCombo_ == nullptr ||
        languageCombo_ == nullptr)
    {
        return;
    }

    SendMessageW(sampleRateCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(bufferCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(languageCombo_, CB_RESETCONTENT, 0, 0);

    const wchar_t* sampleRates[] = {
        L"0", L"44100", L"48000", L"96000", L"192000"
    };

    for (const wchar_t* sampleRate : sampleRates)
    {
        AddComboItem(sampleRateCombo_, sampleRate);
    }

    const wchar_t* buffers[] = {
        L"0", L"2", L"3", L"5", L"10", L"20", L"50", L"100"
    };

    for (const wchar_t* buffer : buffers)
    {
        AddComboItem(bufferCombo_, buffer);
    }

    AddComboItem(languageCombo_, L"Türkçe");
    AddComboItem(languageCombo_, L"English");
}

void ControlWindow::UpdateVolumeLabels()
{
    if (outputVolumeSlider_ == nullptr || monitorVolumeSlider_ == nullptr)
    {
        return;
    }

    const LRESULT outputVolume = SendMessageW(
        outputVolumeSlider_,
        TBM_GETPOS,
        0,
        0
    );
    const LRESULT monitorVolume = SendMessageW(
        monitorVolumeSlider_,
        TBM_GETPOS,
        0,
        0
    );

    SetControlText(
        outputVolumeValue_,
        std::to_wstring(outputVolume) + L"%"
    );
    SetControlText(
        monitorVolumeValue_,
        std::to_wstring(monitorVolume) + L"%"
    );
}

bool ControlWindow::SavePendingSettings()
{
    if (window_ == nullptr)
    {
        return false;
    }

    const std::string outputDevice = WideToUtf8(
        GetControlText(outputCombo_)
    );
    const std::string monitorDevice = WideToUtf8(
        GetControlText(monitorCombo_)
    );

    unsigned int sampleRate = 0;
    unsigned int bufferMilliseconds = 0;

    const bool valid =
        !outputDevice.empty() &&
        !monitorDevice.empty() &&
        ParseUnsignedControl(sampleRateCombo_, sampleRate) &&
        ParseUnsignedControl(bufferCombo_, bufferMilliseconds);

    if (!valid)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Cihaz, örnekleme hızı veya buffer alanlarından biri geçersiz.",
                L"One of the device, sample-rate, or buffer fields is invalid."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    const int outputVolume = static_cast<int>(SendMessageW(
        outputVolumeSlider_,
        TBM_GETPOS,
        0,
        0
    ));
    const int monitorVolume = static_cast<int>(SendMessageW(
        monitorVolumeSlider_,
        TBM_GETPOS,
        0,
        0
    ));

    const int languageIndex = static_cast<int>(SendMessageW(
        languageCombo_,
        CB_GETCURSEL,
        0,
        0
    ));

    Config candidate = currentConfig_;
    candidate.SetLanguage(
        languageIndex == 1
            ? Language::English
            : Language::Turkish
    );
    candidate.SetOutputDevice(outputDevice);
    candidate.SetMonitorDevice(monitorDevice);

    if (!candidate.SetOutputVolume(
            static_cast<float>(outputVolume) / 100.0f
        ) ||
        !candidate.SetMonitorVolume(
            static_cast<float>(monitorVolume) / 100.0f
        ) ||
        !candidate.SetAudioSampleRate(sampleRate) ||
        !candidate.SetAudioBufferMilliseconds(bufferMilliseconds))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Ayar değerlerinden biri desteklenen aralığın dışında.",
                L"One of the setting values is outside the supported range."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    if (!candidate.Save(pendingConfigPath_))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Geçici config dosyası kaydedilemedi. Konsoldaki hata ayrıntılarına bakın.",
                L"The temporary config file could not be saved. Check the console for details."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return false;
    }

    SetStatus(Localization::Text(
        L"Yeni ayarlar doğrulanıyor ve uygulanıyor...",
        L"Validating and applying the new settings..."
    ));
    PostApplicationCommand(commandIds_.applySettings);
    return true;
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

    DWORD conversionFlags = MB_ERR_INVALID_CHARS;
    int requiredCharacters = MultiByteToWideChar(
        CP_UTF8,
        conversionFlags,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );

    if (requiredCharacters <= 0)
    {
        conversionFlags = 0;
        requiredCharacters = MultiByteToWideChar(
            CP_UTF8,
            conversionFlags,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0
        );
    }

    if (requiredCharacters <= 0)
    {
        return {};
    }

    std::wstring converted(
        static_cast<std::size_t>(requiredCharacters),
        L'\0'
    );

    if (MultiByteToWideChar(
            CP_UTF8,
            conversionFlags,
            value.data(),
            static_cast<int>(value.size()),
            converted.data(),
            requiredCharacters
        ) <= 0)
    {
        return {};
    }

    return converted;
}

std::string ControlWindow::WideToUtf8(
    const std::wstring& value
)
{
    if (value.empty())
    {
        return {};
    }

    DWORD conversionFlags = WC_ERR_INVALID_CHARS;
    int requiredBytes = WideCharToMultiByte(
        CP_UTF8,
        conversionFlags,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (requiredBytes <= 0)
    {
        conversionFlags = 0;
        requiredBytes = WideCharToMultiByte(
            CP_UTF8,
            conversionFlags,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );
    }

    if (requiredBytes <= 0)
    {
        return {};
    }

    std::string converted(
        static_cast<std::size_t>(requiredBytes),
        '\0'
    );

    if (WideCharToMultiByte(
            CP_UTF8,
            conversionFlags,
            value.data(),
            static_cast<int>(value.size()),
            converted.data(),
            requiredBytes,
            nullptr,
            nullptr
        ) <= 0)
    {
        return {};
    }

    return converted;
}

std::wstring ControlWindow::GetControlText(const HWND control)
{
    if (control == nullptr)
    {
        return {};
    }

    const int length = GetWindowTextLengthW(control);

    if (length <= 0)
    {
        return {};
    }

    std::wstring value(
        static_cast<std::size_t>(length + 1),
        L'\0'
    );

    const int copied = GetWindowTextW(
        control,
        value.data(),
        length + 1
    );

    if (copied <= 0)
    {
        return {};
    }

    value.resize(static_cast<std::size_t>(copied));
    return value;
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

void ControlWindow::AddComboItem(
    const HWND combo,
    const std::wstring& text
)
{
    if (combo == nullptr || text.empty())
    {
        return;
    }

    const LRESULT existing = SendMessageW(
        combo,
        CB_FINDSTRINGEXACT,
        static_cast<WPARAM>(-1),
        reinterpret_cast<LPARAM>(text.c_str())
    );

    if (existing == CB_ERR)
    {
        SendMessageW(
            combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(text.c_str())
        );
    }
}

void ControlWindow::SelectComboText(
    const HWND combo,
    const std::wstring& text
)
{
    if (combo == nullptr || text.empty())
    {
        return;
    }

    LRESULT index = SendMessageW(
        combo,
        CB_FINDSTRINGEXACT,
        static_cast<WPARAM>(-1),
        reinterpret_cast<LPARAM>(text.c_str())
    );

    if (index == CB_ERR)
    {
        AddComboItem(combo, text);
        index = SendMessageW(
            combo,
            CB_FINDSTRINGEXACT,
            static_cast<WPARAM>(-1),
            reinterpret_cast<LPARAM>(text.c_str())
        );
    }

    if (index != CB_ERR)
    {
        SendMessageW(combo, CB_SETCURSEL, index, 0);
    }
}

bool ControlWindow::ParseUnsignedControl(
    const HWND control,
    unsigned int& value
)
{
    std::wstring text = GetControlText(control);

    const std::size_t first = text.find_first_not_of(L" \t\r\n");

    if (first == std::wstring::npos)
    {
        return false;
    }

    const std::size_t last = text.find_last_not_of(L" \t\r\n");
    text = text.substr(first, last - first + 1);

    const std::string utf8 = WideToUtf8(text);
    const char* begin = utf8.data();
    const char* end = utf8.data() + utf8.size();

    unsigned int parsedValue = 0;
    const auto [pointer, error] =
        std::from_chars(begin, end, parsedValue);

    if (error != std::errc{} || pointer != end)
    {
        return false;
    }

    value = parsedValue;
    return true;
}
