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
    void SetStatus(const std::wstring& status);

private:
    static constexpr int MinimumClientWidth = 860;
    static constexpr int MinimumClientHeight = 680;

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
    void PopulateBindings(const Config& config);
    void PopulateEditorControls();
    void PopulateDeviceCombos();
    void PopulateNumericCombos();
    void UpdateVolumeLabels();
    bool SavePendingSettings();

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

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    DWORD mainThreadId_ = 0;
    ControlWindowCommandIds commandIds_{};

    std::filesystem::path configPath_;
    std::filesystem::path pendingConfigPath_;
    std::filesystem::path soundsFolder_;

    Config currentConfig_;
    std::vector<std::string> playbackDevices_;

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
    HWND sampleRateCaption_ = nullptr;
    HWND sampleRateCombo_ = nullptr;
    HWND bufferCaption_ = nullptr;
    HWND bufferCombo_ = nullptr;
    HWND languageCaption_ = nullptr;
    HWND languageCombo_ = nullptr;
    HWND refreshDevicesButton_ = nullptr;
    HWND applySettingsButton_ = nullptr;

    HWND bindingsGroup_ = nullptr;
    HWND bindingsList_ = nullptr;

    HWND reloadButton_ = nullptr;
    HWND stopButton_ = nullptr;
    HWND outputMuteButton_ = nullptr;
    HWND monitorMuteButton_ = nullptr;
    HWND openConfigButton_ = nullptr;
    HWND openSoundsButton_ = nullptr;
    HWND consoleButton_ = nullptr;
    HWND exitButton_ = nullptr;

    bool classRegistered_ = false;
};
