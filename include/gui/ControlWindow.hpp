#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config/Config.hpp"

#include <Windows.h>

#include <filesystem>
#include <string>
#include <vector>

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
        const ControlWindowCommandIds& commandIds
    );

    void Shutdown();

    void Show();
    void Hide();
    bool ToggleVisibility();
    bool IsVisible() const;

    void UpdateConfig(const Config& config);
    void SetPlaybackDevices(
        const std::vector<std::string>& playbackDevices
    );
    void SetCaptureDevices(
        const std::vector<std::string>& captureDevices
    );
    void SetStatus(const std::wstring& status);

private:
    static constexpr int MinimumClientWidth = 1020;
    static constexpr int MinimumClientHeight = 940;

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
    void LayoutControls(int clientWidth, int clientHeight);
    void RefreshLocalizedText();
    void PopulateBindings();
    void PopulateEditorControls();
    void PopulateDeviceCombos();
    void PopulateNumericCombos();
    void PopulateControlHotkeys();
    void UpdateVolumeLabels();
    void UpdateBindingVolumeLabel();
    bool SavePendingSettings();

    void LoadSelectedBindingIntoEditor();
    void ClearBindingEditor();
    bool AddOrUpdateBinding(bool updateExisting);
    bool RemoveSelectedBinding();
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
    static std::wstring VirtualKeyName(unsigned int virtualKey);
    static std::wstring NormalizeHotkeyText(std::wstring text);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    DWORD mainThreadId_ = 0;
    ControlWindowCommandIds commandIds_{};

    std::filesystem::path configPath_;
    std::filesystem::path pendingConfigPath_;
    std::filesystem::path soundsFolder_;
    std::filesystem::path logsFolder_;

    Config currentConfig_;
    std::vector<std::string> playbackDevices_;
    std::vector<std::string> captureDevices_;
    std::vector<SoundBinding> pendingBindings_;

    HWND headerLabel_ = nullptr;
    HWND statusCaption_ = nullptr;
    HWND statusValue_ = nullptr;

    HWND settingsGroup_ = nullptr;
    HWND outputCaption_ = nullptr;
    HWND outputCombo_ = nullptr;
    HWND outputVolumeCaption_ = nullptr;
    HWND outputVolumeSlider_ = nullptr;
    HWND outputVolumeValue_ = nullptr;
    HWND monitorCaption_ = nullptr;
    HWND monitorCombo_ = nullptr;
    HWND monitorVolumeCaption_ = nullptr;
    HWND monitorVolumeSlider_ = nullptr;
    HWND monitorVolumeValue_ = nullptr;
    HWND microphoneCaption_ = nullptr;
    HWND microphoneCombo_ = nullptr;
    HWND microphoneVolumeCaption_ = nullptr;
    HWND microphoneVolumeSlider_ = nullptr;
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
    HWND showConsoleOnStartCheck_ = nullptr;
    HWND refreshDevicesButton_ = nullptr;
    HWND applySettingsButton_ = nullptr;

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
    HWND consoleButton_ = nullptr;
    HWND exitButton_ = nullptr;

    int selectedBindingIndex_ = -1;
    bool capturingBindingHotkey_ = false;
    bool classRegistered_ = false;
};
