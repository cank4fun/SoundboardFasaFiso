#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config/Config.hpp"
#include "update/UpdateChecker.hpp"

#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class Audio;

struct ControlWindowCommandIds
{
    int applySettings = 0;
    int stop = 0;
    int outputMute = 0;
    int monitorMute = 0;
    int reload = 0;
    int toggleConsole = 0;
    int exit = 0;
};

class ControlWindow
{
public:
    ControlWindow() = default;
    ~ControlWindow();

    ControlWindow(const ControlWindow&) = delete;
    ControlWindow& operator=(const ControlWindow&) = delete;

    bool Initialize(
        const Config& config,
        const std::filesystem::path& configPath,
        const std::filesystem::path& soundsFolder,
        const std::vector<std::string>& playbackDevices,
        const std::vector<std::string>& captureDevices,
        const ControlWindowCommandIds& commandIds,
        Audio* audio
    );

    void Shutdown();

    void Show();
    void Hide();
    bool ToggleVisibility();
    bool IsVisible() const;
    HWND NativeHandle() const noexcept;
    HACCEL AcceleratorTable() const noexcept;

    void UpdateConfig(const Config& config);
    void SetPlaybackDevices(
        const std::vector<std::string>& playbackDevices
    );
    void SetCaptureDevices(
        const std::vector<std::string>& captureDevices
    );
    void SetStatus(const std::wstring& status);
    void CheckForUpdates(bool showCurrentResult = true);

private:
    enum class ControlPage
    {
        Main,
        Settings,
        MicrophoneProcessing,
        Hotkeys
    };

    static constexpr int InitialClientWidth = 960;
    static constexpr int InitialClientHeight = 640;
    static constexpr int MinimumClientWidth = 800;
    static constexpr int MinimumClientHeight = 540;

    static constexpr int IdApplySettings = 1000;
    static constexpr int IdReload = 1001;
    static constexpr int IdStopAll = 1002;
    static constexpr int IdOutputMute = 1003;
    static constexpr int IdMonitorMute = 1004;
    static constexpr int IdOpenConfig = 1005;
    static constexpr int IdOpenSounds = 1006;
    static constexpr int IdToggleConsole = 1007;
    static constexpr int IdExit = 1008;
    static constexpr int IdRefreshDevices = 1009;
    static constexpr int IdOutputVolumeSlider = 1010;
    static constexpr int IdMonitorVolumeSlider = 1011;
    static constexpr int IdBindingsList = 1012;
    static constexpr int IdBindingVolumeSlider = 1013;
    static constexpr int IdBrowseSound = 1014;
    static constexpr int IdCaptureHotkey = 1015;
    static constexpr int IdAddBinding = 1016;
    static constexpr int IdUpdateBinding = 1017;
    static constexpr int IdRemoveBinding = 1018;
    static constexpr int IdClearBinding = 1019;
    static constexpr int IdMicrophoneVolumeSlider = 1020;
    static constexpr int IdOpenLogs = 1021;
    static constexpr int IdThemeToggle = 1022;
    static constexpr int IdCheckUpdates = 1023;
    static constexpr int IdMainTab = 1024;
    static constexpr int IdSettingsTab = 1025;
    static constexpr int IdHotkeysTab = 1026;
    static constexpr int IdCancelHotkeyCapture = 1027;
    static constexpr int IdMicrophoneProcessingTab = 1028;
    static constexpr int IdMicrophoneProcessingPreset = 1029;

    static constexpr UINT_PTR LevelMeterTimerId = 1;
    static constexpr UINT UpdateCheckCompletedMessage = WM_APP + 64;
    static constexpr UINT LevelMeterIntervalMilliseconds = 40;

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

    bool CreateControls();
    bool CreateAccelerators();
    void LayoutControls(int clientWidth, int clientHeight);
    void HandleDpiChanged(UINT dpi, const RECT& suggestedRectangle);
    int Scale(int value) const noexcept;
    int Unscale(int value) const noexcept;
    void SetActivePage(ControlPage page);
    void UpdatePageVisibility();
    void RefreshLocalizedText();
    void ApplyTheme();
    void ApplyFonts();
    void ReleaseFonts();
    void RecreateThemeResources();
    void ReleaseThemeResources();
    void UpdateWindowChrome();
    void DrawOwnerDrawControl(const DRAWITEMSTRUCT& item);
    void DrawCard(const DRAWITEMSTRUCT& item);
    void DrawModernButton(const DRAWITEMSTRUCT& item);
    void DrawModernSlider(HWND slider, HDC deviceContext) const;
    void DrawLevelMeter(const DRAWITEMSTRUCT& item) const;
    void PaintWindowBackground();
    bool IsCardControl(HWND control) const;
    bool IsSliderControl(HWND control) const;
    bool IsLevelMeterControl(HWND control) const;
    bool IsPrimaryButton(HWND control) const;
    bool IsNavigationTab(HWND control) const;
    bool IsDangerButton(HWND control) const;
    HBRUSH StaticBrushFor(HWND control) const;
    void PopulateBindings();
    void PopulateEditorControls();
    void PopulateDeviceCombos();
    void PopulateNumericCombos();
    void PopulateMicrophoneProcessingControls();
    void PopulateMicrophoneProcessingPresetCombo();
    void ApplySelectedMicrophoneProcessingPreset();
    void MarkMicrophoneProcessingPresetCustom();
    void PopulateControlHotkeys();
    void UpdateVolumeLabels();
    void UpdateBindingVolumeLabel();
    void UpdateLevelMeters();
    void HandleUpdateCheckCompleted();
    bool SavePendingSettings();

    void LoadSelectedBindingIntoEditor();
    void ClearBindingEditor();
    bool AddOrUpdateBinding(bool updateExisting);
    bool RemoveSelectedBinding(bool requireConfirmation = true);
    void BrowseForSoundFile();
    void BeginHotkeyCapture();
    bool CaptureHotkeyFromMessage(WPARAM virtualKey);

    std::filesystem::path ImportSoundFile(
        const std::filesystem::path& selectedPath
    );

    void PostApplicationCommand(int commandId) const;
    void OpenPath(const std::filesystem::path& path) const;

    static std::wstring Utf8ToWide(const std::string& value);
    static std::string WideToUtf8(const std::wstring& value);
    static std::wstring GetControlText(HWND control);
    static void SetControlText(HWND control, const std::wstring& text);
    static void ApplyDefaultFont(HWND control);
    static void AddComboItem(HWND combo, const std::wstring& text);
    static void SelectComboText(HWND combo, const std::wstring& text);
    static bool ParseUnsignedControl(
        HWND control,
        unsigned int& value
    );
    static bool ParseFloatControl(HWND control, float& value);
    static std::wstring VirtualKeyName(unsigned int virtualKey);
    static std::wstring NormalizeHotkeyText(std::wstring text);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HACCEL acceleratorTable_ = nullptr;
    DWORD mainThreadId_ = 0;
    UINT currentDpi_ = USER_DEFAULT_SCREEN_DPI;
    ControlWindowCommandIds commandIds_{};
    Audio* audio_ = nullptr;

    std::filesystem::path configPath_;
    std::filesystem::path pendingConfigPath_;
    std::filesystem::path soundsFolder_;
    std::filesystem::path logsFolder_;

    Config currentConfig_;
    std::vector<std::string> playbackDevices_;
    std::vector<std::string> captureDevices_;
    std::vector<SoundBinding> pendingBindings_;

    HWND headerLabel_ = nullptr;
    HWND subtitleLabel_ = nullptr;
    HWND themeToggleButton_ = nullptr;
    HWND mainTabButton_ = nullptr;
    HWND settingsTabButton_ = nullptr;
    HWND microphoneProcessingTabButton_ = nullptr;
    HWND hotkeysTabButton_ = nullptr;
    HWND statusCaption_ = nullptr;
    HWND statusValue_ = nullptr;

    HWND mainQuickGroup_ = nullptr;
    HWND mainOutputMeterCaption_ = nullptr;
    HWND mainOutputLevelMeter_ = nullptr;
    HWND mainMonitorMeterCaption_ = nullptr;
    HWND mainMonitorLevelMeter_ = nullptr;
    HWND mainMicrophoneMeterCaption_ = nullptr;
    HWND mainMicrophoneLevelMeter_ = nullptr;

    HWND settingsGroup_ = nullptr;
    HWND outputCaption_ = nullptr;
    HWND outputCombo_ = nullptr;
    HWND outputVolumeCaption_ = nullptr;
    HWND outputVolumeSlider_ = nullptr;
    HWND outputLevelMeter_ = nullptr;
    HWND outputVolumeValue_ = nullptr;
    HWND monitorCaption_ = nullptr;
    HWND monitorCombo_ = nullptr;
    HWND monitorVolumeCaption_ = nullptr;
    HWND monitorVolumeSlider_ = nullptr;
    HWND monitorLevelMeter_ = nullptr;
    HWND monitorVolumeValue_ = nullptr;
    HWND microphoneCaption_ = nullptr;
    HWND microphoneCombo_ = nullptr;
    HWND microphoneVolumeCaption_ = nullptr;
    HWND microphoneVolumeSlider_ = nullptr;
    HWND microphoneLevelMeter_ = nullptr;
    HWND microphoneVolumeValue_ = nullptr;
    HWND microphoneEnabledCheck_ = nullptr;
    HWND microphoneToOutputCheck_ = nullptr;
    HWND microphoneToMonitorCheck_ = nullptr;
    HWND sampleRateCaption_ = nullptr;
    HWND sampleRateCombo_ = nullptr;
    HWND bufferCaption_ = nullptr;
    HWND bufferCombo_ = nullptr;
    HWND languageCaption_ = nullptr;
    HWND languageCombo_ = nullptr;
    HWND startWithWindowsCheck_ = nullptr;
    HWND checkUpdatesOnStartCheck_ = nullptr;
    HWND refreshDevicesButton_ = nullptr;
    HWND applySettingsButton_ = nullptr;

    HWND microphoneProcessingGroup_ = nullptr;
    HWND microphoneProcessingEnabledCheck_ = nullptr;
    HWND microphoneProcessingPresetCaption_ = nullptr;
    HWND microphoneProcessingPresetCombo_ = nullptr;
    HWND microphoneProcessingStatusCaption_ = nullptr;
    HWND microphoneProcessingStatusValue_ = nullptr;
    HWND microphoneRawMeterCaption_ = nullptr;
    HWND microphoneRawLevelMeter_ = nullptr;
    HWND microphoneProcessedMeterCaption_ = nullptr;
    HWND microphoneProcessedLevelMeter_ = nullptr;
    HWND microphoneHighPassEnabledCheck_ = nullptr;
    HWND microphoneHighPassHzCaption_ = nullptr;
    HWND microphoneHighPassHzEdit_ = nullptr;
    HWND microphoneCompressorEnabledCheck_ = nullptr;
    HWND microphoneCompressorThresholdCaption_ = nullptr;
    HWND microphoneCompressorThresholdEdit_ = nullptr;
    HWND microphoneCompressorRatioCaption_ = nullptr;
    HWND microphoneCompressorRatioEdit_ = nullptr;
    HWND microphoneCompressorAttackCaption_ = nullptr;
    HWND microphoneCompressorAttackEdit_ = nullptr;
    HWND microphoneCompressorReleaseCaption_ = nullptr;
    HWND microphoneCompressorReleaseEdit_ = nullptr;
    HWND microphoneCompressorMakeupCaption_ = nullptr;
    HWND microphoneCompressorMakeupEdit_ = nullptr;
    HWND microphoneLimiterEnabledCheck_ = nullptr;
    HWND microphoneLimiterCeilingCaption_ = nullptr;
    HWND microphoneLimiterCeilingEdit_ = nullptr;
    HWND microphoneUnavailableFeaturesCaption_ = nullptr;

    HWND controlHotkeysGroup_ = nullptr;
    HWND stopHotkeyCaption_ = nullptr;
    HWND stopHotkeyEdit_ = nullptr;
    HWND outputMuteHotkeyCaption_ = nullptr;
    HWND outputMuteHotkeyEdit_ = nullptr;
    HWND monitorMuteHotkeyCaption_ = nullptr;
    HWND monitorMuteHotkeyEdit_ = nullptr;
    HWND reloadHotkeyCaption_ = nullptr;
    HWND reloadHotkeyEdit_ = nullptr;
    HWND exitHotkeyCaption_ = nullptr;
    HWND exitHotkeyEdit_ = nullptr;

    HWND bindingsGroup_ = nullptr;
    HWND bindingsList_ = nullptr;
    HWND bindingEditorGroup_ = nullptr;
    HWND bindingHotkeyCaption_ = nullptr;
    HWND bindingHotkeyEdit_ = nullptr;
    HWND captureHotkeyButton_ = nullptr;
    HWND bindingFileCaption_ = nullptr;
    HWND bindingFileEdit_ = nullptr;
    HWND browseSoundButton_ = nullptr;
    HWND bindingModeCaption_ = nullptr;
    HWND bindingModeCombo_ = nullptr;
    HWND bindingVolumeCaption_ = nullptr;
    HWND bindingVolumeSlider_ = nullptr;
    HWND bindingVolumeValue_ = nullptr;
    HWND addBindingButton_ = nullptr;
    HWND updateBindingButton_ = nullptr;
    HWND removeBindingButton_ = nullptr;
    HWND clearBindingButton_ = nullptr;

    HWND reloadButton_ = nullptr;
    HWND stopButton_ = nullptr;
    HWND outputMuteButton_ = nullptr;
    HWND monitorMuteButton_ = nullptr;
    HWND openConfigButton_ = nullptr;
    HWND openSoundsButton_ = nullptr;
    HWND openLogsButton_ = nullptr;
    HWND checkUpdatesButton_ = nullptr;
    HWND consoleButton_ = nullptr;
    HWND exitButton_ = nullptr;
    HWND settingsToolsGroup_ = nullptr;

    ControlPage activePage_ = ControlPage::Main;
    AppTheme activeTheme_ = AppTheme::Dark;

    HFONT headerFont_ = nullptr;
    HFONT subtitleFont_ = nullptr;
    HFONT bodyFont_ = nullptr;
    HFONT sectionFont_ = nullptr;
    HFONT buttonFont_ = nullptr;

    HBRUSH backgroundBrush_ = nullptr;
    HBRUSH cardBrush_ = nullptr;
    HBRUSH inputBrush_ = nullptr;

    COLORREF backgroundColor_ = RGB(15, 17, 23);
    COLORREF cardColor_ = RGB(24, 28, 36);
    COLORREF inputColor_ = RGB(17, 21, 29);
    COLORREF textColor_ = RGB(244, 246, 250);
    COLORREF mutedTextColor_ = RGB(154, 164, 178);
    COLORREF borderColor_ = RGB(42, 49, 64);
    COLORREF accentColor_ = RGB(124, 92, 255);
    COLORREF accentHoverColor_ = RGB(139, 108, 255);
    COLORREF dangerColor_ = RGB(224, 82, 82);

    float outputMeterLevel_ = 0.0f;
    float monitorMeterLevel_ = 0.0f;
    float microphoneMeterLevel_ = 0.0f;
    float microphoneRawMeterLevel_ = 0.0f;
    float microphoneProcessedMeterLevel_ = 0.0f;
    bool outputMeterAvailable_ = false;
    bool monitorMeterAvailable_ = false;
    bool microphoneMeterAvailable_ = false;
    bool microphoneProcessingMeterAvailable_ = false;
    bool populatingMicrophoneProcessingControls_ = false;

    std::jthread updateCheckThread_;
    std::mutex updateCheckMutex_;
    std::optional<UpdateCheckResult> pendingUpdateResult_;
    bool pendingUpdateShowCurrentResult_ = false;
    std::atomic_bool updateCheckRunning_{false};

    int selectedBindingIndex_ = -1;
    bool capturingBindingHotkey_ = false;
    bool classRegistered_ = false;
};
