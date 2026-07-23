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
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <cwctype>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr wchar_t ControlWindowClassName[] =
        L"SoundBoardFasaFiso.ControlWindow";

    constexpr int BaseMargin = 14;
    constexpr int HeaderHeight = 32;
    constexpr int SubtitleHeight = 18;
    constexpr int NavigationHeight = 36;
    constexpr int StatusHeight = 38;
    constexpr int SettingsGroupHeight = 300;
    constexpr int SettingsToolsGroupHeight = 112;
    constexpr int ControlHotkeysGroupHeight = 188;
    constexpr int MainQuickGroupHeight = 112;
    constexpr int BindingEditorWidth = 420;
    constexpr int ButtonHeight = 34;
    constexpr int ButtonGap = 8;
    constexpr int RowHeight = 28;
    constexpr int ThemeToggleWidth = 190;
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

    constexpr std::array MicrophoneProcessingPresetOrder{
        MicrophoneProcessingPreset::Natural,
        MicrophoneProcessingPreset::Clean,
        MicrophoneProcessingPreset::Strong,
        MicrophoneProcessingPreset::Aggressive,
        MicrophoneProcessingPreset::Custom
    };

    int MicrophoneProcessingPresetIndex(
        const MicrophoneProcessingPreset preset
    )
    {
        const auto iterator = std::find(
            MicrophoneProcessingPresetOrder.begin(),
            MicrophoneProcessingPresetOrder.end(),
            preset
        );

        return iterator == MicrophoneProcessingPresetOrder.end()
            ? static_cast<int>(MicrophoneProcessingPresetOrder.size() - 1)
            : static_cast<int>(std::distance(
                MicrophoneProcessingPresetOrder.begin(),
                iterator
            ));
    }

    std::optional<MicrophoneProcessingPreset>
    MicrophoneProcessingPresetFromIndex(const int index)
    {
        if (index < 0 ||
            static_cast<std::size_t>(index) >=
                MicrophoneProcessingPresetOrder.size())
        {
            return std::nullopt;
        }

        return MicrophoneProcessingPresetOrder[
            static_cast<std::size_t>(index)
        ];
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

    HDC screenContext = GetDC(nullptr);
    if (screenContext != nullptr)
    {
        const int detectedDpi = GetDeviceCaps(screenContext, LOGPIXELSX);
        ReleaseDC(nullptr, screenContext);

        if (detectedDpi > 0)
        {
            currentDpi_ = static_cast<UINT>(detectedDpi);
        }
    }

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
        Scale(InitialClientWidth),
        Scale(InitialClientHeight)
    };

    AdjustWindowRectEx(
        &windowRectangle,
        WS_OVERLAPPEDWINDOW,
        FALSE,
        0
    );

    int initialWindowWidth =
        windowRectangle.right - windowRectangle.left;
    int initialWindowHeight =
        windowRectangle.bottom - windowRectangle.top;

    RECT workArea{};

    if (SystemParametersInfoW(
            SPI_GETWORKAREA,
            0,
            &workArea,
            0
        ) != FALSE)
    {
        initialWindowWidth = std::min(
            initialWindowWidth,
            static_cast<int>(workArea.right - workArea.left)
        );
        initialWindowHeight = std::min(
            initialWindowHeight,
            static_cast<int>(workArea.bottom - workArea.top)
        );
    }

    window_ = CreateWindowExW(
        0,
        ControlWindowClassName,
        L"SoundBoardFasaFiso",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        initialWindowWidth,
        initialWindowHeight,
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

    if (!CreateControls() || !CreateAccelerators())
    {
        Shutdown();
        return false;
    }

    UpdateConfig(config);
    activePage_ = ControlPage::Main;
    UpdatePageVisibility();
    if (config.GetBindings().empty())
    {
        SetStatus(Localization::Text(
            L"Etkin ses ataması yok. Hotkey'ler sekmesinden ses ekleyip Kaydet ve uygula'ya bas.",
            L"No sound binding is active. Add a sound in the Hotkeys tab and click Save and apply."
        ));
    }
    else
    {
        SetStatus(Localization::Text(
            L"Soundboard hazır.",
            L"Soundboard ready."
        ));
    }

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
    StopMicrophoneTestMonitor();

    if (updateCheckThread_.joinable())
    {
        updateCheckThread_.join();
    }

    {
        const std::scoped_lock lock{updateCheckMutex_};
        pendingUpdateResult_.reset();
        pendingUpdateShowCurrentResult_ = false;
    }

    updateCheckRunning_.store(false);

    if (acceleratorTable_ != nullptr)
    {
        DestroyAcceleratorTable(acceleratorTable_);
        acceleratorTable_ = nullptr;
    }

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
    currentDpi_ = USER_DEFAULT_SCREEN_DPI;
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
    mainTabButton_ = nullptr;
    settingsTabButton_ = nullptr;
    microphoneProcessingTabButton_ = nullptr;
    hotkeysTabButton_ = nullptr;
    statusCaption_ = nullptr;
    statusValue_ = nullptr;
    mainQuickGroup_ = nullptr;
    mainOutputMeterCaption_ = nullptr;
    mainOutputLevelMeter_ = nullptr;
    mainMonitorMeterCaption_ = nullptr;
    mainMonitorLevelMeter_ = nullptr;
    mainMicrophoneMeterCaption_ = nullptr;
    mainMicrophoneLevelMeter_ = nullptr;
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
    checkUpdatesOnStartCheck_ = nullptr;
    refreshDevicesButton_ = nullptr;
    applySettingsButton_ = nullptr;
    microphoneProcessingGroup_ = nullptr;
    microphoneProcessingEnabledCheck_ = nullptr;
    microphoneProcessingPresetCaption_ = nullptr;
    microphoneProcessingPresetCombo_ = nullptr;
    microphoneProcessingStatusCaption_ = nullptr;
    microphoneProcessingStatusValue_ = nullptr;
    microphoneTestMonitorButton_ = nullptr;
    microphoneRawMeterCaption_ = nullptr;
    microphoneRawLevelMeter_ = nullptr;
    microphoneProcessedMeterCaption_ = nullptr;
    microphoneProcessedLevelMeter_ = nullptr;
    microphoneHighPassEnabledCheck_ = nullptr;
    microphoneHighPassHzCaption_ = nullptr;
    microphoneHighPassHzEdit_ = nullptr;
    microphoneCompressorEnabledCheck_ = nullptr;
    microphoneCompressorThresholdCaption_ = nullptr;
    microphoneCompressorThresholdEdit_ = nullptr;
    microphoneCompressorRatioCaption_ = nullptr;
    microphoneCompressorRatioEdit_ = nullptr;
    microphoneCompressorAttackCaption_ = nullptr;
    microphoneCompressorAttackEdit_ = nullptr;
    microphoneCompressorReleaseCaption_ = nullptr;
    microphoneCompressorReleaseEdit_ = nullptr;
    microphoneCompressorMakeupCaption_ = nullptr;
    microphoneCompressorMakeupEdit_ = nullptr;
    microphoneLimiterEnabledCheck_ = nullptr;
    microphoneLimiterCeilingCaption_ = nullptr;
    microphoneLimiterCeilingEdit_ = nullptr;
    microphoneUnavailableFeaturesCaption_ = nullptr;
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
    checkUpdatesButton_ = nullptr;
    consoleButton_ = nullptr;
    exitButton_ = nullptr;
    settingsToolsGroup_ = nullptr;
    activePage_ = ControlPage::Main;

    outputMeterLevel_ = 0.0f;
    monitorMeterLevel_ = 0.0f;
    microphoneMeterLevel_ = 0.0f;
    microphoneRawMeterLevel_ = 0.0f;
    microphoneProcessedMeterLevel_ = 0.0f;
    outputMeterAvailable_ = false;
    monitorMeterAvailable_ = false;
    microphoneMeterAvailable_ = false;
    microphoneProcessingMeterAvailable_ = false;
    populatingMicrophoneProcessingControls_ = false;
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
    StopMicrophoneTestMonitor();

    if (window_ == nullptr || IsWindowVisible(window_) == FALSE)
    {
        return;
    }

    SetWindowPos(
        window_,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_HIDEWINDOW |
            SWP_NOMOVE |
            SWP_NOSIZE |
            SWP_NOZORDER |
            SWP_NOACTIVATE
    );
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

HWND ControlWindow::NativeHandle() const noexcept
{
    return window_;
}

HACCEL ControlWindow::AcceleratorTable() const noexcept
{
    return acceleratorTable_;
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

void ControlWindow::CheckForUpdates(const bool showCurrentResult)
{
    if (window_ == nullptr)
    {
        return;
    }

    if (updateCheckRunning_.exchange(true))
    {
        if (showCurrentResult)
        {
            SetStatus(Localization::Text(
                L"Güncelleme denetimi zaten çalışıyor...",
                L"An update check is already running..."
            ));
        }

        return;
    }

    if (updateCheckThread_.joinable())
    {
        updateCheckThread_.join();
    }

    SetStatus(Localization::Text(
        L"Güncellemeler denetleniyor...",
        L"Checking for updates..."
    ));

    const HWND targetWindow = window_;
    const std::string currentVersion{AppVersion::String};

    updateCheckThread_ = std::jthread(
        [this, targetWindow, currentVersion, showCurrentResult]
        {
            UpdateCheckResult result =
                UpdateChecker::CheckLatestRelease(currentVersion);

            {
                const std::scoped_lock lock{updateCheckMutex_};
                pendingUpdateResult_ = std::move(result);
                pendingUpdateShowCurrentResult_ = showCurrentResult;
            }

            if (PostMessageW(
                    targetWindow,
                    UpdateCheckCompletedMessage,
                    0,
                    0
                ) == FALSE)
            {
                const std::scoped_lock lock{updateCheckMutex_};
                pendingUpdateResult_.reset();
                updateCheckRunning_.store(false);
            }
        }
    );
}

void ControlWindow::HandleUpdateCheckCompleted()
{
    std::optional<UpdateCheckResult> result;
    bool showCurrentResult = false;

    {
        const std::scoped_lock lock{updateCheckMutex_};
        result = std::move(pendingUpdateResult_);
        pendingUpdateResult_.reset();
        showCurrentResult = pendingUpdateShowCurrentResult_;
        pendingUpdateShowCurrentResult_ = false;
    }

    updateCheckRunning_.store(false);

    if (updateCheckThread_.joinable())
    {
        updateCheckThread_.join();
    }

    if (!result.has_value())
    {
        return;
    }

    if (result->status == UpdateCheckStatus::UpdateAvailable)
    {
        const std::wstring latestVersion = Utf8ToWide(
            result->latestVersion
        );
        const std::wstring message = std::wstring{
            Localization::Text(
                L"Yeni bir sürüm bulundu: ",
                L"A new version is available: "
            )
        } + latestVersion + Localization::Text(
            L"\n\nGitHub Release sayfasını açmak ister misin?",
            L"\n\nWould you like to open the GitHub Release page?"
        );

        SetStatus(std::wstring{Localization::Text(
            L"Yeni sürüm hazır: ",
            L"New version available: "
        )} + latestVersion);

        std::cout
            << Localization::Text(
                "Yeni sürüm bulundu: ",
                "New version available: "
            )
            << result->latestVersion
            << '\n';

        if (MessageBoxW(
                window_,
                message.c_str(),
                L"SoundBoardFasaFiso",
                MB_YESNO | MB_ICONINFORMATION
            ) == IDYES)
        {
            const std::wstring releaseUrl = Utf8ToWide(
                result->releaseUrl
            );
            ShellExecuteW(
                window_,
                L"open",
                releaseUrl.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL
            );
        }

        return;
    }

    if (result->status == UpdateCheckStatus::UpToDate)
    {
        std::cout << Localization::Text(
            "Güncelleme denetimi tamamlandı. Uygulama güncel.\n",
            "Update check completed. The application is up to date.\n"
        );

        if (showCurrentResult)
        {
            SetStatus(Localization::Text(
                L"Daha yeni bir kararlı sürüm bulunamadı.",
                L"No newer stable release was found."
            ));

            MessageBoxW(
                window_,
                Localization::Text(
                    L"Daha yeni bir kararlı sürüm bulunamadı.",
                    L"No newer stable release was found."
                ),
                L"SoundBoardFasaFiso",
                MB_OK | MB_ICONINFORMATION
            );
        }
        else
        {
            SetStatus(Localization::Text(
                L"Soundboard hazır.",
                L"Soundboard ready."
            ));
        }

        return;
    }

    std::cerr
        << Localization::Text(
            "Güncelleme denetimi başarısız: ",
            "Update check failed: "
        )
        << result->errorMessage
        << '\n';

    if (showCurrentResult)
    {
        SetStatus(Localization::Text(
            L"Güncelleme denetimi başarısız.",
            L"Update check failed."
        ));

        MessageBoxW(
            window_,
            Localization::Text(
                L"Güncellemeler denetlenemedi. İnternet bağlantını kontrol edip tekrar dene.",
                L"Updates could not be checked. Check your internet connection and try again."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
    }
    else
    {
        SetStatus(Localization::Text(
            L"Soundboard hazır.",
            L"Soundboard ready."
        ));
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

        case UpdateCheckCompletedMessage:
            HandleUpdateCheckCompleted();
            return 0;

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

            if (controlId == IdMicrophoneProcessingPreset &&
                notificationCode == CBN_SELCHANGE)
            {
                ApplySelectedMicrophoneProcessingPreset();
                return 0;
            }

            const HWND commandControl = reinterpret_cast<HWND>(lParam);
            const bool nativeFilterEditChanged =
                notificationCode == EN_CHANGE &&
                (commandControl == microphoneHighPassHzEdit_ ||
                    commandControl == microphoneCompressorThresholdEdit_ ||
                    commandControl == microphoneCompressorRatioEdit_ ||
                    commandControl == microphoneCompressorAttackEdit_ ||
                    commandControl == microphoneCompressorReleaseEdit_ ||
                    commandControl == microphoneCompressorMakeupEdit_ ||
                    commandControl == microphoneLimiterCeilingEdit_);
            const bool nativeFilterToggleChanged =
                notificationCode == BN_CLICKED &&
                (commandControl == microphoneHighPassEnabledCheck_ ||
                    commandControl == microphoneCompressorEnabledCheck_ ||
                    commandControl == microphoneLimiterEnabledCheck_);

            if (!populatingMicrophoneProcessingControls_ &&
                (nativeFilterEditChanged || nativeFilterToggleChanged))
            {
                MarkMicrophoneProcessingPresetCustom();
            }

            constexpr int AcceleratorNotificationCode = 1;
            if (notificationCode != BN_CLICKED &&
                notificationCode != AcceleratorNotificationCode)
            {
                break;
            }

            switch (controlId)
            {
                case IdMainTab:
                    SetActivePage(ControlPage::Main);
                    return 0;

                case IdSettingsTab:
                    SetActivePage(ControlPage::Settings);
                    return 0;

                case IdMicrophoneProcessingTab:
                    SetActivePage(ControlPage::MicrophoneProcessing);
                    return 0;

                case IdMicrophoneTestMonitor:
                    ToggleMicrophoneTestMonitor();
                    return 0;

                case IdHotkeysTab:
                    SetActivePage(ControlPage::Hotkeys);
                    return 0;

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

                case IdCheckUpdates:
                    CheckForUpdates(true);
                    return 0;

                case IdToggleConsole:
                    PostApplicationCommand(commandIds_.toggleConsole);
                    return 0;

                case IdExit:
                    PostApplicationCommand(commandIds_.exit);
                    return 0;

                case IdCancelHotkeyCapture:
                    if (capturingBindingHotkey_)
                    {
                        capturingBindingHotkey_ = false;
                        RefreshLocalizedText();
                        SetStatus(Localization::Text(
                            L"Hotkey yakalama iptal edildi.",
                            L"Hotkey capture cancelled."
                        ));
                    }
                    return 0;

                default:
                    break;
            }

            break;
        }

        case WM_VKEYTOITEM:
            if (reinterpret_cast<HWND>(lParam) == bindingsList_ &&
                LOWORD(wParam) == VK_DELETE)
            {
                if (!capturingBindingHotkey_ &&
                    selectedBindingIndex_ >= 0 &&
                    static_cast<std::size_t>(selectedBindingIndex_) <
                        pendingBindings_.size())
                {
                    RemoveSelectedBinding(false);
                }

                return static_cast<LRESULT>(-2);
            }
            return static_cast<LRESULT>(-1);

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

        case WM_DPICHANGED:
        {
            const UINT dpi = HIWORD(wParam);
            const auto* suggestedRectangle =
                reinterpret_cast<const RECT*>(lParam);

            if (suggestedRectangle != nullptr)
            {
                HandleDpiChanged(dpi, *suggestedRectangle);
            }

            return 0;
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
                Scale(MinimumClientWidth),
                Scale(MinimumClientHeight)
            };

            AdjustWindowRectEx(
                &rectangle,
                static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)),
                FALSE,
                static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE))
            );

            int minimumWidth = rectangle.right - rectangle.left;
            int minimumHeight = rectangle.bottom - rectangle.top;

            const HMONITOR monitor = MonitorFromWindow(
                window,
                MONITOR_DEFAULTTONEAREST
            );
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);

            if (monitor != nullptr &&
                GetMonitorInfoW(monitor, &monitorInfo) != FALSE)
            {
                minimumWidth = std::min(
                    minimumWidth,
                    static_cast<int>(
                        monitorInfo.rcWork.right - monitorInfo.rcWork.left
                    )
                );
                minimumHeight = std::min(
                    minimumHeight,
                    static_cast<int>(
                        monitorInfo.rcWork.bottom - monitorInfo.rcWork.top
                    )
                );
            }

            minimumMaximum->ptMinTrackSize.x = minimumWidth;
            minimumMaximum->ptMinTrackSize.y = minimumHeight;
            return 0;
        }

        case WM_SYSCOMMAND:
            if ((wParam & static_cast<WPARAM>(0xFFF0U)) ==
                static_cast<WPARAM>(SC_CLOSE))
            {
                Hide();
                return 0;
            }
            break;

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
    mainTabButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdMainTab
    );
    settingsTabButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdSettingsTab
    );
    microphoneProcessingTabButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP,
        IdMicrophoneProcessingTab
    );
    hotkeysTabButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdHotkeysTab
    );

    statusCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    statusValue_ = createControl(L"STATIC", L"", SS_LEFT, 0);

    mainQuickGroup_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
    mainOutputMeterCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    mainOutputLevelMeter_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
    mainMonitorMeterCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    mainMonitorLevelMeter_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
    mainMicrophoneMeterCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    mainMicrophoneLevelMeter_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);

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
    checkUpdatesOnStartCheck_ = createControl(
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

    microphoneProcessingGroup_ = createControl(
        L"STATIC", L"", SS_OWNERDRAW, 0
    );
    microphoneProcessingEnabledCheck_ = createControl(
        L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP, 0
    );
    microphoneProcessingPresetCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneProcessingPresetCombo_ = createControl(
        L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        IdMicrophoneProcessingPreset,
        WS_EX_CLIENTEDGE
    );
    microphoneProcessingStatusCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneProcessingStatusValue_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneTestMonitorButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP,
        IdMicrophoneTestMonitor
    );
    microphoneRawMeterCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneRawLevelMeter_ = createControl(
        L"STATIC", L"", SS_OWNERDRAW, 0
    );
    microphoneProcessedMeterCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneProcessedLevelMeter_ = createControl(
        L"STATIC", L"", SS_OWNERDRAW, 0
    );
    microphoneHighPassEnabledCheck_ = createControl(
        L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP, 0
    );
    microphoneHighPassHzCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneHighPassHzEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0,
        WS_EX_CLIENTEDGE
    );
    microphoneCompressorEnabledCheck_ = createControl(
        L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP, 0
    );
    microphoneCompressorThresholdCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneCompressorThresholdEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0,
        WS_EX_CLIENTEDGE
    );
    microphoneCompressorRatioCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneCompressorRatioEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0,
        WS_EX_CLIENTEDGE
    );
    microphoneCompressorAttackCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneCompressorAttackEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0,
        WS_EX_CLIENTEDGE
    );
    microphoneCompressorReleaseCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneCompressorReleaseEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0,
        WS_EX_CLIENTEDGE
    );
    microphoneCompressorMakeupCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneCompressorMakeupEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0,
        WS_EX_CLIENTEDGE
    );
    microphoneLimiterEnabledCheck_ = createControl(
        L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP, 0
    );
    microphoneLimiterCeilingCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    microphoneLimiterCeilingEdit_ = createControl(
        L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 0,
        WS_EX_CLIENTEDGE
    );
    microphoneUnavailableFeaturesCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
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
            LBS_NOTIFY | LBS_WANTKEYBOARDINPUT | WS_TABSTOP,
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

    settingsToolsGroup_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);

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
    checkUpdatesButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdCheckUpdates
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
        mainTabButton_, settingsTabButton_,
        microphoneProcessingTabButton_, hotkeysTabButton_,
        statusCaption_, statusValue_, mainQuickGroup_,
        mainOutputMeterCaption_, mainOutputLevelMeter_,
        mainMonitorMeterCaption_, mainMonitorLevelMeter_,
        mainMicrophoneMeterCaption_, mainMicrophoneLevelMeter_,
        settingsGroup_,
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
        startWithWindowsCheck_, checkUpdatesOnStartCheck_,
        refreshDevicesButton_,
        applySettingsButton_, microphoneProcessingGroup_,
        microphoneProcessingEnabledCheck_,
        microphoneProcessingPresetCaption_,
        microphoneProcessingPresetCombo_,
        microphoneProcessingStatusCaption_,
        microphoneProcessingStatusValue_, microphoneTestMonitorButton_,
        microphoneRawMeterCaption_,
        microphoneRawLevelMeter_, microphoneProcessedMeterCaption_,
        microphoneProcessedLevelMeter_,
        microphoneHighPassEnabledCheck_, microphoneHighPassHzCaption_,
        microphoneHighPassHzEdit_, microphoneCompressorEnabledCheck_,
        microphoneCompressorThresholdCaption_,
        microphoneCompressorThresholdEdit_,
        microphoneCompressorRatioCaption_, microphoneCompressorRatioEdit_,
        microphoneCompressorAttackCaption_,
        microphoneCompressorAttackEdit_,
        microphoneCompressorReleaseCaption_,
        microphoneCompressorReleaseEdit_,
        microphoneCompressorMakeupCaption_,
        microphoneCompressorMakeupEdit_, microphoneLimiterEnabledCheck_,
        microphoneLimiterCeilingCaption_, microphoneLimiterCeilingEdit_,
        microphoneUnavailableFeaturesCaption_, controlHotkeysGroup_,
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
        openLogsButton_, checkUpdatesButton_, consoleButton_, exitButton_,
        settingsToolsGroup_
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

bool ControlWindow::CreateAccelerators()
{
    if (acceleratorTable_ != nullptr)
    {
        DestroyAcceleratorTable(acceleratorTable_);
        acceleratorTable_ = nullptr;
    }

    const ACCEL accelerators[]{
        {FCONTROL | FVIRTKEY, static_cast<WORD>('1'), IdMainTab},
        {FCONTROL | FVIRTKEY, static_cast<WORD>('2'), IdSettingsTab},
        {FCONTROL | FVIRTKEY, static_cast<WORD>('3'), IdHotkeysTab},
        {FCONTROL | FVIRTKEY, static_cast<WORD>('4'),
            IdMicrophoneProcessingTab},
        {FCONTROL | FVIRTKEY, static_cast<WORD>('S'), IdApplySettings},
        {FVIRTKEY, VK_ESCAPE, IdCancelHotkeyCapture}
    };

    acceleratorTable_ = CreateAcceleratorTableW(
        const_cast<ACCEL*>(accelerators),
        static_cast<int>(std::size(accelerators))
    );

    return acceleratorTable_ != nullptr;
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

    const int logicalClientWidth = Unscale(clientWidth);
    const int logicalClientHeight = Unscale(clientHeight);

    const auto moveWindow = [this](
        const HWND control,
        const int x,
        const int y,
        const int width,
        const int height,
        const BOOL repaint
    )
    {
        ::MoveWindow(
            control,
            Scale(x),
            Scale(y),
            Scale(width),
            Scale(height),
            repaint
        );
    };

    const int availableWidth = std::max(
        0,
        logicalClientWidth - BaseMargin * 2
    );
    const int contentWidth = std::min(
        availableWidth,
        MaximumContentWidth
    );
    const int contentX = (logicalClientWidth - contentWidth) / 2;

    SendMessageW(window_, WM_SETREDRAW, FALSE, 0);

    int y = BaseMargin;

    moveWindow(
        headerLabel_,
        contentX,
        y,
        contentWidth - ThemeToggleWidth - ButtonGap,
        HeaderHeight,
        TRUE
    );
    moveWindow(
        themeToggleButton_,
        contentX + contentWidth - ThemeToggleWidth,
        y,
        ThemeToggleWidth,
        ButtonHeight,
        TRUE
    );
    moveWindow(
        subtitleLabel_,
        contentX,
        y + HeaderHeight,
        contentWidth - ThemeToggleWidth - ButtonGap,
        SubtitleHeight,
        TRUE
    );
    y += HeaderHeight + SubtitleHeight + 8;

    constexpr int tabWidth = 142;
    constexpr int tabGap = 6;
    const HWND tabs[]{
        mainTabButton_, settingsTabButton_, hotkeysTabButton_,
        microphoneProcessingTabButton_
    };

    for (std::size_t index = 0; index < std::size(tabs); ++index)
    {
        moveWindow(
            tabs[index],
            contentX + static_cast<int>(index) * (tabWidth + tabGap),
            y,
            tabWidth,
            NavigationHeight,
            TRUE
        );
    }
    y += NavigationHeight + 8;

    moveWindow(statusCaption_, contentX + 14, y + 9, 70, 20, TRUE);
    moveWindow(
        statusValue_,
        contentX + 84,
        y + 9,
        contentWidth - 98,
        20,
        TRUE
    );
    y += StatusHeight + 8;

    const int pageY = y;
    const int pageHeight = std::max(
        0,
        logicalClientHeight - pageY - BaseMargin
    );

    if (activePage_ == ControlPage::Main)
    {
        const int bindingsHeight = std::max(
            300,
            pageHeight - MainQuickGroupHeight - 8
        );

        moveWindow(
            bindingsGroup_,
            contentX,
            pageY,
            contentWidth,
            bindingsHeight,
            TRUE
        );

        const int bindingsInnerX = contentX + 14;
        const int bindingsInnerY = pageY + 31;
        const int bindingsInnerWidth = contentWidth - 28;
        const int bindingsInnerHeight = bindingsHeight - 43;
        const int editorWidth = std::clamp(
            bindingsInnerWidth * 38 / 100,
            360,
            BindingEditorWidth
        );
        const int bindingGap = 10;
        const int listWidth = std::max(
            250,
            bindingsInnerWidth - editorWidth - bindingGap
        );

        moveWindow(
            bindingsList_,
            bindingsInnerX,
            bindingsInnerY,
            listWidth,
            bindingsInnerHeight,
            TRUE
        );

        const int editorX = bindingsInnerX + listWidth + bindingGap;
        moveWindow(
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

        moveWindow(
            bindingHotkeyCaption_,
            editorContentX,
            editorY + 4,
            editorLabelWidth,
            20,
            TRUE
        );
        moveWindow(
            bindingHotkeyEdit_,
            editorContentX + editorLabelWidth,
            editorY,
            editorContentWidth - editorLabelWidth -
                editorButtonWidth - ButtonGap,
            25,
            TRUE
        );
        moveWindow(
            captureHotkeyButton_,
            editorContentX + editorContentWidth - editorButtonWidth,
            editorY,
            editorButtonWidth,
            25,
            TRUE
        );

        editorY += 30;

        moveWindow(
            bindingFileCaption_,
            editorContentX,
            editorY + 4,
            editorLabelWidth,
            20,
            TRUE
        );
        moveWindow(
            bindingFileEdit_,
            editorContentX + editorLabelWidth,
            editorY,
            editorContentWidth - editorLabelWidth -
                editorButtonWidth - ButtonGap,
            25,
            TRUE
        );
        moveWindow(
            browseSoundButton_,
            editorContentX + editorContentWidth - editorButtonWidth,
            editorY,
            editorButtonWidth,
            25,
            TRUE
        );

        editorY += 30;

        moveWindow(
            bindingModeCaption_,
            editorContentX,
            editorY + 4,
            editorLabelWidth,
            20,
            TRUE
        );
        moveWindow(
            bindingModeCombo_,
            editorContentX + editorLabelWidth,
            editorY,
            editorContentWidth - editorLabelWidth,
            130,
            TRUE
        );

        editorY += 30;

        moveWindow(
            bindingVolumeCaption_,
            editorContentX,
            editorY + 4,
            editorLabelWidth,
            20,
            TRUE
        );
        moveWindow(
            bindingVolumeSlider_,
            editorContentX + editorLabelWidth,
            editorY + 1,
            editorContentWidth - editorLabelWidth - 42,
            24,
            TRUE
        );
        moveWindow(
            bindingVolumeValue_,
            editorContentX + editorContentWidth - 38,
            editorY + 4,
            38,
            20,
            TRUE
        );

        const int editorActionGap = 6;
        const int editorActionWidth =
            (editorContentWidth - editorActionGap) / 2;
        const int editorActionsHeight =
            ButtonHeight * 2 + editorActionGap;
        const int minimumActionY = editorY + 32;
        const int bottomActionY = bindingsInnerY + bindingsInnerHeight -
            editorActionsHeight - 9;
        const int actionY = std::max(minimumActionY, bottomActionY);
        const HWND editorActions[]{
            addBindingButton_, updateBindingButton_,
            removeBindingButton_, clearBindingButton_
        };

        for (std::size_t index = 0;
            index < std::size(editorActions);
            ++index)
        {
            const int column = static_cast<int>(index % 2);
            const int row = static_cast<int>(index / 2);

            moveWindow(
                editorActions[index],
                editorContentX + column *
                    (editorActionWidth + editorActionGap),
                actionY + row *
                    (ButtonHeight + editorActionGap),
                editorActionWidth,
                ButtonHeight,
                TRUE
            );
        }
        const int quickY = pageY + bindingsHeight + 8;
        moveWindow(
            mainQuickGroup_,
            contentX,
            quickY,
            contentWidth,
            MainQuickGroupHeight,
            TRUE
        );

        const int quickInnerX = contentX + 14;
        const int quickInnerWidth = contentWidth - 28;
        constexpr int meterGap = 14;
        const int meterColumnWidth =
            (quickInnerWidth - meterGap * 2) / 3;
        const HWND meterCaptions[]{
            mainOutputMeterCaption_,
            mainMonitorMeterCaption_,
            mainMicrophoneMeterCaption_
        };
        const HWND meters[]{
            mainOutputLevelMeter_,
            mainMonitorLevelMeter_,
            mainMicrophoneLevelMeter_
        };

        for (std::size_t index = 0;
            index < std::size(meters);
            ++index)
        {
            const int x = quickInnerX + static_cast<int>(index) *
                (meterColumnWidth + meterGap);
            moveWindow(
                meterCaptions[index],
                x,
                quickY + 29,
                meterColumnWidth,
                18,
                TRUE
            );
            moveWindow(
                meters[index],
                x,
                quickY + 51,
                meterColumnWidth,
                7,
                TRUE
            );
        }

        const HWND quickButtons[]{
            stopButton_, outputMuteButton_, monitorMuteButton_,
            reloadButton_, applySettingsButton_
        };
        const int quickButtonGap = 6;
        const int quickButtonWidth =
            (quickInnerWidth - quickButtonGap * 4) / 5;

        for (std::size_t index = 0;
            index < std::size(quickButtons);
            ++index)
        {
            moveWindow(
                quickButtons[index],
                quickInnerX + static_cast<int>(index) *
                    (quickButtonWidth + quickButtonGap),
                quickY + MainQuickGroupHeight - ButtonHeight - 10,
                quickButtonWidth,
                ButtonHeight,
                TRUE
            );
        }
    }
    else if (activePage_ == ControlPage::Settings)
    {
        moveWindow(
            settingsGroup_,
            contentX,
            pageY,
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

        int rowY = pageY + 32;
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
            moveWindow(caption, innerX, currentY + 4, labelWidth, 20, TRUE);
            moveWindow(
                combo,
                innerX + labelWidth,
                currentY,
                comboWidth,
                200,
                TRUE
            );

            int volumeX = innerX + labelWidth + comboWidth + sectionGap;
            moveWindow(
                volumeCaption,
                volumeX,
                currentY + 4,
                volumeCaptionWidth,
                20,
                TRUE
            );
            volumeX += volumeCaptionWidth + fieldGap;
            moveWindow(
                slider,
                volumeX,
                currentY,
                volumeSliderWidth,
                20,
                TRUE
            );
            moveWindow(
                levelMeter,
                volumeX + 7,
                currentY + 22,
                std::max(1, volumeSliderWidth - 14),
                5,
                TRUE
            );
            moveWindow(
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
            monitorVolumeSlider_, monitorLevelMeter_,
            monitorVolumeValue_, rowY
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

        moveWindow(
            microphoneEnabledCheck_,
            microphoneChecksX,
            rowY,
            microphoneCheckWidth,
            22,
            TRUE
        );
        moveWindow(
            microphoneToOutputCheck_,
            microphoneChecksX + microphoneCheckWidth + microphoneCheckGap,
            rowY,
            microphoneCheckWidth,
            22,
            TRUE
        );
        moveWindow(
            microphoneToMonitorCheck_,
            microphoneChecksX +
                (microphoneCheckWidth + microphoneCheckGap) * 2,
            rowY,
            microphoneCheckWidth,
            22,
            TRUE
        );
        rowY += RowHeight + 2;

        const int halfWidth = (innerWidth - sectionGap) / 2;
        const int smallLabelWidth = 130;
        const int smallControlWidth = halfWidth - smallLabelWidth;

        moveWindow(
            sampleRateCaption_, innerX, rowY + 4,
            smallLabelWidth, 20, TRUE
        );
        moveWindow(
            sampleRateCombo_, innerX + smallLabelWidth, rowY,
            smallControlWidth, 160, TRUE
        );

        const int rightColumnX = innerX + halfWidth + sectionGap;
        moveWindow(
            bufferCaption_, rightColumnX, rowY + 4,
            smallLabelWidth, 20, TRUE
        );
        moveWindow(
            bufferCombo_, rightColumnX + smallLabelWidth, rowY,
            smallControlWidth, 160, TRUE
        );
        rowY += RowHeight + 6;

        moveWindow(languageCaption_, innerX, rowY + 4, labelWidth, 20, TRUE);
        moveWindow(languageCombo_, innerX + labelWidth, rowY, 170, 140, TRUE);
        moveWindow(
            checkUpdatesOnStartCheck_,
            innerX + labelWidth + 190,
            rowY + 3,
            280,
            22,
            TRUE
        );
        rowY += RowHeight + 6;

        moveWindow(startWithWindowsCheck_, innerX, rowY + 4, 194, 22, TRUE);

        const int actionButtonWidth = 164;
        const int applyX = innerX + innerWidth - actionButtonWidth;
        moveWindow(
            applySettingsButton_,
            applyX,
            rowY,
            actionButtonWidth,
            ButtonHeight,
            TRUE
        );
        moveWindow(
            refreshDevicesButton_,
            applyX - actionButtonWidth - ButtonGap,
            rowY,
            actionButtonWidth,
            ButtonHeight,
            TRUE
        );

        const int toolsY = pageY + SettingsGroupHeight + 8;
        moveWindow(
            settingsToolsGroup_,
            contentX,
            toolsY,
            contentWidth,
            SettingsToolsGroupHeight,
            TRUE
        );

        const int toolsInnerX = contentX + 14;
        const int toolsInnerWidth = contentWidth - 28;
        const int toolGap = 7;
        const int toolButtonWidth = (toolsInnerWidth - toolGap * 2) / 3;
        const HWND firstTools[]{
            openConfigButton_, openSoundsButton_, openLogsButton_
        };
        const HWND secondTools[]{
            checkUpdatesButton_, consoleButton_, exitButton_
        };

        for (std::size_t index = 0;
            index < std::size(firstTools);
            ++index)
        {
            const int x = toolsInnerX + static_cast<int>(index) *
                (toolButtonWidth + toolGap);
            moveWindow(
                firstTools[index], x, toolsY + 29,
                toolButtonWidth, ButtonHeight, TRUE
            );
            moveWindow(
                secondTools[index], x,
                toolsY + 29 + ButtonHeight + 7,
                toolButtonWidth, ButtonHeight, TRUE
            );
        }
    }
    else if (activePage_ == ControlPage::MicrophoneProcessing)
    {
        moveWindow(
            microphoneProcessingGroup_,
            contentX,
            pageY,
            contentWidth,
            pageHeight,
            TRUE
        );

        const int innerX = contentX + 18;
        const int innerWidth = contentWidth - 36;
        const int columnGap = 24;
        const int columnWidth = (innerWidth - columnGap) / 2;
        const int rightX = innerX + columnWidth + columnGap;
        const int labelWidth = 210;
        const int editWidth = std::max(90, columnWidth - labelWidth);
        int rowY = pageY + 34;

        moveWindow(
            microphoneProcessingEnabledCheck_,
            innerX, rowY, columnWidth, 22, TRUE
        );
        moveWindow(
            microphoneProcessingStatusCaption_,
            rightX, rowY + 2, 94, 20, TRUE
        );
        moveWindow(
            microphoneProcessingStatusValue_,
            rightX + 94, rowY + 2, columnWidth - 94, 20, TRUE
        );
        rowY += 34;

        moveWindow(
            microphoneProcessingPresetCaption_,
            innerX, rowY + 4, 94, 20, TRUE
        );
        moveWindow(
            microphoneProcessingPresetCombo_,
            innerX + 94, rowY,
            std::max(180, columnWidth - 94), 220, TRUE
        );
        moveWindow(
            microphoneTestMonitorButton_,
            rightX, rowY, columnWidth, ButtonHeight, TRUE
        );
        rowY += 34;

        const int meterGap = 24;
        const int meterWidth = (innerWidth - meterGap) / 2;
        moveWindow(
            microphoneRawMeterCaption_,
            innerX, rowY, meterWidth, 20, TRUE
        );
        moveWindow(
            microphoneRawLevelMeter_,
            innerX, rowY + 23, meterWidth, 8, TRUE
        );
        moveWindow(
            microphoneProcessedMeterCaption_,
            innerX + meterWidth + meterGap,
            rowY, meterWidth, 20, TRUE
        );
        moveWindow(
            microphoneProcessedLevelMeter_,
            innerX + meterWidth + meterGap,
            rowY + 23, meterWidth, 8, TRUE
        );
        rowY += 55;

        const auto layoutField = [=](
            const HWND caption,
            const HWND edit,
            const int x,
            const int fieldY
        )
        {
            moveWindow(caption, x, fieldY + 4, labelWidth, 20, TRUE);
            moveWindow(
                edit, x + labelWidth, fieldY, editWidth, 25, TRUE
            );
        };

        moveWindow(
            microphoneHighPassEnabledCheck_,
            innerX, rowY, columnWidth, 22, TRUE
        );
        moveWindow(
            microphoneCompressorEnabledCheck_,
            rightX, rowY, columnWidth, 22, TRUE
        );
        rowY += 32;

        layoutField(
            microphoneHighPassHzCaption_,
            microphoneHighPassHzEdit_,
            innerX, rowY
        );
        layoutField(
            microphoneCompressorThresholdCaption_,
            microphoneCompressorThresholdEdit_,
            rightX, rowY
        );
        rowY += 34;

        layoutField(
            microphoneCompressorRatioCaption_,
            microphoneCompressorRatioEdit_,
            innerX, rowY
        );
        layoutField(
            microphoneCompressorAttackCaption_,
            microphoneCompressorAttackEdit_,
            rightX, rowY
        );
        rowY += 34;

        layoutField(
            microphoneCompressorReleaseCaption_,
            microphoneCompressorReleaseEdit_,
            innerX, rowY
        );
        layoutField(
            microphoneCompressorMakeupCaption_,
            microphoneCompressorMakeupEdit_,
            rightX, rowY
        );
        rowY += 42;

        moveWindow(
            microphoneLimiterEnabledCheck_,
            innerX, rowY, columnWidth, 22, TRUE
        );
        layoutField(
            microphoneLimiterCeilingCaption_,
            microphoneLimiterCeilingEdit_,
            rightX, rowY - 2
        );
        rowY += 40;

        moveWindow(
            microphoneUnavailableFeaturesCaption_,
            innerX, rowY, innerWidth - 190, 42, TRUE
        );

        moveWindow(
            applySettingsButton_,
            contentX + contentWidth - 18 - 180,
            pageY + pageHeight - ButtonHeight - 14,
            180,
            ButtonHeight,
            TRUE
        );
    }
    else
    {
        moveWindow(
            controlHotkeysGroup_,
            contentX,
            pageY,
            contentWidth,
            ControlHotkeysGroupHeight,
            TRUE
        );

        const int innerX = contentX + 14;
        const int innerWidth = contentWidth - 28;
        const int columnGap = 18;
        const int columnWidth = (innerWidth - columnGap) / 2;
        const int labelWidth = 140;
        const int editWidth = columnWidth - labelWidth;
        const int rightX = innerX + columnWidth + columnGap;

        const auto layoutHotkeyField = [=](
            const HWND caption,
            const HWND edit,
            const int x,
            const int fieldY
        )
        {
            moveWindow(caption, x, fieldY + 4, labelWidth, 20, TRUE);
            moveWindow(
                edit,
                x + labelWidth,
                fieldY,
                editWidth,
                25,
                TRUE
            );
        };

        layoutHotkeyField(
            stopHotkeyCaption_, stopHotkeyEdit_,
            innerX, pageY + 38
        );
        layoutHotkeyField(
            outputMuteHotkeyCaption_, outputMuteHotkeyEdit_,
            rightX, pageY + 38
        );
        layoutHotkeyField(
            monitorMuteHotkeyCaption_, monitorMuteHotkeyEdit_,
            innerX, pageY + 76
        );
        layoutHotkeyField(
            reloadHotkeyCaption_, reloadHotkeyEdit_,
            rightX, pageY + 76
        );
        layoutHotkeyField(
            exitHotkeyCaption_, exitHotkeyEdit_,
            innerX, pageY + 114
        );

        moveWindow(
            applySettingsButton_,
            contentX + contentWidth - 14 - 180,
            pageY + ControlHotkeysGroupHeight - ButtonHeight - 12,
            180,
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

int ControlWindow::Scale(const int value) const noexcept
{
    return MulDiv(
        value,
        static_cast<int>(currentDpi_),
        USER_DEFAULT_SCREEN_DPI
    );
}

int ControlWindow::Unscale(const int value) const noexcept
{
    return MulDiv(
        value,
        USER_DEFAULT_SCREEN_DPI,
        static_cast<int>(currentDpi_)
    );
}

void ControlWindow::ReleaseFonts()
{
    HFONT* fonts[]{
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

void ControlWindow::HandleDpiChanged(
    const UINT dpi,
    const RECT& suggestedRectangle
)
{
    if (window_ == nullptr)
    {
        return;
    }

    currentDpi_ = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;

    ReleaseFonts();
    ApplyFonts();

    SetWindowPos(
        window_,
        nullptr,
        suggestedRectangle.left,
        suggestedRectangle.top,
        suggestedRectangle.right - suggestedRectangle.left,
        suggestedRectangle.bottom - suggestedRectangle.top,
        SWP_NOACTIVATE | SWP_NOZORDER
    );

    RECT clientRectangle{};
    GetClientRect(window_, &clientRectangle);
    LayoutControls(
        clientRectangle.right - clientRectangle.left,
        clientRectangle.bottom - clientRectangle.top
    );

    RedrawWindow(
        window_,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
            RDW_FRAME | RDW_UPDATENOW
    );
}

void ControlWindow::SetActivePage(const ControlPage page)
{
    if (window_ == nullptr)
    {
        return;
    }

    activePage_ = page;
    UpdatePageVisibility();

    RECT clientRectangle{};
    GetClientRect(window_, &clientRectangle);
    LayoutControls(
        static_cast<int>(clientRectangle.right - clientRectangle.left),
        static_cast<int>(clientRectangle.bottom - clientRectangle.top)
    );

    HWND activeTab = mainTabButton_;
    if (activePage_ == ControlPage::Settings)
    {
        activeTab = settingsTabButton_;
    }
    else if (activePage_ == ControlPage::MicrophoneProcessing)
    {
        activeTab = microphoneProcessingTabButton_;
    }
    else if (activePage_ == ControlPage::Hotkeys)
    {
        activeTab = hotkeysTabButton_;
    }

    if (activeTab != nullptr)
    {
        SetFocus(activeTab);
    }
}

void ControlWindow::UpdatePageVisibility()
{
    const auto setVisible = [](const HWND control, const bool visible)
    {
        if (control != nullptr)
        {
            ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
        }
    };

    const HWND mainControls[]{
        mainQuickGroup_, mainOutputMeterCaption_, mainOutputLevelMeter_,
        mainMonitorMeterCaption_, mainMonitorLevelMeter_,
        mainMicrophoneMeterCaption_, mainMicrophoneLevelMeter_,
        bindingsGroup_, bindingsList_, bindingEditorGroup_,
        bindingHotkeyCaption_, bindingHotkeyEdit_, captureHotkeyButton_,
        bindingFileCaption_, bindingFileEdit_, browseSoundButton_,
        bindingModeCaption_, bindingModeCombo_, bindingVolumeCaption_,
        bindingVolumeSlider_, bindingVolumeValue_, addBindingButton_,
        updateBindingButton_, removeBindingButton_, clearBindingButton_,
        reloadButton_, stopButton_, outputMuteButton_, monitorMuteButton_
    };

    const HWND settingsControls[]{
        settingsGroup_, outputCaption_, outputCombo_, outputVolumeCaption_,
        outputVolumeSlider_, outputLevelMeter_, outputVolumeValue_,
        monitorCaption_, monitorCombo_, monitorVolumeCaption_,
        monitorVolumeSlider_, monitorLevelMeter_, monitorVolumeValue_,
        microphoneCaption_, microphoneCombo_, microphoneVolumeCaption_,
        microphoneVolumeSlider_, microphoneLevelMeter_,
        microphoneVolumeValue_, microphoneEnabledCheck_,
        microphoneToOutputCheck_, microphoneToMonitorCheck_,
        sampleRateCaption_, sampleRateCombo_, bufferCaption_, bufferCombo_,
        languageCaption_, languageCombo_, startWithWindowsCheck_,
        checkUpdatesOnStartCheck_, refreshDevicesButton_,
        settingsToolsGroup_, openConfigButton_, openSoundsButton_,
        openLogsButton_, checkUpdatesButton_, consoleButton_, exitButton_
    };

    const HWND microphoneProcessingControls[]{
        microphoneProcessingGroup_, microphoneProcessingEnabledCheck_,
        microphoneProcessingPresetCaption_,
        microphoneProcessingPresetCombo_,
        microphoneProcessingStatusCaption_,
        microphoneProcessingStatusValue_, microphoneTestMonitorButton_,
        microphoneRawMeterCaption_,
        microphoneRawLevelMeter_, microphoneProcessedMeterCaption_,
        microphoneProcessedLevelMeter_, microphoneHighPassEnabledCheck_,
        microphoneHighPassHzCaption_, microphoneHighPassHzEdit_,
        microphoneCompressorEnabledCheck_,
        microphoneCompressorThresholdCaption_,
        microphoneCompressorThresholdEdit_,
        microphoneCompressorRatioCaption_, microphoneCompressorRatioEdit_,
        microphoneCompressorAttackCaption_,
        microphoneCompressorAttackEdit_,
        microphoneCompressorReleaseCaption_,
        microphoneCompressorReleaseEdit_,
        microphoneCompressorMakeupCaption_,
        microphoneCompressorMakeupEdit_, microphoneLimiterEnabledCheck_,
        microphoneLimiterCeilingCaption_, microphoneLimiterCeilingEdit_,
        microphoneUnavailableFeaturesCaption_
    };

    const HWND hotkeyControls[]{
        controlHotkeysGroup_, stopHotkeyCaption_, stopHotkeyEdit_,
        outputMuteHotkeyCaption_, outputMuteHotkeyEdit_,
        monitorMuteHotkeyCaption_, monitorMuteHotkeyEdit_,
        reloadHotkeyCaption_, reloadHotkeyEdit_,
        exitHotkeyCaption_, exitHotkeyEdit_
    };

    for (const HWND control : mainControls)
    {
        setVisible(control, activePage_ == ControlPage::Main);
    }
    for (const HWND control : settingsControls)
    {
        setVisible(control, activePage_ == ControlPage::Settings);
    }
    for (const HWND control : microphoneProcessingControls)
    {
        setVisible(
            control,
            activePage_ == ControlPage::MicrophoneProcessing
        );
    }
    for (const HWND control : hotkeyControls)
    {
        setVisible(control, activePage_ == ControlPage::Hotkeys);
    }

    setVisible(applySettingsButton_, true);

    RedrawWindow(
        mainTabButton_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW
    );
    RedrawWindow(
        settingsTabButton_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW
    );
    RedrawWindow(
        microphoneProcessingTabButton_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW
    );
    RedrawWindow(
        hotkeysTabButton_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW
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

    const std::array<HWND, 6> combos{
        outputCombo_, monitorCombo_, microphoneCombo_,
        sampleRateCombo_, bufferCombo_, microphoneProcessingPresetCombo_
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

    const std::array<HWND, 15> edits{
        stopHotkeyEdit_, outputMuteHotkeyEdit_, monitorMuteHotkeyEdit_,
        reloadHotkeyEdit_, exitHotkeyEdit_, bindingHotkeyEdit_,
        bindingFileEdit_, bindingsList_, microphoneHighPassHzEdit_,
        microphoneCompressorThresholdEdit_,
        microphoneCompressorRatioEdit_, microphoneCompressorAttackEdit_,
        microphoneCompressorReleaseEdit_,
        microphoneCompressorMakeupEdit_, microphoneLimiterCeilingEdit_
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

    const std::array<HWND, 9> checkBoxes{
        microphoneEnabledCheck_, microphoneToOutputCheck_,
        microphoneToMonitorCheck_, startWithWindowsCheck_,
        checkUpdatesOnStartCheck_, microphoneProcessingEnabledCheck_,
        microphoneHighPassEnabledCheck_,
        microphoneCompressorEnabledCheck_, microphoneLimiterEnabledCheck_
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
            -Scale(28), 0, 0, 0, 600, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    if (subtitleFont_ == nullptr)
    {
        subtitleFont_ = CreateFontW(
            -Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    if (bodyFont_ == nullptr)
    {
        bodyFont_ = CreateFontW(
            -Scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    if (sectionFont_ == nullptr)
    {
        sectionFont_ = CreateFontW(
            -Scale(17), 0, 0, 0, 600, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    if (buttonFont_ == nullptr)
    {
        buttonFont_ = CreateFontW(
            -Scale(15), 0, 0, 0, 600, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }

    const HWND bodyControls[]{
        statusCaption_, statusValue_, mainOutputMeterCaption_,
        mainMonitorMeterCaption_, mainMicrophoneMeterCaption_,
        outputCaption_, outputCombo_, outputVolumeCaption_,
        outputVolumeSlider_, outputVolumeValue_, monitorCaption_,
        monitorCombo_, monitorVolumeCaption_, monitorVolumeSlider_,
        monitorVolumeValue_, microphoneCaption_, microphoneCombo_,
        microphoneVolumeCaption_, microphoneVolumeSlider_,
        microphoneVolumeValue_, microphoneEnabledCheck_,
        microphoneToOutputCheck_, microphoneToMonitorCheck_,
        sampleRateCaption_, sampleRateCombo_, bufferCaption_, bufferCombo_,
        languageCaption_, languageCombo_, startWithWindowsCheck_,
        checkUpdatesOnStartCheck_, microphoneProcessingEnabledCheck_,
        microphoneProcessingPresetCaption_,
        microphoneProcessingPresetCombo_,
        microphoneProcessingStatusCaption_,
        microphoneProcessingStatusValue_, microphoneRawMeterCaption_,
        microphoneProcessedMeterCaption_,
        microphoneHighPassEnabledCheck_, microphoneHighPassHzCaption_,
        microphoneHighPassHzEdit_, microphoneCompressorEnabledCheck_,
        microphoneCompressorThresholdCaption_,
        microphoneCompressorThresholdEdit_,
        microphoneCompressorRatioCaption_, microphoneCompressorRatioEdit_,
        microphoneCompressorAttackCaption_,
        microphoneCompressorAttackEdit_,
        microphoneCompressorReleaseCaption_,
        microphoneCompressorReleaseEdit_,
        microphoneCompressorMakeupCaption_,
        microphoneCompressorMakeupEdit_, microphoneLimiterEnabledCheck_,
        microphoneLimiterCeilingCaption_, microphoneLimiterCeilingEdit_,
        microphoneUnavailableFeaturesCaption_,
        stopHotkeyCaption_, stopHotkeyEdit_, outputMuteHotkeyCaption_,
        outputMuteHotkeyEdit_, monitorMuteHotkeyCaption_,
        monitorMuteHotkeyEdit_, reloadHotkeyCaption_, reloadHotkeyEdit_,
        exitHotkeyCaption_, exitHotkeyEdit_, bindingsList_,
        bindingHotkeyCaption_, bindingHotkeyEdit_, bindingFileCaption_,
        bindingFileEdit_, bindingModeCaption_, bindingModeCombo_,
        bindingVolumeCaption_, bindingVolumeSlider_, bindingVolumeValue_
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

    const HWND buttons[]{
        themeToggleButton_, mainTabButton_, settingsTabButton_,
        microphoneProcessingTabButton_, hotkeysTabButton_,
        refreshDevicesButton_, applySettingsButton_,
        microphoneTestMonitorButton_,
        captureHotkeyButton_, browseSoundButton_, addBindingButton_,
        updateBindingButton_, removeBindingButton_, clearBindingButton_,
        reloadButton_, stopButton_, outputMuteButton_, monitorMuteButton_,
        openConfigButton_, openSoundsButton_, openLogsButton_,
        checkUpdatesButton_, consoleButton_, exitButton_
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

    const HWND cards[]{
        mainQuickGroup_, settingsGroup_, settingsToolsGroup_,
        microphoneProcessingGroup_, controlHotkeysGroup_, bindingsGroup_,
        bindingEditorGroup_
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

    ReleaseFonts();
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

    if (item.hwndItem == outputLevelMeter_ ||
        item.hwndItem == mainOutputLevelMeter_)
    {
        level = outputMeterLevel_;
        available = outputMeterAvailable_;
    }
    else if (item.hwndItem == monitorLevelMeter_ ||
        item.hwndItem == mainMonitorLevelMeter_)
    {
        level = monitorMeterLevel_;
        available = monitorMeterAvailable_;
    }
    else if (item.hwndItem == microphoneLevelMeter_ ||
        item.hwndItem == mainMicrophoneLevelMeter_)
    {
        level = microphoneMeterLevel_;
        available = microphoneMeterAvailable_;
    }
    else if (item.hwndItem == microphoneRawLevelMeter_)
    {
        level = microphoneRawMeterLevel_;
        available = microphoneProcessingMeterAvailable_;
    }
    else if (item.hwndItem == microphoneProcessedLevelMeter_)
    {
        level = microphoneProcessedMeterLevel_;
        available = microphoneProcessingMeterAvailable_;
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

    if (IsNavigationTab(item.hwndItem))
    {
        const bool active =
            (item.hwndItem == mainTabButton_ &&
                activePage_ == ControlPage::Main) ||
            (item.hwndItem == settingsTabButton_ &&
                activePage_ == ControlPage::Settings) ||
            (item.hwndItem == microphoneProcessingTabButton_ &&
                activePage_ == ControlPage::MicrophoneProcessing) ||
            (item.hwndItem == hotkeysTabButton_ &&
                activePage_ == ControlPage::Hotkeys);

        COLORREF surface = active
            ? BlendColor(cardColor_, accentColor_, 18)
            : backgroundColor_;
        if (!active && hot)
        {
            surface = BlendColor(backgroundColor_, cardColor_, 62);
        }
        if (pressed)
        {
            surface = BlendColor(surface, accentColor_, 14);
        }

        FillRoundedRectangle(item.hDC, rectangle, surface, 10);
        if (active || focused)
        {
            DrawRoundedBorder(
                item.hDC,
                rectangle,
                active ? accentColor_ : borderColor_,
                10,
                active ? 2 : 1
            );
        }

        if (active)
        {
            RECT indicator = rectangle;
            indicator.left += 18;
            indicator.right -= 18;
            indicator.top = indicator.bottom - 4;
            FillRoundedRectangle(
                item.hDC,
                indicator,
                accentColor_,
                4
            );
        }

        const std::wstring text = GetControlText(item.hwndItem);
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(
            item.hDC,
            active ? textColor_ : mutedTextColor_
        );
        const HGDIOBJ oldFont = SelectObject(item.hDC, buttonFont_);
        RECT textRectangle = rectangle;
        textRectangle.left += 10;
        textRectangle.right -= 10;
        textRectangle.bottom -= active ? 2 : 0;
        DrawTextW(
            item.hDC,
            text.c_str(),
            static_cast<int>(text.size()),
            &textRectangle,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS
        );
        SelectObject(item.hDC, oldFont);
        return;
    }

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
        control == microphoneLevelMeter_ ||
        control == mainOutputLevelMeter_ ||
        control == mainMonitorLevelMeter_ ||
        control == mainMicrophoneLevelMeter_ ||
        control == microphoneRawLevelMeter_ ||
        control == microphoneProcessedLevelMeter_;
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
    return control == mainQuickGroup_ ||
        control == settingsGroup_ ||
        control == settingsToolsGroup_ ||
        control == microphoneProcessingGroup_ ||
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

bool ControlWindow::IsNavigationTab(const HWND control) const
{
    return control == mainTabButton_ ||
        control == settingsTabButton_ ||
        control == microphoneProcessingTabButton_ ||
        control == hotkeysTabButton_;
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
            L"Tek pencere • Ctrl+1/2/3/4: sekmeler • Ctrl+S: kaydet",
            L"Single window • Ctrl+1/2/3/4: tabs • Ctrl+S: save"
        )
    );
    SetControlText(
        themeToggleButton_,
        activeTheme_ == AppTheme::Dark
            ? Localization::Text(L"Koyu tema", L"Dark theme")
            : Localization::Text(L"Açık tema", L"Light theme")
    );
    SetControlText(
        mainTabButton_,
        Localization::Text(L"Ana ekran", L"Home")
    );
    SetControlText(
        settingsTabButton_,
        Localization::Text(L"Ayarlar", L"Settings")
    );
    SetControlText(
        microphoneProcessingTabButton_,
        Localization::Text(L"Mikrofon filtreleri", L"Mic filters")
    );
    SetControlText(
        hotkeysTabButton_,
        Localization::Text(L"Hotkey'ler", L"Hotkeys")
    );

    SetControlText(statusCaption_, Localization::Text(L"Durum:", L"Status:"));
    SetControlText(
        mainQuickGroup_,
        Localization::Text(L"Hızlı kontroller", L"Quick controls")
    );
    SetControlText(
        mainOutputMeterCaption_,
        Localization::Text(L"Ana çıkış", L"Main output")
    );
    SetControlText(
        mainMonitorMeterCaption_,
        Localization::Text(L"Monitör", L"Monitor")
    );
    SetControlText(
        mainMicrophoneMeterCaption_,
        Localization::Text(L"Mikrofon", L"Microphone")
    );
    SetControlText(
        settingsGroup_,
        Localization::Text(L"Ses ve uygulama ayarları", L"Audio and app settings")
    );
    SetControlText(
        settingsToolsGroup_,
        Localization::Text(L"Araçlar", L"Tools")
    );
    SetControlText(outputCaption_, Localization::Text(L"Ana çıkış:", L"Main output:"));
    SetControlText(monitorCaption_, Localization::Text(L"Monitör çıkışı:", L"Monitor output:"));
    SetControlText(microphoneCaption_, Localization::Text(L"Mikrofon girişi:", L"Microphone input:"));
    SetControlText(outputVolumeCaption_, Localization::Text(L"Ses:", L"Volume:"));
    SetControlText(monitorVolumeCaption_, Localization::Text(L"Ses:", L"Volume:"));
    SetControlText(microphoneVolumeCaption_, Localization::Text(L"Ses:", L"Volume:"));
    SetControlText(
        microphoneEnabledCheck_,
        Localization::Text(L"Mikrofon karıştırmayı etkinleştir", L"Enable microphone mix")
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
        checkUpdatesOnStartCheck_,
        Localization::Text(
            L"Başlangıçta güncellemeleri denetle",
            L"Check for updates on startup"
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
        microphoneProcessingGroup_,
        Localization::Text(L"Mikrofon işleme", L"Microphone processing")
    );
    SetControlText(
        microphoneProcessingEnabledCheck_,
        Localization::Text(
            L"Mikrofon işlemeyi etkinleştir",
            L"Enable microphone processing"
        )
    );
    SetControlText(
        microphoneProcessingPresetCaption_,
        Localization::Text(L"Preset:", L"Preset:")
    );
    SetControlText(
        microphoneProcessingStatusCaption_,
        Localization::Text(L"Canlı durum:", L"Live status:")
    );
    SetControlText(
        microphoneTestMonitorButton_,
        audio_ != nullptr && audio_->IsMicrophoneTestMonitorEnabled()
            ? Localization::Text(
                L"Mikrofon testini durdur",
                L"Stop microphone test"
            )
            : Localization::Text(
                L"Mikrofonu monitörde test et",
                L"Test microphone in monitor"
            )
    );
    SetControlText(
        microphoneRawMeterCaption_,
        Localization::Text(L"Ham mikrofon seviyesi", L"Raw microphone level")
    );
    SetControlText(
        microphoneProcessedMeterCaption_,
        Localization::Text(
            L"İşlenmiş mikrofon seviyesi",
            L"Processed microphone level"
        )
    );
    SetControlText(
        microphoneHighPassEnabledCheck_,
        Localization::Text(
            L"High-pass filtresini etkinleştir",
            L"Enable high-pass filter"
        )
    );
    SetControlText(
        microphoneHighPassHzCaption_,
        Localization::Text(
            L"High-pass frekansı (20-300 Hz):",
            L"High-pass frequency (20-300 Hz):"
        )
    );
    SetControlText(
        microphoneCompressorEnabledCheck_,
        Localization::Text(
            L"Compressor'ı etkinleştir",
            L"Enable compressor"
        )
    );
    SetControlText(
        microphoneCompressorThresholdCaption_,
        Localization::Text(
            L"Eşik (-60 ile 0 dB):",
            L"Threshold (-60 to 0 dB):"
        )
    );
    SetControlText(
        microphoneCompressorRatioCaption_,
        Localization::Text(L"Oran (1-20):", L"Ratio (1-20):")
    );
    SetControlText(
        microphoneCompressorAttackCaption_,
        Localization::Text(
            L"Attack (0.1-200 ms):",
            L"Attack (0.1-200 ms):"
        )
    );
    SetControlText(
        microphoneCompressorReleaseCaption_,
        Localization::Text(
            L"Release (5-2000 ms):",
            L"Release (5-2000 ms):"
        )
    );
    SetControlText(
        microphoneCompressorMakeupCaption_,
        Localization::Text(
            L"Makeup gain (-12 ile 24 dB):",
            L"Makeup gain (-12 to 24 dB):"
        )
    );
    SetControlText(
        microphoneLimiterEnabledCheck_,
        Localization::Text(L"Limiter'ı etkinleştir", L"Enable limiter")
    );
    SetControlText(
        microphoneLimiterCeilingCaption_,
        Localization::Text(
            L"Limiter tavanı (-12 ile 0 dB):",
            L"Limiter ceiling (-12 to 0 dB):"
        )
    );
    SetControlText(
        microphoneUnavailableFeaturesCaption_,
        Localization::Text(
            L"Test düğmesi işlenmiş mikrofonu geçici olarak monitöre gönderir. Pencere kapanınca test durur. AGC sonraki aşamada eklenecek.",
            L"The test button temporarily sends the processed microphone to the monitor. The test stops when the window closes. AGC will be added later."
        )
    );

    PopulateMicrophoneProcessingPresetCombo();

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
    SetControlText(checkUpdatesButton_, Localization::Text(L"Güncelleme denetle", L"Check for updates"));
    SetControlText(consoleButton_, Localization::Text(L"Hata ayıklama konsolunu aç/gizle", L"Open/hide debug console"));
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
        checkUpdatesOnStartCheck_,
        BM_SETCHECK,
        currentConfig_.GetCheckUpdatesOnStart() ? BST_CHECKED : BST_UNCHECKED,
        0
    );

    PopulateMicrophoneProcessingControls();
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

void ControlWindow::PopulateMicrophoneProcessingPresetCombo()
{
    if (microphoneProcessingPresetCombo_ == nullptr)
    {
        return;
    }

    MicrophoneProcessingPreset selectedPreset =
        currentConfig_.GetMicrophoneProcessingSettings().preset;
    const int currentSelection = static_cast<int>(SendMessageW(
        microphoneProcessingPresetCombo_,
        CB_GETCURSEL,
        0,
        0
    ));

    if (const auto currentPreset =
            MicrophoneProcessingPresetFromIndex(currentSelection))
    {
        selectedPreset = *currentPreset;
    }

    const bool previousPopulationState =
        populatingMicrophoneProcessingControls_;
    populatingMicrophoneProcessingControls_ = true;

    SendMessageW(
        microphoneProcessingPresetCombo_,
        CB_RESETCONTENT,
        0,
        0
    );
    AddComboItem(
        microphoneProcessingPresetCombo_,
        Localization::Text(L"Doğal", L"Natural")
    );
    AddComboItem(
        microphoneProcessingPresetCombo_,
        Localization::Text(L"Temiz", L"Clean")
    );
    AddComboItem(
        microphoneProcessingPresetCombo_,
        Localization::Text(L"Güçlü", L"Strong")
    );
    AddComboItem(
        microphoneProcessingPresetCombo_,
        Localization::Text(L"Agresif", L"Aggressive")
    );
    AddComboItem(
        microphoneProcessingPresetCombo_,
        Localization::Text(L"Özel", L"Custom")
    );

    SendMessageW(
        microphoneProcessingPresetCombo_,
        CB_SETCURSEL,
        static_cast<WPARAM>(
            MicrophoneProcessingPresetIndex(selectedPreset)
        ),
        0
    );

    populatingMicrophoneProcessingControls_ = previousPopulationState;
}

void ControlWindow::ApplySelectedMicrophoneProcessingPreset()
{
    if (populatingMicrophoneProcessingControls_ ||
        microphoneProcessingPresetCombo_ == nullptr)
    {
        return;
    }

    const int selection = static_cast<int>(SendMessageW(
        microphoneProcessingPresetCombo_,
        CB_GETCURSEL,
        0,
        0
    ));
    const auto preset = MicrophoneProcessingPresetFromIndex(selection);

    if (!preset.has_value() ||
        *preset == MicrophoneProcessingPreset::Custom)
    {
        return;
    }

    const bool enabled = SendMessageW(
        microphoneProcessingEnabledCheck_,
        BM_GETCHECK,
        0,
        0
    ) == BST_CHECKED;
    const auto settings = BuildMicrophoneProcessingPreset(
        *preset,
        enabled
    );

    if (!settings.has_value())
    {
        return;
    }

    const auto setCheck = [](const HWND control, const bool checked)
    {
        SendMessageW(
            control,
            BM_SETCHECK,
            checked ? BST_CHECKED : BST_UNCHECKED,
            0
        );
    };
    const auto formatValue = [](const float value)
    {
        std::wostringstream stream;
        stream << std::fixed << std::setprecision(3) << value;
        return stream.str();
    };

    populatingMicrophoneProcessingControls_ = true;
    setCheck(
        microphoneHighPassEnabledCheck_,
        settings->highPassEnabled
    );
    setCheck(
        microphoneCompressorEnabledCheck_,
        settings->compressorEnabled
    );
    setCheck(
        microphoneLimiterEnabledCheck_,
        settings->limiterEnabled
    );
    SetControlText(
        microphoneHighPassHzEdit_,
        formatValue(settings->highPassHz)
    );
    SetControlText(
        microphoneCompressorThresholdEdit_,
        formatValue(settings->compressorThresholdDb)
    );
    SetControlText(
        microphoneCompressorRatioEdit_,
        formatValue(settings->compressorRatio)
    );
    SetControlText(
        microphoneCompressorAttackEdit_,
        formatValue(settings->compressorAttackMs)
    );
    SetControlText(
        microphoneCompressorReleaseEdit_,
        formatValue(settings->compressorReleaseMs)
    );
    SetControlText(
        microphoneCompressorMakeupEdit_,
        formatValue(settings->compressorMakeupDb)
    );
    SetControlText(
        microphoneLimiterCeilingEdit_,
        formatValue(settings->limiterCeilingDb)
    );
    populatingMicrophoneProcessingControls_ = false;

    SetStatus(Localization::Text(
        L"Preset değerleri önizlendi. Uygulamak için Kaydet ve uygula'ya basın.",
        L"Preset values are previewed. Select Save and apply to activate them."
    ));
}

void ControlWindow::MarkMicrophoneProcessingPresetCustom()
{
    if (populatingMicrophoneProcessingControls_ ||
        microphoneProcessingPresetCombo_ == nullptr)
    {
        return;
    }

    SendMessageW(
        microphoneProcessingPresetCombo_,
        CB_SETCURSEL,
        static_cast<WPARAM>(MicrophoneProcessingPresetIndex(
            MicrophoneProcessingPreset::Custom
        )),
        0
    );
}

void ControlWindow::PopulateMicrophoneProcessingControls()
{
    if (microphoneProcessingEnabledCheck_ == nullptr)
    {
        return;
    }

    populatingMicrophoneProcessingControls_ = true;
    PopulateMicrophoneProcessingPresetCombo();

    const MicrophoneProcessingSettings& settings =
        currentConfig_.GetMicrophoneProcessingSettings();

    const auto setCheck = [](const HWND control, const bool checked)
    {
        SendMessageW(
            control,
            BM_SETCHECK,
            checked ? BST_CHECKED : BST_UNCHECKED,
            0
        );
    };

    const auto formatValue = [](const float value)
    {
        std::wostringstream stream;
        stream << std::fixed << std::setprecision(3) << value;
        return stream.str();
    };

    setCheck(microphoneProcessingEnabledCheck_, settings.enabled);
    setCheck(microphoneHighPassEnabledCheck_, settings.highPassEnabled);
    setCheck(
        microphoneCompressorEnabledCheck_,
        settings.compressorEnabled
    );
    setCheck(microphoneLimiterEnabledCheck_, settings.limiterEnabled);
    const MicrophoneProcessingPreset displayedPreset =
        MicrophoneProcessingSettingsMatchPreset(
            settings,
            settings.preset
        )
            ? settings.preset
            : MicrophoneProcessingPreset::Custom;
    SendMessageW(
        microphoneProcessingPresetCombo_,
        CB_SETCURSEL,
        static_cast<WPARAM>(MicrophoneProcessingPresetIndex(
            displayedPreset
        )),
        0
    );

    SetControlText(
        microphoneHighPassHzEdit_,
        formatValue(settings.highPassHz)
    );
    SetControlText(
        microphoneCompressorThresholdEdit_,
        formatValue(settings.compressorThresholdDb)
    );
    SetControlText(
        microphoneCompressorRatioEdit_,
        formatValue(settings.compressorRatio)
    );
    SetControlText(
        microphoneCompressorAttackEdit_,
        formatValue(settings.compressorAttackMs)
    );
    SetControlText(
        microphoneCompressorReleaseEdit_,
        formatValue(settings.compressorReleaseMs)
    );
    SetControlText(
        microphoneCompressorMakeupEdit_,
        formatValue(settings.compressorMakeupDb)
    );
    SetControlText(
        microphoneLimiterCeilingEdit_,
        formatValue(settings.limiterCeilingDb)
    );
    populatingMicrophoneProcessingControls_ = false;
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
    if (window_ == nullptr || IsWindowVisible(window_) == FALSE)
    {
        return;
    }

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
    microphoneRawMeterLevel_ = smoothLevel(
        microphoneRawMeterLevel_,
        snapshot.microphoneRaw
    );
    microphoneProcessedMeterLevel_ = smoothLevel(
        microphoneProcessedMeterLevel_,
        snapshot.microphoneProcessed
    );

    outputMeterAvailable_ = snapshot.outputAvailable;
    monitorMeterAvailable_ = snapshot.monitorAvailable;
    microphoneMeterAvailable_ = snapshot.microphoneAvailable;
    microphoneProcessingMeterAvailable_ = snapshot.microphoneAvailable;

    const bool microphonePermanentlyMonitored =
        currentConfig_.GetMicrophoneToMonitor();
    const bool microphoneTestMonitorAvailable =
        snapshot.microphoneAvailable &&
        snapshot.monitorAvailable &&
        !microphonePermanentlyMonitored;

    if (microphoneTestMonitorButton_ != nullptr)
    {
        EnableWindow(
            microphoneTestMonitorButton_,
            snapshot.microphoneTestMonitorActive ||
                microphoneTestMonitorAvailable
        );

        if (microphonePermanentlyMonitored)
        {
            SetControlText(
                microphoneTestMonitorButton_,
                Localization::Text(
                    L"Mikrofon zaten monitörde",
                    L"Microphone already monitored"
                )
            );
        }
        else
        {
            SetControlText(
                microphoneTestMonitorButton_,
                snapshot.microphoneTestMonitorActive
                    ? Localization::Text(
                        L"Mikrofon testini durdur",
                        L"Stop microphone test"
                    )
                    : Localization::Text(
                        L"Mikrofonu monitörde test et",
                        L"Test microphone in monitor"
                    )
            );
        }
    }

    const auto formatDbfs = [](const float linearLevel)
    {
        if (!std::isfinite(linearLevel) || linearLevel <= 0.000001f)
        {
            return std::wstring(L"-inf");
        }

        std::wostringstream stream;
        stream
            << std::fixed
            << std::setprecision(1)
            << 20.0f * std::log10(std::clamp(
                linearLevel,
                0.000001f,
                1.0f
            ));
        return stream.str();
    };

    const auto updateProcessingMeterCaption = [this, &formatDbfs](
        const HWND caption,
        const std::wstring& label,
        const float peak,
        const float rms,
        const bool available
    )
    {
        if (!available)
        {
            SetControlText(caption, label);
            return;
        }

        std::wstring text = label;
        text += L"  P ";
        text += formatDbfs(peak);
        text += L"  R ";
        text += formatDbfs(rms);
        text += L" dBFS";
        SetControlText(caption, text);
    };

    const bool processingMeterTelemetryAvailable =
        snapshot.microphoneAvailable &&
        snapshot.microphoneProcessingActive;
    updateProcessingMeterCaption(
        microphoneRawMeterCaption_,
        Localization::Text(L"Ham", L"Raw"),
        snapshot.microphoneRaw,
        snapshot.microphoneRawRms,
        processingMeterTelemetryAvailable
    );
    updateProcessingMeterCaption(
        microphoneProcessedMeterCaption_,
        Localization::Text(L"İşlenmiş", L"Processed"),
        snapshot.microphoneProcessed,
        snapshot.microphoneProcessedRms,
        processingMeterTelemetryAvailable
    );

    if (activePage_ == ControlPage::MicrophoneProcessing)
    {
        std::wstring status;

        if (!snapshot.microphoneAvailable)
        {
            status = Localization::Text(
                L"Mikrofon kullanılamıyor",
                L"Microphone unavailable"
            );
        }
        else if (!snapshot.microphoneProcessingActive)
        {
            status = Localization::Text(
                L"Bypass / kapalı",
                L"Bypassed / disabled"
            );
        }
        else if (snapshot.microphoneNoiseSuppressionFailed)
        {
            status = Localization::Text(
                L"Aktif • noise suppression bypass edildi",
                L"Active • noise suppression bypassed"
            );
        }
        else if (snapshot.microphoneInvalidSampleDetected)
        {
            status = Localization::Text(
                L"Aktif • geçersiz sample algılandı",
                L"Active • invalid sample detected"
            );
        }
        else if (snapshot.microphoneInputClipped)
        {
            status = Localization::Text(
                L"Aktif • giriş clipping",
                L"Active • input clipping"
            );
        }
        else if (snapshot.microphoneNoiseSuppressionActive)
        {
            status = Localization::Text(
                L"Aktif • noise suppression",
                L"Active • noise suppression"
            );
        }
        else
        {
            status = Localization::Text(L"Aktif", L"Active");
        }

        if (snapshot.microphoneNoiseSuppressionActive)
        {
            const int voicePercentage = std::clamp(
                static_cast<int>(std::lround(
                    snapshot.microphoneVoiceActivityProbability * 100.0f
                )),
                0,
                100
            );
            status += Localization::Text(L" • ses: ", L" • voice: ");
            status += std::to_wstring(voicePercentage);
            status += L"%";
        }

        if (snapshot.microphoneTestMonitorActive)
        {
            status += Localization::Text(
                L" • test monitörü açık",
                L" • test monitor active"
            );
        }

        if (snapshot.microphoneDroppedInputFrames != 0)
        {
            status += Localization::Text(
                L" • düşen frame: ",
                L" • dropped frames: "
            );
            status += std::to_wstring(
                snapshot.microphoneDroppedInputFrames
            );
        }

        SetControlText(microphoneProcessingStatusValue_, status);
    }

    const std::array<HWND, 3> meters =
        activePage_ == ControlPage::Main
            ? std::array<HWND, 3>{
                mainOutputLevelMeter_,
                mainMonitorLevelMeter_,
                mainMicrophoneLevelMeter_
            }
            : activePage_ == ControlPage::Settings
                ? std::array<HWND, 3>{
                    outputLevelMeter_,
                    monitorLevelMeter_,
                    microphoneLevelMeter_
                }
                : std::array<HWND, 3>{nullptr, nullptr, nullptr};

    for (const HWND meter : meters)
    {
        if (meter != nullptr)
        {
            InvalidateRect(meter, nullptr, FALSE);
        }
    }

    if (activePage_ == ControlPage::MicrophoneProcessing)
    {
        InvalidateRect(microphoneRawLevelMeter_, nullptr, FALSE);
        InvalidateRect(microphoneProcessedLevelMeter_, nullptr, FALSE);
    }
}

void ControlWindow::ToggleMicrophoneTestMonitor()
{
    if (audio_ == nullptr)
    {
        SetStatus(Localization::Text(
            L"Mikrofon test monitörü kullanılamıyor.",
            L"The microphone test monitor is unavailable."
        ));
        return;
    }

    const bool enable = !audio_->IsMicrophoneTestMonitorEnabled();
    const MicrophoneTestMonitorResult result =
        audio_->SetMicrophoneTestMonitorEnabled(enable);

    switch (result)
    {
        case MicrophoneTestMonitorResult::Enabled:
            SetStatus(Localization::Text(
                L"Mikrofon geçici olarak monitöre gönderiliyor. Kontrol penceresi kapanınca test duracak.",
                L"The microphone is temporarily being sent to the monitor. Closing the control window will stop the test."
            ));
            break;

        case MicrophoneTestMonitorResult::Disabled:
            SetStatus(Localization::Text(
                L"Mikrofon test monitörü durduruldu.",
                L"The microphone test monitor was stopped."
            ));
            break;

        case MicrophoneTestMonitorResult::AlreadyRouted:
            SetStatus(Localization::Text(
                L"Mikrofon ayarlardan zaten monitöre yönlendiriliyor.",
                L"The microphone is already routed to the monitor by the active settings."
            ));
            break;

        case MicrophoneTestMonitorResult::Unavailable:
            SetStatus(Localization::Text(
                L"Test için etkin bir mikrofon ve monitör çıkışı gerekiyor.",
                L"An active microphone and monitor output are required for the test."
            ));
            break;

        case MicrophoneTestMonitorResult::Failed:
            SetStatus(Localization::Text(
                L"Mikrofon test monitörü başlatılamadı; normal ses rotaları değişmedi.",
                L"The microphone test monitor could not start; the normal audio routes were not changed."
            ));
            break;
    }

    UpdateLevelMeters();
}

void ControlWindow::StopMicrophoneTestMonitor()
{
    if (audio_ != nullptr && audio_->IsMicrophoneTestMonitorEnabled())
    {
        static_cast<void>(
            audio_->SetMicrophoneTestMonitorEnabled(false)
        );
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

    MicrophoneProcessingSettings processingSettings =
        currentConfig_.GetMicrophoneProcessingSettings();

    float highPassHz = 0.0f;
    float compressorThresholdDb = 0.0f;
    float compressorRatio = 0.0f;
    float compressorAttackMs = 0.0f;
    float compressorReleaseMs = 0.0f;
    float compressorMakeupDb = 0.0f;
    float limiterCeilingDb = 0.0f;

    const bool processingFieldsValid =
        ParseFloatControl(microphoneHighPassHzEdit_, highPassHz) &&
        ParseFloatControl(
            microphoneCompressorThresholdEdit_,
            compressorThresholdDb
        ) &&
        ParseFloatControl(
            microphoneCompressorRatioEdit_,
            compressorRatio
        ) &&
        ParseFloatControl(
            microphoneCompressorAttackEdit_,
            compressorAttackMs
        ) &&
        ParseFloatControl(
            microphoneCompressorReleaseEdit_,
            compressorReleaseMs
        ) &&
        ParseFloatControl(
            microphoneCompressorMakeupEdit_,
            compressorMakeupDb
        ) &&
        ParseFloatControl(
            microphoneLimiterCeilingEdit_,
            limiterCeilingDb
        );

    if (!processingFieldsValid)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Mikrofon filtresi alanlarından biri geçerli ve sonlu bir sayı değil.",
                L"One of the microphone-filter fields is not a valid finite number."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    const int presetSelection = static_cast<int>(SendMessageW(
        microphoneProcessingPresetCombo_,
        CB_GETCURSEL,
        0,
        0
    ));
    const auto selectedPreset =
        MicrophoneProcessingPresetFromIndex(presetSelection);

    if (!selectedPreset.has_value())
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Geçerli bir mikrofon preset'i seçin.",
                L"Select a valid microphone preset."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    const bool processingEnabled = SendMessageW(
        microphoneProcessingEnabledCheck_,
        BM_GETCHECK,
        0,
        0
    ) == BST_CHECKED;

    if (*selectedPreset != MicrophoneProcessingPreset::Custom)
    {
        const auto presetSettings = BuildMicrophoneProcessingPreset(
            *selectedPreset,
            processingEnabled
        );

        if (!presetSettings.has_value())
        {
            return false;
        }

        processingSettings = *presetSettings;
    }
    else
    {
        processingSettings.enabled = processingEnabled;
        processingSettings.preset = MicrophoneProcessingPreset::Custom;
        processingSettings.highPassEnabled = SendMessageW(
            microphoneHighPassEnabledCheck_,
            BM_GETCHECK,
            0,
            0
        ) == BST_CHECKED;
        processingSettings.highPassHz = highPassHz;
        processingSettings.compressorEnabled = SendMessageW(
            microphoneCompressorEnabledCheck_,
            BM_GETCHECK,
            0,
            0
        ) == BST_CHECKED;
        processingSettings.compressorThresholdDb =
            compressorThresholdDb;
        processingSettings.compressorRatio = compressorRatio;
        processingSettings.compressorAttackMs = compressorAttackMs;
        processingSettings.compressorReleaseMs = compressorReleaseMs;
        processingSettings.compressorMakeupDb = compressorMakeupDb;
        processingSettings.limiterEnabled = SendMessageW(
            microphoneLimiterEnabledCheck_,
            BM_GETCHECK,
            0,
            0
        ) == BST_CHECKED;
        processingSettings.limiterCeilingDb = limiterCeilingDb;
    }

    if (!IsValidMicrophoneProcessingSettings(processingSettings))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Mikrofon filtresi değerlerinden biri desteklenen aralığın dışında.",
                L"One of the microphone-filter values is outside the supported range."
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
    const bool checkUpdatesOnStart = SendMessageW(
        checkUpdatesOnStartCheck_,
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

    if (!candidate.SetMicrophoneProcessingSettings(
            processingSettings
        ))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Mikrofon işleme ayarları kabul edilmedi.",
                L"The microphone-processing settings were not accepted."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return false;
    }

    candidate.SetStartWithWindows(startWithWindows);
    candidate.SetShowConsoleOnStart(false);
    candidate.SetCheckUpdatesOnStart(checkUpdatesOnStart);
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
                L"The temporary config file could not be saved. Check logs\\latest.log for details."
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

bool ControlWindow::RemoveSelectedBinding(
    const bool requireConfirmation
)
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

    if (requireConfirmation)
    {
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

bool ControlWindow::ParseFloatControl(
    const HWND control,
    float& value
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

    std::string utf8 = WideToUtf8(text);
    std::replace(utf8.begin(), utf8.end(), ',', '.');

    const char* begin = utf8.data();
    const char* end = utf8.data() + utf8.size();
    float parsedValue = 0.0f;
    const auto [pointer, error] =
        std::from_chars(begin, end, parsedValue);

    if (error != std::errc{} || pointer != end ||
        !std::isfinite(parsedValue))
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
