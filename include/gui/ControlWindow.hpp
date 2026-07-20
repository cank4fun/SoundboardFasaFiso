#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <filesystem>
#include <string>

class Config;

struct ControlWindowCommandIds
{
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
        const ControlWindowCommandIds& commandIds
    );

    void Shutdown();

    void Show();
    void Hide();
    bool ToggleVisibility();
    bool IsVisible() const;

    void UpdateConfig(const Config& config);
    void SetStatus(const std::wstring& status);

private:
    static constexpr int MinimumClientWidth = 720;
    static constexpr int MinimumClientHeight = 560;

    static constexpr int IdReload = 1001;
    static constexpr int IdStopAll = 1002;
    static constexpr int IdOutputMute = 1003;
    static constexpr int IdMonitorMute = 1004;
    static constexpr int IdOpenConfig = 1005;
    static constexpr int IdOpenSounds = 1006;
    static constexpr int IdToggleConsole = 1007;
    static constexpr int IdExit = 1008;

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
    void PostApplicationCommand(int commandId) const;
    void OpenPath(const std::filesystem::path& path) const;

    static std::wstring Utf8ToWide(const std::string& value);
    static void SetControlText(HWND control, const std::wstring& text);
    static void ApplyDefaultFont(HWND control);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    DWORD mainThreadId_ = 0;
    ControlWindowCommandIds commandIds_{};

    std::filesystem::path configPath_;
    std::filesystem::path soundsFolder_;

    HWND headerLabel_ = nullptr;
    HWND statusCaption_ = nullptr;
    HWND statusValue_ = nullptr;

    HWND audioGroup_ = nullptr;
    HWND outputLabel_ = nullptr;
    HWND monitorLabel_ = nullptr;
    HWND audioSettingsLabel_ = nullptr;
    HWND languageLabel_ = nullptr;

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
