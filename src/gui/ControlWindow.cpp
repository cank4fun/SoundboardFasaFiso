#include "gui/ControlWindow.hpp"

#include "app/Version.hpp"
#include "audio/Audio.hpp"
#include "localization/Localization.hpp"
#include "platform/Utf8Path.hpp"
#include "sound/SoundFileFormat.hpp"
#include "ResourceIds.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cwctype>
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
    constexpr int ControlHotkeysGroupHeight = 94;
    constexpr int BindingEditorWidth = 372;
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
    pendingBindings_.clear();
    selectedBindingIndex_ = -1;
    capturingBindingHotkey_ = false;

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
    controlHotkeysGroup_ = nullptr;
    stopHotkeyCaption_ = nullptr;
    stopHotkeyEdit_ = nullptr;
    outputMuteHotkeyCaption_ = nullptr;
    outputMuteHotkeyEdit_ = nullptr;
    monitorMuteHotkeyCaption_ = nullptr;
    monitorMuteHotkeyEdit_ = nullptr;
    reloadHotkeyCaption_ = nullptr;
    reloadHotkeyEdit_ = nullptr;
    exitHotkeyCaption_ = nullptr;
    exitHotkeyEdit_ = nullptr;
    bindingsGroup_ = nullptr;
    bindingsList_ = nullptr;
    bindingEditorGroup_ = nullptr;
    bindingHotkeyCaption_ = nullptr;
    bindingHotkeyEdit_ = nullptr;
    captureHotkeyButton_ = nullptr;
    bindingFileCaption_ = nullptr;
    bindingFileEdit_ = nullptr;
    browseSoundButton_ = nullptr;
    bindingModeCaption_ = nullptr;
    bindingModeCombo_ = nullptr;
    bindingVolumeCaption_ = nullptr;
    bindingVolumeSlider_ = nullptr;
    bindingVolumeValue_ = nullptr;
    addBindingButton_ = nullptr;
    updateBindingButton_ = nullptr;
    removeBindingButton_ = nullptr;
    clearBindingButton_ = nullptr;
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
    pendingBindings_ = config.GetBindings();
    selectedBindingIndex_ = -1;
    capturingBindingHotkey_ = false;

    RefreshLocalizedText();
    PopulateDeviceCombos();
    PopulateNumericCombos();
    PopulateEditorControls();
    PopulateControlHotkeys();
    PopulateBindings();
    ClearBindingEditor();
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
            const int controlId = LOWORD(wParam);
            const int notificationCode = HIWORD(wParam);

            if (controlId == IdBindingsList &&
                (notificationCode == LBN_SELCHANGE ||
                    notificationCode == LBN_DBLCLK))
            {
                LoadSelectedBindingIntoEditor();
                return 0;
            }

            if (notificationCode != BN_CLICKED)
            {
                break;
            }

            switch (controlId)
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

                case IdBrowseSound:
                    BrowseForSoundFile();
                    return 0;

                case IdCaptureHotkey:
                    BeginHotkeyCapture();
                    return 0;

                case IdAddBinding:
                    AddOrUpdateBinding(false);
                    return 0;

                case IdUpdateBinding:
                    AddOrUpdateBinding(true);
                    return 0;

                case IdRemoveBinding:
                    RemoveSelectedBinding();
                    return 0;

                case IdClearBinding:
                    ClearBindingEditor();
                    return 0;

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

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (capturingBindingHotkey_ &&
                CaptureHotkeyFromMessage(wParam))
            {
                return 0;
            }
            break;

        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == outputVolumeSlider_ ||
                reinterpret_cast<HWND>(lParam) == monitorVolumeSlider_)
            {
                UpdateVolumeLabels();
                return 0;
            }

            if (reinterpret_cast<HWND>(lParam) == bindingVolumeSlider_)
            {
                UpdateBindingVolumeLabel();
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

    settingsGroup_ = createControl(L"BUTTON", L"", BS_GROUPBOX, 0);
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

    controlHotkeysGroup_ = createControl(L"BUTTON", L"", BS_GROUPBOX, 0);
    stopHotkeyCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    stopHotkeyEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE
    );
    outputMuteHotkeyCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    outputMuteHotkeyEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE
    );
    monitorMuteHotkeyCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    monitorMuteHotkeyEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE
    );
    reloadHotkeyCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    reloadHotkeyEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE
    );
    exitHotkeyCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    exitHotkeyEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE
    );

    bindingsGroup_ = createControl(L"BUTTON", L"", BS_GROUPBOX, 0);
    bindingsList_ = createControl(
        L"LISTBOX",
        L"",
        WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT |
            LBS_NOTIFY | WS_TABSTOP,
        IdBindingsList,
        WS_EX_CLIENTEDGE
    );

    bindingEditorGroup_ = createControl(L"BUTTON", L"", BS_GROUPBOX, 0);
    bindingHotkeyCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    bindingHotkeyEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE
    );
    captureHotkeyButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdCaptureHotkey
    );
    bindingFileCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    bindingFileEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE
    );
    browseSoundButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdBrowseSound
    );
    bindingModeCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    bindingModeCombo_ = createControl(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        0,
        WS_EX_CLIENTEDGE
    );
    bindingVolumeCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    bindingVolumeSlider_ = createControl(
        TRACKBAR_CLASSW,
        L"",
        TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
        IdBindingVolumeSlider
    );
    bindingVolumeValue_ = createControl(L"STATIC", L"", SS_RIGHT, 0);
    addBindingButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdAddBinding
    );
    updateBindingButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdUpdateBinding
    );
    removeBindingButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdRemoveBinding
    );
    clearBindingButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdClearBinding
    );

    SendMessageW(bindingVolumeSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(bindingVolumeSlider_, TBM_SETPAGESIZE, 0, 5);

    reloadButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdReload
    );
    stopButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdStopAll
    );
    outputMuteButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdOutputMute
    );
    monitorMuteButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdMonitorMute
    );
    openConfigButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdOpenConfig
    );
    openSoundsButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdOpenSounds
    );
    consoleButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdToggleConsole
    );
    exitButton_ = createControl(
        L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IdExit
    );

    AddComboItem(bindingModeCombo_, L"restart");
    AddComboItem(bindingModeCombo_, L"toggle");
    AddComboItem(bindingModeCombo_, L"loop");
    AddComboItem(bindingModeCombo_, L"overlap");

    const HWND controls[] = {
        headerLabel_, statusCaption_, statusValue_, settingsGroup_,
        outputCaption_, outputCombo_, outputVolumeCaption_,
        outputVolumeSlider_, outputVolumeValue_, monitorCaption_,
        monitorCombo_, monitorVolumeCaption_, monitorVolumeSlider_,
        monitorVolumeValue_, sampleRateCaption_, sampleRateCombo_,
        bufferCaption_, bufferCombo_, languageCaption_, languageCombo_,
        refreshDevicesButton_, applySettingsButton_, controlHotkeysGroup_,
        stopHotkeyCaption_, stopHotkeyEdit_, outputMuteHotkeyCaption_,
        outputMuteHotkeyEdit_, monitorMuteHotkeyCaption_,
        monitorMuteHotkeyEdit_, reloadHotkeyCaption_, reloadHotkeyEdit_,
        exitHotkeyCaption_, exitHotkeyEdit_, bindingsGroup_, bindingsList_,
        bindingEditorGroup_, bindingHotkeyCaption_, bindingHotkeyEdit_,
        captureHotkeyButton_, bindingFileCaption_, bindingFileEdit_,
        browseSoundButton_, bindingModeCaption_, bindingModeCombo_,
        bindingVolumeCaption_, bindingVolumeSlider_, bindingVolumeValue_,
        addBindingButton_, updateBindingButton_, removeBindingButton_,
        clearBindingButton_, reloadButton_, stopButton_, outputMuteButton_,
        monitorMuteButton_, openConfigButton_, openSoundsButton_,
        consoleButton_, exitButton_
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

    MoveWindow(headerLabel_, BaseMargin, y, contentWidth, HeaderHeight, TRUE);
    y += HeaderHeight + 4;

    MoveWindow(statusCaption_, BaseMargin, y, 72, StatusHeight, TRUE);
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
    MoveWindow(outputCombo_, innerX + labelWidth, rowY, comboWidth, 220, TRUE);

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
    MoveWindow(outputVolumeSlider_, volumeX, rowY, volumeSliderWidth, 28, TRUE);
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
    MoveWindow(monitorCombo_, innerX + labelWidth, rowY, comboWidth, 220, TRUE);

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
    MoveWindow(monitorVolumeSlider_, volumeX, rowY, volumeSliderWidth, 28, TRUE);
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
    MoveWindow(languageCombo_, innerX + labelWidth, rowY, 190, 150, TRUE);

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

    MoveWindow(
        controlHotkeysGroup_,
        BaseMargin,
        y,
        contentWidth,
        ControlHotkeysGroupHeight,
        TRUE
    );

    const int hotkeyGap = 10;
    const int hotkeyInnerX = BaseMargin + 14;
    const int hotkeyInnerWidth = contentWidth - 28;
    const int hotkeyColumnWidth =
        (hotkeyInnerWidth - hotkeyGap * 4) / 5;
    const HWND hotkeyCaptions[] = {
        stopHotkeyCaption_,
        outputMuteHotkeyCaption_,
        monitorMuteHotkeyCaption_,
        reloadHotkeyCaption_,
        exitHotkeyCaption_
    };
    const HWND hotkeyEdits[] = {
        stopHotkeyEdit_,
        outputMuteHotkeyEdit_,
        monitorMuteHotkeyEdit_,
        reloadHotkeyEdit_,
        exitHotkeyEdit_
    };

    for (std::size_t index = 0; index < 5; ++index)
    {
        const int x = hotkeyInnerX +
            static_cast<int>(index) * (hotkeyColumnWidth + hotkeyGap);

        MoveWindow(hotkeyCaptions[index], x, y + 24, hotkeyColumnWidth, 20, TRUE);
        MoveWindow(hotkeyEdits[index], x, y + 46, hotkeyColumnWidth, 26, TRUE);
    }

    y += ControlHotkeysGroupHeight + 8;

    const int buttonsAreaHeight =
        ButtonHeight * 2 + ButtonGap + BaseMargin;

    const int bindingsHeight = std::max(
        240,
        clientHeight - y - buttonsAreaHeight
    );

    MoveWindow(bindingsGroup_, BaseMargin, y, contentWidth, bindingsHeight, TRUE);

    const int bindingsInnerX = BaseMargin + 12;
    const int bindingsInnerY = y + 24;
    const int bindingsInnerWidth = contentWidth - 24;
    const int bindingsInnerHeight = bindingsHeight - 36;
    const int editorWidth = std::min(
        BindingEditorWidth,
        std::max(330, bindingsInnerWidth * 42 / 100)
    );
    const int bindingGap = 12;
    const int listWidth = std::max(
        260,
        bindingsInnerWidth - editorWidth - bindingGap
    );

    MoveWindow(
        bindingsList_,
        bindingsInnerX,
        bindingsInnerY,
        listWidth,
        bindingsInnerHeight,
        TRUE
    );

    const int editorX = bindingsInnerX + listWidth + bindingGap;
    MoveWindow(
        bindingEditorGroup_,
        editorX,
        bindingsInnerY,
        editorWidth,
        bindingsInnerHeight,
        TRUE
    );

    const int editorMargin = 12;
    const int editorLabelWidth = 78;
    const int editorButtonWidth = 96;
    const int editorContentX = editorX + editorMargin;
    const int editorContentWidth = editorWidth - editorMargin * 2;
    int editorY = bindingsInnerY + 28;

    MoveWindow(
        bindingHotkeyCaption_,
        editorContentX,
        editorY + 5,
        editorLabelWidth,
        22,
        TRUE
    );
    MoveWindow(
        bindingHotkeyEdit_,
        editorContentX + editorLabelWidth,
        editorY,
        editorContentWidth - editorLabelWidth - editorButtonWidth - ButtonGap,
        26,
        TRUE
    );
    MoveWindow(
        captureHotkeyButton_,
        editorContentX + editorContentWidth - editorButtonWidth,
        editorY,
        editorButtonWidth,
        26,
        TRUE
    );

    editorY += 36;

    MoveWindow(
        bindingFileCaption_,
        editorContentX,
        editorY + 5,
        editorLabelWidth,
        22,
        TRUE
    );
    MoveWindow(
        bindingFileEdit_,
        editorContentX + editorLabelWidth,
        editorY,
        editorContentWidth - editorLabelWidth - editorButtonWidth - ButtonGap,
        26,
        TRUE
    );
    MoveWindow(
        browseSoundButton_,
        editorContentX + editorContentWidth - editorButtonWidth,
        editorY,
        editorButtonWidth,
        26,
        TRUE
    );

    editorY += 36;

    MoveWindow(
        bindingModeCaption_,
        editorContentX,
        editorY + 5,
        editorLabelWidth,
        22,
        TRUE
    );
    MoveWindow(
        bindingModeCombo_,
        editorContentX + editorLabelWidth,
        editorY,
        editorContentWidth - editorLabelWidth,
        140,
        TRUE
    );

    editorY += 36;

    MoveWindow(
        bindingVolumeCaption_,
        editorContentX,
        editorY + 5,
        editorLabelWidth,
        22,
        TRUE
    );
    MoveWindow(
        bindingVolumeSlider_,
        editorContentX + editorLabelWidth,
        editorY,
        editorContentWidth - editorLabelWidth - 46,
        28,
        TRUE
    );
    MoveWindow(
        bindingVolumeValue_,
        editorContentX + editorContentWidth - 42,
        editorY + 5,
        42,
        22,
        TRUE
    );

    const int actionGap = 6;
    const int actionWidth =
        (editorContentWidth - actionGap) / 2;
    const int actionY = std::max(
        editorY + 42,
        bindingsInnerY + bindingsInnerHeight - ButtonHeight * 2 - actionGap - 12
    );

    MoveWindow(
        addBindingButton_,
        editorContentX,
        actionY,
        actionWidth,
        ButtonHeight,
        TRUE
    );
    MoveWindow(
        updateBindingButton_,
        editorContentX + actionWidth + actionGap,
        actionY,
        actionWidth,
        ButtonHeight,
        TRUE
    );
    MoveWindow(
        removeBindingButton_,
        editorContentX,
        actionY + ButtonHeight + actionGap,
        actionWidth,
        ButtonHeight,
        TRUE
    );
    MoveWindow(
        clearBindingButton_,
        editorContentX + actionWidth + actionGap,
        actionY + ButtonHeight + actionGap,
        actionWidth,
        ButtonHeight,
        TRUE
    );

    y += bindingsHeight + 8;

    const HWND firstRow[] = {
        reloadButton_, stopButton_, outputMuteButton_, monitorMuteButton_
    };
    const HWND secondRow[] = {
        openConfigButton_, openSoundsButton_, consoleButton_, exitButton_
    };

    for (std::size_t index = 0; index < 4; ++index)
    {
        const int x = BaseMargin +
            static_cast<int>(index) * (buttonWidth + ButtonGap);
        MoveWindow(firstRow[index], x, y, buttonWidth, ButtonHeight, TRUE);
    }

    y += ButtonHeight + ButtonGap;

    for (std::size_t index = 0; index < 4; ++index)
    {
        const int x = BaseMargin +
            static_cast<int>(index) * (buttonWidth + ButtonGap);
        MoveWindow(secondRow[index], x, y, buttonWidth, ButtonHeight, TRUE);
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

    SetControlText(
        controlHotkeysGroup_,
        Localization::Text(L"Kontrol hotkey'leri", L"Control hotkeys")
    );
    SetControlText(stopHotkeyCaption_, Localization::Text(L"Tümünü durdur", L"Stop all"));
    SetControlText(outputMuteHotkeyCaption_, Localization::Text(L"Ana mute", L"Main mute"));
    SetControlText(monitorMuteHotkeyCaption_, Localization::Text(L"Monitör mute", L"Monitor mute"));
    SetControlText(reloadHotkeyCaption_, Localization::Text(L"Reload", L"Reload"));
    SetControlText(exitHotkeyCaption_, Localization::Text(L"Çıkış", L"Exit"));

    SetControlText(bindingsGroup_, Localization::Text(L"Ses atamaları", L"Sound bindings"));
    SetControlText(bindingEditorGroup_, Localization::Text(L"Atama düzenleyici", L"Binding editor"));
    SetControlText(bindingHotkeyCaption_, Localization::Text(L"Hotkey:", L"Hotkey:"));
    SetControlText(bindingFileCaption_, Localization::Text(L"Ses:", L"Sound:"));
    SetControlText(bindingModeCaption_, Localization::Text(L"Mod:", L"Mode:"));
    SetControlText(bindingVolumeCaption_, Localization::Text(L"Ses:", L"Volume:"));
    SetControlText(
        captureHotkeyButton_,
        capturingBindingHotkey_
            ? Localization::Text(L"Tuşa bas...", L"Press key...")
            : Localization::Text(L"Yakala", L"Capture")
    );
    SetControlText(browseSoundButton_, Localization::Text(L"Gözat", L"Browse"));
    SetControlText(addBindingButton_, Localization::Text(L"Yeni ekle", L"Add new"));
    SetControlText(updateBindingButton_, Localization::Text(L"Seçileni güncelle", L"Update selected"));
    SetControlText(removeBindingButton_, Localization::Text(L"Seçileni sil", L"Remove selected"));
    SetControlText(clearBindingButton_, Localization::Text(L"Alanları temizle", L"Clear fields"));

    SetControlText(reloadButton_, Localization::Text(L"Config'i yenile", L"Reload config"));
    SetControlText(stopButton_, Localization::Text(L"Tümünü durdur", L"Stop all"));
    SetControlText(outputMuteButton_, Localization::Text(L"Ana çıkışı sustur/aç", L"Toggle main mute"));
    SetControlText(monitorMuteButton_, Localization::Text(L"Monitörü sustur/aç", L"Toggle monitor mute"));
    SetControlText(openConfigButton_, Localization::Text(L"Config'i aç", L"Open config"));
    SetControlText(openSoundsButton_, Localization::Text(L"Ses klasörünü aç", L"Open sounds folder"));
    SetControlText(consoleButton_, Localization::Text(L"Konsolu göster/gizle", L"Show/hide console"));
    SetControlText(exitButton_, Localization::Text(L"Programı kapat", L"Exit"));
}

void ControlWindow::PopulateBindings()
{
    if (bindingsList_ == nullptr)
    {
        return;
    }

    SendMessageW(bindingsList_, LB_RESETCONTENT, 0, 0);

    int maximumWidth = 0;
    HDC deviceContext = GetDC(bindingsList_);

    for (const SoundBinding& binding : pendingBindings_)
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

    if (selectedBindingIndex_ >= 0 &&
        static_cast<std::size_t>(selectedBindingIndex_) <
            pendingBindings_.size())
    {
        SendMessageW(
            bindingsList_,
            LB_SETCURSEL,
            static_cast<WPARAM>(selectedBindingIndex_),
            0
        );
    }
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

void ControlWindow::PopulateControlHotkeys()
{
    SetControlText(
        stopHotkeyEdit_,
        Utf8ToWide(currentConfig_.GetStopKeyName())
    );
    SetControlText(
        outputMuteHotkeyEdit_,
        Utf8ToWide(currentConfig_.GetOutputMuteKeyName())
    );
    SetControlText(
        monitorMuteHotkeyEdit_,
        Utf8ToWide(currentConfig_.GetMonitorMuteKeyName())
    );
    SetControlText(
        reloadHotkeyEdit_,
        Utf8ToWide(currentConfig_.GetReloadKeyName())
    );
    SetControlText(
        exitHotkeyEdit_,
        Utf8ToWide(currentConfig_.GetExitKeyName())
    );
}

void ControlWindow::UpdateBindingVolumeLabel()
{
    if (bindingVolumeSlider_ == nullptr)
    {
        return;
    }

    const LRESULT volume = SendMessageW(
        bindingVolumeSlider_,
        TBM_GETPOS,
        0,
        0
    );

    SetControlText(
        bindingVolumeValue_,
        std::to_wstring(volume) + L"%"
    );
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

    if (pendingBindings_.empty())
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"En az bir ses ataması olmalı.",
                L"At least one sound binding is required."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    const std::wstring stopHotkey = GetControlText(stopHotkeyEdit_);
    const std::wstring outputMuteHotkey =
        GetControlText(outputMuteHotkeyEdit_);
    const std::wstring monitorMuteHotkey =
        GetControlText(monitorMuteHotkeyEdit_);
    const std::wstring reloadHotkey = GetControlText(reloadHotkeyEdit_);
    const std::wstring exitHotkey = GetControlText(exitHotkeyEdit_);

    if (stopHotkey.empty() || outputMuteHotkey.empty() ||
        monitorMuteHotkey.empty() || reloadHotkey.empty() ||
        exitHotkey.empty())
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Kontrol hotkey alanları boş bırakılamaz.",
                L"Control hotkey fields cannot be empty."
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
    const bool hotkeysAccepted = candidate.SetControlHotkeys(
        WideToUtf8(stopHotkey),
        WideToUtf8(outputMuteHotkey),
        WideToUtf8(monitorMuteHotkey),
        WideToUtf8(reloadHotkey),
        WideToUtf8(exitHotkey)
    );
    const bool bindingsAccepted =
        hotkeysAccepted && candidate.SetBindings(pendingBindings_);

    if (!hotkeysAccepted || !bindingsAccepted)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Hotkey veya ses atamalarından biri geçersiz ya da başka bir atamayla çakışıyor.",
                L"A hotkey or sound binding is invalid or conflicts with another assignment."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

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
        L"Yeni ayarlar, hotkey'ler ve ses atamaları doğrulanıyor...",
        L"Validating settings, hotkeys, and sound bindings..."
    ));
    PostApplicationCommand(commandIds_.applySettings);
    return true;
}

void ControlWindow::LoadSelectedBindingIntoEditor()
{
    if (bindingsList_ == nullptr)
    {
        return;
    }

    const LRESULT selected = SendMessageW(
        bindingsList_,
        LB_GETCURSEL,
        0,
        0
    );

    if (selected == LB_ERR || selected < 0 ||
        static_cast<std::size_t>(selected) >= pendingBindings_.size())
    {
        selectedBindingIndex_ = -1;
        return;
    }

    selectedBindingIndex_ = static_cast<int>(selected);
    const SoundBinding& binding =
        pendingBindings_[static_cast<std::size_t>(selectedBindingIndex_)];

    SetControlText(bindingHotkeyEdit_, Utf8ToWide(binding.keyName));
    SetControlText(bindingFileEdit_, binding.soundFile.wstring());
    SelectComboText(
        bindingModeCombo_,
        Utf8ToWide(std::string{PlaybackModeName(binding.mode)})
    );
    SendMessageW(
        bindingVolumeSlider_,
        TBM_SETPOS,
        TRUE,
        static_cast<LPARAM>(binding.volume * 100.0f + 0.5f)
    );
    UpdateBindingVolumeLabel();

    SetStatus(Localization::Text(
        L"Seçili ses ataması düzenleyiciye yüklendi.",
        L"The selected sound binding was loaded into the editor."
    ));
}

void ControlWindow::ClearBindingEditor()
{
    selectedBindingIndex_ = -1;
    capturingBindingHotkey_ = false;

    if (bindingsList_ != nullptr)
    {
        SendMessageW(bindingsList_, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    }

    SetControlText(bindingHotkeyEdit_, L"");
    SetControlText(bindingFileEdit_, L"");
    SelectComboText(bindingModeCombo_, L"restart");
    SendMessageW(bindingVolumeSlider_, TBM_SETPOS, TRUE, 100);
    UpdateBindingVolumeLabel();
    RefreshLocalizedText();
}

bool ControlWindow::AddOrUpdateBinding(const bool updateExisting)
{
    std::wstring hotkeyText = GetControlText(bindingHotkeyEdit_);
    std::wstring fileText = GetControlText(bindingFileEdit_);

    const auto trim = [](std::wstring& value)
    {
        const std::size_t first = value.find_first_not_of(L" \t\r\n");

        if (first == std::wstring::npos)
        {
            value.clear();
            return;
        }

        const std::size_t last = value.find_last_not_of(L" \t\r\n");
        value = value.substr(first, last - first + 1);
    };

    trim(hotkeyText);
    trim(fileText);

    if (hotkeyText.empty() || fileText.empty())
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Hotkey ve ses dosyası alanları boş bırakılamaz.",
                L"Hotkey and sound-file fields cannot be empty."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    std::filesystem::path relativePath{fileText};

    if (relativePath.has_root_path())
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Ses dosyası sounds klasörüne göre göreli olmalı. Gözat düğmesini kullanabilirsin.",
                L"The sound file must be relative to the sounds folder. You can use the Browse button."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    for (const auto& component : relativePath)
    {
        if (component == L"..")
        {
            MessageBoxW(
                window_,
                Localization::Text(
                    L"Ses dosyası sounds klasörünün dışına çıkamaz.",
                    L"The sound file cannot leave the sounds folder."
                ),
                L"SoundBoardFasaFiso",
                MB_OK | MB_ICONWARNING
            );
            return false;
        }
    }

    if (!SoundFileFormat::IsSupported(relativePath))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Yalnızca WAV, MP3 ve FLAC dosyaları destekleniyor.",
                L"Only WAV, MP3, and FLAC files are supported."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    std::error_code fileError;
    const std::filesystem::path fullPath =
        (soundsFolder_ / relativePath).lexically_normal();

    if (!std::filesystem::is_regular_file(fullPath, fileError) || fileError)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Seçilen ses dosyası sounds klasöründe bulunamadı.",
                L"The selected sound file was not found in the sounds folder."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    const std::wstring normalizedHotkey =
        NormalizeHotkeyText(hotkeyText);

    const HWND controlHotkeyEdits[] = {
        stopHotkeyEdit_, outputMuteHotkeyEdit_, monitorMuteHotkeyEdit_,
        reloadHotkeyEdit_, exitHotkeyEdit_
    };

    for (const HWND edit : controlHotkeyEdits)
    {
        if (NormalizeHotkeyText(GetControlText(edit)) == normalizedHotkey)
        {
            MessageBoxW(
                window_,
                Localization::Text(
                    L"Bu hotkey kontrol tuşlarından biri tarafından kullanılıyor.",
                    L"This hotkey is already used by a control command."
                ),
                L"SoundBoardFasaFiso",
                MB_OK | MB_ICONWARNING
            );
            return false;
        }
    }

    for (std::size_t index = 0; index < pendingBindings_.size(); ++index)
    {
        if (updateExisting &&
            static_cast<int>(index) == selectedBindingIndex_)
        {
            continue;
        }

        if (NormalizeHotkeyText(
                Utf8ToWide(pendingBindings_[index].keyName)
            ) == normalizedHotkey)
        {
            MessageBoxW(
                window_,
                Localization::Text(
                    L"Bu hotkey başka bir ses atamasında kullanılıyor.",
                    L"This hotkey is already used by another sound binding."
                ),
                L"SoundBoardFasaFiso",
                MB_OK | MB_ICONWARNING
            );
            return false;
        }
    }

    if (updateExisting &&
        (selectedBindingIndex_ < 0 ||
            static_cast<std::size_t>(selectedBindingIndex_) >=
                pendingBindings_.size()))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Güncellemek için önce listeden bir atama seç.",
                L"Select a binding from the list before updating it."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONINFORMATION
        );
        return false;
    }

    const int modeIndex = static_cast<int>(SendMessageW(
        bindingModeCombo_,
        CB_GETCURSEL,
        0,
        0
    ));

    PlaybackMode mode = PlaybackMode::Restart;

    if (modeIndex == 1)
    {
        mode = PlaybackMode::Toggle;
    }
    else if (modeIndex == 2)
    {
        mode = PlaybackMode::Loop;
    }
    else if (modeIndex == 3)
    {
        mode = PlaybackMode::Overlap;
    }

    const int volume = static_cast<int>(SendMessageW(
        bindingVolumeSlider_,
        TBM_GETPOS,
        0,
        0
    ));

    SoundBinding binding;
    binding.keyName = WideToUtf8(hotkeyText);
    binding.soundFile = relativePath.lexically_normal();
    binding.volume = static_cast<float>(volume) / 100.0f;
    binding.mode = mode;

    if (updateExisting)
    {
        pendingBindings_[
            static_cast<std::size_t>(selectedBindingIndex_)
        ] = std::move(binding);

        SetStatus(Localization::Text(
            L"Ses ataması güncellendi. Değişiklikleri etkinleştirmek için Kaydet ve uygula'ya bas.",
            L"Sound binding updated. Click Save and apply to activate the changes."
        ));
    }
    else
    {
        pendingBindings_.push_back(std::move(binding));
        selectedBindingIndex_ =
            static_cast<int>(pendingBindings_.size() - 1);

        SetStatus(Localization::Text(
            L"Yeni ses ataması eklendi. Değişiklikleri etkinleştirmek için Kaydet ve uygula'ya bas.",
            L"New sound binding added. Click Save and apply to activate the changes."
        ));
    }

    PopulateBindings();
    LoadSelectedBindingIntoEditor();
    return true;
}

bool ControlWindow::RemoveSelectedBinding()
{
    if (selectedBindingIndex_ < 0 ||
        static_cast<std::size_t>(selectedBindingIndex_) >=
            pendingBindings_.size())
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Silmek için önce listeden bir atama seç.",
                L"Select a binding from the list before removing it."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONINFORMATION
        );
        return false;
    }

    if (pendingBindings_.size() == 1)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Son ses ataması silinemez. Config içinde en az bir atama olmalı.",
                L"The last sound binding cannot be removed. The config must contain at least one binding."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    const int answer = MessageBoxW(
        window_,
        Localization::Text(
            L"Seçili ses ataması silinsin mi? Ses dosyasının kendisi silinmeyecek.",
            L"Remove the selected sound binding? The audio file itself will not be deleted."
        ),
        L"SoundBoardFasaFiso",
        MB_YESNO | MB_ICONQUESTION
    );

    if (answer != IDYES)
    {
        return false;
    }

    pendingBindings_.erase(
        pendingBindings_.begin() + selectedBindingIndex_
    );

    selectedBindingIndex_ = -1;
    PopulateBindings();
    ClearBindingEditor();
    SetStatus(Localization::Text(
        L"Ses ataması kaldırıldı. Kaydet ve uygula'ya basınca etkinleşecek.",
        L"Sound binding removed. It will take effect after Save and apply."
    ));
    return true;
}

void ControlWindow::BrowseForSoundFile()
{
    wchar_t selectedPath[32768]{};
    const std::wstring initialFolder = soundsFolder_.wstring();

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFile = selectedPath;
    dialog.nMaxFile = static_cast<DWORD>(std::size(selectedPath));
    dialog.lpstrFilter =
        L"Audio files (*.wav;*.mp3;*.flac)\0*.wav;*.mp3;*.flac\0"
        L"WAV files (*.wav)\0*.wav\0"
        L"MP3 files (*.mp3)\0*.mp3\0"
        L"FLAC files (*.flac)\0*.flac\0"
        L"All files (*.*)\0*.*\0\0";
    dialog.lpstrInitialDir = initialFolder.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameW(&dialog) == FALSE)
    {
        return;
    }

    const std::filesystem::path imported =
        ImportSoundFile(std::filesystem::path{selectedPath});

    if (imported.empty())
    {
        return;
    }

    SetControlText(bindingFileEdit_, imported.wstring());
    SetStatus(Localization::Text(
        L"Ses dosyası atama düzenleyicisine eklendi.",
        L"The audio file was added to the binding editor."
    ));
}

void ControlWindow::BeginHotkeyCapture()
{
    capturingBindingHotkey_ = !capturingBindingHotkey_;
    RefreshLocalizedText();

    if (capturingBindingHotkey_)
    {
        SetFocus(window_);
        SetStatus(Localization::Text(
            L"Yeni hotkey kombinasyonuna bas. CTRL, SHIFT, ALT ve WIN kullanılabilir.",
            L"Press the new hotkey combination. CTRL, SHIFT, ALT, and WIN are supported."
        ));
    }
    else
    {
        SetStatus(Localization::Text(
            L"Hotkey yakalama iptal edildi.",
            L"Hotkey capture cancelled."
        ));
    }
}

bool ControlWindow::CaptureHotkeyFromMessage(const WPARAM virtualKey)
{
    const unsigned int key = static_cast<unsigned int>(virtualKey);

    if (key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
        key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
        key == VK_MENU || key == VK_LMENU || key == VK_RMENU ||
        key == VK_LWIN || key == VK_RWIN)
    {
        return true;
    }

    const std::wstring baseKey = VirtualKeyName(key);

    if (baseKey.empty())
    {
        SetStatus(Localization::Text(
            L"Bu tuş desteklenmiyor; başka bir ana tuşa bas.",
            L"This key is not supported; press another primary key."
        ));
        return true;
    }

    std::wstring hotkey;

    const auto appendPart = [&hotkey](const std::wstring_view part)
    {
        if (!hotkey.empty())
        {
            hotkey += L'+';
        }

        hotkey += part;
    };

    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
    {
        appendPart(L"CTRL");
    }

    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
    {
        appendPart(L"SHIFT");
    }

    if ((GetKeyState(VK_MENU) & 0x8000) != 0)
    {
        appendPart(L"ALT");
    }

    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 ||
        (GetKeyState(VK_RWIN) & 0x8000) != 0)
    {
        appendPart(L"WIN");
    }

    appendPart(baseKey);

    SetControlText(bindingHotkeyEdit_, hotkey);
    capturingBindingHotkey_ = false;
    RefreshLocalizedText();
    SetStatus(Localization::Text(
        L"Hotkey yakalandı.",
        L"Hotkey captured."
    ));
    return true;
}

std::filesystem::path ControlWindow::ImportSoundFile(
    const std::filesystem::path& selectedPath
)
{
    if (selectedPath.empty() ||
        !SoundFileFormat::IsSupported(selectedPath))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Yalnızca WAV, MP3 ve FLAC dosyaları seçilebilir.",
                L"Only WAV, MP3, and FLAC files can be selected."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return {};
    }

    std::error_code error;
    const std::filesystem::path source =
        std::filesystem::weakly_canonical(selectedPath, error);

    if (error || !std::filesystem::is_regular_file(source, error) || error)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Seçilen ses dosyası okunamıyor.",
                L"The selected audio file cannot be read."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return {};
    }

    error.clear();
    std::filesystem::create_directories(soundsFolder_, error);

    if (error)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"sounds klasörü oluşturulamadı.",
                L"The sounds folder could not be created."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return {};
    }

    const std::filesystem::path soundsRoot =
        std::filesystem::weakly_canonical(soundsFolder_, error);

    if (!error)
    {
        error.clear();
        const std::filesystem::path relative =
            std::filesystem::relative(source, soundsRoot, error);

        bool staysInside = !error && !relative.empty() &&
            !relative.has_root_path();

        if (staysInside)
        {
            for (const auto& component : relative)
            {
                if (component == L"..")
                {
                    staysInside = false;
                    break;
                }
            }
        }

        if (staysInside)
        {
            return relative.lexically_normal();
        }
    }

    const int answer = MessageBoxW(
        window_,
        Localization::Text(
            L"Seçilen dosya sounds klasörünün dışında. Portable kullanım için sounds klasörüne kopyalansın mı?",
            L"The selected file is outside the sounds folder. Copy it into the sounds folder for portable use?"
        ),
        L"SoundBoardFasaFiso",
        MB_YESNO | MB_ICONQUESTION
    );

    if (answer != IDYES)
    {
        return {};
    }

    std::filesystem::path destination =
        soundsFolder_ / source.filename();

    if (std::filesystem::exists(destination, error) && !error)
    {
        error.clear();

        if (std::filesystem::equivalent(source, destination, error) &&
            !error)
        {
            return destination.filename();
        }

        const std::filesystem::path stem = source.stem();
        const std::filesystem::path extension = source.extension();

        for (unsigned int suffix = 2; suffix < 10000; ++suffix)
        {
            destination = soundsFolder_ /
                (stem.wstring() + L"_" + std::to_wstring(suffix) +
                    extension.wstring());

            error.clear();

            if (!std::filesystem::exists(destination, error) && !error)
            {
                break;
            }
        }
    }

    error.clear();
    std::filesystem::copy_file(
        source,
        destination,
        std::filesystem::copy_options::none,
        error
    );

    if (error)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Ses dosyası sounds klasörüne kopyalanamadı.",
                L"The audio file could not be copied into the sounds folder."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return {};
    }

    return destination.filename();
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

std::wstring ControlWindow::VirtualKeyName(
    const unsigned int virtualKey
)
{
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24)
    {
        return L"F" + std::to_wstring(virtualKey - VK_F1 + 1);
    }

    if ((virtualKey >= 'A' && virtualKey <= 'Z') ||
        (virtualKey >= '0' && virtualKey <= '9'))
    {
        return std::wstring{
            1,
            static_cast<wchar_t>(virtualKey)
        };
    }

    if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)
    {
        return L"NUMPAD" +
            std::to_wstring(virtualKey - VK_NUMPAD0);
    }

    const std::pair<unsigned int, std::wstring_view> namedKeys[] = {
        {VK_SPACE, L"SPACE"},
        {VK_TAB, L"TAB"},
        {VK_RETURN, L"ENTER"},
        {VK_ESCAPE, L"ESC"},
        {VK_BACK, L"BACKSPACE"},
        {VK_INSERT, L"INSERT"},
        {VK_DELETE, L"DELETE"},
        {VK_HOME, L"HOME"},
        {VK_END, L"END"},
        {VK_PRIOR, L"PAGEUP"},
        {VK_NEXT, L"PAGEDOWN"},
        {VK_UP, L"UP"},
        {VK_DOWN, L"DOWN"},
        {VK_LEFT, L"LEFT"},
        {VK_RIGHT, L"RIGHT"},
        {VK_ADD, L"NUMPAD_ADD"},
        {VK_SUBTRACT, L"NUMPAD_SUBTRACT"},
        {VK_MULTIPLY, L"NUMPAD_MULTIPLY"},
        {VK_DIVIDE, L"NUMPAD_DIVIDE"},
        {VK_DECIMAL, L"NUMPAD_DECIMAL"}
    };

    for (const auto& [key, name] : namedKeys)
    {
        if (virtualKey == key)
        {
            return std::wstring{name};
        }
    }

    return {};
}

std::wstring ControlWindow::NormalizeHotkeyText(
    std::wstring text
)
{
    text.erase(
        std::remove_if(
            text.begin(),
            text.end(),
            [](const wchar_t character)
            {
                return std::iswspace(character) != 0;
            }
        ),
        text.end()
    );

    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](const wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        }
    );

    return text;
}
