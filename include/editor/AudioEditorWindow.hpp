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

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

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
        std::string previewDevice,
        float previewVolume,
        const std::optional<std::filesystem::path>& initialFile = std::nullopt
    );

    bool ShowEmbedded(
        HINSTANCE instance,
        HWND parent,
        AppTheme theme,
        std::string previewDevice,
        float previewVolume,
        const std::optional<std::filesystem::path>& initialFile = std::nullopt
    );

    void SetEmbeddedBounds(int x, int y, int width, int height);
    void SetEmbeddedVisible(bool visible);
    void Focus();
    void SaveCurrent();
    void SaveAs();
    void CancelOrClear();
    void Shutdown();
    void SetPreviewRoute(std::string previewDevice, float previewVolume);
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

    struct AudioLoadJobResult final
    {
        std::filesystem::path filePath;
        std::optional<AudioDocument> document;
        std::optional<AudioWaveformCache> waveformCache;
        std::string errorMessage;
        bool cancelled = false;
    };

    struct AudioSaveJobResult final
    {
        std::filesystem::path filePath;
        std::uint64_t stateIdentifier = 0U;
        std::string errorMessage;
        bool succeeded = false;
        bool cancelled = false;
    };

    static constexpr int InitialClientWidth = 1120;
    static constexpr int InitialClientHeight = 840;
    static constexpr int MinimumClientWidth = 980;
    static constexpr int MinimumClientHeight = 700;
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
    static constexpr int IdCut = 2127;
    static constexpr int IdCopy = 2128;
    static constexpr int IdPaste = 2129;
    static constexpr int IdSilenceSelection = 2130;
    static constexpr int IdTrimSilence = 2131;
    static constexpr UINT LoadJobCompletedMessage = WM_APP + 80U;
    static constexpr UINT SaveJobCompletedMessage = WM_APP + 81U;
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

    bool EnsureWindow(HINSTANCE instance, HWND owner, bool embedded);
    bool CreateControls();
    void LayoutControls(int clientWidth, int clientHeight);
    void HandleDpiChanged(UINT dpi, const RECT& suggestedRectangle);
    void BrowseForWav();
    void BrowseSaveAs(bool synchronous = false);
    bool LoadFile(const std::filesystem::path& filePath);
    void RequestLoadCancellation();
    void HandleLoadJobCompleted();
    void StopLoadJob(bool waitForCompletion);
    void UpdateLoadControls();
    bool SaveOverCurrent(bool synchronous = false);
    bool SaveToFile(
        const std::filesystem::path& filePath,
        bool replaceExisting,
        bool synchronous = false
    );
    void RequestSaveCancellation();
    void HandleSaveJobCompleted();
    void StopSaveJob(bool waitForCompletion);
    void UpdateSaveControls();
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
    void DrawButtonGlyph(
        HDC deviceContext,
        HWND button,
        const RECT& rectangle,
        COLORREF color,
        COLORREF backgroundColor
    ) const;
    [[nodiscard]] bool HasButtonGlyph(HWND button) const;
    [[nodiscard]] bool IsPrimaryButton(HWND control) const;
    [[nodiscard]] bool IsDangerButton(HWND control) const;
    void DrawSectionHeader(
        HDC deviceContext,
        int top,
        const wchar_t* text
    ) const;
    void ApplyTheme();
    void ApplyFonts();
    void ReleaseResources();
    void UpdateMetadataText();
    void UpdateTransportControls();
    void UpdateEditControls();
    void UpdateSelectionControls(bool force = false);
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
    void SetSelectionBoundaryAtPlayhead(AudioSelectionBoundary boundary);
    void ApplySelectionTimes();
    void SnapSelectionToZeroCrossings();
    void ZoomToSelection();
    void ApplySelectionEdit(SelectionEdit edit);
    bool CopySelection();
    void CutSelection();
    void PasteClipboard();
    void SilenceSelection();
    void TrimBoundarySilence();
    bool FinalizeDocumentEdit(
        AudioDocument before,
        std::uint64_t beforeState,
        std::optional<AudioFrameRange> nextSelection,
        std::size_t nextPlayheadFrame,
        bool fitWaveform,
        const wchar_t* turkishStatus,
        const wchar_t* englishStatus
    );
    void ToggleEffectScope();
    void ApplyAudioEffect(AudioEffect effect);
    void UndoEdit();
    void RedoEdit();
    bool RebuildAfterDocumentChange(bool fitWaveform);

    [[nodiscard]] RECT WaveformInnerRectangle() const noexcept;
    [[nodiscard]] std::size_t CurrentPlayheadFrame() const noexcept;
    [[nodiscard]] bool HasSelection() const noexcept;
    [[nodiscard]] bool IsModified() const noexcept;
    [[nodiscard]] bool IsLoadRunning() const noexcept;
    [[nodiscard]] bool IsSaveRunning() const noexcept;
    [[nodiscard]] bool IsBusy() const noexcept;
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
    HWND cutButton_ = nullptr;
    HWND copyButton_ = nullptr;
    HWND pasteButton_ = nullptr;
    HWND silenceButton_ = nullptr;
    HWND trimSilenceButton_ = nullptr;
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
    HWND hintLabel_ = nullptr;
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
    bool embedded_ = false;

    COLORREF backgroundColor_ = RGB(15, 17, 23);
    COLORREF panelColor_ = RGB(24, 28, 36);
    COLORREF inputColor_ = RGB(17, 21, 29);
    COLORREF textColor_ = RGB(244, 246, 250);
    COLORREF mutedTextColor_ = RGB(154, 164, 178);
    COLORREF borderColor_ = RGB(42, 49, 64);
    COLORREF accentColor_ = RGB(124, 92, 255);
    COLORREF accentHoverColor_ = RGB(139, 108, 255);
    COLORREF dangerColor_ = RGB(224, 82, 82);
    COLORREF waveformColor_ = RGB(139, 108, 255);
    COLORREF centerLineColor_ = RGB(70, 78, 96);
    COLORREF playheadColor_ = RGB(255, 93, 115);
    COLORREF selectionColor_ = RGB(54, 47, 86);
    COLORREF selectionBorderColor_ = RGB(177, 155, 255);

    RECT waveformRectangle_{};
    RECT hintRectangle_{};
    int fileSectionTop_ = 0;
    int editSectionTop_ = 0;
    int selectionSectionTop_ = 0;
    int effectsSectionTop_ = 0;
    std::filesystem::path loadedFile_;
    std::optional<AudioDocument> document_;
    std::jthread loadThread_;
    std::atomic_bool loadRunning_{false};
    std::atomic_bool loadCancellationRequested_{false};
    std::mutex loadResultMutex_;
    std::optional<AudioLoadJobResult> pendingLoadResult_;
    std::jthread saveThread_;
    std::atomic_bool saveRunning_{false};
    std::atomic_bool saveCancellationRequested_{false};
    std::mutex saveResultMutex_;
    std::optional<AudioSaveJobResult> pendingSaveResult_;
    std::optional<AudioDocument> clipboard_;
    std::optional<AudioWaveformCache> waveformCache_;
    AudioEditHistory editHistory_;
    AudioEditorViewport viewport_;
    AudioPreviewPlayer previewPlayer_;
    std::string previewDeviceRequest_ = "default";
    float previewVolume_ = 1.0f;
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
