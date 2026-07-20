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
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
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

    constexpr int BaseMargin = 14;
    constexpr int HeaderHeight = 32;
    constexpr int SubtitleHeight = 18;
    constexpr int StatusHeight = 38;
    constexpr int SettingsGroupHeight = 300;
    constexpr int ControlHotkeysGroupHeight = 88;
    constexpr int BindingEditorWidth = 360;
    constexpr int ButtonHeight = 34;
    constexpr int ButtonGap = 8;
    constexpr int RowHeight = 28;
    constexpr int ThemeToggleWidth = 148;
    constexpr int CardRadius = 12;
    constexpr int MaximumContentWidth = 1260;

    COLORREF BlendColor(
        const COLORREF first,
        const COLORREF second,
        const int secondWeight
    )
    {
        const int firstWeight = 100 - secondWeight;
        return RGB(
            (GetRValue(first) * firstWeight +
                GetRValue(second) * secondWeight) / 100,
            (GetGValue(first) * firstWeight +
                GetGValue(second) * secondWeight) / 100,
            (GetBValue(first) * firstWeight +
                GetBValue(second) * secondWeight) / 100
        );
    }

    void FillRoundedRectangle(
        const HDC deviceContext,
        const RECT& rectangle,
        const COLORREF color,
        const int radius
    )
    {
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_NULL, 0, color);
        const HGDIOBJ oldBrush = SelectObject(deviceContext, brush);
        const HGDIOBJ oldPen = SelectObject(deviceContext, pen);

        RoundRect(
            deviceContext,
            rectangle.left,
            rectangle.top,
            rectangle.right,
            rectangle.bottom,
            radius,
            radius
        );

        SelectObject(deviceContext, oldPen);
        SelectObject(deviceContext, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    void DrawRoundedBorder(
        const HDC deviceContext,
        const RECT& rectangle,
        const COLORREF color,
        const int radius,
        const int width = 1
    )
    {
        HPEN pen = CreatePen(PS_SOLID, width, color);
        const HGDIOBJ oldPen = SelectObject(deviceContext, pen);
        const HGDIOBJ oldBrush = SelectObject(
            deviceContext,
            GetStockObject(HOLLOW_BRUSH)
        );

        RoundRect(
            deviceContext,
            rectangle.left,
            rectangle.top,
            rectangle.right,
            rectangle.bottom,
            radius,
            radius
        );

        SelectObject(deviceContext, oldBrush);
        SelectObject(deviceContext, oldPen);
        DeleteObject(pen);
    }

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
    const std::vector<std::string>& captureDevices,
    const ControlWindowCommandIds& commandIds,
    Audio* const audio
)
{
    Shutdown();

    instance_ = GetModuleHandleW(nullptr);
    mainThreadId_ = GetCurrentThreadId();
    commandIds_ = commandIds;
    audio_ = audio;
    configPath_ = configPath;
    pendingConfigPath_ = configPath;
    pendingConfigPath_ += L".pending";
    soundsFolder_ = soundsFolder;
    logsFolder_ = configPath.parent_path() / L"logs";
    currentConfig_ = config;
    activeTheme_ = config.GetTheme();
    playbackDevices_ = playbackDevices;
    captureDevices_ = captureDevices;

    if (instance_ == nullptr)
    {
        return false;
    }

    RecreateThemeResources();

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
    windowClass.hbrBackground = nullptr;
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

    SetTimer(
        window_,
        LevelMeterTimerId,
        LevelMeterIntervalMilliseconds,
        nullptr
    );
    UpdateLevelMeters();

    return true;
}

void ControlWindow::Shutdown()
{
    if (window_ != nullptr)
    {
        KillTimer(window_, LevelMeterTimerId);
        DestroyWindow(window_);
        window_ = nullptr;
    }

    if (classRegistered_ && instance_ != nullptr)
    {
        UnregisterClassW(ControlWindowClassName, instance_);
    }

    ReleaseThemeResources();

    classRegistered_ = false;
    instance_ = nullptr;
    mainThreadId_ = 0;
    commandIds_ = {};
    audio_ = nullptr;
    configPath_.clear();
    pendingConfigPath_.clear();
    soundsFolder_.clear();
    logsFolder_.clear();
    playbackDevices_.clear();
    captureDevices_.clear();
    pendingBindings_.clear();
    selectedBindingIndex_ = -1;
    capturingBindingHotkey_ = false;

    headerLabel_ = nullptr;
    subtitleLabel_ = nullptr;
    themeToggleButton_ = nullptr;
    statusCaption_ = nullptr;
    statusValue_ = nullptr;
    settingsGroup_ = nullptr;
    outputCaption_ = nullptr;
    outputCombo_ = nullptr;
    outputVolumeCaption_ = nullptr;
    outputVolumeSlider_ = nullptr;
    outputLevelMeter_ = nullptr;
    outputVolumeValue_ = nullptr;
    monitorCaption_ = nullptr;
    monitorCombo_ = nullptr;
    monitorVolumeCaption_ = nullptr;
    monitorVolumeSlider_ = nullptr;
    monitorLevelMeter_ = nullptr;
    monitorVolumeValue_ = nullptr;
    microphoneCaption_ = nullptr;
    microphoneCombo_ = nullptr;
    microphoneVolumeCaption_ = nullptr;
    microphoneVolumeSlider_ = nullptr;
    microphoneLevelMeter_ = nullptr;
    microphoneVolumeValue_ = nullptr;
    microphoneEnabledCheck_ = nullptr;
    microphoneToOutputCheck_ = nullptr;
    microphoneToMonitorCheck_ = nullptr;
    sampleRateCaption_ = nullptr;
    sampleRateCombo_ = nullptr;
    bufferCaption_ = nullptr;
    bufferCombo_ = nullptr;
    languageCaption_ = nullptr;
    languageCombo_ = nullptr;
    startWithWindowsCheck_ = nullptr;
    showConsoleOnStartCheck_ = nullptr;
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
    openLogsButton_ = nullptr;
    consoleButton_ = nullptr;
    exitButton_ = nullptr;

    outputMeterLevel_ = 0.0f;
    monitorMeterLevel_ = 0.0f;
    microphoneMeterLevel_ = 0.0f;
    outputMeterAvailable_ = false;
    monitorMeterAvailable_ = false;
    microphoneMeterAvailable_ = false;
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
    activeTheme_ = config.GetTheme();
    pendingBindings_ = config.GetBindings();
    selectedBindingIndex_ = -1;
    capturingBindingHotkey_ = false;

    RecreateThemeResources();
    ApplyFonts();
    ApplyTheme();
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

void ControlWindow::SetCaptureDevices(
    const std::vector<std::string>& captureDevices
)
{
    captureDevices_ = captureDevices;

    if (window_ != nullptr)
    {
        PopulateDeviceCombos();
        PopulateEditorControls();
    }
}

void ControlWindow::SetStatus(const std::wstring& status)
{
    SetControlText(statusValue_, status);

    if (window_ != nullptr)
    {
        RedrawWindow(
            window_,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
                RDW_UPDATENOW
        );
    }
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
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintWindowBackground();
            return 0;

        case WM_DRAWITEM:
            if (lParam != 0)
            {
                DrawOwnerDrawControl(
                    *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam)
                );
                return TRUE;
            }
            break;

        case WM_CTLCOLORSTATIC:
        {
            const HDC deviceContext = reinterpret_cast<HDC>(wParam);
            const HWND control = reinterpret_cast<HWND>(lParam);
            SetTextColor(
                deviceContext,
                control == subtitleLabel_ || control == statusCaption_
                    ? mutedTextColor_
                    : textColor_
            );
            SetBkMode(deviceContext, TRANSPARENT);
            return reinterpret_cast<LRESULT>(StaticBrushFor(control));
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        {
            const HDC deviceContext = reinterpret_cast<HDC>(wParam);
            SetTextColor(deviceContext, textColor_);
            SetBkColor(deviceContext, inputColor_);
            return reinterpret_cast<LRESULT>(inputBrush_);
        }

        case WM_CTLCOLORBTN:
        {
            const HDC deviceContext = reinterpret_cast<HDC>(wParam);
            SetTextColor(deviceContext, textColor_);
            SetBkColor(deviceContext, cardColor_);
            SetBkMode(deviceContext, TRANSPARENT);
            return reinterpret_cast<LRESULT>(cardBrush_);
        }

        case WM_NOTIFY:
            if (lParam != 0)
            {
                const auto* customDraw =
                    reinterpret_cast<const NMCUSTOMDRAW*>(lParam);

                if (customDraw->hdr.code == NM_CUSTOMDRAW &&
                    customDraw->dwDrawStage == CDDS_PREPAINT &&
                    IsSliderControl(customDraw->hdr.hwndFrom))
                {
                    DrawModernSlider(
                        customDraw->hdr.hwndFrom,
                        customDraw->hdc
                    );
                    return CDRF_SKIPDEFAULT;
                }
            }
            break;

        case WM_TIMER:
            if (wParam == LevelMeterTimerId)
            {
                UpdateLevelMeters();
                return 0;
            }
            break;

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

                case IdThemeToggle:
                    activeTheme_ = activeTheme_ == AppTheme::Dark
                        ? AppTheme::Light
                        : AppTheme::Dark;
                    RecreateThemeResources();
                    ApplyTheme();
                    RefreshLocalizedText();
                    SetStatus(Localization::Text(
                        L"Tema önizlemesi değiştirildi. Kalıcı olması için Kaydet ve uygula'ya basın.",
                        L"Theme preview changed. Select Save and apply to keep it."
                    ));
                    return 0;

                case IdRefreshDevices:
                {
                    SetStatus(Localization::Text(
                        L"Ses cihazları yenileniyor...",
                        L"Refreshing audio devices..."
                    ));

                    const std::vector<std::string> playbackDevices =
                        Audio::EnumeratePlaybackDevices();
                    const std::vector<std::string> captureDevices =
                        Audio::EnumerateCaptureDevices();

                    SetPlaybackDevices(playbackDevices);
                    SetCaptureDevices(captureDevices);

                    SetStatus(
                        playbackDevices.empty() && captureDevices.empty()
                            ? Localization::Text(
                                L"Ses cihazı listeleri alınamadı.",
                                L"The audio device lists could not be read."
                            )
                            : Localization::Text(
                                L"Çıkış ve mikrofon cihazları yenilendi.",
                                L"Playback and microphone devices refreshed."
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

                case IdOpenLogs:
                    OpenPath(logsFolder_);
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
        {
            const HWND slider = reinterpret_cast<HWND>(lParam);

            if (slider == outputVolumeSlider_ ||
                slider == monitorVolumeSlider_ ||
                slider == microphoneVolumeSlider_)
            {
                UpdateVolumeLabels();
                RedrawWindow(
                    slider,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW
                );
                return 0;
            }

            if (slider == bindingVolumeSlider_)
            {
                UpdateBindingVolumeLabel();
                RedrawWindow(
                    slider,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW
                );
                return 0;
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
    subtitleLabel_ = createControl(
        L"STATIC",
        L"",
        SS_LEFT,
        0
    );
    themeToggleButton_ = createControl(
        L"BUTTON",
        L"",
        BS_OWNERDRAW | WS_TABSTOP,
        IdThemeToggle
    );

    statusCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    statusValue_ = createControl(L"STATIC", L"", SS_LEFT, 0);

    settingsGroup_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
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
    outputLevelMeter_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
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
    monitorLevelMeter_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
    monitorVolumeValue_ = createControl(L"STATIC", L"", SS_RIGHT, 0);

    microphoneCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    microphoneCombo_ = createControl(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        0,
        WS_EX_CLIENTEDGE
    );
    microphoneVolumeCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    microphoneVolumeSlider_ = createControl(
        TRACKBAR_CLASSW,
        L"",
        TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
        IdMicrophoneVolumeSlider
    );
    microphoneLevelMeter_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
    microphoneVolumeValue_ = createControl(L"STATIC", L"", SS_RIGHT, 0);
    microphoneEnabledCheck_ = createControl(
        L"BUTTON",
        L"",
        BS_AUTOCHECKBOX | WS_TABSTOP,
        0
    );
    microphoneToOutputCheck_ = createControl(
        L"BUTTON",
        L"",
        BS_AUTOCHECKBOX | WS_TABSTOP,
        0
    );
    microphoneToMonitorCheck_ = createControl(
        L"BUTTON",
        L"",
        BS_AUTOCHECKBOX | WS_TABSTOP,
        0
    );

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
    startWithWindowsCheck_ = createControl(
        L"BUTTON",
        L"",
        BS_AUTOCHECKBOX | WS_TABSTOP,
        0
    );
    showConsoleOnStartCheck_ = createControl(
        L"BUTTON",
        L"",
        BS_AUTOCHECKBOX | WS_TABSTOP,
        0
    );
    refreshDevicesButton_ = createControl(
        L"BUTTON",
        L"",
        BS_OWNERDRAW | WS_TABSTOP,
        IdRefreshDevices
    );
    applySettingsButton_ = createControl(
        L"BUTTON",
        L"",
        BS_OWNERDRAW | WS_TABSTOP,
        IdApplySettings
    );

    SendMessageW(outputVolumeSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(monitorVolumeSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(microphoneVolumeSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(outputVolumeSlider_, TBM_SETPAGESIZE, 0, 5);
    SendMessageW(monitorVolumeSlider_, TBM_SETPAGESIZE, 0, 5);
    SendMessageW(microphoneVolumeSlider_, TBM_SETPAGESIZE, 0, 5);

    controlHotkeysGroup_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
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

    bindingsGroup_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
    bindingsList_ = createControl(
        L"LISTBOX",
        L"",
        WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT |
            LBS_NOTIFY | WS_TABSTOP,
        IdBindingsList,
        WS_EX_CLIENTEDGE
    );

    bindingEditorGroup_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
    bindingHotkeyCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    bindingHotkeyEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE
    );
    captureHotkeyButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdCaptureHotkey
    );
    bindingFileCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    bindingFileEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE
    );
    browseSoundButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdBrowseSound
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
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdAddBinding
    );
    updateBindingButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdUpdateBinding
    );
    removeBindingButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdRemoveBinding
    );
    clearBindingButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdClearBinding
    );

    SendMessageW(bindingVolumeSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(bindingVolumeSlider_, TBM_SETPAGESIZE, 0, 5);

    reloadButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdReload
    );
    stopButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdStopAll
    );
    outputMuteButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdOutputMute
    );
    monitorMuteButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdMonitorMute
    );
    openConfigButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdOpenConfig
    );
    openSoundsButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdOpenSounds
    );
    openLogsButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdOpenLogs
    );
    consoleButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdToggleConsole
    );
    exitButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdExit
    );

    AddComboItem(bindingModeCombo_, L"restart");
    AddComboItem(bindingModeCombo_, L"toggle");
    AddComboItem(bindingModeCombo_, L"loop");
    AddComboItem(bindingModeCombo_, L"overlap");

    const HWND controls[] = {
        headerLabel_, subtitleLabel_, themeToggleButton_,
        statusCaption_, statusValue_, settingsGroup_,
        outputCaption_, outputCombo_, outputVolumeCaption_,
        outputVolumeSlider_, outputLevelMeter_, outputVolumeValue_,
        monitorCaption_, monitorCombo_, monitorVolumeCaption_,
        monitorVolumeSlider_, monitorLevelMeter_, monitorVolumeValue_,
        microphoneCaption_, microphoneCombo_, microphoneVolumeCaption_,
        microphoneVolumeSlider_, microphoneLevelMeter_,
        microphoneVolumeValue_, microphoneEnabledCheck_,
        microphoneToOutputCheck_, microphoneToMonitorCheck_,
        sampleRateCaption_, sampleRateCombo_,
        bufferCaption_, bufferCombo_, languageCaption_, languageCombo_,
        startWithWindowsCheck_, showConsoleOnStartCheck_,
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
        openLogsButton_, consoleButton_, exitButton_
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

    const int availableWidth = std::max(0, clientWidth - BaseMargin * 2);
    const int contentWidth = std::min(availableWidth, MaximumContentWidth);
    const int contentX = (clientWidth - contentWidth) / 2;
    const int firstRowButtonWidth =
        (contentWidth - ButtonGap * 3) / 4;

    SendMessageW(window_, WM_SETREDRAW, FALSE, 0);

    int y = BaseMargin;

    MoveWindow(
        headerLabel_,
        contentX,
        y,
        contentWidth - ThemeToggleWidth - ButtonGap,
        HeaderHeight,
        TRUE
    );
    MoveWindow(
        themeToggleButton_,
        contentX + contentWidth - ThemeToggleWidth,
        y,
        ThemeToggleWidth,
        ButtonHeight,
        TRUE
    );
    MoveWindow(
        subtitleLabel_,
        contentX,
        y + HeaderHeight,
        contentWidth - ThemeToggleWidth - ButtonGap,
        SubtitleHeight,
        TRUE
    );
    y += HeaderHeight + SubtitleHeight + 8;

    MoveWindow(
        statusCaption_,
        contentX + 14,
        y + 9,
        70,
        20,
        TRUE
    );
    MoveWindow(
        statusValue_,
        contentX + 84,
        y + 9,
        contentWidth - 98,
        20,
        TRUE
    );
    y += StatusHeight + 8;

    MoveWindow(
        settingsGroup_,
        contentX,
        y,
        contentWidth,
        SettingsGroupHeight,
        TRUE
    );

    const int innerX = contentX + 14;
    const int innerWidth = contentWidth - 28;
    const int labelWidth = 104;
    const int comboWidth = std::clamp(
        innerWidth * 36 / 100,
        260,
        430
    );
    const int volumeCaptionWidth = 64;
    const int volumeValueWidth = 42;
    const int fieldGap = 6;
    const int sectionGap = 12;
    const int volumeSliderWidth = std::max(
        100,
        innerWidth - labelWidth - comboWidth - volumeCaptionWidth -
            volumeValueWidth - fieldGap * 3 - sectionGap
    );

    int rowY = y + 32;

    const auto layoutAudioRow = [=](
        const HWND caption,
        const HWND combo,
        const HWND volumeCaption,
        const HWND slider,
        const HWND levelMeter,
        const HWND volumeValue,
        const int currentY
    )
    {
        MoveWindow(caption, innerX, currentY + 4, labelWidth, 20, TRUE);
        MoveWindow(
            combo,
            innerX + labelWidth,
            currentY,
            comboWidth,
            200,
            TRUE
        );

        int volumeX = innerX + labelWidth + comboWidth + sectionGap;
        MoveWindow(
            volumeCaption,
            volumeX,
            currentY + 4,
            volumeCaptionWidth,
            20,
            TRUE
        );
        volumeX += volumeCaptionWidth + fieldGap;
        MoveWindow(
            slider,
            volumeX,
            currentY,
            volumeSliderWidth,
            20,
            TRUE
        );
        MoveWindow(
            levelMeter,
            volumeX + 7,
            currentY + 22,
            std::max(1, volumeSliderWidth - 14),
            5,
            TRUE
        );
        MoveWindow(
            volumeValue,
            volumeX + volumeSliderWidth + fieldGap,
            currentY + 4,
            volumeValueWidth,
            20,
            TRUE
        );
    };

    layoutAudioRow(
        outputCaption_, outputCombo_, outputVolumeCaption_,
        outputVolumeSlider_, outputLevelMeter_, outputVolumeValue_, rowY
    );
    rowY += RowHeight + 6;

    layoutAudioRow(
        monitorCaption_, monitorCombo_, monitorVolumeCaption_,
        monitorVolumeSlider_, monitorLevelMeter_, monitorVolumeValue_, rowY
    );
    rowY += RowHeight + 6;

    layoutAudioRow(
        microphoneCaption_, microphoneCombo_, microphoneVolumeCaption_,
        microphoneVolumeSlider_, microphoneLevelMeter_,
        microphoneVolumeValue_, rowY
    );
    rowY += RowHeight + 2;

    const int microphoneChecksX = innerX + labelWidth;
    const int microphoneChecksWidth = innerWidth - labelWidth;
    const int microphoneCheckGap = 8;
    const int microphoneCheckWidth =
        (microphoneChecksWidth - microphoneCheckGap * 2) / 3;

    MoveWindow(
        microphoneEnabledCheck_,
        microphoneChecksX,
        rowY,
        microphoneCheckWidth,
        22,
        TRUE
    );
    MoveWindow(
        microphoneToOutputCheck_,
        microphoneChecksX + microphoneCheckWidth + microphoneCheckGap,
        rowY,
        microphoneCheckWidth,
        22,
        TRUE
    );
    MoveWindow(
        microphoneToMonitorCheck_,
        microphoneChecksX + (microphoneCheckWidth + microphoneCheckGap) * 2,
        rowY,
        microphoneCheckWidth,
        22,
        TRUE
    );

    rowY += RowHeight + 2;

    const int halfWidth = (innerWidth - sectionGap) / 2;
    const int smallLabelWidth = 130;
    const int smallControlWidth = halfWidth - smallLabelWidth;

    MoveWindow(sampleRateCaption_, innerX, rowY + 4, smallLabelWidth, 20, TRUE);
    MoveWindow(
        sampleRateCombo_,
        innerX + smallLabelWidth,
        rowY,
        smallControlWidth,
        160,
        TRUE
    );

    const int rightColumnX = innerX + halfWidth + sectionGap;
    MoveWindow(bufferCaption_, rightColumnX, rowY + 4, smallLabelWidth, 20, TRUE);
    MoveWindow(
        bufferCombo_,
        rightColumnX + smallLabelWidth,
        rowY,
        smallControlWidth,
        160,
        TRUE
    );

    rowY += RowHeight + 6;

    MoveWindow(languageCaption_, innerX, rowY + 4, labelWidth, 20, TRUE);
    MoveWindow(languageCombo_, innerX + labelWidth, rowY, 170, 140, TRUE);

    rowY += RowHeight + 6;

    const int applicationCheckWidth = 194;
    MoveWindow(
        startWithWindowsCheck_,
        innerX,
        rowY + 4,
        applicationCheckWidth,
        22,
        TRUE
    );
    MoveWindow(
        showConsoleOnStartCheck_,
        innerX + applicationCheckWidth + 8,
        rowY + 4,
        applicationCheckWidth,
        22,
        TRUE
    );

    const int actionButtonWidth = 164;
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

    y += SettingsGroupHeight + 6;

    MoveWindow(
        controlHotkeysGroup_,
        contentX,
        y,
        contentWidth,
        ControlHotkeysGroupHeight,
        TRUE
    );

    const int hotkeyGap = 8;
    const int hotkeyInnerX = contentX + 14;
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

        MoveWindow(hotkeyCaptions[index], x, y + 27, hotkeyColumnWidth, 18, TRUE);
        MoveWindow(hotkeyEdits[index], x, y + 48, hotkeyColumnWidth, 25, TRUE);
    }

    y += ControlHotkeysGroupHeight + 6;

    const int buttonsAreaHeight = ButtonHeight * 2 + ButtonGap;
    const int availableBindingsHeight =
        clientHeight - y - buttonsAreaHeight - BaseMargin - 6;
    const int bindingsHeight = std::max(220, availableBindingsHeight);

    MoveWindow(bindingsGroup_, contentX, y, contentWidth, bindingsHeight, TRUE);

    const int bindingsInnerX = contentX + 14;
    const int bindingsInnerY = y + 31;
    const int bindingsInnerWidth = contentWidth - 28;
    const int bindingsInnerHeight = bindingsHeight - 43;
    const int editorWidth = std::clamp(
        bindingsInnerWidth * 38 / 100,
        330,
        BindingEditorWidth
    );
    const int bindingGap = 10;
    const int listWidth = std::max(
        250,
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

    const int editorMargin = 10;
    const int editorLabelWidth = 72;
    const int editorButtonWidth = 88;
    const int editorContentX = editorX + editorMargin;
    const int editorContentWidth = editorWidth - editorMargin * 2;
    int editorY = bindingsInnerY + 32;

    MoveWindow(
        bindingHotkeyCaption_,
        editorContentX,
        editorY + 4,
        editorLabelWidth,
        20,
        TRUE
    );
    MoveWindow(
        bindingHotkeyEdit_,
        editorContentX + editorLabelWidth,
        editorY,
        editorContentWidth - editorLabelWidth - editorButtonWidth - ButtonGap,
        25,
        TRUE
    );
    MoveWindow(
        captureHotkeyButton_,
        editorContentX + editorContentWidth - editorButtonWidth,
        editorY,
        editorButtonWidth,
        25,
        TRUE
    );

    editorY += 30;

    MoveWindow(
        bindingFileCaption_,
        editorContentX,
        editorY + 4,
        editorLabelWidth,
        20,
        TRUE
    );
    MoveWindow(
        bindingFileEdit_,
        editorContentX + editorLabelWidth,
        editorY,
        editorContentWidth - editorLabelWidth - editorButtonWidth - ButtonGap,
        25,
        TRUE
    );
    MoveWindow(
        browseSoundButton_,
        editorContentX + editorContentWidth - editorButtonWidth,
        editorY,
        editorButtonWidth,
        25,
        TRUE
    );

    editorY += 30;

    MoveWindow(
        bindingModeCaption_,
        editorContentX,
        editorY + 4,
        editorLabelWidth,
        20,
        TRUE
    );
    MoveWindow(
        bindingModeCombo_,
        editorContentX + editorLabelWidth,
        editorY,
        editorContentWidth - editorLabelWidth,
        130,
        TRUE
    );

    editorY += 30;

    MoveWindow(
        bindingVolumeCaption_,
        editorContentX,
        editorY + 4,
        editorLabelWidth,
        20,
        TRUE
    );
    MoveWindow(
        bindingVolumeSlider_,
        editorContentX + editorLabelWidth,
        editorY + 1,
        editorContentWidth - editorLabelWidth - 42,
        24,
        TRUE
    );
    MoveWindow(
        bindingVolumeValue_,
        editorContentX + editorContentWidth - 38,
        editorY + 4,
        38,
        20,
        TRUE
    );

    const int editorActionGap = 5;
    const int editorActionWidth =
        (editorContentWidth - editorActionGap * 3) / 4;
    const int minimumActionY = editorY + 32;
    const int bottomActionY = bindingsInnerY + bindingsInnerHeight -
        ButtonHeight - 9;
    const int actionY = std::max(minimumActionY, bottomActionY);

    const HWND editorActions[] = {
        addBindingButton_, updateBindingButton_,
        removeBindingButton_, clearBindingButton_
    };

    for (std::size_t index = 0; index < 4; ++index)
    {
        MoveWindow(
            editorActions[index],
            editorContentX + static_cast<int>(index) *
                (editorActionWidth + editorActionGap),
            actionY,
            editorActionWidth,
            ButtonHeight,
            TRUE
        );
    }

    y += bindingsHeight + 6;

    const HWND firstRow[] = {
        reloadButton_, stopButton_, outputMuteButton_, monitorMuteButton_
    };
    const HWND secondRow[] = {
        openConfigButton_, openSoundsButton_, openLogsButton_,
        consoleButton_, exitButton_
    };

    for (std::size_t index = 0; index < 4; ++index)
    {
        const int x = contentX +
            static_cast<int>(index) *
                (firstRowButtonWidth + ButtonGap);
        MoveWindow(
            firstRow[index],
            x,
            y,
            firstRowButtonWidth,
            ButtonHeight,
            TRUE
        );
    }

    y += ButtonHeight + ButtonGap;

    const int secondRowButtonWidth =
        (contentWidth - ButtonGap * 4) / 5;

    for (std::size_t index = 0; index < 5; ++index)
    {
        const int x = contentX +
            static_cast<int>(index) *
                (secondRowButtonWidth + ButtonGap);
        MoveWindow(
            secondRow[index],
            x,
            y,
            secondRowButtonWidth,
            ButtonHeight,
            TRUE
        );
    }

    SendMessageW(window_, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(
        window_,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
            RDW_FRAME | RDW_UPDATENOW
    );
}

void ControlWindow::ApplyTheme()
{
    if (window_ == nullptr)
    {
        return;
    }

    if (backgroundBrush_ == nullptr || cardBrush_ == nullptr ||
        inputBrush_ == nullptr)
    {
        RecreateThemeResources();
    }

    UpdateWindowChrome();

    const bool dark = activeTheme_ == AppTheme::Dark;
    const wchar_t* explorerTheme = dark
        ? L"DarkMode_Explorer"
        : L"Explorer";
    const wchar_t* comboTheme = dark
        ? L"DarkMode_CFD"
        : L"Explorer";

    const std::array<HWND, 5> combos{
        outputCombo_, monitorCombo_, microphoneCombo_,
        sampleRateCombo_, bufferCombo_
    };

    for (const HWND control : combos)
    {
        if (control != nullptr)
        {
            SetWindowTheme(control, comboTheme, nullptr);
        }
    }

    if (languageCombo_ != nullptr)
    {
        SetWindowTheme(languageCombo_, comboTheme, nullptr);
    }

    if (bindingModeCombo_ != nullptr)
    {
        SetWindowTheme(bindingModeCombo_, comboTheme, nullptr);
    }

    const std::array<HWND, 8> edits{
        stopHotkeyEdit_, outputMuteHotkeyEdit_, monitorMuteHotkeyEdit_,
        reloadHotkeyEdit_, exitHotkeyEdit_, bindingHotkeyEdit_,
        bindingFileEdit_, bindingsList_
    };

    for (const HWND control : edits)
    {
        if (control != nullptr)
        {
            SetWindowTheme(control, explorerTheme, nullptr);
        }
    }

    const std::array<HWND, 4> sliders{
        outputVolumeSlider_, monitorVolumeSlider_,
        microphoneVolumeSlider_, bindingVolumeSlider_
    };

    for (const HWND control : sliders)
    {
        if (control != nullptr)
        {
            SetWindowTheme(control, L"", L"");
            InvalidateRect(control, nullptr, TRUE);
        }
    }

    const std::array<HWND, 5> checkBoxes{
        microphoneEnabledCheck_, microphoneToOutputCheck_,
        microphoneToMonitorCheck_, startWithWindowsCheck_,
        showConsoleOnStartCheck_
    };

    for (const HWND control : checkBoxes)
    {
        if (control != nullptr)
        {
            SetWindowTheme(control, explorerTheme, nullptr);
        }
    }

    RedrawWindow(
        window_,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME
    );
}

void ControlWindow::ApplyFonts()
{
    if (headerFont_ == nullptr)
    {
        headerFont_ = CreateFontW(
            -28, 0, 0, 0, 600, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    if (subtitleFont_ == nullptr)
    {
        subtitleFont_ = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    if (bodyFont_ == nullptr)
    {
        bodyFont_ = CreateFontW(
            -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    if (sectionFont_ == nullptr)
    {
        sectionFont_ = CreateFontW(
            -17, 0, 0, 0, 600, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    if (buttonFont_ == nullptr)
    {
        buttonFont_ = CreateFontW(
            -15, 0, 0, 0, 600, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    const std::array<HWND, 63> bodyControls{
        statusCaption_, statusValue_, outputCaption_, outputCombo_,
        outputVolumeCaption_, outputVolumeSlider_, outputVolumeValue_,
        monitorCaption_, monitorCombo_, monitorVolumeCaption_,
        monitorVolumeSlider_, monitorVolumeValue_, microphoneCaption_,
        microphoneCombo_, microphoneVolumeCaption_, microphoneVolumeSlider_,
        microphoneVolumeValue_, microphoneEnabledCheck_,
        microphoneToOutputCheck_, microphoneToMonitorCheck_,
        sampleRateCaption_, sampleRateCombo_, bufferCaption_, bufferCombo_,
        languageCaption_, languageCombo_, startWithWindowsCheck_,
        showConsoleOnStartCheck_, stopHotkeyCaption_, stopHotkeyEdit_,
        outputMuteHotkeyCaption_, outputMuteHotkeyEdit_,
        monitorMuteHotkeyCaption_, monitorMuteHotkeyEdit_,
        reloadHotkeyCaption_, reloadHotkeyEdit_, exitHotkeyCaption_,
        exitHotkeyEdit_, bindingsList_, bindingHotkeyCaption_,
        bindingHotkeyEdit_, bindingFileCaption_, bindingFileEdit_,
        bindingModeCaption_, bindingModeCombo_, bindingVolumeCaption_,
        bindingVolumeSlider_, bindingVolumeValue_, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr
    };

    for (const HWND control : bodyControls)
    {
        if (control != nullptr)
        {
            SendMessageW(
                control,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(bodyFont_),
                TRUE
            );
        }
    }

    const std::array<HWND, 15> buttons{
        themeToggleButton_, refreshDevicesButton_, applySettingsButton_,
        captureHotkeyButton_, browseSoundButton_, addBindingButton_,
        updateBindingButton_, removeBindingButton_, clearBindingButton_,
        reloadButton_, stopButton_, outputMuteButton_, monitorMuteButton_,
        openConfigButton_, openSoundsButton_
    };

    for (const HWND control : buttons)
    {
        if (control != nullptr)
        {
            SendMessageW(
                control,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(buttonFont_),
                TRUE
            );
        }
    }

    const std::array<HWND, 4> remainingButtons{
        openLogsButton_, consoleButton_, exitButton_, clearBindingButton_
    };

    for (const HWND control : remainingButtons)
    {
        if (control != nullptr)
        {
            SendMessageW(
                control,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(buttonFont_),
                TRUE
            );
        }
    }

    SendMessageW(
        headerLabel_,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(headerFont_),
        TRUE
    );
    SendMessageW(
        subtitleLabel_,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(subtitleFont_),
        TRUE
    );

    const std::array<HWND, 4> cards{
        settingsGroup_, controlHotkeysGroup_,
        bindingsGroup_, bindingEditorGroup_
    };

    for (const HWND control : cards)
    {
        if (control != nullptr)
        {
            SendMessageW(
                control,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(sectionFont_),
                TRUE
            );
        }
    }
}

void ControlWindow::RecreateThemeResources()
{
    if (backgroundBrush_ != nullptr)
    {
        DeleteObject(backgroundBrush_);
        backgroundBrush_ = nullptr;
    }

    if (cardBrush_ != nullptr)
    {
        DeleteObject(cardBrush_);
        cardBrush_ = nullptr;
    }

    if (inputBrush_ != nullptr)
    {
        DeleteObject(inputBrush_);
        inputBrush_ = nullptr;
    }

    if (activeTheme_ == AppTheme::Dark)
    {
        backgroundColor_ = RGB(15, 17, 23);
        cardColor_ = RGB(24, 28, 36);
        inputColor_ = RGB(17, 21, 29);
        textColor_ = RGB(244, 246, 250);
        mutedTextColor_ = RGB(154, 164, 178);
        borderColor_ = RGB(42, 49, 64);
        accentColor_ = RGB(124, 92, 255);
        accentHoverColor_ = RGB(139, 108, 255);
        dangerColor_ = RGB(224, 82, 82);
    }
    else
    {
        backgroundColor_ = RGB(244, 246, 250);
        cardColor_ = RGB(255, 255, 255);
        inputColor_ = RGB(249, 250, 252);
        textColor_ = RGB(27, 31, 40);
        mutedTextColor_ = RGB(104, 115, 134);
        borderColor_ = RGB(221, 226, 234);
        accentColor_ = RGB(93, 70, 215);
        accentHoverColor_ = RGB(108, 85, 229);
        dangerColor_ = RGB(201, 64, 64);
    }

    backgroundBrush_ = CreateSolidBrush(backgroundColor_);
    cardBrush_ = CreateSolidBrush(cardColor_);
    inputBrush_ = CreateSolidBrush(inputColor_);
}

void ControlWindow::ReleaseThemeResources()
{
    if (backgroundBrush_ != nullptr)
    {
        DeleteObject(backgroundBrush_);
        backgroundBrush_ = nullptr;
    }

    if (cardBrush_ != nullptr)
    {
        DeleteObject(cardBrush_);
        cardBrush_ = nullptr;
    }

    if (inputBrush_ != nullptr)
    {
        DeleteObject(inputBrush_);
        inputBrush_ = nullptr;
    }

    HFONT* fonts[] = {
        &headerFont_, &subtitleFont_, &bodyFont_,
        &sectionFont_, &buttonFont_
    };

    for (HFONT* font : fonts)
    {
        if (*font != nullptr)
        {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
}

void ControlWindow::UpdateWindowChrome()
{
    if (window_ == nullptr)
    {
        return;
    }

    const BOOL useDarkMode = activeTheme_ == AppTheme::Dark;
    constexpr DWORD immersiveDarkModeAttribute = 20;
    constexpr DWORD legacyImmersiveDarkModeAttribute = 19;
    constexpr DWORD cornerPreferenceAttribute = 33;
    constexpr int roundedCornerPreference = 2;

    if (FAILED(DwmSetWindowAttribute(
            window_,
            immersiveDarkModeAttribute,
            &useDarkMode,
            sizeof(useDarkMode)
        )))
    {
        DwmSetWindowAttribute(
            window_,
            legacyImmersiveDarkModeAttribute,
            &useDarkMode,
            sizeof(useDarkMode)
        );
    }

    DwmSetWindowAttribute(
        window_,
        cornerPreferenceAttribute,
        &roundedCornerPreference,
        sizeof(roundedCornerPreference)
    );
}

void ControlWindow::DrawOwnerDrawControl(const DRAWITEMSTRUCT& item)
{
    if (IsLevelMeterControl(item.hwndItem))
    {
        DrawLevelMeter(item);
        return;
    }

    if (IsCardControl(item.hwndItem))
    {
        DrawCard(item);
        return;
    }

    DrawModernButton(item);
}


void ControlWindow::DrawLevelMeter(
    const DRAWITEMSTRUCT& item
) const
{
    float level = 0.0f;
    bool available = false;

    if (item.hwndItem == outputLevelMeter_)
    {
        level = outputMeterLevel_;
        available = outputMeterAvailable_;
    }
    else if (item.hwndItem == monitorLevelMeter_)
    {
        level = monitorMeterLevel_;
        available = monitorMeterAvailable_;
    }
    else if (item.hwndItem == microphoneLevelMeter_)
    {
        level = microphoneMeterLevel_;
        available = microphoneMeterAvailable_;
    }

    RECT trackRectangle = item.rcItem;
    trackRectangle.right -= 1;
    trackRectangle.bottom -= 1;

    const COLORREF trackColor = available
        ? BlendColor(cardColor_, borderColor_, 58)
        : BlendColor(cardColor_, backgroundColor_, 48);

    FillRoundedRectangle(
        item.hDC,
        trackRectangle,
        trackColor,
        5
    );

    level = std::clamp(level, 0.0f, 1.0f);

    if (!available || level <= 0.002f)
    {
        return;
    }

    COLORREF meterColor = RGB(64, 201, 126);

    if (level >= 0.90f)
    {
        meterColor = dangerColor_;
    }
    else if (level >= 0.72f)
    {
        meterColor = RGB(236, 177, 71);
    }

    RECT fillRectangle = trackRectangle;
    const int trackWidth = static_cast<int>(
        trackRectangle.right - trackRectangle.left
    );
    fillRectangle.right = fillRectangle.left + std::max(
        2,
        static_cast<int>(
            static_cast<float>(trackWidth) * level + 0.5f
        )
    );
    fillRectangle.right = std::min(
        fillRectangle.right,
        trackRectangle.right
    );

    FillRoundedRectangle(
        item.hDC,
        fillRectangle,
        meterColor,
        5
    );
}

void ControlWindow::DrawCard(const DRAWITEMSTRUCT& item)
{
    RECT rectangle = item.rcItem;
    rectangle.right -= 1;
    rectangle.bottom -= 1;

    FillRoundedRectangle(item.hDC, rectangle, cardColor_, CardRadius);
    DrawRoundedBorder(item.hDC, rectangle, borderColor_, CardRadius);

    std::wstring title = GetControlText(item.hwndItem);
    if (title.empty())
    {
        return;
    }

    RECT titleRectangle = rectangle;
    titleRectangle.left += 18;
    titleRectangle.top += 10;
    titleRectangle.right -= 18;
    titleRectangle.bottom = titleRectangle.top + 24;

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, textColor_);
    const HGDIOBJ oldFont = SelectObject(item.hDC, sectionFont_);
    DrawTextW(
        item.hDC,
        title.c_str(),
        static_cast<int>(title.size()),
        &titleRectangle,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS
    );
    SelectObject(item.hDC, oldFont);
}

void ControlWindow::DrawModernButton(const DRAWITEMSTRUCT& item)
{
    RECT rectangle = item.rcItem;
    rectangle.right -= 1;
    rectangle.bottom -= 1;

    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;

    if (item.hwndItem == themeToggleButton_)
    {
        COLORREF surface = hot
            ? BlendColor(cardColor_, accentColor_, 9)
            : cardColor_;
        if (pressed)
        {
            surface = BlendColor(surface, accentColor_, 16);
        }

        FillRoundedRectangle(item.hDC, rectangle, surface, 12);
        DrawRoundedBorder(item.hDC, rectangle, borderColor_, 12);

        RECT textRectangle = rectangle;
        textRectangle.left += 14;
        textRectangle.right -= 60;

        const std::wstring text = GetControlText(item.hwndItem);
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, textColor_);
        const HGDIOBJ oldFont = SelectObject(item.hDC, buttonFont_);
        DrawTextW(
            item.hDC,
            text.c_str(),
            static_cast<int>(text.size()),
            &textRectangle,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS
        );

        RECT track{
            rectangle.right - 50,
            rectangle.top + (rectangle.bottom - rectangle.top - 22) / 2,
            rectangle.right - 12,
            rectangle.top + (rectangle.bottom - rectangle.top - 22) / 2 + 22
        };
        const bool dark = activeTheme_ == AppTheme::Dark;
        FillRoundedRectangle(
            item.hDC,
            track,
            dark ? accentColor_ : borderColor_,
            22
        );

        RECT knob{
            dark ? track.right - 19 : track.left + 3,
            track.top + 3,
            dark ? track.right - 3 : track.left + 19,
            track.bottom - 3
        };
        FillRoundedRectangle(
            item.hDC,
            knob,
            RGB(255, 255, 255),
            16
        );
        SelectObject(item.hDC, oldFont);
        return;
    }

    COLORREF fillColor = inputColor_;
    COLORREF buttonTextColor = textColor_;

    if (IsPrimaryButton(item.hwndItem))
    {
        fillColor = hot ? accentHoverColor_ : accentColor_;
        buttonTextColor = RGB(255, 255, 255);
    }
    else if (IsDangerButton(item.hwndItem))
    {
        fillColor = hot
            ? BlendColor(dangerColor_, RGB(255, 255, 255), 10)
            : dangerColor_;
        buttonTextColor = RGB(255, 255, 255);
    }
    else if (hot)
    {
        fillColor = BlendColor(inputColor_, accentColor_, 10);
    }

    if (pressed)
    {
        fillColor = BlendColor(fillColor, backgroundColor_, 22);
    }

    if (disabled)
    {
        fillColor = BlendColor(fillColor, backgroundColor_, 45);
        buttonTextColor = mutedTextColor_;
    }

    FillRoundedRectangle(item.hDC, rectangle, fillColor, 10);
    DrawRoundedBorder(
        item.hDC,
        rectangle,
        focused ? accentColor_ : borderColor_,
        10,
        focused ? 2 : 1
    );

    const std::wstring text = GetControlText(item.hwndItem);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, buttonTextColor);
    const HGDIOBJ oldFont = SelectObject(item.hDC, buttonFont_);
    RECT textRectangle = rectangle;
    textRectangle.left += 10;
    textRectangle.right -= 10;
    DrawTextW(
        item.hDC,
        text.c_str(),
        static_cast<int>(text.size()),
        &textRectangle,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS
    );
    SelectObject(item.hDC, oldFont);
}

bool ControlWindow::IsSliderControl(const HWND control) const
{
    return control == outputVolumeSlider_ ||
        control == monitorVolumeSlider_ ||
        control == microphoneVolumeSlider_ ||
        control == bindingVolumeSlider_;
}

bool ControlWindow::IsLevelMeterControl(const HWND control) const
{
    return control == outputLevelMeter_ ||
        control == monitorLevelMeter_ ||
        control == microphoneLevelMeter_;
}

void ControlWindow::DrawModernSlider(
    const HWND slider,
    const HDC deviceContext
) const
{
    if (slider == nullptr || deviceContext == nullptr)
    {
        return;
    }

    RECT clientRectangle{};
    GetClientRect(slider, &clientRectangle);

    const int clientWidth =
        static_cast<int>(clientRectangle.right - clientRectangle.left);
    const int clientHeight =
        static_cast<int>(clientRectangle.bottom - clientRectangle.top);

    if (clientWidth <= 0 || clientHeight <= 0)
    {
        return;
    }

    HDC bufferContext = CreateCompatibleDC(deviceContext);
    HBITMAP bufferBitmap = bufferContext != nullptr
        ? CreateCompatibleBitmap(deviceContext, clientWidth, clientHeight)
        : nullptr;
    HGDIOBJ previousBitmap = nullptr;
    HDC drawContext = deviceContext;

    if (bufferContext != nullptr && bufferBitmap != nullptr)
    {
        previousBitmap = SelectObject(bufferContext, bufferBitmap);
        drawContext = bufferContext;
    }
    else
    {
        if (bufferBitmap != nullptr)
        {
            DeleteObject(bufferBitmap);
            bufferBitmap = nullptr;
        }

        if (bufferContext != nullptr)
        {
            DeleteDC(bufferContext);
            bufferContext = nullptr;
        }
    }

    FillRect(drawContext, &clientRectangle, cardBrush_);

    RECT channelRectangle{};
    SendMessageW(
        slider,
        TBM_GETCHANNELRECT,
        0,
        reinterpret_cast<LPARAM>(&channelRectangle)
    );

    RECT thumbRectangle{};
    SendMessageW(
        slider,
        TBM_GETTHUMBRECT,
        0,
        reinterpret_cast<LPARAM>(&thumbRectangle)
    );

    const int centerY = clientHeight / 2;

    int trackLeft = static_cast<int>(channelRectangle.left);
    int trackRight = static_cast<int>(channelRectangle.right);
    if (trackRight <= trackLeft)
    {
        trackLeft = 8;
        trackRight = std::max(trackLeft + 1, clientWidth - 8);
    }

    const int thumbCenter = std::clamp(
        static_cast<int>((thumbRectangle.left + thumbRectangle.right) / 2),
        trackLeft,
        trackRight
    );

    RECT trackRectangle{
        static_cast<LONG>(trackLeft),
        static_cast<LONG>(centerY - 3),
        static_cast<LONG>(trackRight),
        static_cast<LONG>(centerY + 3)
    };
    FillRoundedRectangle(
        drawContext,
        trackRectangle,
        BlendColor(inputColor_, borderColor_, 45),
        6
    );

    RECT activeRectangle = trackRectangle;
    activeRectangle.right = std::max<LONG>(
        activeRectangle.left,
        static_cast<LONG>(thumbCenter)
    );
    if (activeRectangle.right > activeRectangle.left)
    {
        FillRoundedRectangle(
            drawContext,
            activeRectangle,
            accentColor_,
            6
        );
    }

    RECT knobRectangle{
        static_cast<LONG>(thumbCenter - 8),
        static_cast<LONG>(centerY - 8),
        static_cast<LONG>(thumbCenter + 8),
        static_cast<LONG>(centerY + 8)
    };

    if (GetFocus() == slider)
    {
        RECT focusHalo = knobRectangle;
        InflateRect(&focusHalo, 3, 3);
        DrawRoundedBorder(
            drawContext,
            focusHalo,
            BlendColor(accentColor_, backgroundColor_, 35),
            22,
            2
        );
    }

    FillRoundedRectangle(
        drawContext,
        knobRectangle,
        accentColor_,
        16
    );
    DrawRoundedBorder(
        drawContext,
        knobRectangle,
        BlendColor(accentColor_, RGB(255, 255, 255), 28),
        16,
        1
    );

    if (bufferContext != nullptr && bufferBitmap != nullptr)
    {
        BitBlt(
            deviceContext,
            0,
            0,
            clientWidth,
            clientHeight,
            bufferContext,
            0,
            0,
            SRCCOPY
        );

        SelectObject(bufferContext, previousBitmap);
        DeleteObject(bufferBitmap);
        DeleteDC(bufferContext);
    }
}

void ControlWindow::PaintWindowBackground()
{
    if (window_ == nullptr)
    {
        return;
    }

    if (backgroundBrush_ == nullptr || cardBrush_ == nullptr)
    {
        RecreateThemeResources();
    }

    PAINTSTRUCT paint{};
    const HDC deviceContext = BeginPaint(window_, &paint);
    if (deviceContext == nullptr)
    {
        return;
    }

    FillRect(deviceContext, &paint.rcPaint, backgroundBrush_);

    RECT clientRectangle{};
    GetClientRect(window_, &clientRectangle);

    const int availableWidth = std::max(
        0,
        static_cast<int>(clientRectangle.right) - BaseMargin * 2
    );
    const int contentWidth = std::min(availableWidth, MaximumContentWidth);
    const int contentX = (clientRectangle.right - contentWidth) / 2;

    RECT accentRectangle{
        contentX,
        BaseMargin + 4,
        contentX + 4,
        BaseMargin + HeaderHeight - 4
    };
    HBRUSH accentBrush = CreateSolidBrush(accentColor_);
    FillRect(deviceContext, &accentRectangle, accentBrush);
    DeleteObject(accentBrush);

    if (statusCaption_ != nullptr)
    {
        RECT statusRectangle{};
        GetWindowRect(statusCaption_, &statusRectangle);
        MapWindowPoints(
            HWND_DESKTOP,
            window_,
            reinterpret_cast<POINT*>(&statusRectangle),
            2
        );
        statusRectangle.left = contentX;
        statusRectangle.right = contentX + contentWidth;
        statusRectangle.top -= 12;
        statusRectangle.bottom = statusRectangle.top + StatusHeight;

        FillRoundedRectangle(
            deviceContext,
            statusRectangle,
            cardColor_,
            12
        );
        DrawRoundedBorder(
            deviceContext,
            statusRectangle,
            borderColor_,
            12
        );
    }

    EndPaint(window_, &paint);
}

bool ControlWindow::IsCardControl(const HWND control) const
{
    return control == settingsGroup_ ||
        control == controlHotkeysGroup_ ||
        control == bindingsGroup_ ||
        control == bindingEditorGroup_;
}

bool ControlWindow::IsPrimaryButton(const HWND control) const
{
    return control == applySettingsButton_ ||
        control == addBindingButton_ ||
        control == updateBindingButton_;
}

bool ControlWindow::IsDangerButton(const HWND control) const
{
    return control == exitButton_ ||
        control == removeBindingButton_;
}

HBRUSH ControlWindow::StaticBrushFor(const HWND control) const
{
    if (control == headerLabel_ || control == subtitleLabel_)
    {
        return backgroundBrush_;
    }

    return cardBrush_;
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
        subtitleLabel_,
        Localization::Text(
            L"Sesler, hotkey'ler ve mikrofon miksleme tek panelde.",
            L"Sounds, hotkeys, and microphone mixing in one panel."
        )
    );
    SetControlText(
        themeToggleButton_,
        activeTheme_ == AppTheme::Dark
            ? Localization::Text(L"Koyu tema", L"Dark theme")
            : Localization::Text(L"Açık tema", L"Light theme")
    );

    SetControlText(statusCaption_, Localization::Text(L"Durum:", L"Status:"));
    SetControlText(settingsGroup_, Localization::Text(L"Ayarlar", L"Settings"));
    SetControlText(outputCaption_, Localization::Text(L"Ana çıkış:", L"Main output:"));
    SetControlText(monitorCaption_, Localization::Text(L"Monitör çıkışı:", L"Monitor output:"));
    SetControlText(microphoneCaption_, Localization::Text(L"Mikrofon:", L"Microphone:"));
    SetControlText(outputVolumeCaption_, Localization::Text(L"Ses:", L"Volume:"));
    SetControlText(monitorVolumeCaption_, Localization::Text(L"Ses:", L"Volume:"));
    SetControlText(microphoneVolumeCaption_, Localization::Text(L"Ses:", L"Volume:"));
    SetControlText(
        microphoneEnabledCheck_,
        Localization::Text(L"Mikrofonu etkinleştir", L"Enable microphone")
    );
    SetControlText(
        microphoneToOutputCheck_,
        Localization::Text(L"Ana çıkışa gönder", L"Send to main output")
    );
    SetControlText(
        microphoneToMonitorCheck_,
        Localization::Text(L"Monitöre gönder", L"Send to monitor")
    );
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
        startWithWindowsCheck_,
        Localization::Text(
            L"Windows ile başlat",
            L"Start with Windows"
        )
    );
    SetControlText(
        showConsoleOnStartCheck_,
        Localization::Text(
            L"Başlangıçta konsolu göster",
            L"Show console on startup"
        )
    );
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
    SetControlText(openLogsButton_, Localization::Text(L"Log klasörünü aç", L"Open logs folder"));
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
    SelectComboText(
        microphoneCombo_,
        Utf8ToWide(currentConfig_.GetMicrophoneDevice())
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
    SendMessageW(
        microphoneVolumeSlider_,
        TBM_SETPOS,
        TRUE,
        static_cast<LPARAM>(
            currentConfig_.GetMicrophoneVolume() * 100.0f + 0.5f
        )
    );

    SendMessageW(
        microphoneEnabledCheck_,
        BM_SETCHECK,
        currentConfig_.GetMicrophoneEnabled() ? BST_CHECKED : BST_UNCHECKED,
        0
    );
    SendMessageW(
        microphoneToOutputCheck_,
        BM_SETCHECK,
        currentConfig_.GetMicrophoneToOutput() ? BST_CHECKED : BST_UNCHECKED,
        0
    );
    SendMessageW(
        microphoneToMonitorCheck_,
        BM_SETCHECK,
        currentConfig_.GetMicrophoneToMonitor() ? BST_CHECKED : BST_UNCHECKED,
        0
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
    SendMessageW(
        startWithWindowsCheck_,
        BM_SETCHECK,
        currentConfig_.GetStartWithWindows() ? BST_CHECKED : BST_UNCHECKED,
        0
    );
    SendMessageW(
        showConsoleOnStartCheck_,
        BM_SETCHECK,
        currentConfig_.GetShowConsoleOnStart() ? BST_CHECKED : BST_UNCHECKED,
        0
    );

    UpdateVolumeLabels();
}

void ControlWindow::PopulateDeviceCombos()
{
    if (outputCombo_ == nullptr || monitorCombo_ == nullptr ||
        microphoneCombo_ == nullptr)
    {
        return;
    }

    SendMessageW(outputCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(monitorCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(microphoneCombo_, CB_RESETCONTENT, 0, 0);

    AddComboItem(outputCombo_, L"default");
    AddComboItem(monitorCombo_, L"none");
    AddComboItem(monitorCombo_, L"default");
    AddComboItem(microphoneCombo_, L"default");

    const std::wstring configuredOutput =
        Utf8ToWide(currentConfig_.GetOutputDevice());
    const std::wstring configuredMonitor =
        Utf8ToWide(currentConfig_.GetMonitorDevice());
    const std::wstring configuredMicrophone =
        Utf8ToWide(currentConfig_.GetMicrophoneDevice());

    AddComboItem(outputCombo_, configuredOutput);
    AddComboItem(monitorCombo_, configuredMonitor);
    AddComboItem(microphoneCombo_, configuredMicrophone);

    for (const std::string& device : playbackDevices_)
    {
        const std::wstring deviceName = Utf8ToWide(device);
        AddComboItem(outputCombo_, deviceName);
        AddComboItem(monitorCombo_, deviceName);
    }

    for (const std::string& device : captureDevices_)
    {
        AddComboItem(microphoneCombo_, Utf8ToWide(device));
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
    if (outputVolumeSlider_ == nullptr ||
        monitorVolumeSlider_ == nullptr ||
        microphoneVolumeSlider_ == nullptr)
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
    const LRESULT microphoneVolume = SendMessageW(
        microphoneVolumeSlider_,
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
    SetControlText(
        microphoneVolumeValue_,
        std::to_wstring(microphoneVolume) + L"%"
    );
}


void ControlWindow::UpdateLevelMeters()
{
    AudioLevelSnapshot snapshot;

    if (audio_ != nullptr)
    {
        snapshot = audio_->GetLevelSnapshot();
    }

    const auto smoothLevel = [](
        const float current,
        const float target
    )
    {
        const float clampedTarget = std::clamp(
            target,
            0.0f,
            1.0f
        );
        const float response = clampedTarget > current
            ? 0.62f
            : 0.14f;
        const float next = current +
            (clampedTarget - current) * response;

        return next < 0.004f ? 0.0f : next;
    };

    outputMeterLevel_ = smoothLevel(
        outputMeterLevel_,
        snapshot.output
    );
    monitorMeterLevel_ = smoothLevel(
        monitorMeterLevel_,
        snapshot.monitor
    );
    microphoneMeterLevel_ = smoothLevel(
        microphoneMeterLevel_,
        snapshot.microphone
    );

    outputMeterAvailable_ = snapshot.outputAvailable;
    monitorMeterAvailable_ = snapshot.monitorAvailable;
    microphoneMeterAvailable_ = snapshot.microphoneAvailable;

    const HWND meters[]{
        outputLevelMeter_,
        monitorLevelMeter_,
        microphoneLevelMeter_
    };

    for (const HWND meter : meters)
    {
        if (meter != nullptr)
        {
            RedrawWindow(
                meter,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW
            );
        }
    }
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
    const std::string microphoneDevice = WideToUtf8(
        GetControlText(microphoneCombo_)
    );

    unsigned int sampleRate = 0;
    unsigned int bufferMilliseconds = 0;

    const bool valid =
        !outputDevice.empty() &&
        !monitorDevice.empty() &&
        !microphoneDevice.empty() &&
        ParseUnsignedControl(sampleRateCombo_, sampleRate) &&
        ParseUnsignedControl(bufferCombo_, bufferMilliseconds);

    if (!valid)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Cihaz, mikrofon, örnekleme hızı veya buffer alanlarından biri geçersiz.",
                L"One of the device, microphone, sample-rate, or buffer fields is invalid."
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
    const int microphoneVolume = static_cast<int>(SendMessageW(
        microphoneVolumeSlider_,
        TBM_GETPOS,
        0,
        0
    ));

    const bool microphoneEnabled = SendMessageW(
        microphoneEnabledCheck_,
        BM_GETCHECK,
        0,
        0
    ) == BST_CHECKED;
    const bool microphoneToOutput = SendMessageW(
        microphoneToOutputCheck_,
        BM_GETCHECK,
        0,
        0
    ) == BST_CHECKED;
    const bool microphoneToMonitor = SendMessageW(
        microphoneToMonitorCheck_,
        BM_GETCHECK,
        0,
        0
    ) == BST_CHECKED;
    const bool startWithWindows = SendMessageW(
        startWithWindowsCheck_,
        BM_GETCHECK,
        0,
        0
    ) == BST_CHECKED;
    const bool showConsoleOnStart = SendMessageW(
        showConsoleOnStartCheck_,
        BM_GETCHECK,
        0,
        0
    ) == BST_CHECKED;

    if (microphoneEnabled &&
        !microphoneToOutput &&
        !microphoneToMonitor)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Mikrofon etkin olduğunda en az bir yönlendirme seçmelisin.",
                L"Select at least one route when the microphone is enabled."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

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
    candidate.SetTheme(activeTheme_);
    candidate.SetOutputDevice(outputDevice);
    candidate.SetMonitorDevice(monitorDevice);
    candidate.SetMicrophoneEnabled(microphoneEnabled);
    candidate.SetMicrophoneDevice(microphoneDevice);
    candidate.SetMicrophoneToOutput(microphoneToOutput);
    candidate.SetMicrophoneToMonitor(microphoneToMonitor);
    candidate.SetStartWithWindows(startWithWindows);
    candidate.SetShowConsoleOnStart(showConsoleOnStart);
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
        !candidate.SetMicrophoneVolume(
            static_cast<float>(microphoneVolume) / 100.0f
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
