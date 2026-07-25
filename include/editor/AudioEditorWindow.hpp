#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config/Config.hpp"
#include "editor/AudioDocument.hpp"
#include "editor/AudioWaveformCache.hpp"

#include <Windows.h>

#include <filesystem>
#include <optional>
#include <string>

class AudioEditorWindow final
{
public:
    AudioEditorWindow() = default;
    ~AudioEditorWindow();

    AudioEditorWindow(const AudioEditorWindow&) = delete;
    AudioEditorWindow& operator=(const AudioEditorWindow&) = delete;

    bool Show(
        HINSTANCE instance,
        HWND owner,
        AppTheme theme,
        const std::optional<std::filesystem::path>& initialFile = std::nullopt
    );

    void Shutdown();
    void SetTheme(AppTheme theme);
    void RefreshLocalizedText();

    [[nodiscard]] bool IsVisible() const noexcept;

private:
    static constexpr int InitialClientWidth = 980;
    static constexpr int InitialClientHeight = 580;
    static constexpr int MinimumClientWidth = 720;
    static constexpr int MinimumClientHeight = 420;
    static constexpr int IdOpenFile = 2100;
    static constexpr int IdClose = 2101;

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

    bool EnsureWindow(HINSTANCE instance, HWND owner);
    bool CreateControls();
    void LayoutControls(int clientWidth, int clientHeight);
    void HandleDpiChanged(UINT dpi, const RECT& suggestedRectangle);
    void BrowseForWav();
    bool LoadFile(const std::filesystem::path& filePath);
    void ClearDocument();
    void Paint();
    void DrawWaveform(HDC deviceContext, const RECT& rectangle);
    void DrawButton(const DRAWITEMSTRUCT& item) const;
    void ApplyTheme();
    void ApplyFonts();
    void ReleaseResources();
    void UpdateMetadataText();
    void SetStatusText(const std::wstring& text);

    [[nodiscard]] int Scale(int value) const noexcept;

    static std::wstring Utf8ToWide(const std::string& value);
    static std::wstring FormatDuration(double seconds);

    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND openButton_ = nullptr;
    HWND closeButton_ = nullptr;
    HWND fileLabel_ = nullptr;
    HWND metadataLabel_ = nullptr;
    HWND statusLabel_ = nullptr;
    HFONT bodyFont_ = nullptr;
    HFONT buttonFont_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    UINT currentDpi_ = USER_DEFAULT_SCREEN_DPI;
    AppTheme theme_ = AppTheme::Dark;
    bool classRegistered_ = false;

    COLORREF backgroundColor_ = RGB(15, 17, 23);
    COLORREF panelColor_ = RGB(24, 28, 36);
    COLORREF textColor_ = RGB(244, 246, 250);
    COLORREF mutedTextColor_ = RGB(154, 164, 178);
    COLORREF borderColor_ = RGB(42, 49, 64);
    COLORREF accentColor_ = RGB(124, 92, 255);
    COLORREF waveformColor_ = RGB(139, 108, 255);
    COLORREF centerLineColor_ = RGB(70, 78, 96);

    RECT waveformRectangle_{};
    std::filesystem::path loadedFile_;
    std::optional<AudioDocument> document_;
    std::optional<AudioWaveformCache> waveformCache_;
};
