#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config/Config.hpp"
#include "editor/AudioDocument.hpp"
#include "editor/AudioEditHistory.hpp"
#include "editor/AudioEditorEffects.hpp"
#include "editor/AudioEditorViewport.hpp"
#include "editor/AudioPreviewPlayer.hpp"
#include "editor/AudioSelectionTools.hpp"
#include "editor/AudioWaveformCache.hpp"

#include <Windows.h>

#include <cstdint>
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
    enum class SelectionEdit
    {
        Crop,
        Delete
    };

    enum class AudioEffect
    {
        Gain,
        Normalize,
        FadeIn,
        FadeOut,
        ConvertToMono
    };

    enum class SelectionDragMode
    {
        None,
        NewSelection,
        AdjustBegin,
        AdjustEnd
    };

    static constexpr int InitialClientWidth = 1080;
    static constexpr int InitialClientHeight = 820;
    static constexpr int MinimumClientWidth = 900;
    static constexpr int MinimumClientHeight = 680;
    static constexpr int IdOpenFile = 2100;
    static constexpr int IdClose = 2101;
    static constexpr int IdPlayPause = 2102;
    static constexpr int IdStop = 2103;
    static constexpr int IdZoomOut = 2104;
    static constexpr int IdZoomFit = 2105;
    static constexpr int IdZoomIn = 2106;
    static constexpr int IdWaveformScroll = 2107;
    static constexpr int IdSaveAs = 2108;
    static constexpr int IdOverwrite = 2109;
    static constexpr int IdUndo = 2110;
    static constexpr int IdRedo = 2111;
    static constexpr int IdCrop = 2112;
    static constexpr int IdDeleteSelection = 2113;
    static constexpr int IdSelectionStart = 2114;
    static constexpr int IdSelectionEnd = 2115;
    static constexpr int IdApplySelectionTimes = 2116;
    static constexpr int IdSnapZeroCrossings = 2117;
    static constexpr int IdZoomSelection = 2118;
    static constexpr int IdSelectAll = 2119;
    static constexpr int IdEffectScope = 2120;
    static constexpr int IdGainEdit = 2121;
    static constexpr int IdApplyGain = 2122;
    static constexpr int IdNormalize = 2123;
    static constexpr int IdFadeIn = 2124;
    static constexpr int IdFadeOut = 2125;
    static constexpr int IdConvertMono = 2126;
    static constexpr UINT_PTR PlaybackTimerId = 1U;
    static constexpr UINT PlaybackTimerMilliseconds = 40U;
    static constexpr int ScrollRangeMaximum = 10000;

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
    void BrowseSaveAs();
    bool LoadFile(const std::filesystem::path& filePath);
    bool SaveOverCurrent();
    bool SaveToFile(
        const std::filesystem::path& filePath,
        bool replaceExisting
    );
    bool ConfirmDiscardChanges();
    void ClearDocument();
    void Paint();
    void DrawWaveformBase(HDC deviceContext, const RECT& rectangle);
    void DrawWaveformOverlays(HDC deviceContext, const RECT& rectangle);
    bool EnsureWaveformBuffers(HDC referenceDeviceContext, int width, int height);
    bool RebuildWaveformBase();
    void ReleaseWaveformBuffers() noexcept;
    void MarkWaveformBaseDirty() noexcept;
    void InvalidatePlayheadTransition(
        std::size_t previousFrame,
        std::size_t currentFrame,
        bool viewportChanged
    );
    void InvalidateSelectionTransition(
        const std::optional<AudioFrameRange>& previousSelection,
        const std::optional<AudioFrameRange>& currentSelection
    );
    void DrawButton(const DRAWITEMSTRUCT& item) const;
    void ApplyTheme();
    void ApplyFonts();
    void ReleaseResources();
    void UpdateMetadataText();
    void UpdateTransportControls();
    void UpdateEditControls();
    void UpdateSelectionControls();
    void UpdateEffectControls();
    void UpdatePlaybackTimeText();
    void UpdateWaveformScrollBar();
    void UpdateWindowTitle();
    void SetStatusText(const std::wstring& text);

    void TogglePlayback();
    void StopPlayback();
    void SeekToFrame(std::size_t frame);
    void HandlePlaybackTimer();
    void ZoomAtFrame(std::size_t anchorFrame, double magnification);
    void FitWaveform();
    void HandleHorizontalScroll(WPARAM wParam);
    void HandleMouseWheel(WPARAM wParam, LPARAM lParam);
    void HandleWaveformMouseDown(int x, int y);
    void HandleWaveformMouseMove(int x, int y, WPARAM keyState);
    void HandleWaveformMouseUp(int x, int y);
    void UpdateSelectionFromPoint(int x);
    void ClearSelection();
    void SelectAllAudio();
    void ApplySelectionTimes();
    void SnapSelectionToZeroCrossings();
    void ZoomToSelection();
    void ApplySelectionEdit(SelectionEdit edit);
    void ToggleEffectScope();
    void ApplyAudioEffect(AudioEffect effect);
    void UndoEdit();
    void RedoEdit();
    bool RebuildAfterDocumentChange(bool fitWaveform);

    [[nodiscard]] RECT WaveformInnerRectangle() const noexcept;
    [[nodiscard]] std::size_t CurrentPlayheadFrame() const noexcept;
    [[nodiscard]] bool HasSelection() const noexcept;
    [[nodiscard]] bool IsModified() const noexcept;
    [[nodiscard]] int Scale(int value) const noexcept;

    static std::wstring Utf8ToWide(const std::string& value);
    static std::string WideToUtf8(const std::wstring& value);
    static std::wstring FormatDuration(double seconds);

    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND openButton_ = nullptr;
    HWND saveAsButton_ = nullptr;
    HWND overwriteButton_ = nullptr;
    HWND closeButton_ = nullptr;
    HWND playPauseButton_ = nullptr;
    HWND stopButton_ = nullptr;
    HWND undoButton_ = nullptr;
    HWND redoButton_ = nullptr;
    HWND cropButton_ = nullptr;
    HWND deleteButton_ = nullptr;
    HWND selectionStartLabel_ = nullptr;
    HWND selectionStartEdit_ = nullptr;
    HWND selectionEndLabel_ = nullptr;
    HWND selectionEndEdit_ = nullptr;
    HWND applySelectionButton_ = nullptr;
    HWND snapZeroButton_ = nullptr;
    HWND zoomSelectionButton_ = nullptr;
    HWND selectAllButton_ = nullptr;
    HWND effectScopeButton_ = nullptr;
    HWND gainLabel_ = nullptr;
    HWND gainEdit_ = nullptr;
    HWND applyGainButton_ = nullptr;
    HWND normalizeButton_ = nullptr;
    HWND fadeInButton_ = nullptr;
    HWND fadeOutButton_ = nullptr;
    HWND convertMonoButton_ = nullptr;
    HWND zoomOutButton_ = nullptr;
    HWND zoomFitButton_ = nullptr;
    HWND zoomInButton_ = nullptr;
    HWND waveformScrollBar_ = nullptr;
    HWND fileLabel_ = nullptr;
    HWND metadataLabel_ = nullptr;
    HWND timeLabel_ = nullptr;
    HWND statusLabel_ = nullptr;
    HFONT bodyFont_ = nullptr;
    HFONT buttonFont_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    HDC waveformBaseDeviceContext_ = nullptr;
    HBITMAP waveformBaseBitmap_ = nullptr;
    HGDIOBJ waveformBaseOriginalBitmap_ = nullptr;
    HDC waveformBufferDeviceContext_ = nullptr;
    HBITMAP waveformBufferBitmap_ = nullptr;
    HGDIOBJ waveformBufferOriginalBitmap_ = nullptr;
    int waveformBufferWidth_ = 0;
    int waveformBufferHeight_ = 0;
    bool waveformBaseDirty_ = true;
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
    COLORREF playheadColor_ = RGB(255, 93, 115);
    COLORREF selectionColor_ = RGB(54, 47, 86);
    COLORREF selectionBorderColor_ = RGB(177, 155, 255);

    RECT waveformRectangle_{};
    std::filesystem::path loadedFile_;
    std::optional<AudioDocument> document_;
    std::optional<AudioWaveformCache> waveformCache_;
    AudioEditHistory editHistory_;
    AudioEditorViewport viewport_;
    AudioPreviewPlayer previewPlayer_;
    std::optional<AudioFrameRange> selection_;
    std::size_t selectionAnchorFrame_ = 0U;
    int selectionAnchorX_ = 0;
    bool selecting_ = false;
    bool selectionDragged_ = false;
    bool updatingSelectionFields_ = false;
    SelectionDragMode selectionDragMode_ = SelectionDragMode::None;
    AudioEffectScope effectScope_ = AudioEffectScope::Selection;
    std::size_t playheadFrame_ = 0U;
    std::uint64_t currentStateIdentifier_ = 0U;
    std::uint64_t savedStateIdentifier_ = 0U;
    std::uint64_t nextStateIdentifier_ = 1U;
};
