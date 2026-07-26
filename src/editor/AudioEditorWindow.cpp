#include "editor/AudioEditorWindow.hpp"

#include "editor/AudioDocumentWav.hpp"
#include "editor/AudioPreviewDeviceSelection.hpp"
#include "localization/Localization.hpp"
#include "ResourceIds.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <exception>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr wchar_t AudioEditorWindowClassName[] =
        L"SoundBoardFasaFiso.AudioEditorWindow";
    constexpr int Margin = 18;
    constexpr int ButtonHeight = 38;
    constexpr int ButtonGap = 10;
    constexpr int HeaderRowHeight = 92;
    constexpr int TransportRowHeight = 44;
    constexpr int EditRowHeight = 44;
    constexpr int ClipboardRowHeight = 44;
    constexpr int SelectionRowHeight = 44;
    constexpr int EffectsRowHeight = 44;
    constexpr int TransportButtonWidth = 92;
    constexpr int CompactButtonWidth = 60;
    constexpr int ScrollBarHeight = 18;
    constexpr int StatusRowHeight = 24;
    constexpr int PanelRadius = 12;
    constexpr DWORD AudioEditorWindowStyle =
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    constexpr DWORD EmbeddedAudioEditorWindowStyle =
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

    COLORREF BlendColor(
        const COLORREF base,
        const COLORREF overlay,
        const int overlayPercent
    )
    {
        const int clampedPercent = std::clamp(overlayPercent, 0, 100);
        const int basePercent = 100 - clampedPercent;
        return RGB(
            (GetRValue(base) * basePercent +
                GetRValue(overlay) * clampedPercent) / 100,
            (GetGValue(base) * basePercent +
                GetGValue(overlay) * clampedPercent) / 100,
            (GetBValue(base) * basePercent +
                GetBValue(overlay) * clampedPercent) / 100
        );
    }

    bool IsAudioEditorShortcut(const WPARAM key)
    {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        if (!control && !alt && (key == 'I' || key == 'O'))
        {
            return true;
        }

        if (control)
        {
            switch (key)
            {
                case 'A':
                case 'C':
                case 'E':
                case 'L':
                case 'O':
                case 'S':
                case 'V':
                case 'X':
                case 'Y':
                case 'Z':
                    return true;
                default:
                    break;
            }
        }

        switch (key)
        {
            case VK_SPACE:
            case VK_DELETE:
            case VK_ESCAPE:
            case VK_HOME:
            case VK_END:
            case VK_ADD:
            case VK_SUBTRACT:
            case VK_OEM_PLUS:
            case VK_OEM_MINUS:
            case 'S':
            case 'Z':
                return true;
            default:
                return false;
        }
    }

    LRESULT CALLBACK AudioEditorControlSubclassProcedure(
        const HWND control,
        const UINT message,
        const WPARAM wParam,
        const LPARAM lParam,
        const UINT_PTR subclassIdentifier,
        const DWORD_PTR referenceData
    )
    {
        if (message == WM_GETDLGCODE && lParam != 0)
        {
            const auto* keyMessage = reinterpret_cast<const MSG*>(lParam);
            if (keyMessage->message == WM_KEYDOWN &&
                IsAudioEditorShortcut(keyMessage->wParam))
            {
                return DLGC_WANTMESSAGE;
            }
        }

        if (message == WM_KEYDOWN && IsAudioEditorShortcut(wParam))
        {
            const HWND editorWindow = reinterpret_cast<HWND>(referenceData);
            if (editorWindow != nullptr && IsWindow(editorWindow) != FALSE)
            {
                return SendMessageW(editorWindow, message, wParam, lParam);
            }
        }

        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(
                control,
                AudioEditorControlSubclassProcedure,
                subclassIdentifier
            );
        }

        return DefSubclassProc(control, message, wParam, lParam);
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

    bool EnsureBitmapSurface(
        const HDC referenceDeviceContext,
        const int width,
        const int height,
        HDC& surfaceDeviceContext,
        HBITMAP& surfaceBitmap,
        HGDIOBJ& originalBitmap
    )
    {
        if (surfaceDeviceContext == nullptr)
        {
            surfaceDeviceContext = CreateCompatibleDC(referenceDeviceContext);
            if (surfaceDeviceContext == nullptr)
            {
                return false;
            }
        }

        if (surfaceBitmap != nullptr)
        {
            if (originalBitmap != nullptr)
            {
                SelectObject(surfaceDeviceContext, originalBitmap);
            }
            DeleteObject(surfaceBitmap);
            surfaceBitmap = nullptr;
        }

        HBITMAP bitmap = CreateCompatibleBitmap(
            referenceDeviceContext,
            width,
            height
        );
        if (bitmap == nullptr)
        {
            return false;
        }

        const HGDIOBJ previousBitmap = SelectObject(
            surfaceDeviceContext,
            bitmap
        );
        if (previousBitmap == nullptr || previousBitmap == HGDI_ERROR)
        {
            DeleteObject(bitmap);
            return false;
        }

        if (originalBitmap == nullptr)
        {
            originalBitmap = previousBitmap;
        }
        surfaceBitmap = bitmap;
        return true;
    }

    void ReleaseBitmapSurface(
        HDC& surfaceDeviceContext,
        HBITMAP& surfaceBitmap,
        HGDIOBJ& originalBitmap
    ) noexcept
    {
        if (surfaceDeviceContext != nullptr && originalBitmap != nullptr)
        {
            SelectObject(surfaceDeviceContext, originalBitmap);
        }
        if (surfaceBitmap != nullptr)
        {
            DeleteObject(surfaceBitmap);
            surfaceBitmap = nullptr;
        }
        if (surfaceDeviceContext != nullptr)
        {
            DeleteDC(surfaceDeviceContext);
            surfaceDeviceContext = nullptr;
        }
        originalBitmap = nullptr;
    }
}

AudioEditorWindow::~AudioEditorWindow()
{
    Shutdown();
}

bool AudioEditorWindow::Show(
    const HINSTANCE instance,
    const HWND owner,
    const AppTheme theme,
    std::string previewDevice,
    const float previewVolume,
    const std::optional<std::filesystem::path>& initialFile
)
{
    theme_ = theme;
    SetPreviewRoute(std::move(previewDevice), previewVolume);

    if (!EnsureWindow(instance, owner, false))
    {
        return false;
    }

    ApplyTheme();
    RefreshLocalizedText();

    if (initialFile.has_value() &&
        (!document_.has_value() || loadedFile_ != *initialFile))
    {
        LoadFile(*initialFile);
    }

    ShowWindow(window_, SW_SHOW);
    ShowWindow(window_, SW_RESTORE);
    SetForegroundWindow(window_);
    return true;
}

bool AudioEditorWindow::ShowEmbedded(
    const HINSTANCE instance,
    const HWND parent,
    const AppTheme theme,
    std::string previewDevice,
    const float previewVolume,
    const std::optional<std::filesystem::path>& initialFile
)
{
    theme_ = theme;
    SetPreviewRoute(std::move(previewDevice), previewVolume);

    if (!EnsureWindow(instance, parent, true))
    {
        return false;
    }

    ApplyTheme();
    RefreshLocalizedText();

    if (initialFile.has_value() &&
        (!document_.has_value() || loadedFile_ != *initialFile))
    {
        LoadFile(*initialFile);
    }

    ShowWindow(window_, SW_SHOWNA);
    return true;
}

void AudioEditorWindow::SetEmbeddedBounds(
    const int x,
    const int y,
    const int width,
    const int height
)
{
    if (!embedded_ || window_ == nullptr)
    {
        return;
    }

    SetWindowPos(
        window_,
        nullptr,
        x,
        y,
        std::max(1, width),
        std::max(1, height),
        SWP_NOZORDER | SWP_NOACTIVATE
    );
}

void AudioEditorWindow::SetEmbeddedVisible(const bool visible)
{
    if (!embedded_ || window_ == nullptr)
    {
        return;
    }

    if (!visible)
    {
        StopPlayback();
    }

    ShowWindow(window_, visible ? SW_SHOWNA : SW_HIDE);
}

void AudioEditorWindow::Focus()
{
    if (window_ != nullptr && IsWindowVisible(window_) != FALSE)
    {
        SetFocus(window_);
    }
}

void AudioEditorWindow::SaveCurrent()
{
    if (!IsBusy())
    {
        SaveOverCurrent();
    }
}

void AudioEditorWindow::SaveAs()
{
    if (!IsBusy())
    {
        BrowseSaveAs();
    }
}

void AudioEditorWindow::CancelOrClear()
{
    if (IsLoadRunning())
    {
        RequestLoadCancellation();
        return;
    }
    if (IsSaveRunning())
    {
        RequestSaveCancellation();
        return;
    }

    ClearSelection();
}

void AudioEditorWindow::Shutdown()
{
    StopLoadJob(true);
    StopSaveJob(true);

    if (window_ != nullptr && IsModified())
    {
        const int choice = MessageBoxW(
            window_,
            Localization::Text(
                L"Ses editöründeki değişiklikler kaydedilsin mi?",
                L"Save the changes in the audio editor?"
            ),
            L"SoundBoardFasaFiso",
            MB_YESNO | MB_ICONWARNING
        );
        if (choice == IDYES)
        {
            SaveOverCurrent(true);
        }
    }

    if (window_ != nullptr)
    {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    if (classRegistered_ && instance_ != nullptr)
    {
        UnregisterClassW(AudioEditorWindowClassName, instance_);
    }

    ReleaseResources();
    ClearDocument();
    openButton_ = nullptr;
    saveAsButton_ = nullptr;
    overwriteButton_ = nullptr;
    closeButton_ = nullptr;
    playPauseButton_ = nullptr;
    stopButton_ = nullptr;
    undoButton_ = nullptr;
    redoButton_ = nullptr;
    cropButton_ = nullptr;
    deleteButton_ = nullptr;
    selectionStartLabel_ = nullptr;
    selectionStartEdit_ = nullptr;
    selectionEndLabel_ = nullptr;
    selectionEndEdit_ = nullptr;
    applySelectionButton_ = nullptr;
    snapZeroButton_ = nullptr;
    zoomSelectionButton_ = nullptr;
    selectAllButton_ = nullptr;
    effectScopeButton_ = nullptr;
    gainLabel_ = nullptr;
    gainEdit_ = nullptr;
    applyGainButton_ = nullptr;
    normalizeButton_ = nullptr;
    fadeInButton_ = nullptr;
    fadeOutButton_ = nullptr;
    convertMonoButton_ = nullptr;
    zoomOutButton_ = nullptr;
    zoomFitButton_ = nullptr;
    zoomInButton_ = nullptr;
    waveformScrollBar_ = nullptr;
    fileLabel_ = nullptr;
    metadataLabel_ = nullptr;
    hintLabel_ = nullptr;
    timeLabel_ = nullptr;
    statusLabel_ = nullptr;
    owner_ = nullptr;
    instance_ = nullptr;
    currentDpi_ = USER_DEFAULT_SCREEN_DPI;
    classRegistered_ = false;
    embedded_ = false;
    waveformRectangle_ = {};
    playheadFrame_ = 0U;
    viewport_.Reset(0U);
}



void AudioEditorWindow::SetPreviewRoute(
    std::string previewDevice,
    const float previewVolume
)
{
    const std::string nextKey =
        NormalizeAudioPreviewDeviceRequest(previewDevice);
    const std::string currentKey =
        NormalizeAudioPreviewDeviceRequest(previewDeviceRequest_);
    const bool deviceChanged = nextKey != currentKey;

    previewVolume_ = std::isfinite(previewVolume)
        ? std::clamp(previewVolume, 0.0f, 1.0f)
        : 1.0f;
    previewPlayer_.SetVolume(previewVolume_);

    if (!deviceChanged)
    {
        return;
    }

    StopPlayback();
    previewPlayer_.Shutdown();
    previewDeviceRequest_ = std::move(previewDevice);

    if (window_ != nullptr)
    {
        SetStatusText(Localization::Text(
            L"Önizleme çıkışı monitör ayarına güncellendi.",
            L"Preview output was updated to the monitor setting."
        ));
    }
}

void AudioEditorWindow::SetTheme(const AppTheme theme)
{
    theme_ = theme;
    ApplyTheme();
}

void AudioEditorWindow::RefreshLocalizedText()
{
    if (window_ == nullptr)
    {
        return;
    }

    SetWindowTextW(
        openButton_,
        IsLoadRunning()
            ? Localization::Text(L"İptal", L"Cancel")
            : embedded_
                ? Localization::Text(L"WAV aç", L"Open WAV")
                : Localization::Text(L"WAV aç", L"Open WAV")
    );
    SetWindowTextW(
        saveAsButton_,
        Localization::Text(L"Farklı kaydet", L"Save As")
    );
    SetWindowTextW(
        overwriteButton_,
        IsSaveRunning()
            ? Localization::Text(L"İptal", L"Cancel")
            : embedded_
                ? Localization::Text(L"Üzerine yaz", L"Overwrite")
                : Localization::Text(L"Üzerine yaz", L"Overwrite")
    );
    SetWindowTextW(closeButton_, Localization::Text(L"Kapat", L"Close"));
    SetWindowTextW(stopButton_, Localization::Text(L"Durdur", L"Stop"));
    SetWindowTextW(undoButton_, Localization::Text(L"Geri al", L"Undo"));
    SetWindowTextW(redoButton_, Localization::Text(L"Yinele", L"Redo"));
    SetWindowTextW(cropButton_, Localization::Text(L"Seçimi kırp", L"Trim to selection"));
    SetWindowTextW(deleteButton_, Localization::Text(L"Seçimi sil", L"Delete selection"));
    SetWindowTextW(
        cutButton_,
        Localization::Text(L"Kes", L"Cut")
    );
    SetWindowTextW(
        copyButton_,
        Localization::Text(L"Kopyala", L"Copy")
    );
    SetWindowTextW(
        pasteButton_,
        Localization::Text(L"Yapıştır", L"Paste")
    );
    SetWindowTextW(silenceButton_, Localization::Text(L"Sessize al", L"Silence"));
    SetWindowTextW(trimSilenceButton_, Localization::Text(L"Sessizliği kırp", L"Trim silence"));
    SetWindowTextW(selectionStartLabel_, Localization::Text(L"Başlangıç", L"Start"));
    SetWindowTextW(selectionEndLabel_, Localization::Text(L"Bitiş", L"End"));
    SetWindowTextW(applySelectionButton_, Localization::Text(L"Uygula", L"Apply"));
    SetWindowTextW(snapZeroButton_, Localization::Text(L"Sıfır geçiş", L"Zero crossing"));
    SetWindowTextW(zoomSelectionButton_, Localization::Text(L"Seçime zoom", L"Zoom selection"));
    SetWindowTextW(selectAllButton_, Localization::Text(L"Tümünü seç", L"Select all"));
    SetWindowTextW(gainLabel_, Localization::Text(L"Kazanç (dB)", L"Gain (dB)"));
    SetWindowTextW(applyGainButton_, Localization::Text(L"Kazanç uygula", L"Apply gain"));
    SetWindowTextW(normalizeButton_, Localization::Text(L"Normalize", L"Normalize"));
    SetWindowTextW(fadeInButton_, Localization::Text(L"Fade-in", L"Fade in"));
    SetWindowTextW(fadeOutButton_, Localization::Text(L"Fade-out", L"Fade out"));
    SetWindowTextW(convertMonoButton_, Localization::Text(L"Mono yap", L"Convert to mono"));
    SetWindowTextW(
        hintLabel_,
        Localization::Text(
            L"Boşluk: oynat  ·  Ctrl+X/C/V  ·  Ctrl+S",
            L"Space: play  ·  Ctrl+X/C/V  ·  Ctrl+S"
        )
    );
    SetWindowTextW(zoomOutButton_, L"−");
    SetWindowTextW(zoomFitButton_, Localization::Text(L"Sığdır", L"Fit"));
    SetWindowTextW(zoomInButton_, L"+");

    UpdateWindowTitle();
    if (document_.has_value())
    {
        UpdateMetadataText();
    }
    else
    {
        SetWindowTextW(
            fileLabel_,
            Localization::Text(
                L"Düzenlemek için bir WAV dosyası aç.",
                L"Open a WAV file to begin editing."
            )
        );
        SetWindowTextW(metadataLabel_, L"");
        SetWindowTextW(timeLabel_, L"0:00.000 / 0:00.000");
        SetStatusText(Localization::Text(
            L"WAV aç; waveform üzerinde sürükleyerek seçim yap.",
            L"Open a WAV and drag on the waveform to select audio."
        ));
    }

    if (IsLoadRunning())
    {
        SetStatusText(
            loadCancellationRequested_.load(std::memory_order_relaxed)
                ? Localization::Text(
                    L"WAV yükleme iptal ediliyor...",
                    L"Cancelling WAV load..."
                )
                : Localization::Text(
                    L"WAV arka planda yükleniyor...",
                    L"Loading WAV in the background..."
                )
        );
    }
    else if (IsSaveRunning())
    {
        SetStatusText(
            saveCancellationRequested_.load(std::memory_order_relaxed)
                ? Localization::Text(
                    L"WAV kaydetme iptal ediliyor...",
                    L"Cancelling WAV save..."
                )
                : Localization::Text(
                    L"WAV arka planda güvenli biçimde kaydediliyor...",
                    L"Saving WAV safely in the background..."
                )
        );
    }

    UpdateLoadControls();
    UpdateSaveControls();
    UpdateTransportControls();
    UpdateEditControls();
    UpdateSelectionControls();
    UpdatePlaybackTimeText();
    MarkWaveformBaseDirty();
    InvalidateRect(window_, nullptr, TRUE);
}

bool AudioEditorWindow::IsVisible() const noexcept
{
    return window_ != nullptr && IsWindowVisible(window_) != FALSE;
}

LRESULT CALLBACK AudioEditorWindow::WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam
)
{
    AudioEditorWindow* editor = reinterpret_cast<AudioEditorWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );

    if (message == WM_NCCREATE)
    {
        const auto* createData =
            reinterpret_cast<const CREATESTRUCTW*>(lParam);
        editor = static_cast<AudioEditorWindow*>(createData->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(editor)
        );
    }

    if (editor != nullptr)
    {
        return editor->HandleWindowMessage(window, message, wParam, lParam);
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT AudioEditorWindow::HandleWindowMessage(
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
            Paint();
            return 0;

        case WM_DRAWITEM:
            if (lParam != 0)
            {
                DrawButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
                return TRUE;
            }
            break;

        case WM_CTLCOLORSTATIC:
        {
            const HDC deviceContext = reinterpret_cast<HDC>(wParam);
            const HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(deviceContext, TRANSPARENT);
            const bool hintContent = embedded_ &&
                (control == hintLabel_ || control == timeLabel_);
            SetTextColor(
                deviceContext,
                control == fileLabel_ || (hintContent && control == timeLabel_)
                    ? textColor_
                    : mutedTextColor_
            );
            return reinterpret_cast<LRESULT>(
                hintContent ? panelBrush_ : backgroundBrush_
            );
        }

        case WM_CTLCOLOREDIT:
        {
            const HDC deviceContext = reinterpret_cast<HDC>(wParam);
            SetBkColor(deviceContext, panelColor_);
            SetTextColor(deviceContext, textColor_);
            return reinterpret_cast<LRESULT>(panelBrush_);
        }

        case WM_CTLCOLORBTN:
        {
            const HDC deviceContext = reinterpret_cast<HDC>(wParam);
            SetBkColor(deviceContext, backgroundColor_);
            SetTextColor(deviceContext, textColor_);
            SetBkMode(deviceContext, TRANSPARENT);
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        }

        case WM_CTLCOLORSCROLLBAR:
            return reinterpret_cast<LRESULT>(panelBrush_);

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                const WORD commandIdentifier = LOWORD(wParam);
                if (IsLoadRunning() && commandIdentifier != IdOpenFile &&
                    commandIdentifier != IdClose)
                {
                    return 0;
                }
                if (IsSaveRunning() && commandIdentifier != IdOverwrite &&
                    commandIdentifier != IdClose)
                {
                    return 0;
                }

                switch (commandIdentifier)
                {
                    case IdOpenFile:
                        if (IsLoadRunning()) RequestLoadCancellation();
                        else if (!IsSaveRunning()) BrowseForWav();
                        return 0;
                    case IdSaveAs:
                        if (!IsBusy()) BrowseSaveAs();
                        return 0;
                    case IdOverwrite:
                        if (IsSaveRunning()) RequestSaveCancellation();
                        else SaveOverCurrent();
                        return 0;
                    case IdClose:
                        StopPlayback();
                        if (IsLoadRunning()) RequestLoadCancellation();
                        if (IsSaveRunning()) RequestSaveCancellation();
                        ShowWindow(window_, SW_HIDE);
                        return 0;
                    case IdPlayPause: TogglePlayback(); return 0;
                    case IdStop: StopPlayback(); return 0;
                    case IdUndo: UndoEdit(); return 0;
                    case IdRedo: RedoEdit(); return 0;
                    case IdCrop: ApplySelectionEdit(SelectionEdit::Crop); return 0;
                    case IdDeleteSelection: ApplySelectionEdit(SelectionEdit::Delete); return 0;
                    case IdCut: CutSelection(); return 0;
                    case IdCopy: CopySelection(); return 0;
                    case IdPaste: PasteClipboard(); return 0;
                    case IdSilenceSelection: SilenceSelection(); return 0;
                    case IdTrimSilence: TrimBoundarySilence(); return 0;
                    case IdApplySelectionTimes: ApplySelectionTimes(); return 0;
                    case IdSnapZeroCrossings: SnapSelectionToZeroCrossings(); return 0;
                    case IdZoomSelection: ZoomToSelection(); return 0;
                    case IdSelectAll: SelectAllAudio(); return 0;
                    case IdEffectScope: ToggleEffectScope(); return 0;
                    case IdApplyGain: ApplyAudioEffect(AudioEffect::Gain); return 0;
                    case IdNormalize: ApplyAudioEffect(AudioEffect::Normalize); return 0;
                    case IdFadeIn: ApplyAudioEffect(AudioEffect::FadeIn); return 0;
                    case IdFadeOut: ApplyAudioEffect(AudioEffect::FadeOut); return 0;
                    case IdConvertMono: ApplyAudioEffect(AudioEffect::ConvertToMono); return 0;
                    case IdZoomOut:
                        if (document_.has_value())
                        {
                            const AudioFrameRange range = viewport_.VisibleRange();
                            ZoomAtFrame(range.beginFrame + range.FrameCount() / 2U, 0.5);
                        }
                        return 0;
                    case IdZoomFit: FitWaveform(); return 0;
                    case IdZoomIn:
                        if (document_.has_value())
                        {
                            const AudioFrameRange range = viewport_.VisibleRange();
                            ZoomAtFrame(range.beginFrame + range.FrameCount() / 2U, 2.0);
                        }
                        return 0;
                    default: break;
                }
            }
            break;

        case WM_HSCROLL:
            if (IsBusy())
            {
                return 0;
            }
            if (reinterpret_cast<HWND>(lParam) == waveformScrollBar_)
            {
                HandleHorizontalScroll(wParam);
                return 0;
            }
            break;

        case WM_MOUSEWHEEL:
            if (IsBusy()) return 0;
            HandleMouseWheel(wParam, lParam);
            return 0;

        case WM_LBUTTONDOWN:
            if (IsBusy()) return 0;
            HandleWaveformMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSEMOVE:
            if (IsBusy()) return 0;
            HandleWaveformMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
            return 0;

        case WM_LBUTTONUP:
            if (IsBusy()) return 0;
            HandleWaveformMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_LBUTTONDBLCLK:
            if (IsBusy()) return 0;
            HandleWaveformMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (selecting_)
            {
                selecting_ = false;
                selectionDragMode_ = SelectionDragMode::None;
                if (GetCapture() == window_) ReleaseCapture();
                SelectAllAudio();
            }
            return 0;

        case WM_CAPTURECHANGED:
            selecting_ = false;
            selectionDragMode_ = SelectionDragMode::None;
            return 0;

        case LoadJobCompletedMessage:
            HandleLoadJobCompleted();
            return 0;

        case SaveJobCompletedMessage:
            HandleSaveJobCompleted();
            return 0;

        case WM_TIMER:
            if (wParam == PlaybackTimerId)
            {
                HandlePlaybackTimer();
                return 0;
            }
            break;

        case WM_KEYDOWN:
        {
            if (IsBusy())
            {
                if (wParam == VK_ESCAPE)
                {
                    if (IsLoadRunning()) RequestLoadCancellation();
                    if (IsSaveRunning()) RequestSaveCancellation();
                }
                return 0;
            }

            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (control && wParam == 'O') { BrowseForWav(); return 0; }
            if (control && wParam == 'Z') { UndoEdit(); return 0; }
            if (control && wParam == 'Y') { RedoEdit(); return 0; }
            if (control && wParam == 'A') { SelectAllAudio(); return 0; }
            if (control && wParam == 'C') { CopySelection(); return 0; }
            if (control && wParam == 'X') { CutSelection(); return 0; }
            if (control && wParam == 'V') { PasteClipboard(); return 0; }
            if (control && wParam == 'L') { SilenceSelection(); return 0; }
            if (control && wParam == 'E') { ZoomToSelection(); return 0; }
            if (!control && wParam == 'Z') { SnapSelectionToZeroCrossings(); return 0; }
            if (control && wParam == 'S')
            {
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) BrowseSaveAs();
                else SaveOverCurrent();
                return 0;
            }
            if (!control && !alt && wParam == 'I')
            {
                SetSelectionBoundaryAtPlayhead(AudioSelectionBoundary::Begin);
                return 0;
            }
            if (!control && !alt && wParam == 'O')
            {
                SetSelectionBoundaryAtPlayhead(AudioSelectionBoundary::End);
                return 0;
            }
            if (wParam == VK_DELETE) { ApplySelectionEdit(SelectionEdit::Delete); return 0; }
            if (wParam == VK_ESCAPE) { ClearSelection(); return 0; }
            if (wParam == VK_SPACE) { TogglePlayback(); return 0; }
            if (wParam == 'S') { StopPlayback(); return 0; }
            if (wParam == VK_HOME) { SeekToFrame(0U); return 0; }
            if (wParam == VK_END && document_.has_value())
            {
                SeekToFrame(document_->FrameCount());
                return 0;
            }
            if ((wParam == VK_ADD || wParam == VK_OEM_PLUS) &&
                document_.has_value())
            {
                const AudioFrameRange range = viewport_.VisibleRange();
                ZoomAtFrame(
                    range.beginFrame + range.FrameCount() / 2U,
                    2.0
                );
                return 0;
            }
            if ((wParam == VK_SUBTRACT || wParam == VK_OEM_MINUS) &&
                document_.has_value())
            {
                const AudioFrameRange range = viewport_.VisibleRange();
                ZoomAtFrame(
                    range.beginFrame + range.FrameCount() / 2U,
                    0.5
                );
                return 0;
            }
            break;
        }

        case WM_SIZE:
            LayoutControls(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_DPICHANGED:
            HandleDpiChanged(HIWORD(wParam), *reinterpret_cast<const RECT*>(lParam));
            return 0;

        case WM_GETMINMAXINFO:
            if (!embedded_ && lParam != 0)
            {
                auto* minimumMaximum = reinterpret_cast<MINMAXINFO*>(lParam);
                RECT minimumRectangle{0, 0, Scale(MinimumClientWidth), Scale(MinimumClientHeight)};
                AdjustWindowRectEx(
                    &minimumRectangle,
                    AudioEditorWindowStyle,
                    FALSE,
                    0
                );
                minimumMaximum->ptMinTrackSize.x = minimumRectangle.right - minimumRectangle.left;
                minimumMaximum->ptMinTrackSize.y = minimumRectangle.bottom - minimumRectangle.top;
                return 0;
            }
            break;

        case WM_CLOSE:
            StopPlayback();
            if (IsLoadRunning()) RequestLoadCancellation();
            if (IsSaveRunning()) RequestSaveCancellation();
            if (!embedded_)
            {
                ShowWindow(window_, SW_HIDE);
            }
            return 0;

        case WM_NCDESTROY:
            KillTimer(window, PlaybackTimerId);
            StopLoadJob(true);
            StopSaveJob(true);
            previewPlayer_.Shutdown();
            if (window_ == window) window_ = nullptr;
            return DefWindowProcW(window, message, wParam, lParam);

        default:
            break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool AudioEditorWindow::EnsureWindow(
    const HINSTANCE instance,
    const HWND owner,
    const bool embedded
)
{
    if (window_ != nullptr)
    {
        if (embedded_ != embedded)
        {
            return false;
        }

        owner_ = owner;
        return true;
    }

    instance_ = instance;
    owner_ = owner;
    embedded_ = embedded;

    if (instance_ == nullptr)
    {
        return false;
    }

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

    ApplyTheme();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = &AudioEditorWindow::WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR | LR_SHARED
    ));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED
    ));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = AudioEditorWindowClassName;

    const ATOM classAtom = RegisterClassExW(&windowClass);
    if (classAtom == 0)
    {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }
    }
    else
    {
        classRegistered_ = true;
    }

    const DWORD windowStyle = embedded_
        ? EmbeddedAudioEditorWindowStyle
        : AudioEditorWindowStyle;

    RECT windowRectangle{
        0,
        0,
        Scale(InitialClientWidth),
        Scale(InitialClientHeight)
    };
    if (!embedded_)
    {
        AdjustWindowRectEx(
            &windowRectangle,
            windowStyle,
            FALSE,
            0
        );
    }

    window_ = CreateWindowExW(
        0,
        AudioEditorWindowClassName,
        L"SoundBoardFasaFiso Audio Editor",
        windowStyle,
        embedded_ ? 0 : CW_USEDEFAULT,
        embedded_ ? 0 : CW_USEDEFAULT,
        windowRectangle.right - windowRectangle.left,
        windowRectangle.bottom - windowRectangle.top,
        owner_,
        nullptr,
        instance_,
        this
    );

    if (window_ == nullptr)
    {
        return false;
    }

    if (!CreateControls())
    {
        DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    ApplyFonts();
    ApplyTheme();
    RefreshLocalizedText();

    RECT clientRectangle{};
    GetClientRect(window_, &clientRectangle);
    LayoutControls(
        clientRectangle.right - clientRectangle.left,
        clientRectangle.bottom - clientRectangle.top
    );
    return true;
}

bool AudioEditorWindow::CreateControls()
{
    const auto createControl = [this](
        const wchar_t* className,
        const wchar_t* text,
        const DWORD style,
        const int id
    )
    {
        return CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_, nullptr);
    };

    openButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdOpenFile);
    saveAsButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdSaveAs);
    overwriteButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdOverwrite);
    closeButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdClose);
    playPauseButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdPlayPause);
    stopButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdStop);
    undoButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdUndo);
    redoButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdRedo);
    cropButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdCrop);
    deleteButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdDeleteSelection);
    cutButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdCut);
    copyButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdCopy);
    pasteButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdPaste);
    silenceButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdSilenceSelection);
    trimSilenceButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdTrimSilence);
    selectionStartLabel_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    selectionStartEdit_ = createControl(
        L"EDIT",
        L"",
        WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT,
        IdSelectionStart
    );
    selectionEndLabel_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    selectionEndEdit_ = createControl(
        L"EDIT",
        L"",
        WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT,
        IdSelectionEnd
    );
    applySelectionButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdApplySelectionTimes);
    snapZeroButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdSnapZeroCrossings);
    zoomSelectionButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdZoomSelection);
    selectAllButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdSelectAll);
    effectScopeButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdEffectScope);
    gainLabel_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    gainEdit_ = createControl(
        L"EDIT",
        L"0.0",
        WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT,
        IdGainEdit
    );
    applyGainButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdApplyGain);
    normalizeButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdNormalize);
    fadeInButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdFadeIn);
    fadeOutButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdFadeOut);
    convertMonoButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdConvertMono);
    zoomOutButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdZoomOut);
    zoomFitButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdZoomFit);
    zoomInButton_ = createControl(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdZoomIn);
    waveformScrollBar_ = createControl(L"SCROLLBAR", L"", SBS_HORZ | WS_TABSTOP, IdWaveformScroll);
    fileLabel_ = createControl(L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS, 0);
    metadataLabel_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    hintLabel_ = createControl(L"STATIC", L"", SS_CENTER | SS_PATHELLIPSIS, 0);
    timeLabel_ = createControl(L"STATIC", L"", SS_RIGHT | SS_PATHELLIPSIS, 0);
    statusLabel_ = createControl(L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS, 0);

    if (selectionStartEdit_ != nullptr)
    {
        SendMessageW(selectionStartEdit_, EM_SETLIMITTEXT, 48U, 0);
    }
    if (selectionEndEdit_ != nullptr)
    {
        SendMessageW(selectionEndEdit_, EM_SETLIMITTEXT, 48U, 0);
    }
    if (gainEdit_ != nullptr)
    {
        SendMessageW(gainEdit_, EM_SETLIMITTEXT, 16U, 0);
    }

    const HWND shortcutControls[]{
        openButton_, saveAsButton_, overwriteButton_, closeButton_,
        playPauseButton_, stopButton_, undoButton_, redoButton_,
        cropButton_, deleteButton_, cutButton_, copyButton_, pasteButton_,
        silenceButton_, trimSilenceButton_, selectionStartEdit_,
        selectionEndEdit_, applySelectionButton_, snapZeroButton_,
        zoomSelectionButton_, selectAllButton_, effectScopeButton_,
        gainEdit_, applyGainButton_, normalizeButton_, fadeInButton_,
        fadeOutButton_, convertMonoButton_, zoomOutButton_, zoomFitButton_,
        zoomInButton_, waveformScrollBar_
    };

    bool shortcutsReady = true;
    for (const HWND control : shortcutControls)
    {
        if (control == nullptr || SetWindowSubclass(
                control,
                AudioEditorControlSubclassProcedure,
                1U,
                reinterpret_cast<DWORD_PTR>(window_)
            ) == FALSE)
        {
            shortcutsReady = false;
        }
    }

    return shortcutsReady &&
        openButton_ != nullptr && saveAsButton_ != nullptr &&
        overwriteButton_ != nullptr && closeButton_ != nullptr &&
        playPauseButton_ != nullptr && stopButton_ != nullptr &&
        undoButton_ != nullptr && redoButton_ != nullptr &&
        cropButton_ != nullptr && deleteButton_ != nullptr &&
        cutButton_ != nullptr && copyButton_ != nullptr &&
        pasteButton_ != nullptr && silenceButton_ != nullptr &&
        trimSilenceButton_ != nullptr &&
        selectionStartLabel_ != nullptr && selectionStartEdit_ != nullptr &&
        selectionEndLabel_ != nullptr && selectionEndEdit_ != nullptr &&
        applySelectionButton_ != nullptr && snapZeroButton_ != nullptr &&
        zoomSelectionButton_ != nullptr && selectAllButton_ != nullptr &&
        effectScopeButton_ != nullptr && gainLabel_ != nullptr &&
        gainEdit_ != nullptr && applyGainButton_ != nullptr &&
        normalizeButton_ != nullptr && fadeInButton_ != nullptr &&
        fadeOutButton_ != nullptr && convertMonoButton_ != nullptr &&
        zoomOutButton_ != nullptr && zoomFitButton_ != nullptr &&
        zoomInButton_ != nullptr && waveformScrollBar_ != nullptr &&
        fileLabel_ != nullptr && metadataLabel_ != nullptr &&
        hintLabel_ != nullptr && timeLabel_ != nullptr &&
        statusLabel_ != nullptr;
}

void AudioEditorWindow::LayoutControls(
    const int clientWidth,
    const int clientHeight
)
{
    if (window_ == nullptr) return;

    if (embedded_)
    {
        const int margin = Scale(10);
        const int gap = Scale(8);
        const int groupGap = Scale(18);
        const int buttonHeight = Scale(32);
        const int sectionHeight = Scale(15);
        const int rowGap = Scale(6);
        const int labelHeight = Scale(16);
        const int scrollBarHeight = Scale(16);
        const int statusHeight = Scale(20);

        ShowWindow(closeButton_, SW_HIDE);
        ShowWindow(hintLabel_, SW_SHOWNA);

        fileSectionTop_ = margin;
        int y = fileSectionTop_ + sectionHeight;
        int x = margin;

        const int zoomButtonWidth = Scale(44);
        const int zoomFitWidth = Scale(62);
        const int zoomClusterWidth =
            zoomButtonWidth * 2 + zoomFitWidth + gap * 2;
        const int zoomLeft = clientWidth - margin - zoomClusterWidth;

        int toolbarGap = gap;
        int toolbarGroupGap = groupGap;
        int openWidth = Scale(104);
        int saveAsWidth = Scale(132);
        int overwriteWidth = Scale(126);
        int playWidth = Scale(92);
        int stopWidth = Scale(84);
        int undoWidth = Scale(84);
        int redoWidth = Scale(84);

        const auto toolbarWidth = [&]()
        {
            return openWidth + saveAsWidth + overwriteWidth + playWidth +
                stopWidth + undoWidth + redoWidth + toolbarGap * 6 +
                toolbarGroupGap * 2;
        };
        const int availableToolbarWidth = std::max(
            1,
            zoomLeft - gap - margin
        );
        int toolbarDeficit = std::max(
            0,
            toolbarWidth() - availableToolbarWidth
        );

        const auto compressRepeatedSpacing = [](
            int& spacing,
            const int minimumSpacing,
            const int occurrenceCount,
            int& remainingDeficit
        )
        {
            if (remainingDeficit <= 0 || occurrenceCount <= 0)
            {
                return;
            }

            const int availableReduction = std::max(
                0,
                spacing - minimumSpacing
            );
            const int requestedReduction =
                (remainingDeficit + occurrenceCount - 1) / occurrenceCount;
            const int reduction = std::min(
                availableReduction,
                requestedReduction
            );
            spacing -= reduction;
            remainingDeficit = std::max(
                0,
                remainingDeficit - reduction * occurrenceCount
            );
        };

        compressRepeatedSpacing(
            toolbarGroupGap,
            Scale(8),
            2,
            toolbarDeficit
        );
        compressRepeatedSpacing(
            toolbarGap,
            Scale(6),
            6,
            toolbarDeficit
        );

        const auto shrinkToolbarButton = [this](
            int& width,
            const int minimumLogicalWidth,
            int& remainingDeficit
        )
        {
            if (remainingDeficit <= 0)
            {
                return;
            }

            const int minimumWidth = Scale(minimumLogicalWidth);
            const int reduction = std::min(
                remainingDeficit,
                std::max(0, width - minimumWidth)
            );
            width -= reduction;
            remainingDeficit -= reduction;
        };

        shrinkToolbarButton(saveAsWidth, 112, toolbarDeficit);
        shrinkToolbarButton(overwriteWidth, 108, toolbarDeficit);
        shrinkToolbarButton(openWidth, 92, toolbarDeficit);
        shrinkToolbarButton(playWidth, 82, toolbarDeficit);
        shrinkToolbarButton(undoWidth, 76, toolbarDeficit);
        shrinkToolbarButton(redoWidth, 76, toolbarDeficit);
        shrinkToolbarButton(stopWidth, 76, toolbarDeficit);

        const auto placeToolbarButton = [&](const HWND control, const int width)
        {
            MoveWindow(control, x, y, width, buttonHeight, TRUE);
            x += width + toolbarGap;
        };

        placeToolbarButton(openButton_, openWidth);
        placeToolbarButton(saveAsButton_, saveAsWidth);
        placeToolbarButton(overwriteButton_, overwriteWidth);
        x += toolbarGroupGap;
        placeToolbarButton(playPauseButton_, playWidth);
        placeToolbarButton(stopButton_, stopWidth);
        x += toolbarGroupGap;
        placeToolbarButton(undoButton_, undoWidth);
        placeToolbarButton(redoButton_, redoWidth);

        int zoomX = zoomLeft;
        MoveWindow(zoomOutButton_, zoomX, y, zoomButtonWidth, buttonHeight, TRUE);
        zoomX += zoomButtonWidth + gap;
        MoveWindow(zoomFitButton_, zoomX, y, zoomFitWidth, buttonHeight, TRUE);
        zoomX += zoomFitWidth + gap;
        MoveWindow(zoomInButton_, zoomX, y, zoomButtonWidth, buttonHeight, TRUE);
        const int informationTop = y + buttonHeight + Scale(8);
        const int hintWidth = std::min(
            Scale(360),
            std::max(Scale(280), clientWidth / 3)
        );
        const int hintLeft = clientWidth - margin - hintWidth;
        const int informationRight = std::max(
            margin + Scale(240),
            hintLeft - gap
        );
        MoveWindow(
            fileLabel_,
            margin,
            informationTop,
            std::max(1, informationRight - margin),
            labelHeight,
            TRUE
        );
        MoveWindow(
            metadataLabel_,
            margin,
            informationTop + labelHeight + Scale(2),
            std::max(1, informationRight - margin),
            labelHeight,
            TRUE
        );
        hintRectangle_ = RECT{
            hintLeft,
            informationTop,
            clientWidth - margin,
            informationTop + labelHeight * 2 + Scale(2)
        };
        const int hintContentWidth = std::max(
            1,
            static_cast<int>(hintRectangle_.right - hintRectangle_.left) - Scale(20)
        );
        MoveWindow(
            timeLabel_,
            hintRectangle_.left + Scale(10),
            hintRectangle_.top + Scale(2),
            hintContentWidth,
            labelHeight,
            TRUE
        );
        MoveWindow(
            hintLabel_,
            hintRectangle_.left + Scale(10),
            hintRectangle_.top + labelHeight + Scale(2),
            hintContentWidth,
            labelHeight,
            TRUE
        );

        editSectionTop_ = informationTop + labelHeight * 2 + rowGap;
        y = editSectionTop_ + sectionHeight;
        x = margin;
        const auto placeEditButton = [&](const HWND control, const int width)
        {
            MoveWindow(control, x, y, Scale(width), buttonHeight, TRUE);
            x += Scale(width) + gap;
        };
        placeEditButton(cutButton_, 76);
        placeEditButton(copyButton_, 92);
        placeEditButton(pasteButton_, 92);
        x += groupGap;
        placeEditButton(cropButton_, 112);
        placeEditButton(deleteButton_, 112);
        placeEditButton(silenceButton_, 104);
        placeEditButton(trimSilenceButton_, 132);

        selectionSectionTop_ = y + buttonHeight + rowGap;
        y = selectionSectionTop_ + sectionHeight;
        x = margin;
        MoveWindow(selectionStartLabel_, x, y + Scale(8), Scale(48), labelHeight, TRUE);
        x += Scale(48) + Scale(4);
        MoveWindow(selectionStartEdit_, x, y, Scale(102), buttonHeight, TRUE);
        x += Scale(102) + gap;
        MoveWindow(selectionEndLabel_, x, y + Scale(8), Scale(28), labelHeight, TRUE);
        x += Scale(28) + Scale(4);
        MoveWindow(selectionEndEdit_, x, y, Scale(102), buttonHeight, TRUE);
        x += Scale(102) + gap;
        MoveWindow(applySelectionButton_, x, y, Scale(84), buttonHeight, TRUE);
        x += Scale(84) + gap;
        MoveWindow(selectAllButton_, x, y, Scale(100), buttonHeight, TRUE);
        x += Scale(100) + gap;
        MoveWindow(snapZeroButton_, x, y, Scale(108), buttonHeight, TRUE);
        x += Scale(108) + gap;
        MoveWindow(zoomSelectionButton_, x, y, Scale(116), buttonHeight, TRUE);
        MoveWindow(
            effectScopeButton_,
            clientWidth - margin - Scale(152),
            y,
            Scale(152),
            buttonHeight,
            TRUE
        );

        effectsSectionTop_ = y + buttonHeight + rowGap;
        y = effectsSectionTop_ + sectionHeight;
        x = margin;
        MoveWindow(gainLabel_, x, y + Scale(8), Scale(50), labelHeight, TRUE);
        x += Scale(50) + Scale(4);
        MoveWindow(gainEdit_, x, y, Scale(70), buttonHeight, TRUE);
        x += Scale(70) + gap;
        MoveWindow(applyGainButton_, x, y, Scale(124), buttonHeight, TRUE);
        x += Scale(124) + gap;
        MoveWindow(normalizeButton_, x, y, Scale(108), buttonHeight, TRUE);
        x += Scale(108) + gap;
        MoveWindow(fadeInButton_, x, y, Scale(96), buttonHeight, TRUE);
        x += Scale(96) + gap;
        MoveWindow(fadeOutButton_, x, y, Scale(96), buttonHeight, TRUE);
        x += Scale(96) + gap;
        MoveWindow(convertMonoButton_, x, y, Scale(108), buttonHeight, TRUE);

        const int waveformTop = y + buttonHeight + Scale(8);
        const int statusTop = std::max(
            waveformTop,
            clientHeight - margin - statusHeight
        );
        const int scrollTop = std::max(
            waveformTop,
            statusTop - Scale(5) - scrollBarHeight
        );
        waveformRectangle_ = RECT{
            margin,
            waveformTop,
            std::max(margin + 1, clientWidth - margin),
            std::max(waveformTop + 1, scrollTop - Scale(5))
        };

        MoveWindow(
            waveformScrollBar_,
            margin + Scale(8),
            scrollTop,
            std::max(1, clientWidth - margin * 2 - Scale(16)),
            scrollBarHeight,
            TRUE
        );
        MoveWindow(
            statusLabel_,
            margin,
            statusTop,
            std::max(1, clientWidth - margin * 2),
            statusHeight,
            TRUE
        );

        UpdateWaveformScrollBar();
        MarkWaveformBaseDirty();
        InvalidateRect(window_, nullptr, TRUE);
        return;
    }

    ShowWindow(closeButton_, SW_SHOWNA);
    ShowWindow(hintLabel_, SW_HIDE);
    hintRectangle_ = RECT{};
    fileSectionTop_ = 0;
    editSectionTop_ = 0;
    selectionSectionTop_ = 0;
    effectsSectionTop_ = 0;

    const int margin = Scale(Margin);
    const int buttonHeight = Scale(ButtonHeight);
    const int buttonGap = Scale(ButtonGap);
    const int headerRowHeight = Scale(HeaderRowHeight);
    const int transportRowHeight = Scale(TransportRowHeight);
    const int editRowHeight = Scale(EditRowHeight);
    const int clipboardRowHeight = Scale(ClipboardRowHeight);
    const int selectionRowHeight = Scale(SelectionRowHeight);
    const int effectsRowHeight = Scale(EffectsRowHeight);
    const int transportButtonWidth = Scale(TransportButtonWidth);
    const int compactButtonWidth = Scale(CompactButtonWidth);
    const int scrollBarHeight = Scale(ScrollBarHeight);
    const int statusRowHeight = Scale(StatusRowHeight);

    int headerX = margin;
    MoveWindow(openButton_, headerX, margin, Scale(104), buttonHeight, TRUE);
    headerX += Scale(104) + buttonGap;
    MoveWindow(saveAsButton_, headerX, margin, Scale(132), buttonHeight, TRUE);
    headerX += Scale(132) + buttonGap;
    MoveWindow(overwriteButton_, headerX, margin, Scale(126), buttonHeight, TRUE);
    MoveWindow(closeButton_, clientWidth - margin - Scale(104), margin, Scale(104), buttonHeight, TRUE);

    MoveWindow(fileLabel_, margin, margin + buttonHeight + Scale(10),
        std::max(1, clientWidth - margin * 2), Scale(22), TRUE);
    MoveWindow(metadataLabel_, margin, margin + buttonHeight + Scale(34),
        std::max(1, clientWidth - margin * 2), Scale(18), TRUE);

    const int transportTop = margin + headerRowHeight + Scale(10);
    int transportX = margin;
    MoveWindow(playPauseButton_, transportX, transportTop, transportButtonWidth, buttonHeight, TRUE);
    transportX += transportButtonWidth + buttonGap;
    MoveWindow(stopButton_, transportX, transportTop, transportButtonWidth, buttonHeight, TRUE);

    int zoomX = clientWidth - margin - compactButtonWidth;
    MoveWindow(zoomInButton_, zoomX, transportTop, compactButtonWidth, buttonHeight, TRUE);
    zoomX -= buttonGap + transportButtonWidth;
    MoveWindow(zoomFitButton_, zoomX, transportTop, transportButtonWidth, buttonHeight, TRUE);
    zoomX -= buttonGap + compactButtonWidth;
    MoveWindow(zoomOutButton_, zoomX, transportTop, compactButtonWidth, buttonHeight, TRUE);

    const int timeX = transportX + transportButtonWidth + buttonGap;
    MoveWindow(timeLabel_, timeX, transportTop + Scale(8),
        std::max(1, zoomX - buttonGap - timeX), Scale(22), TRUE);

    const int editTop = transportTop + transportRowHeight + Scale(10);
    int editX = margin;
    MoveWindow(undoButton_, editX, editTop, Scale(90), buttonHeight, TRUE);
    editX += Scale(90) + buttonGap;
    MoveWindow(redoButton_, editX, editTop, Scale(90), buttonHeight, TRUE);
    editX += Scale(90) + Scale(24);
    MoveWindow(cropButton_, editX, editTop, Scale(118), buttonHeight, TRUE);
    editX += Scale(118) + buttonGap;
    MoveWindow(deleteButton_, editX, editTop, Scale(118), buttonHeight, TRUE);

    const int clipboardTop = editTop + editRowHeight + Scale(10);
    int clipboardX = margin;
    MoveWindow(cutButton_, clipboardX, clipboardTop, Scale(82), buttonHeight, TRUE);
    clipboardX += Scale(82) + buttonGap;
    MoveWindow(copyButton_, clipboardX, clipboardTop, Scale(98), buttonHeight, TRUE);
    clipboardX += Scale(98) + buttonGap;
    MoveWindow(pasteButton_, clipboardX, clipboardTop, Scale(98), buttonHeight, TRUE);
    clipboardX += Scale(98) + Scale(24);
    MoveWindow(silenceButton_, clipboardX, clipboardTop, Scale(108), buttonHeight, TRUE);
    clipboardX += Scale(108) + buttonGap;
    MoveWindow(trimSilenceButton_, clipboardX, clipboardTop, Scale(126), buttonHeight, TRUE);

    const int selectionTop = clipboardTop + clipboardRowHeight + Scale(10);
    const int selectionLabelWidth = Scale(64);
    const int selectionEditWidth = Scale(110);
    const int selectionButtonWidth = Scale(108);
    int selectionX = margin;
    MoveWindow(selectionStartLabel_, selectionX, selectionTop + Scale(8),
        selectionLabelWidth, Scale(22), TRUE);
    selectionX += selectionLabelWidth + Scale(4);
    MoveWindow(selectionStartEdit_, selectionX, selectionTop + Scale(2),
        selectionEditWidth, buttonHeight - Scale(4), TRUE);
    selectionX += selectionEditWidth + buttonGap;
    MoveWindow(selectionEndLabel_, selectionX, selectionTop + Scale(8),
        Scale(42), Scale(22), TRUE);
    selectionX += Scale(42) + Scale(4);
    MoveWindow(selectionEndEdit_, selectionX, selectionTop + Scale(2),
        selectionEditWidth, buttonHeight - Scale(4), TRUE);
    selectionX += selectionEditWidth + buttonGap;
    MoveWindow(applySelectionButton_, selectionX, selectionTop,
        Scale(84), buttonHeight, TRUE);
    selectionX += Scale(84) + buttonGap;
    MoveWindow(snapZeroButton_, selectionX, selectionTop,
        selectionButtonWidth, buttonHeight, TRUE);
    selectionX += selectionButtonWidth + buttonGap;
    MoveWindow(zoomSelectionButton_, selectionX, selectionTop,
        selectionButtonWidth, buttonHeight, TRUE);
    selectionX += selectionButtonWidth + buttonGap;
    MoveWindow(selectAllButton_, selectionX, selectionTop,
        Scale(96), buttonHeight, TRUE);

    const int effectsTop = selectionTop + selectionRowHeight + Scale(10);
    int effectsX = margin;
    MoveWindow(gainLabel_, effectsX, effectsTop + Scale(8),
        Scale(82), Scale(22), TRUE);
    effectsX += Scale(82) + Scale(4);
    MoveWindow(gainEdit_, effectsX, effectsTop + Scale(2),
        Scale(74), buttonHeight - Scale(4), TRUE);
    effectsX += Scale(74) + buttonGap;
    MoveWindow(applyGainButton_, effectsX, effectsTop,
        Scale(116), buttonHeight, TRUE);
    effectsX += Scale(116) + buttonGap;
    MoveWindow(normalizeButton_, effectsX, effectsTop,
        Scale(100), buttonHeight, TRUE);
    effectsX += Scale(100) + buttonGap;
    MoveWindow(fadeInButton_, effectsX, effectsTop,
        Scale(92), buttonHeight, TRUE);
    effectsX += Scale(92) + buttonGap;
    MoveWindow(fadeOutButton_, effectsX, effectsTop,
        Scale(92), buttonHeight, TRUE);
    effectsX += Scale(92) + buttonGap;
    MoveWindow(convertMonoButton_, effectsX, effectsTop,
        Scale(100), buttonHeight, TRUE);
    MoveWindow(effectScopeButton_, clientWidth - margin - Scale(158), effectsTop,
        Scale(158), buttonHeight, TRUE);

    const int waveformTop = effectsTop + effectsRowHeight + Scale(12);
    const int statusTop = std::max(waveformTop, clientHeight - margin - statusRowHeight);
    const int scrollTop = std::max(waveformTop, statusTop - Scale(8) - scrollBarHeight);
    waveformRectangle_ = RECT{margin, waveformTop, std::max(margin + 1, clientWidth - margin),
        std::max(waveformTop + 1, scrollTop - Scale(8))};

    MoveWindow(waveformScrollBar_, margin + Scale(8), scrollTop,
        std::max(1, clientWidth - margin * 2 - Scale(16)), scrollBarHeight, TRUE);
    MoveWindow(statusLabel_, margin, statusTop, std::max(1, clientWidth - margin * 2), statusRowHeight, TRUE);

    UpdateWaveformScrollBar();
    MarkWaveformBaseDirty();
    InvalidateRect(window_, nullptr, TRUE);
}

void AudioEditorWindow::HandleDpiChanged(
    const UINT dpi,
    const RECT& suggestedRectangle
)
{
    currentDpi_ = dpi == 0U ? USER_DEFAULT_SCREEN_DPI : dpi;
    ApplyFonts();
    SetWindowPos(
        window_,
        nullptr,
        suggestedRectangle.left,
        suggestedRectangle.top,
        suggestedRectangle.right - suggestedRectangle.left,
        suggestedRectangle.bottom - suggestedRectangle.top,
        SWP_NOZORDER | SWP_NOACTIVATE
    );
}

void AudioEditorWindow::BrowseForWav()
{
    std::vector<wchar_t> selectedPath(32768, L'\0');
    std::wstring initialDirectory;

    if (!loadedFile_.empty() && loadedFile_.has_parent_path())
    {
        initialDirectory = loadedFile_.parent_path().wstring();
    }

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFile = selectedPath.data();
    dialog.nMaxFile = static_cast<DWORD>(selectedPath.size());
    dialog.lpstrFilter =
        L"WAV audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0\0";
    dialog.lpstrInitialDir = initialDirectory.empty()
        ? nullptr
        : initialDirectory.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameW(&dialog) != FALSE)
    {
        LoadFile(std::filesystem::path{selectedPath.data()});
    }
}

void AudioEditorWindow::BrowseSaveAs(const bool synchronous)
{
    if (!document_.has_value() || document_->Empty() ||
        (!synchronous && IsBusy()))
    {
        return;
    }

    std::vector<wchar_t> selectedPath(32768, L'\0');
    std::wstring suggestedName = loadedFile_.empty()
        ? std::wstring{L"edited.wav"}
        : loadedFile_.filename().wstring();
    std::copy_n(
        suggestedName.c_str(),
        std::min(suggestedName.size(), selectedPath.size() - 1U),
        selectedPath.data()
    );

    std::wstring initialDirectory;
    if (!loadedFile_.empty() && loadedFile_.has_parent_path())
    {
        initialDirectory = loadedFile_.parent_path().wstring();
    }

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFile = selectedPath.data();
    dialog.nMaxFile = static_cast<DWORD>(selectedPath.size());
    dialog.lpstrFilter = L"WAV audio (*.wav)\0*.wav\0\0";
    dialog.lpstrDefExt = L"wav";
    dialog.lpstrInitialDir = initialDirectory.empty()
        ? nullptr
        : initialDirectory.c_str();
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
        OFN_EXPLORER | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&dialog) != FALSE)
    {
        SaveToFile(
            std::filesystem::path{selectedPath.data()},
            true,
            synchronous
        );
    }
}

bool AudioEditorWindow::SaveOverCurrent(const bool synchronous)
{
    if (!document_.has_value() || document_->Empty())
    {
        return false;
    }

    if (!synchronous && IsBusy())
    {
        return false;
    }

    if (loadedFile_.empty())
    {
        BrowseSaveAs(synchronous);
        return !IsModified();
    }

    return SaveToFile(loadedFile_, true, synchronous);
}

bool AudioEditorWindow::SaveToFile(
    const std::filesystem::path& filePath,
    const bool replaceExisting,
    const bool synchronous
)
{
    if (!document_.has_value() || document_->Empty())
    {
        return false;
    }

    if (synchronous)
    {
        SetStatusText(Localization::Text(
            L"WAV güvenli biçimde kaydediliyor...",
            L"Saving WAV safely..."
        ));
        const AudioWavSaveResult result = AudioDocumentWav::SavePcm16(
            *document_,
            filePath,
            replaceExisting
                ? AudioWavSaveMode::ReplaceExisting
                : AudioWavSaveMode::CreateNew
        );

        if (!result.Succeeded())
        {
            const std::wstring detail = Utf8ToWide(result.errorMessage);
            SetStatusText(Localization::Text(
                L"WAV kaydedilemedi.",
                L"The WAV could not be saved."
            ));
            MessageBoxW(
                window_,
                detail.empty()
                    ? Localization::Text(
                        L"WAV kaydedilemedi.",
                        L"The WAV could not be saved."
                    )
                    : detail.c_str(),
                L"SoundBoardFasaFiso",
                MB_OK | MB_ICONERROR
            );
            return false;
        }

        loadedFile_ = filePath;
        savedStateIdentifier_ = currentStateIdentifier_;
        UpdateWindowTitle();
        UpdateMetadataText();
        UpdateEditControls();
        SetStatusText(Localization::Text(
            L"WAV PCM16 olarak atomik biçimde kaydedildi.",
            L"WAV saved atomically as PCM16."
        ));
        return true;
    }

    if (IsBusy())
    {
        return false;
    }

    StopPlayback();
    previewPlayer_.Shutdown();
    saveCancellationRequested_.store(false, std::memory_order_relaxed);
    saveRunning_.store(true, std::memory_order_release);
    UpdateLoadControls();
    UpdateSaveControls();
    UpdateTransportControls();
    UpdateEditControls();
    UpdateWaveformScrollBar();
    SetStatusText(Localization::Text(
        L"WAV arka planda güvenli biçimde kaydediliyor...",
        L"Saving WAV safely in the background..."
    ));

    const HWND targetWindow = window_;
    const AudioDocument* const snapshot = &*document_;
    const std::uint64_t stateIdentifier = currentStateIdentifier_;
    const AudioWavSaveMode saveMode = replaceExisting
        ? AudioWavSaveMode::ReplaceExisting
        : AudioWavSaveMode::CreateNew;

    try
    {
        saveThread_ = std::jthread(
            [this, targetWindow, snapshot, filePath, stateIdentifier, saveMode]()
            {
                AudioSaveJobResult result;
                result.filePath = filePath;
                result.stateIdentifier = stateIdentifier;

                try
                {
                    const AudioWavSaveResult saveResult =
                        AudioDocumentWav::SavePcm16(
                            *snapshot,
                            filePath,
                            saveMode,
                            &saveCancellationRequested_
                        );
                    result.succeeded = saveResult.Succeeded();
                    result.cancelled =
                        saveResult.error == AudioWavFileError::Cancelled;
                    result.errorMessage = saveResult.errorMessage;
                }
                catch (const std::exception& error)
                {
                    result.errorMessage = error.what();
                }
                catch (...)
                {
                    result.errorMessage =
                        "The WAV save worker failed unexpectedly.";
                }

                {
                    const std::scoped_lock lock{saveResultMutex_};
                    pendingSaveResult_ = std::move(result);
                }

                if (PostMessageW(
                        targetWindow,
                        SaveJobCompletedMessage,
                        0,
                        0
                    ) == FALSE)
                {
                    const std::scoped_lock lock{saveResultMutex_};
                    pendingSaveResult_.reset();
                    saveRunning_.store(false, std::memory_order_release);
                    saveCancellationRequested_.store(
                        false,
                        std::memory_order_relaxed
                    );
                }
            }
        );
    }
    catch (const std::system_error& error)
    {
        saveRunning_.store(false, std::memory_order_release);
        saveCancellationRequested_.store(false, std::memory_order_relaxed);
        UpdateLoadControls();
        UpdateSaveControls();
        UpdateTransportControls();
        UpdateEditControls();
        UpdateWaveformScrollBar();

        const std::wstring detail = Utf8ToWide(error.what());
        SetStatusText(Localization::Text(
            L"WAV kaydetme iş parçacığı başlatılamadı.",
            L"The WAV save worker could not be started."
        ));
        MessageBoxW(
            window_,
            detail.empty()
                ? Localization::Text(
                    L"WAV kaydetme iş parçacığı başlatılamadı.",
                    L"The WAV save worker could not be started."
                )
                : detail.c_str(),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return false;
    }

    return true;
}

void AudioEditorWindow::RequestSaveCancellation()
{
    if (!IsSaveRunning())
    {
        return;
    }

    saveCancellationRequested_.store(true, std::memory_order_relaxed);
    if (saveThread_.joinable())
    {
        saveThread_.request_stop();
    }

    SetStatusText(Localization::Text(
        L"WAV kaydetme iptal ediliyor...",
        L"Cancelling WAV save..."
    ));
    UpdateSaveControls();
}

void AudioEditorWindow::HandleSaveJobCompleted()
{
    std::optional<AudioSaveJobResult> result;

    {
        const std::scoped_lock lock{saveResultMutex_};
        result = std::move(pendingSaveResult_);
        pendingSaveResult_.reset();
    }

    if (saveThread_.joinable())
    {
        saveThread_.join();
    }

    saveRunning_.store(false, std::memory_order_release);
    saveCancellationRequested_.store(false, std::memory_order_relaxed);
    UpdateLoadControls();
    UpdateSaveControls();
    UpdateTransportControls();
    UpdateEditControls();
    UpdateWaveformScrollBar();

    if (!result.has_value())
    {
        return;
    }

    if (result->cancelled)
    {
        SetStatusText(Localization::Text(
            L"WAV kaydetme iptal edildi.",
            L"WAV saving was cancelled."
        ));
        return;
    }

    if (!result->succeeded)
    {
        const std::wstring detail = Utf8ToWide(result->errorMessage);
        SetStatusText(Localization::Text(
            L"WAV kaydedilemedi.",
            L"The WAV could not be saved."
        ));
        MessageBoxW(
            window_,
            detail.empty()
                ? Localization::Text(
                    L"WAV kaydedilemedi.",
                    L"The WAV could not be saved."
                )
                : detail.c_str(),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return;
    }

    loadedFile_ = std::move(result->filePath);
    if (currentStateIdentifier_ == result->stateIdentifier)
    {
        savedStateIdentifier_ = result->stateIdentifier;
    }
    UpdateWindowTitle();
    UpdateMetadataText();
    UpdateEditControls();
    SetStatusText(Localization::Text(
        L"WAV PCM16 olarak atomik biçimde kaydedildi.",
        L"WAV saved atomically as PCM16."
    ));
}

void AudioEditorWindow::StopSaveJob(const bool waitForCompletion)
{
    if (!saveThread_.joinable())
    {
        if (waitForCompletion)
        {
            saveRunning_.store(false, std::memory_order_release);
            saveCancellationRequested_.store(
                false,
                std::memory_order_relaxed
            );
        }
        return;
    }

    saveCancellationRequested_.store(true, std::memory_order_relaxed);
    saveThread_.request_stop();

    if (!waitForCompletion)
    {
        return;
    }

    saveThread_.join();

    {
        const std::scoped_lock lock{saveResultMutex_};
        pendingSaveResult_.reset();
    }

    saveRunning_.store(false, std::memory_order_release);
    saveCancellationRequested_.store(false, std::memory_order_relaxed);
}

void AudioEditorWindow::UpdateSaveControls()
{
    const bool hasDocument = document_.has_value() && !document_->Empty();
    const bool saveRunning = IsSaveRunning();

    if (saveAsButton_ != nullptr)
    {
        EnableWindow(
            saveAsButton_,
            hasDocument && !IsBusy() ? TRUE : FALSE
        );
    }

    if (overwriteButton_ != nullptr)
    {
        SetWindowTextW(
            overwriteButton_,
            saveRunning
                ? Localization::Text(L"İptal", L"Cancel")
                : Localization::Text(L"Üzerine yaz", L"Overwrite")
        );
        EnableWindow(
            overwriteButton_,
            saveRunning ||
                (hasDocument && !IsBusy() && IsModified())
                ? TRUE
                : FALSE
        );
        InvalidateRect(overwriteButton_, nullptr, TRUE);
    }
}

bool AudioEditorWindow::ConfirmDiscardChanges()
{
    if (!IsModified())
    {
        return true;
    }

    SetStatusText(Localization::Text(
        L"Kaydedilmemiş değişiklik var. Önce Ctrl+S ile kaydet veya Geri al ile temizle.",
        L"There are unsaved changes. Save with Ctrl+S or clear them with Undo first."
    ));
    return false;
}

bool AudioEditorWindow::LoadFile(
    const std::filesystem::path& filePath
)
{
    if (IsBusy())
    {
        return false;
    }

    if (document_.has_value() && !ConfirmDiscardChanges())
    {
        return false;
    }

    StopPlayback();
    previewPlayer_.Shutdown();
    loadCancellationRequested_.store(false, std::memory_order_relaxed);
    loadRunning_.store(true, std::memory_order_release);
    UpdateLoadControls();
    UpdateTransportControls();
    UpdateEditControls();
    UpdateWaveformScrollBar();
    SetStatusText(Localization::Text(
        L"WAV arka planda yükleniyor...",
        L"Loading WAV in the background..."
    ));

    const HWND targetWindow = window_;

    try
    {
        loadThread_ = std::jthread(
            [this, targetWindow, filePath]()
            {
                AudioLoadJobResult result;
                result.filePath = filePath;

                try
                {
                    AudioWavLoadResult loadResult = AudioDocumentWav::Load(
                        filePath,
                        &loadCancellationRequested_
                    );

                    if (loadResult.error == AudioWavFileError::Cancelled ||
                        loadCancellationRequested_.load(
                            std::memory_order_relaxed
                        ))
                    {
                        result.cancelled = true;
                    }
                    else if (!loadResult.Succeeded() ||
                        !loadResult.document.has_value())
                    {
                        result.errorMessage = std::move(
                            loadResult.errorMessage
                        );
                    }
                    else
                    {
                        result.document = std::move(*loadResult.document);
                        std::string cacheError;
                        result.waveformCache = AudioWaveformCache::Build(
                            *result.document,
                            cacheError,
                            &loadCancellationRequested_
                        );

                        if (loadCancellationRequested_.load(
                                std::memory_order_relaxed
                            ))
                        {
                            result.cancelled = true;
                            result.document.reset();
                            result.waveformCache.reset();
                        }
                        else if (!result.waveformCache.has_value())
                        {
                            result.errorMessage = std::move(cacheError);
                            result.document.reset();
                        }
                    }
                }
                catch (const std::exception& error)
                {
                    result.errorMessage = error.what();
                }
                catch (...)
                {
                    result.errorMessage =
                        "The WAV load worker failed unexpectedly.";
                }

                {
                    const std::scoped_lock lock{loadResultMutex_};
                    pendingLoadResult_ = std::move(result);
                }

                if (PostMessageW(
                        targetWindow,
                        LoadJobCompletedMessage,
                        0,
                        0
                    ) == FALSE)
                {
                    const std::scoped_lock lock{loadResultMutex_};
                    pendingLoadResult_.reset();
                    loadRunning_.store(false, std::memory_order_release);
                    loadCancellationRequested_.store(
                        false,
                        std::memory_order_relaxed
                    );
                }
            }
        );
    }
    catch (const std::system_error& error)
    {
        loadRunning_.store(false, std::memory_order_release);
        loadCancellationRequested_.store(false, std::memory_order_relaxed);
        UpdateLoadControls();
        UpdateTransportControls();
        UpdateEditControls();
        UpdateWaveformScrollBar();

        const std::wstring detail = Utf8ToWide(error.what());
        SetStatusText(Localization::Text(
            L"WAV yükleme iş parçacığı başlatılamadı.",
            L"The WAV load worker could not be started."
        ));
        MessageBoxW(
            window_,
            detail.empty()
                ? Localization::Text(
                    L"WAV yükleme iş parçacığı başlatılamadı.",
                    L"The WAV load worker could not be started."
                )
                : detail.c_str(),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return false;
    }

    return true;
}

void AudioEditorWindow::RequestLoadCancellation()
{
    if (!IsLoadRunning())
    {
        return;
    }

    loadCancellationRequested_.store(true, std::memory_order_relaxed);
    if (loadThread_.joinable())
    {
        loadThread_.request_stop();
    }

    SetStatusText(Localization::Text(
        L"WAV yükleme iptal ediliyor...",
        L"Cancelling WAV load..."
    ));
    UpdateLoadControls();
}

void AudioEditorWindow::HandleLoadJobCompleted()
{
    std::optional<AudioLoadJobResult> result;

    {
        const std::scoped_lock lock{loadResultMutex_};
        result = std::move(pendingLoadResult_);
        pendingLoadResult_.reset();
    }

    const bool cancellationRequested =
        loadCancellationRequested_.load(std::memory_order_relaxed);

    if (loadThread_.joinable())
    {
        loadThread_.join();
    }

    loadRunning_.store(false, std::memory_order_release);
    loadCancellationRequested_.store(false, std::memory_order_relaxed);
    UpdateLoadControls();
    UpdateTransportControls();
    UpdateEditControls();
    UpdateWaveformScrollBar();

    if (!result.has_value())
    {
        return;
    }

    if (result->cancelled || cancellationRequested)
    {
        SetStatusText(Localization::Text(
            L"WAV yükleme iptal edildi.",
            L"WAV loading was cancelled."
        ));
        return;
    }

    if (!result->document.has_value() ||
        !result->waveformCache.has_value())
    {
        const std::wstring detail = Utf8ToWide(result->errorMessage);
        SetStatusText(Localization::Text(
            L"WAV açılamadı.",
            L"The WAV file could not be opened."
        ));
        MessageBoxW(
            window_,
            detail.empty()
                ? Localization::Text(
                    L"WAV dosyası açılamadı.",
                    L"The WAV file could not be opened."
                )
                : detail.c_str(),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return;
    }

    loadedFile_ = std::move(result->filePath);
    document_ = std::move(result->document);
    waveformCache_ = std::move(result->waveformCache);
    editHistory_.Clear();
    currentStateIdentifier_ = nextStateIdentifier_++;
    savedStateIdentifier_ = currentStateIdentifier_;
    selection_.reset();
    effectScope_ = AudioEffectScope::Selection;
    viewport_.Reset(document_->FrameCount());
    playheadFrame_ = 0U;
    UpdateWindowTitle();
    UpdateMetadataText();
    UpdateTransportControls();
    UpdateEditControls();
    UpdatePlaybackTimeText();
    UpdateWaveformScrollBar();
    SetStatusText(Localization::Text(
        L"WAV yüklendi. Seçim yap; kenarları sürükleyerek hassaslaştır.",
        L"WAV loaded. Select audio and drag the edges for precision."
    ));
    MarkWaveformBaseDirty();
    InvalidateRect(window_, nullptr, TRUE);
}

void AudioEditorWindow::StopLoadJob(const bool waitForCompletion)
{
    if (!loadThread_.joinable())
    {
        if (waitForCompletion)
        {
            loadRunning_.store(false, std::memory_order_release);
            loadCancellationRequested_.store(
                false,
                std::memory_order_relaxed
            );
        }
        return;
    }

    loadCancellationRequested_.store(true, std::memory_order_relaxed);
    loadThread_.request_stop();

    if (!waitForCompletion)
    {
        return;
    }

    loadThread_.join();

    {
        const std::scoped_lock lock{loadResultMutex_};
        pendingLoadResult_.reset();
    }

    loadRunning_.store(false, std::memory_order_release);
    loadCancellationRequested_.store(false, std::memory_order_relaxed);
}

void AudioEditorWindow::UpdateLoadControls()
{
    if (openButton_ == nullptr)
    {
        return;
    }

    SetWindowTextW(
        openButton_,
        IsLoadRunning()
            ? Localization::Text(L"İptal", L"Cancel")
            : Localization::Text(L"WAV aç", L"Open WAV")
    );
    EnableWindow(
        openButton_,
        IsLoadRunning() || !IsSaveRunning() ? TRUE : FALSE
    );
    InvalidateRect(openButton_, nullptr, TRUE);
}

void AudioEditorWindow::ClearDocument()
{
    if (window_ != nullptr) KillTimer(window_, PlaybackTimerId);
    previewPlayer_.Shutdown();
    waveformCache_.reset();
    document_.reset();
    loadedFile_.clear();
    editHistory_.Clear();
    selection_.reset();
    selecting_ = false;
    selectionDragged_ = false;
    selectionDragMode_ = SelectionDragMode::None;
    updatingSelectionFields_ = false;
    effectScope_ = AudioEffectScope::Selection;
    viewport_.Reset(0U);
    playheadFrame_ = 0U;
    currentStateIdentifier_ = 0U;
    savedStateIdentifier_ = 0U;
    MarkWaveformBaseDirty();
}

void AudioEditorWindow::Paint()
{
    if (window_ == nullptr)
    {
        return;
    }

    PAINTSTRUCT paint{};
    const HDC deviceContext = BeginPaint(window_, &paint);
    RECT clientRectangle{};
    GetClientRect(window_, &clientRectangle);
    FillRect(deviceContext, &clientRectangle, backgroundBrush_);

    if (embedded_)
    {
        if (hintRectangle_.right > hintRectangle_.left &&
            hintRectangle_.bottom > hintRectangle_.top)
        {
            FillRoundedRectangle(
                deviceContext,
                hintRectangle_,
                BlendColor(panelColor_, accentColor_, 10),
                Scale(10)
            );
            DrawRoundedBorder(
                deviceContext,
                hintRectangle_,
                borderColor_,
                Scale(10)
            );
        }

        DrawSectionHeader(
            deviceContext,
            fileSectionTop_,
            Localization::Text(L"DOSYA VE OYNATMA", L"FILE AND PLAYBACK")
        );
        DrawSectionHeader(
            deviceContext,
            editSectionTop_,
            Localization::Text(L"DÜZENLEME", L"EDITING")
        );
        DrawSectionHeader(
            deviceContext,
            selectionSectionTop_,
            Localization::Text(L"SEÇİM", L"SELECTION")
        );
        DrawSectionHeader(
            deviceContext,
            effectsSectionTop_,
            Localization::Text(L"EFEKTLER", L"EFFECTS")
        );
    }

    if (waveformRectangle_.right > waveformRectangle_.left &&
        waveformRectangle_.bottom > waveformRectangle_.top)
    {
        RECT intersection{};
        if (IntersectRect(
                &intersection,
                &paint.rcPaint,
                &waveformRectangle_
            ) != FALSE)
        {
            const int width = waveformRectangle_.right - waveformRectangle_.left;
            const int height = waveformRectangle_.bottom - waveformRectangle_.top;

            if (EnsureWaveformBuffers(deviceContext, width, height) &&
                RebuildWaveformBase())
            {
                const int sourceX = intersection.left - waveformRectangle_.left;
                const int sourceY = intersection.top - waveformRectangle_.top;
                const int dirtyWidth = intersection.right - intersection.left;
                const int dirtyHeight = intersection.bottom - intersection.top;
                BitBlt(
                    waveformBufferDeviceContext_,
                    sourceX,
                    sourceY,
                    dirtyWidth,
                    dirtyHeight,
                    waveformBaseDeviceContext_,
                    sourceX,
                    sourceY,
                    SRCCOPY
                );

                const int savedDeviceContext = SaveDC(
                    waveformBufferDeviceContext_
                );
                POINT previousOrigin{};
                SetViewportOrgEx(
                    waveformBufferDeviceContext_,
                    -waveformRectangle_.left,
                    -waveformRectangle_.top,
                    &previousOrigin
                );
                if (savedDeviceContext != 0)
                {
                    IntersectClipRect(
                        waveformBufferDeviceContext_,
                        intersection.left,
                        intersection.top,
                        intersection.right,
                        intersection.bottom
                    );
                }
                DrawWaveformOverlays(
                    waveformBufferDeviceContext_,
                    WaveformInnerRectangle()
                );
                if (savedDeviceContext != 0)
                {
                    RestoreDC(
                        waveformBufferDeviceContext_,
                        savedDeviceContext
                    );
                }
                else
                {
                    SetViewportOrgEx(
                        waveformBufferDeviceContext_,
                        previousOrigin.x,
                        previousOrigin.y,
                        nullptr
                    );
                }

                BitBlt(
                    deviceContext,
                    intersection.left,
                    intersection.top,
                    dirtyWidth,
                    dirtyHeight,
                    waveformBufferDeviceContext_,
                    sourceX,
                    sourceY,
                    SRCCOPY
                );
            }
            else
            {
                FillRoundedRectangle(
                    deviceContext,
                    waveformRectangle_,
                    panelColor_,
                    Scale(PanelRadius)
                );
                DrawRoundedBorder(
                    deviceContext,
                    waveformRectangle_,
                    borderColor_,
                    Scale(PanelRadius)
                );
                DrawWaveformBase(deviceContext, WaveformInnerRectangle());
                DrawWaveformOverlays(deviceContext, WaveformInnerRectangle());
            }
        }
    }

    EndPaint(window_, &paint);
}

bool AudioEditorWindow::EnsureWaveformBuffers(
    const HDC referenceDeviceContext,
    const int width,
    const int height
)
{
    if (referenceDeviceContext == nullptr || width <= 0 || height <= 0)
    {
        return false;
    }

    if (waveformBaseDeviceContext_ != nullptr &&
        waveformBaseBitmap_ != nullptr &&
        waveformBufferDeviceContext_ != nullptr &&
        waveformBufferBitmap_ != nullptr &&
        waveformBufferWidth_ == width && waveformBufferHeight_ == height)
    {
        return true;
    }

    ReleaseWaveformBuffers();
    if (!EnsureBitmapSurface(
            referenceDeviceContext,
            width,
            height,
            waveformBaseDeviceContext_,
            waveformBaseBitmap_,
            waveformBaseOriginalBitmap_
        ) ||
        !EnsureBitmapSurface(
            referenceDeviceContext,
            width,
            height,
            waveformBufferDeviceContext_,
            waveformBufferBitmap_,
            waveformBufferOriginalBitmap_
        ))
    {
        ReleaseWaveformBuffers();
        return false;
    }

    waveformBufferWidth_ = width;
    waveformBufferHeight_ = height;
    waveformBaseDirty_ = true;
    return true;
}

bool AudioEditorWindow::RebuildWaveformBase()
{
    if (!waveformBaseDirty_)
    {
        return true;
    }
    if (waveformBaseDeviceContext_ == nullptr ||
        waveformBufferWidth_ <= 0 || waveformBufferHeight_ <= 0)
    {
        return false;
    }

    const RECT localRectangle{
        0,
        0,
        waveformBufferWidth_,
        waveformBufferHeight_
    };
    FillRect(
        waveformBaseDeviceContext_,
        &localRectangle,
        backgroundBrush_
    );

    POINT previousOrigin{};
    SetViewportOrgEx(
        waveformBaseDeviceContext_,
        -waveformRectangle_.left,
        -waveformRectangle_.top,
        &previousOrigin
    );
    FillRoundedRectangle(
        waveformBaseDeviceContext_,
        waveformRectangle_,
        panelColor_,
        Scale(PanelRadius)
    );
    DrawRoundedBorder(
        waveformBaseDeviceContext_,
        waveformRectangle_,
        borderColor_,
        Scale(PanelRadius)
    );
    DrawWaveformBase(
        waveformBaseDeviceContext_,
        WaveformInnerRectangle()
    );
    SetViewportOrgEx(
        waveformBaseDeviceContext_,
        previousOrigin.x,
        previousOrigin.y,
        nullptr
    );

    waveformBaseDirty_ = false;
    return true;
}

void AudioEditorWindow::ReleaseWaveformBuffers() noexcept
{
    ReleaseBitmapSurface(
        waveformBaseDeviceContext_,
        waveformBaseBitmap_,
        waveformBaseOriginalBitmap_
    );
    ReleaseBitmapSurface(
        waveformBufferDeviceContext_,
        waveformBufferBitmap_,
        waveformBufferOriginalBitmap_
    );
    waveformBufferWidth_ = 0;
    waveformBufferHeight_ = 0;
    waveformBaseDirty_ = true;
}

void AudioEditorWindow::MarkWaveformBaseDirty() noexcept
{
    waveformBaseDirty_ = true;
}

void AudioEditorWindow::InvalidatePlayheadTransition(
    const std::size_t previousFrame,
    const std::size_t currentFrame,
    const bool viewportChanged
)
{
    if (window_ == nullptr)
    {
        return;
    }

    if (viewportChanged || !document_.has_value())
    {
        if (viewportChanged)
        {
            MarkWaveformBaseDirty();
        }
        InvalidateRect(window_, &waveformRectangle_, FALSE);
        return;
    }

    const RECT inner = WaveformInnerRectangle();
    const int width = inner.right - inner.left;
    if (width <= 0)
    {
        return;
    }

    const AudioFrameRange visibleRange = viewport_.VisibleRange();
    const auto invalidateFrame = [this, &inner, width, visibleRange](
        const std::size_t frame
    )
    {
        if (visibleRange.IsEmpty() || frame < visibleRange.beginFrame ||
            frame > visibleRange.endFrame)
        {
            return;
        }

        const int x = inner.left + viewport_.PixelForFrame(frame, width);
        const int padding = std::max(Scale(4), 3);
        RECT dirty{
            x - padding,
            waveformRectangle_.top,
            x + padding + 1,
            waveformRectangle_.bottom
        };
        IntersectRect(&dirty, &dirty, &waveformRectangle_);
        if (dirty.right > dirty.left && dirty.bottom > dirty.top)
        {
            InvalidateRect(window_, &dirty, FALSE);
        }
    };

    invalidateFrame(previousFrame);
    invalidateFrame(currentFrame);
}

void AudioEditorWindow::InvalidateSelectionTransition(
    const std::optional<AudioFrameRange>& previousSelection,
    const std::optional<AudioFrameRange>& currentSelection
)
{
    if (window_ == nullptr || !document_.has_value())
    {
        return;
    }

    const RECT inner = WaveformInnerRectangle();
    const int width = inner.right - inner.left;
    if (width <= 0)
    {
        return;
    }

    const AudioFrameRange visibleRange = viewport_.VisibleRange();
    const auto visibleSelectionRectangle = [this, &inner, width, visibleRange](
        const std::optional<AudioFrameRange>& selection
    ) -> std::optional<RECT>
    {
        if (!selection.has_value() || selection->IsEmpty() ||
            selection->endFrame <= visibleRange.beginFrame ||
            selection->beginFrame >= visibleRange.endFrame)
        {
            return std::nullopt;
        }

        const std::size_t beginFrame = std::max(
            selection->beginFrame,
            visibleRange.beginFrame
        );
        const std::size_t endFrame = std::min(
            selection->endFrame,
            visibleRange.endFrame
        );
        const int left = inner.left + viewport_.PixelForFrame(
            beginFrame,
            width
        );
        const LONG right = std::min(
            inner.right,
            static_cast<LONG>(
                inner.left + viewport_.PixelForFrame(endFrame, width) + 1
            )
        );
        return RECT{
            left,
            waveformRectangle_.top,
            right,
            waveformRectangle_.bottom
        };
    };

    const std::optional<RECT> previousRectangle =
        visibleSelectionRectangle(previousSelection);
    const std::optional<RECT> currentRectangle =
        visibleSelectionRectangle(currentSelection);
    if (!previousRectangle.has_value() && !currentRectangle.has_value())
    {
        return;
    }

    if (previousRectangle.has_value() && currentRectangle.has_value() &&
        EqualRect(&*previousRectangle, &*currentRectangle) != FALSE)
    {
        return;
    }

    RECT dirtyRectangle{};
    if (previousRectangle.has_value() && currentRectangle.has_value())
    {
        UnionRect(
            &dirtyRectangle,
            &*previousRectangle,
            &*currentRectangle
        );
    }
    else
    {
        dirtyRectangle = previousRectangle.has_value()
            ? *previousRectangle
            : *currentRectangle;
    }

    const int padding = std::max(Scale(5), 4);
    dirtyRectangle.left -= padding;
    dirtyRectangle.right += padding;
    IntersectRect(
        &dirtyRectangle,
        &dirtyRectangle,
        &waveformRectangle_
    );
    if (dirtyRectangle.right > dirtyRectangle.left &&
        dirtyRectangle.bottom > dirtyRectangle.top)
    {
        InvalidateRect(window_, &dirtyRectangle, FALSE);
    }
}

void AudioEditorWindow::DrawWaveformBase(
    const HDC deviceContext,
    const RECT& rectangle
)
{
    if (!document_.has_value() || !waveformCache_.has_value() ||
        document_->Empty())
    {
        SetBkMode(deviceContext, TRANSPARENT);
        SetTextColor(deviceContext, mutedTextColor_);
        const HGDIOBJ oldFont = SelectObject(deviceContext, bodyFont_);
        RECT textRectangle = rectangle;
        DrawTextW(
            deviceContext,
            Localization::Text(
                L"WAV açıldığında waveform burada gösterilecek.",
                L"The waveform will appear here after opening a WAV file."
            ),
            -1,
            &textRectangle,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );
        SelectObject(deviceContext, oldFont);
        return;
    }

    const int availableWidth = rectangle.right - rectangle.left;
    const int availableHeight = rectangle.bottom - rectangle.top;
    if (availableWidth <= 0 || availableHeight <= 0)
    {
        return;
    }

    const std::size_t requestedColumns = std::min<std::size_t>(
        static_cast<std::size_t>(availableWidth),
        AudioWaveformCache::MaximumViewColumns
    );
    std::string viewError;
    const std::optional<AudioWaveformView> view =
        waveformCache_->CreateView(
            *document_,
            viewport_.VisibleRange(),
            requestedColumns,
            viewError
        );

    if (!view.has_value())
    {
        SetBkMode(deviceContext, TRANSPARENT);
        SetTextColor(deviceContext, mutedTextColor_);
        const std::wstring text = Utf8ToWide(viewError);
        RECT textRectangle = rectangle;
        DrawTextW(
            deviceContext,
            text.empty() ? L"Waveform unavailable." : text.c_str(),
            -1,
            &textRectangle,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );
        return;
    }

    const std::uint32_t channelCount = view->ChannelCount();
    if (channelCount == 0U)
    {
        return;
    }

    HPEN waveformPen = CreatePen(PS_SOLID, 1, waveformColor_);
    HPEN centerPen = CreatePen(PS_SOLID, 1, centerLineColor_);
    const HGDIOBJ oldPen = SelectObject(deviceContext, centerPen);

    const int channelGap = Scale(8);
    const int totalGap = channelGap *
        static_cast<int>(channelCount > 0U ? channelCount - 1U : 0U);
    const int channelHeight = std::max(
        1,
        (availableHeight - totalGap) / static_cast<int>(channelCount)
    );

    for (std::uint32_t channel = 0U; channel < channelCount; ++channel)
    {
        const int channelTop = rectangle.top +
            static_cast<int>(channel) * (channelHeight + channelGap);
        const int channelBottom = std::min(
            static_cast<int>(rectangle.bottom),
            channelTop + channelHeight
        );
        const int centerY = channelTop + (channelBottom - channelTop) / 2;

        MoveToEx(deviceContext, rectangle.left, centerY, nullptr);
        LineTo(deviceContext, rectangle.right, centerY);

        SelectObject(deviceContext, waveformPen);
        const std::span<const AudioWaveformPeak> peaks =
            view->PeaksForChannel(channel);
        const int halfHeight = std::max(
            1,
            (channelBottom - channelTop - Scale(4)) / 2
        );

        for (std::size_t column = 0U; column < peaks.size(); ++column)
        {
            const float minimum = std::clamp(
                peaks[column].minimum,
                -1.0f,
                1.0f
            );
            const float maximum = std::clamp(
                peaks[column].maximum,
                -1.0f,
                1.0f
            );
            const int xBegin = rectangle.left + static_cast<int>(
                column * static_cast<std::size_t>(availableWidth) /
                peaks.size()
            );
            const int xEnd = rectangle.left + static_cast<int>(
                (column + 1U) * static_cast<std::size_t>(availableWidth) /
                peaks.size()
            );
            const int yTop = centerY - static_cast<int>(
                std::lround(maximum * static_cast<float>(halfHeight))
            );
            const int yBottom = centerY - static_cast<int>(
                std::lround(minimum * static_cast<float>(halfHeight))
            );

            for (int x = xBegin; x < std::max(xBegin + 1, xEnd); ++x)
            {
                MoveToEx(deviceContext, x, yTop, nullptr);
                LineTo(deviceContext, x, yBottom + 1);
            }
        }

        SelectObject(deviceContext, centerPen);
    }

    SelectObject(deviceContext, oldPen);
    DeleteObject(centerPen);
    DeleteObject(waveformPen);
}

void AudioEditorWindow::DrawWaveformOverlays(
    const HDC deviceContext,
    const RECT& rectangle
)
{
    if (!document_.has_value() || document_->Empty())
    {
        return;
    }

    const int availableWidth = rectangle.right - rectangle.left;
    const int availableHeight = rectangle.bottom - rectangle.top;
    if (availableWidth <= 0 || availableHeight <= 0)
    {
        return;
    }

    const AudioFrameRange visibleRange = viewport_.VisibleRange();
    if (selection_.has_value() && !selection_->IsEmpty() &&
        selection_->endFrame > visibleRange.beginFrame &&
        selection_->beginFrame < visibleRange.endFrame)
    {
        const std::size_t beginFrame = std::max(
            selection_->beginFrame,
            visibleRange.beginFrame
        );
        const std::size_t endFrame = std::min(
            selection_->endFrame,
            visibleRange.endFrame
        );
        RECT selectionRectangle = rectangle;
        selectionRectangle.left += viewport_.PixelForFrame(
            beginFrame,
            availableWidth
        );
        selectionRectangle.right = std::min(
            rectangle.right,
            static_cast<LONG>(
                rectangle.left +
                    viewport_.PixelForFrame(endFrame, availableWidth) + 1
            )
        );

        HPEN shadePen = CreatePen(PS_SOLID, 1, selectionColor_);
        const HGDIOBJ previousPen = SelectObject(deviceContext, shadePen);
        const int shadeSpacing = std::max(2, Scale(3));
        int firstShadeX = selectionRectangle.left;
        const int shadeOffset =
            (firstShadeX - rectangle.left) % shadeSpacing;
        if (shadeOffset != 0)
        {
            firstShadeX += shadeSpacing - shadeOffset;
        }
        for (int x = firstShadeX;
             x < selectionRectangle.right;
             x += shadeSpacing)
        {
            MoveToEx(deviceContext, x, rectangle.top, nullptr);
            LineTo(deviceContext, x, rectangle.bottom);
        }
        SelectObject(deviceContext, previousPen);
        DeleteObject(shadePen);

        HPEN selectionPen = CreatePen(PS_SOLID, 1, selectionBorderColor_);
        const HGDIOBJ previousBorderPen = SelectObject(
            deviceContext,
            selectionPen
        );
        MoveToEx(
            deviceContext,
            selectionRectangle.left,
            rectangle.top,
            nullptr
        );
        LineTo(deviceContext, selectionRectangle.left, rectangle.bottom);
        MoveToEx(
            deviceContext,
            selectionRectangle.right - 1,
            rectangle.top,
            nullptr
        );
        LineTo(deviceContext, selectionRectangle.right - 1, rectangle.bottom);
        SelectObject(deviceContext, previousBorderPen);
        DeleteObject(selectionPen);

        HBRUSH handleBrush = CreateSolidBrush(selectionBorderColor_);
        const int handleHalfWidth = Scale(3);
        const int handleHeight = Scale(9);
        RECT beginHandle{
            selectionRectangle.left - handleHalfWidth,
            rectangle.top,
            selectionRectangle.left + handleHalfWidth + 1,
            std::min(rectangle.bottom, rectangle.top + handleHeight)
        };
        RECT endHandle{
            selectionRectangle.right - 1 - handleHalfWidth,
            rectangle.top,
            selectionRectangle.right + handleHalfWidth,
            std::min(rectangle.bottom, rectangle.top + handleHeight)
        };
        FillRect(deviceContext, &beginHandle, handleBrush);
        FillRect(deviceContext, &endHandle, handleBrush);
        DeleteObject(handleBrush);
    }

    const std::size_t playheadFrame = CurrentPlayheadFrame();
    if (!visibleRange.IsEmpty() && playheadFrame >= visibleRange.beginFrame &&
        playheadFrame <= visibleRange.endFrame)
    {
        HPEN playheadPen = CreatePen(PS_SOLID, Scale(2), playheadColor_);
        const HGDIOBJ previousPen = SelectObject(deviceContext, playheadPen);
        const int playheadX = rectangle.left + viewport_.PixelForFrame(
            playheadFrame,
            availableWidth
        );
        MoveToEx(deviceContext, playheadX, rectangle.top, nullptr);
        LineTo(deviceContext, playheadX, rectangle.bottom);
        SelectObject(deviceContext, previousPen);
        DeleteObject(playheadPen);
    }
}

void AudioEditorWindow::DrawButton(
    const DRAWITEMSTRUCT& item
) const
{
    FillRect(item.hDC, &item.rcItem, backgroundBrush_);

    RECT rectangle = item.rcItem;
    rectangle.right -= 1;
    rectangle.bottom -= 1;

    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;
    const bool primaryButton = IsPrimaryButton(item.hwndItem);
    const bool dangerButton = IsDangerButton(item.hwndItem);

    COLORREF fillColor = inputColor_;
    COLORREF buttonTextColor = textColor_;

    if (dangerButton)
    {
        fillColor = hot
            ? BlendColor(dangerColor_, RGB(255, 255, 255), 10)
            : dangerColor_;
        buttonTextColor = RGB(255, 255, 255);
    }
    else if (primaryButton)
    {
        fillColor = hot ? accentHoverColor_ : accentColor_;
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

    FillRoundedRectangle(
        item.hDC,
        rectangle,
        fillColor,
        Scale(10)
    );
    DrawRoundedBorder(
        item.hDC,
        rectangle,
        focused ? accentColor_ : borderColor_,
        Scale(10),
        focused ? 2 : 1
    );

    wchar_t textBuffer[128]{};
    GetWindowTextW(item.hwndItem, textBuffer, static_cast<int>(std::size(textBuffer)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, buttonTextColor);
    const HGDIOBJ oldFont = SelectObject(item.hDC, buttonFont_);

    RECT textRectangle = rectangle;
    textRectangle.left += Scale(10);
    textRectangle.right -= Scale(10);
    UINT textFlags = DT_CENTER | DT_VCENTER | DT_SINGLELINE |
        DT_END_ELLIPSIS | DT_NOPREFIX;

    if (HasButtonGlyph(item.hwndItem))
    {
        const int glyphSize = Scale(13);
        const int glyphGap = Scale(6);
        const int availableWidth = std::max(
            0,
            static_cast<int>(textRectangle.right - textRectangle.left)
        );
        SIZE textSize{};
        const int textLength = static_cast<int>(wcslen(textBuffer));
        GetTextExtentPoint32W(
            item.hDC,
            textBuffer,
            textLength,
            &textSize
        );

        const int requestedWidth = glyphSize + glyphGap + textSize.cx;
        const bool contentFits = requestedWidth <= availableWidth;
        const int contentLeft = textRectangle.left + (
            contentFits
                ? std::max(0, (availableWidth - requestedWidth) / 2)
                : 0
        );
        const int glyphTop = rectangle.top + std::max(
            0,
            (static_cast<int>(rectangle.bottom - rectangle.top) - glyphSize) /
                2
        );
        const RECT glyphRectangle{
            contentLeft,
            glyphTop,
            contentLeft + glyphSize,
            glyphTop + glyphSize
        };
        DrawButtonGlyph(
            item.hDC,
            item.hwndItem,
            glyphRectangle,
            buttonTextColor,
            fillColor
        );

        textRectangle.left = glyphRectangle.right + glyphGap;
        textFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE |
            DT_END_ELLIPSIS | DT_NOPREFIX;
    }

    DrawTextW(
        item.hDC,
        textBuffer,
        -1,
        &textRectangle,
        textFlags
    );
    SelectObject(item.hDC, oldFont);
}

void AudioEditorWindow::DrawButtonGlyph(
    const HDC deviceContext,
    const HWND button,
    const RECT& rectangle,
    const COLORREF color,
    const COLORREF backgroundColor
) const
{
    const int size = static_cast<int>(rectangle.right - rectangle.left);
    const int left = rectangle.left;
    const int top = rectangle.top;
    const int right = rectangle.right;
    const int bottom = rectangle.bottom;
    const int strokeWidth = std::max(1, Scale(2));
    HPEN pen = CreatePen(PS_SOLID, strokeWidth, color);
    HBRUSH brush = CreateSolidBrush(color);
    HBRUSH backgroundBrush = CreateSolidBrush(backgroundColor);
    const HGDIOBJ oldPen = SelectObject(deviceContext, pen);
    const HGDIOBJ oldBrush = SelectObject(deviceContext, brush);

    const bool cancelButton =
        (button == openButton_ && IsLoadRunning()) ||
        (button == overwriteButton_ && IsSaveRunning());

    if (cancelButton)
    {
        MoveToEx(deviceContext, left + Scale(2), top + Scale(2), nullptr);
        LineTo(deviceContext, right - Scale(2), bottom - Scale(2));
        MoveToEx(deviceContext, right - Scale(2), top + Scale(2), nullptr);
        LineTo(deviceContext, left + Scale(2), bottom - Scale(2));
    }
    else if (button == openButton_)
    {
        POINT points[]{
            {left, top + Scale(4)},
            {left + Scale(5), top + Scale(4)},
            {left + Scale(7), top + Scale(2)},
            {right, top + Scale(2)},
            {right, bottom},
            {left, bottom}
        };
        Polygon(deviceContext, points, static_cast<int>(std::size(points)));
    }
    else if (button == saveAsButton_ || button == overwriteButton_)
    {
        Rectangle(deviceContext, left, top, right, bottom);
        RECT slot{left + Scale(3), top, right - Scale(3), top + Scale(5)};
        FillRect(deviceContext, &slot, backgroundBrush);
        RECT label{left + Scale(3), bottom - Scale(5), right - Scale(3), bottom - Scale(2)};
        FillRect(deviceContext, &label, backgroundBrush);
    }
    else if (button == playPauseButton_)
    {
        if (previewPlayer_.State() == AudioPreviewState::Playing)
        {
            Rectangle(
                deviceContext,
                left + Scale(2),
                top + Scale(1),
                left + Scale(6),
                bottom - Scale(1)
            );
            Rectangle(
                deviceContext,
                right - Scale(6),
                top + Scale(1),
                right - Scale(2),
                bottom - Scale(1)
            );
        }
        else
        {
            POINT triangle[]{
                {left + Scale(2), top + Scale(1)},
                {right - Scale(1), top + size / 2},
                {left + Scale(2), bottom - Scale(1)}
            };
            Polygon(deviceContext, triangle, static_cast<int>(std::size(triangle)));
        }
    }
    else if (button == stopButton_)
    {
        Rectangle(
            deviceContext,
            left + Scale(2),
            top + Scale(2),
            right - Scale(2),
            bottom - Scale(2)
        );
    }
    else if (button == undoButton_ || button == redoButton_)
    {
        const bool redo = button == redoButton_;
        const int direction = redo ? 1 : -1;
        const int centerX = left + size / 2;
        POINT curve[]{
            {centerX - direction * Scale(5), top + Scale(4)},
            {centerX, top + Scale(2)},
            {centerX + direction * Scale(5), top + Scale(5)},
            {centerX + direction * Scale(5), bottom - Scale(3)},
            {centerX, bottom - Scale(1)}
        };
        Polyline(deviceContext, curve, static_cast<int>(std::size(curve)));
        POINT arrow[]{
            {centerX - direction * Scale(5), top + Scale(4)},
            {centerX - direction * Scale(1), top + Scale(1)},
            {centerX - direction * Scale(1), top + Scale(7)}
        };
        Polygon(deviceContext, arrow, static_cast<int>(std::size(arrow)));
    }
    else if (button == cutButton_)
    {
        Ellipse(
            deviceContext,
            left,
            bottom - Scale(6),
            left + Scale(6),
            bottom
        );
        Ellipse(
            deviceContext,
            right - Scale(6),
            bottom - Scale(6),
            right,
            bottom
        );
        MoveToEx(deviceContext, left + Scale(3), bottom - Scale(6), nullptr);
        LineTo(deviceContext, right - Scale(2), top + Scale(1));
        MoveToEx(deviceContext, right - Scale(3), bottom - Scale(6), nullptr);
        LineTo(deviceContext, left + Scale(2), top + Scale(1));
    }
    else if (button == copyButton_)
    {
        Rectangle(
            deviceContext,
            left + Scale(3),
            top,
            right,
            bottom - Scale(3)
        );
        Rectangle(
            deviceContext,
            left,
            top + Scale(3),
            right - Scale(3),
            bottom
        );
    }
    else if (button == pasteButton_)
    {
        Rectangle(
            deviceContext,
            left + Scale(1),
            top + Scale(3),
            right - Scale(1),
            bottom
        );
        Rectangle(
            deviceContext,
            left + Scale(4),
            top,
            right - Scale(4),
            top + Scale(5)
        );
    }
    else if (button == cropButton_)
    {
        MoveToEx(deviceContext, left + Scale(2), top, nullptr);
        LineTo(deviceContext, left + Scale(2), bottom);
        MoveToEx(deviceContext, right - Scale(2), top, nullptr);
        LineTo(deviceContext, right - Scale(2), bottom);
        MoveToEx(deviceContext, left + Scale(2), top + size / 2, nullptr);
        LineTo(deviceContext, left + Scale(6), top + size / 2);
        MoveToEx(deviceContext, right - Scale(2), top + size / 2, nullptr);
        LineTo(deviceContext, right - Scale(6), top + size / 2);
    }
    else if (button == deleteButton_)
    {
        MoveToEx(deviceContext, left + Scale(2), top + Scale(2), nullptr);
        LineTo(deviceContext, right - Scale(2), bottom - Scale(2));
        MoveToEx(deviceContext, right - Scale(2), top + Scale(2), nullptr);
        LineTo(deviceContext, left + Scale(2), bottom - Scale(2));
    }
    else if (button == silenceButton_)
    {
        POINT speaker[]{
            {left, top + Scale(5)},
            {left + Scale(4), top + Scale(5)},
            {left + Scale(8), top + Scale(1)},
            {left + Scale(8), bottom - Scale(1)},
            {left + Scale(4), bottom - Scale(5)},
            {left, bottom - Scale(5)}
        };
        Polygon(deviceContext, speaker, static_cast<int>(std::size(speaker)));
        MoveToEx(deviceContext, right - Scale(5), top + Scale(4), nullptr);
        LineTo(deviceContext, right, bottom - Scale(4));
        MoveToEx(deviceContext, right, top + Scale(4), nullptr);
        LineTo(deviceContext, right - Scale(5), bottom - Scale(4));
    }
    else if (button == trimSilenceButton_)
    {
        MoveToEx(deviceContext, left + Scale(1), top, nullptr);
        LineTo(deviceContext, left + Scale(1), bottom);
        MoveToEx(deviceContext, right - Scale(1), top, nullptr);
        LineTo(deviceContext, right - Scale(1), bottom);
        MoveToEx(deviceContext, left + Scale(3), top + size / 2, nullptr);
        LineTo(deviceContext, left + Scale(7), top + size / 2);
        MoveToEx(deviceContext, right - Scale(3), top + size / 2, nullptr);
        LineTo(deviceContext, right - Scale(7), top + size / 2);
    }

    SelectObject(deviceContext, oldBrush);
    SelectObject(deviceContext, oldPen);
    DeleteObject(backgroundBrush);
    DeleteObject(brush);
    DeleteObject(pen);
}

bool AudioEditorWindow::HasButtonGlyph(const HWND button) const
{
    return button == openButton_ || button == saveAsButton_ ||
        button == overwriteButton_ || button == playPauseButton_ ||
        button == stopButton_ || button == undoButton_ ||
        button == redoButton_ || button == cutButton_ ||
        button == copyButton_ || button == pasteButton_ ||
        button == cropButton_ || button == deleteButton_ ||
        button == silenceButton_ || button == trimSilenceButton_;
}

bool AudioEditorWindow::IsPrimaryButton(const HWND control) const
{
    return control == openButton_ ||
        control == saveAsButton_ ||
        control == playPauseButton_ ||
        control == applySelectionButton_ ||
        control == zoomSelectionButton_ ||
        control == applyGainButton_ ||
        control == normalizeButton_;
}

bool AudioEditorWindow::IsDangerButton(const HWND control) const
{
    return control == overwriteButton_ ||
        control == deleteButton_ ||
        (control == openButton_ && IsLoadRunning()) ||
        (control == overwriteButton_ && IsSaveRunning());
}

void AudioEditorWindow::DrawSectionHeader(
    const HDC deviceContext,
    const int top,
    const wchar_t* text
) const
{
    if (top <= 0 || text == nullptr || *text == L'\0')
    {
        return;
    }

    RECT clientRectangle{};
    GetClientRect(window_, &clientRectangle);
    const int margin = Scale(8);
    const HGDIOBJ oldFont = SelectObject(deviceContext, bodyFont_);
    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(deviceContext, mutedTextColor_);

    SIZE textSize{};
    const int textLength = static_cast<int>(wcslen(text));
    GetTextExtentPoint32W(deviceContext, text, textLength, &textSize);
    RECT textRectangle{
        margin,
        top,
        margin + textSize.cx + Scale(2),
        top + Scale(14)
    };
    DrawTextW(
        deviceContext,
        text,
        textLength,
        &textRectangle,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
    );

    const int lineY = top + Scale(7);
    const int lineLeft = textRectangle.right + Scale(10);
    if (lineLeft < clientRectangle.right - margin)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, borderColor_);
        const HGDIOBJ oldPen = SelectObject(deviceContext, pen);
        MoveToEx(deviceContext, lineLeft, lineY, nullptr);
        LineTo(deviceContext, clientRectangle.right - margin, lineY);
        SelectObject(deviceContext, oldPen);
        DeleteObject(pen);
    }

    SelectObject(deviceContext, oldFont);
}

void AudioEditorWindow::ApplyTheme()
{
    if (theme_ == AppTheme::Dark)
    {
        backgroundColor_ = RGB(15, 17, 23);
        panelColor_ = RGB(24, 28, 36);
        inputColor_ = RGB(17, 21, 29);
        textColor_ = RGB(244, 246, 250);
        mutedTextColor_ = RGB(154, 164, 178);
        borderColor_ = RGB(42, 49, 64);
        accentColor_ = RGB(124, 92, 255);
        accentHoverColor_ = RGB(139, 108, 255);
        dangerColor_ = RGB(224, 82, 82);
        waveformColor_ = RGB(139, 108, 255);
        centerLineColor_ = RGB(70, 78, 96);
        playheadColor_ = RGB(255, 93, 115);
        selectionColor_ = RGB(54, 47, 86);
        selectionBorderColor_ = RGB(177, 155, 255);
    }
    else
    {
        backgroundColor_ = RGB(244, 246, 250);
        panelColor_ = RGB(255, 255, 255);
        inputColor_ = RGB(249, 250, 252);
        textColor_ = RGB(27, 31, 40);
        mutedTextColor_ = RGB(104, 115, 134);
        borderColor_ = RGB(221, 226, 234);
        accentColor_ = RGB(93, 70, 215);
        accentHoverColor_ = RGB(108, 85, 229);
        dangerColor_ = RGB(201, 64, 64);
        waveformColor_ = RGB(98, 70, 230);
        centerLineColor_ = RGB(207, 214, 225);
        playheadColor_ = RGB(220, 55, 82);
        selectionColor_ = RGB(226, 218, 255);
        selectionBorderColor_ = RGB(98, 70, 230);
    }

    if (backgroundBrush_ != nullptr)
    {
        DeleteObject(backgroundBrush_);
    }
    if (panelBrush_ != nullptr)
    {
        DeleteObject(panelBrush_);
    }

    backgroundBrush_ = CreateSolidBrush(backgroundColor_);
    panelBrush_ = CreateSolidBrush(panelColor_);

    const wchar_t* explorerTheme = theme_ == AppTheme::Dark
        ? L"DarkMode_Explorer"
        : L"Explorer";
    const HWND nativeControls[]{
        selectionStartEdit_,
        selectionEndEdit_,
        gainEdit_,
        waveformScrollBar_
    };
    for (const HWND control : nativeControls)
    {
        if (control != nullptr)
        {
            SetWindowTheme(control, explorerTheme, nullptr);
            RedrawWindow(
                control,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_FRAME
            );
        }
    }

    if (window_ != nullptr)
    {
        const BOOL useDarkMode = theme_ == AppTheme::Dark;
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
        MarkWaveformBaseDirty();
        RedrawWindow(
            window_,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN
        );
    }
}

void AudioEditorWindow::ApplyFonts()
{
    if (bodyFont_ != nullptr)
    {
        DeleteObject(bodyFont_);
        bodyFont_ = nullptr;
    }
    if (buttonFont_ != nullptr)
    {
        DeleteObject(buttonFont_);
        buttonFont_ = nullptr;
    }

    const int bodyFontSize = embedded_ ? 14 : 15;
    const int buttonFontSize = embedded_ ? 13 : 15;

    bodyFont_ = CreateFontW(
        -Scale(bodyFontSize), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    buttonFont_ = CreateFontW(
        -Scale(buttonFontSize), 0, 0, 0, 600, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    const HWND labels[]{
        fileLabel_,
        metadataLabel_,
        hintLabel_,
        timeLabel_,
        statusLabel_,
        selectionStartLabel_,
        selectionEndLabel_,
        selectionStartEdit_,
        selectionEndEdit_,
        gainLabel_,
        gainEdit_
    };
    for (const HWND label : labels)
    {
        if (label != nullptr)
        {
            SendMessageW(
                label,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(bodyFont_),
                TRUE
            );
        }
    }

    const HWND buttons[]{
        openButton_,
        saveAsButton_,
        overwriteButton_,
        closeButton_,
        playPauseButton_,
        stopButton_,
        undoButton_,
        redoButton_,
        cropButton_,
        deleteButton_,
        cutButton_,
        copyButton_,
        pasteButton_,
        silenceButton_,
        trimSilenceButton_,
        applySelectionButton_,
        snapZeroButton_,
        zoomSelectionButton_,
        selectAllButton_,
        effectScopeButton_,
        applyGainButton_,
        normalizeButton_,
        fadeInButton_,
        fadeOutButton_,
        convertMonoButton_,
        zoomOutButton_,
        zoomFitButton_,
        zoomInButton_
    };
    for (const HWND button : buttons)
    {
        if (button != nullptr)
        {
            SendMessageW(
                button,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(buttonFont_),
                TRUE
            );
        }
    }
}

void AudioEditorWindow::ReleaseResources()
{
    ReleaseWaveformBuffers();
    if (backgroundBrush_ != nullptr)
    {
        DeleteObject(backgroundBrush_);
        backgroundBrush_ = nullptr;
    }
    if (panelBrush_ != nullptr)
    {
        DeleteObject(panelBrush_);
        panelBrush_ = nullptr;
    }
    if (bodyFont_ != nullptr)
    {
        DeleteObject(bodyFont_);
        bodyFont_ = nullptr;
    }
    if (buttonFont_ != nullptr)
    {
        DeleteObject(buttonFont_);
        buttonFont_ = nullptr;
    }
}

void AudioEditorWindow::UpdateMetadataText()
{
    if (!document_.has_value()) return;

    std::wstring fileText = loadedFile_.filename().wstring();
    if (IsModified()) fileText.append(L" *");
    SetWindowTextW(fileLabel_, fileText.c_str());

    std::wostringstream metadata;
    metadata << document_->SampleRate() << L" Hz · "
        << document_->ChannelCount() << L" "
        << Localization::Text(L"kanal", document_->ChannelCount() == 1U ? L"channel" : L"channels")
        << L" · " << FormatDuration(document_->DurationSeconds())
        << L" · " << document_->FrameCount() << L" "
        << Localization::Text(L"frame", L"frames");
    SetWindowTextW(metadataLabel_, metadata.str().c_str());
}

void AudioEditorWindow::UpdateTransportControls()
{
    const bool hasDocument = !IsBusy() &&
        document_.has_value() && !document_->Empty();
    const AudioPreviewState state = previewPlayer_.State();
    const bool playing = state == AudioPreviewState::Playing;
    const bool canStop = hasDocument &&
        (state != AudioPreviewState::Stopped || CurrentPlayheadFrame() != 0U);

    if (playPauseButton_ != nullptr)
    {
        EnableWindow(playPauseButton_, hasDocument ? TRUE : FALSE);
        SetWindowTextW(
            playPauseButton_,
            playing
                ? Localization::Text(L"Duraklat", L"Pause")
                : Localization::Text(L"Oynat", L"Play")
        );
    }
    if (stopButton_ != nullptr)
    {
        EnableWindow(stopButton_, canStop ? TRUE : FALSE);
    }

    const BOOL zoomEnabled = hasDocument ? TRUE : FALSE;
    if (zoomOutButton_ != nullptr)
    {
        EnableWindow(
            zoomOutButton_,
            hasDocument && !viewport_.IsFit() ? TRUE : FALSE
        );
    }
    if (zoomFitButton_ != nullptr)
    {
        EnableWindow(
            zoomFitButton_,
            hasDocument && !viewport_.IsFit() ? TRUE : FALSE
        );
    }
    if (zoomInButton_ != nullptr)
    {
        EnableWindow(zoomInButton_, zoomEnabled);
    }
}

void AudioEditorWindow::UpdateEditControls()
{
    UpdateSaveControls();
    const bool hasDocument = !IsBusy() &&
        document_.has_value() && !document_->Empty();
    const bool hasSelection = hasDocument && HasSelection();
    if (undoButton_ != nullptr) EnableWindow(
            undoButton_,
            hasDocument && editHistory_.CanUndo() ? TRUE : FALSE
        );
    if (redoButton_ != nullptr) EnableWindow(
            redoButton_,
            hasDocument && editHistory_.CanRedo() ? TRUE : FALSE
        );
    if (cropButton_ != nullptr) EnableWindow(cropButton_, hasSelection ? TRUE : FALSE);
    const bool canDelete = hasSelection &&
        selection_->FrameCount() < document_->FrameCount();
    if (deleteButton_ != nullptr)
    {
        EnableWindow(deleteButton_, canDelete ? TRUE : FALSE);
    }
    if (cutButton_ != nullptr)
    {
        EnableWindow(cutButton_, canDelete ? TRUE : FALSE);
    }
    if (copyButton_ != nullptr)
    {
        EnableWindow(copyButton_, hasSelection ? TRUE : FALSE);
    }
    if (pasteButton_ != nullptr)
    {
        const bool compatibleClipboard = hasDocument && clipboard_.has_value() &&
            clipboard_->SampleRate() == document_->SampleRate() &&
            clipboard_->ChannelCount() == document_->ChannelCount();
        EnableWindow(pasteButton_, compatibleClipboard ? TRUE : FALSE);
    }
    if (silenceButton_ != nullptr)
    {
        EnableWindow(silenceButton_, hasSelection ? TRUE : FALSE);
    }
    if (trimSilenceButton_ != nullptr)
    {
        EnableWindow(trimSilenceButton_, hasDocument ? TRUE : FALSE);
    }
    if (selectionStartEdit_ != nullptr) EnableWindow(selectionStartEdit_, hasDocument ? TRUE : FALSE);
    if (selectionEndEdit_ != nullptr) EnableWindow(selectionEndEdit_, hasDocument ? TRUE : FALSE);
    if (applySelectionButton_ != nullptr) EnableWindow(applySelectionButton_, hasDocument ? TRUE : FALSE);
    if (snapZeroButton_ != nullptr) EnableWindow(snapZeroButton_, hasSelection ? TRUE : FALSE);
    if (zoomSelectionButton_ != nullptr) EnableWindow(zoomSelectionButton_, hasSelection ? TRUE : FALSE);
    if (selectAllButton_ != nullptr) EnableWindow(selectAllButton_, hasDocument ? TRUE : FALSE);
    UpdateSelectionControls();
    UpdateEffectControls();
}

void AudioEditorWindow::UpdateSelectionControls(const bool force)
{
    if (selectionStartEdit_ == nullptr || selectionEndEdit_ == nullptr ||
        updatingSelectionFields_)
    {
        return;
    }

    if (!force &&
        (GetFocus() == selectionStartEdit_ || GetFocus() == selectionEndEdit_))
    {
        return;
    }

    std::size_t startFrame = 0U;
    std::size_t endFrame = 0U;
    std::uint32_t sampleRate = 1U;

    if (document_.has_value())
    {
        sampleRate = document_->SampleRate();
        if (HasSelection())
        {
            startFrame = selection_->beginFrame;
            endFrame = selection_->endFrame;
        }
        else
        {
            startFrame = CurrentPlayheadFrame();
            endFrame = startFrame;
        }
    }

    const std::wstring startText = Utf8ToWide(
        FormatAudioFrameTime(startFrame, sampleRate)
    );
    const std::wstring endText = Utf8ToWide(
        FormatAudioFrameTime(endFrame, sampleRate)
    );

    updatingSelectionFields_ = true;
    SetWindowTextW(selectionStartEdit_, startText.c_str());
    SetWindowTextW(selectionEndEdit_, endText.c_str());
    updatingSelectionFields_ = false;
}

void AudioEditorWindow::UpdateWindowTitle()
{
    if (window_ == nullptr) return;
    std::wstring title = Localization::Text(
        L"SoundBoardFasaFiso Ses Editörü",
        L"SoundBoardFasaFiso Audio Editor"
    );
    if (IsModified()) title.append(L" *");
    SetWindowTextW(window_, title.c_str());
}

void AudioEditorWindow::UpdateEffectControls()
{
    const bool hasDocument = !IsBusy() &&
        document_.has_value() && !document_->Empty();
    const bool hasSelection = hasDocument && HasSelection();
    const bool selectionScope = effectScope_ == AudioEffectScope::Selection &&
        hasSelection;

    if (effectScopeButton_ != nullptr)
    {
        SetWindowTextW(
            effectScopeButton_,
            selectionScope
                ? Localization::Text(L"Hedef: Seçim", L"Target: Selection")
                : Localization::Text(L"Hedef: Tüm ses", L"Target: Whole audio")
        );
        EnableWindow(effectScopeButton_, hasSelection ? TRUE : FALSE);
    }

    if (gainEdit_ != nullptr)
    {
        EnableWindow(gainEdit_, hasDocument ? TRUE : FALSE);
    }
    if (applyGainButton_ != nullptr)
    {
        EnableWindow(applyGainButton_, hasDocument ? TRUE : FALSE);
    }
    if (normalizeButton_ != nullptr)
    {
        EnableWindow(normalizeButton_, hasDocument ? TRUE : FALSE);
    }
    if (fadeInButton_ != nullptr)
    {
        EnableWindow(fadeInButton_, hasDocument ? TRUE : FALSE);
    }
    if (fadeOutButton_ != nullptr)
    {
        EnableWindow(fadeOutButton_, hasDocument ? TRUE : FALSE);
    }
    if (convertMonoButton_ != nullptr)
    {
        EnableWindow(
            convertMonoButton_,
            hasDocument && document_->ChannelCount() > 1U ? TRUE : FALSE
        );
    }
}

void AudioEditorWindow::UpdatePlaybackTimeText()
{
    if (timeLabel_ == nullptr)
    {
        return;
    }

    if (!document_.has_value() || document_->SampleRate() == 0U)
    {
        SetWindowTextW(timeLabel_, L"0:00.000 / 0:00.000");
        return;
    }

    const double currentSeconds = static_cast<double>(CurrentPlayheadFrame()) /
        static_cast<double>(document_->SampleRate());
    std::wstring text = FormatDuration(currentSeconds);
    text.append(L" / ");
    text.append(FormatDuration(document_->DurationSeconds()));
    SetWindowTextW(timeLabel_, text.c_str());
    UpdateSelectionControls();
}

void AudioEditorWindow::UpdateWaveformScrollBar()
{
    if (waveformScrollBar_ == nullptr)
    {
        return;
    }

    SCROLLINFO information{};
    information.cbSize = sizeof(information);
    information.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    information.nMin = 0;
    information.nMax = ScrollRangeMaximum;

    const bool scrollable = !IsBusy() &&
        document_.has_value() && !viewport_.IsFit();
    if (!scrollable)
    {
        information.nPage = static_cast<UINT>(ScrollRangeMaximum + 1);
        information.nPos = 0;
        SetScrollInfo(waveformScrollBar_, SB_CTL, &information, TRUE);
        EnableWindow(waveformScrollBar_, FALSE);
        return;
    }

    const int pageSize = std::clamp(
        static_cast<int>(std::lround(
            viewport_.VisibleRatio() *
                static_cast<double>(ScrollRangeMaximum + 1)
        )),
        1,
        ScrollRangeMaximum + 1
    );
    const int maximumPosition = std::max(
        0,
        ScrollRangeMaximum - pageSize + 1
    );
    information.nPage = static_cast<UINT>(pageSize);
    information.nPos = static_cast<int>(std::lround(
        viewport_.ScrollRatio() * static_cast<double>(maximumPosition)
    ));
    SetScrollInfo(waveformScrollBar_, SB_CTL, &information, TRUE);
    EnableWindow(waveformScrollBar_, TRUE);
}

void AudioEditorWindow::TogglePlayback()
{
    if (!document_.has_value() || document_->Empty())
    {
        return;
    }

    const std::size_t previousPlayheadFrame = CurrentPlayheadFrame();
    std::string errorMessage;
    if (!previewPlayer_.Matches(*document_, previewDeviceRequest_) &&
        !previewPlayer_.Prepare(
            *document_,
            previewDeviceRequest_,
            errorMessage
        ))
    {
        const std::wstring detail = Utf8ToWide(errorMessage);
        MessageBoxW(
            window_,
            detail.empty()
                ? Localization::Text(
                    L"Önizleme ses aygıtı açılamadı.",
                    L"The preview audio device could not be opened."
                )
                : detail.c_str(),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        SetStatusText(Localization::Text(
            L"Ses önizlemesi başlatılamadı.",
            L"Audio preview could not be started."
        ));
        return;
    }

    bool succeeded = false;
    if (previewPlayer_.State() == AudioPreviewState::Playing)
    {
        succeeded = previewPlayer_.Pause(errorMessage);
        if (succeeded)
        {
            playheadFrame_ = previewPlayer_.CurrentFrame();
            KillTimer(window_, PlaybackTimerId);
            SetStatusText(Localization::Text(
                L"Önizleme duraklatıldı.",
                L"Preview paused."
            ));
        }
    }
    else
    {
        const std::size_t startFrame = playheadFrame_ >= document_->FrameCount()
            ? 0U
            : playheadFrame_;
        succeeded = previewPlayer_.PlayFrom(startFrame, errorMessage);
        if (succeeded)
        {
            SetTimer(
                window_,
                PlaybackTimerId,
                PlaybackTimerMilliseconds,
                nullptr
            );
            SetStatusText(Localization::Text(
                L"Önizleme oynatılıyor.",
                L"Preview playing."
            ));
        }
    }

    if (!succeeded)
    {
        const std::wstring detail = Utf8ToWide(errorMessage);
        MessageBoxW(
            window_,
            detail.empty()
                ? Localization::Text(
                    L"Ses önizleme işlemi başarısız oldu.",
                    L"The audio preview operation failed."
                )
                : detail.c_str(),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
    }

    UpdateTransportControls();
    UpdatePlaybackTimeText();
    InvalidatePlayheadTransition(
        previousPlayheadFrame,
        CurrentPlayheadFrame(),
        false
    );
}

void AudioEditorWindow::StopPlayback()
{
    const std::size_t previousPlayheadFrame = CurrentPlayheadFrame();

    if (window_ != nullptr)
    {
        KillTimer(window_, PlaybackTimerId);
    }

    std::string errorMessage;
    if (previewPlayer_.IsPrepared() && !previewPlayer_.Stop(errorMessage) &&
        window_ != nullptr)
    {
        const std::wstring detail = Utf8ToWide(errorMessage);
        SetStatusText(
            detail.empty()
                ? Localization::Text(
                    L"Önizleme durdurulamadı.",
                    L"Preview could not be stopped."
                )
                : detail
        );
    }

    playheadFrame_ = 0U;
    UpdateTransportControls();
    UpdatePlaybackTimeText();
    InvalidatePlayheadTransition(
        previousPlayheadFrame,
        playheadFrame_,
        false
    );
}

void AudioEditorWindow::SeekToFrame(const std::size_t frame)
{
    if (!document_.has_value())
    {
        return;
    }

    const std::size_t previousPlayheadFrame = CurrentPlayheadFrame();
    const std::size_t clampedFrame = std::min(frame, document_->FrameCount());
    playheadFrame_ = clampedFrame;

    std::string errorMessage;
    if (previewPlayer_.Matches(*document_, previewDeviceRequest_) &&
        !previewPlayer_.Seek(clampedFrame, errorMessage))
    {
        const std::wstring detail = Utf8ToWide(errorMessage);
        SetStatusText(
            detail.empty()
                ? Localization::Text(
                    L"Önizleme konumu değiştirilemedi.",
                    L"The preview position could not be changed."
                )
                : detail
        );
    }

    UpdateTransportControls();
    UpdatePlaybackTimeText();
    InvalidatePlayheadTransition(
        previousPlayheadFrame,
        playheadFrame_,
        false
    );
}

void AudioEditorWindow::HandlePlaybackTimer()
{
    if (!document_.has_value())
    {
        KillTimer(window_, PlaybackTimerId);
        return;
    }

    const std::size_t previousPlayheadFrame = playheadFrame_;
    bool viewportChanged = false;
    playheadFrame_ = previewPlayer_.CurrentFrame();
    const AudioFrameRange visibleRange = viewport_.VisibleRange();
    if (!visibleRange.IsEmpty() && !viewport_.IsFit())
    {
        if (playheadFrame_ >= visibleRange.endFrame &&
            playheadFrame_ < document_->FrameCount())
        {
            const std::size_t leadFrames = std::max<std::size_t>(
                1U,
                visibleRange.FrameCount() / 10U
            );
            const std::size_t targetEnd = std::min(
                document_->FrameCount(),
                playheadFrame_ + leadFrames
            );
            const std::size_t desiredBegin = targetEnd > visibleRange.FrameCount()
                ? targetEnd - visibleRange.FrameCount()
                : 0U;
            const std::int64_t delta = desiredBegin >= visibleRange.beginFrame
                ? static_cast<std::int64_t>(
                    std::min<std::size_t>(
                        desiredBegin - visibleRange.beginFrame,
                        static_cast<std::size_t>(
                            std::numeric_limits<std::int64_t>::max()
                        )
                    )
                )
                : -static_cast<std::int64_t>(
                    std::min<std::size_t>(
                        visibleRange.beginFrame - desiredBegin,
                        static_cast<std::size_t>(
                            std::numeric_limits<std::int64_t>::max()
                        )
                    )
                );
            viewport_.PanFrames(delta);
            viewportChanged = true;
            UpdateWaveformScrollBar();
        }
    }

    if (previewPlayer_.State() == AudioPreviewState::Finished)
    {
        std::string errorMessage;
        previewPlayer_.FinalizeFinished(errorMessage);
        KillTimer(window_, PlaybackTimerId);
        playheadFrame_ = document_->FrameCount();
        SetStatusText(Localization::Text(
            L"Önizleme tamamlandı.",
            L"Preview finished."
        ));
    }

    UpdateTransportControls();
    UpdatePlaybackTimeText();
    InvalidatePlayheadTransition(
        previousPlayheadFrame,
        playheadFrame_,
        viewportChanged
    );
}

void AudioEditorWindow::ZoomAtFrame(
    const std::size_t anchorFrame,
    const double magnification
)
{
    if (!document_.has_value())
    {
        return;
    }

    if (viewport_.ZoomAt(anchorFrame, magnification))
    {
        UpdateWaveformScrollBar();
        UpdateTransportControls();
        MarkWaveformBaseDirty();
        InvalidateRect(window_, &waveformRectangle_, FALSE);
    }
}

void AudioEditorWindow::FitWaveform()
{
    if (!document_.has_value())
    {
        return;
    }

    viewport_.Reset(document_->FrameCount());
    UpdateWaveformScrollBar();
    UpdateTransportControls();
    MarkWaveformBaseDirty();
    InvalidateRect(window_, &waveformRectangle_, FALSE);
}

void AudioEditorWindow::HandleHorizontalScroll(const WPARAM wParam)
{
    if (!document_.has_value() || viewport_.IsFit())
    {
        return;
    }

    const AudioFrameRange visibleRange = viewport_.VisibleRange();
    const std::size_t lineFrames = std::max<std::size_t>(
        1U,
        visibleRange.FrameCount() / 20U
    );
    const std::size_t pageFrames = std::max<std::size_t>(
        1U,
        visibleRange.FrameCount() / 2U
    );
    const auto boundedDelta = [](const std::size_t value)
    {
        return static_cast<std::int64_t>(std::min<std::size_t>(
            value,
            static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max()
            )
        ));
    };

    bool changed = false;
    switch (LOWORD(wParam))
    {
        case SB_LINELEFT:
            changed = viewport_.PanFrames(-boundedDelta(lineFrames));
            break;

        case SB_LINERIGHT:
            changed = viewport_.PanFrames(boundedDelta(lineFrames));
            break;

        case SB_PAGELEFT:
            changed = viewport_.PanFrames(-boundedDelta(pageFrames));
            break;

        case SB_PAGERIGHT:
            changed = viewport_.PanFrames(boundedDelta(pageFrames));
            break;

        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
        {
            SCROLLINFO information{};
            information.cbSize = sizeof(information);
            information.fMask = SIF_TRACKPOS | SIF_PAGE | SIF_RANGE;
            GetScrollInfo(waveformScrollBar_, SB_CTL, &information);
            const int maximumPosition = std::max(
                0,
                information.nMax - static_cast<int>(information.nPage) + 1
            );
            const double ratio = maximumPosition == 0
                ? 0.0
                : static_cast<double>(information.nTrackPos) /
                    static_cast<double>(maximumPosition);
            changed = viewport_.SetScrollRatio(ratio);
            break;
        }

        case SB_LEFT:
            changed = viewport_.SetScrollRatio(0.0);
            break;

        case SB_RIGHT:
            changed = viewport_.SetScrollRatio(1.0);
            break;

        default:
            break;
    }

    if (changed)
    {
        UpdateWaveformScrollBar();
        MarkWaveformBaseDirty();
        InvalidateRect(window_, &waveformRectangle_, FALSE);
    }
}

void AudioEditorWindow::HandleMouseWheel(
    const WPARAM wParam,
    const LPARAM lParam
)
{
    if (!document_.has_value())
    {
        return;
    }

    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(window_, &point);
    const RECT inner = WaveformInnerRectangle();
    if (PtInRect(&inner, point) == FALSE)
    {
        return;
    }

    const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
    if (wheelDelta == 0)
    {
        return;
    }

    if ((GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0)
    {
        const AudioFrameRange visibleRange = viewport_.VisibleRange();
        const std::size_t step = std::max<std::size_t>(
            1U,
            visibleRange.FrameCount() / 10U
        );
        const std::int64_t boundedStep = static_cast<std::int64_t>(
            std::min<std::size_t>(
                step,
                static_cast<std::size_t>(
                    std::numeric_limits<std::int64_t>::max()
                )
            )
        );
        if (viewport_.PanFrames(wheelDelta > 0 ? -boundedStep : boundedStep))
        {
            UpdateWaveformScrollBar();
            MarkWaveformBaseDirty();
            InvalidateRect(window_, &waveformRectangle_, FALSE);
        }
        return;
    }

    const int width = inner.right - inner.left;
    const std::size_t anchorFrame = viewport_.FrameAtPixel(
        point.x - inner.left,
        width
    );
    ZoomAtFrame(anchorFrame, wheelDelta > 0 ? 1.5 : (1.0 / 1.5));
}

void AudioEditorWindow::HandleWaveformMouseDown(const int x, const int y)
{
    if (!document_.has_value())
    {
        return;
    }

    const RECT inner = WaveformInnerRectangle();
    const POINT point{x, y};
    if (PtInRect(&inner, point) == FALSE)
    {
        return;
    }

    const int width = inner.right - inner.left;
    const std::size_t clickedFrame = viewport_.FrameAtPixel(
        x - inner.left,
        width
    );
    const std::optional<AudioFrameRange> previousSelection = selection_;
    const bool extendSelection =
        (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const int edgeTolerance = Scale(7);

    selectionDragMode_ = SelectionDragMode::NewSelection;
    selectionAnchorFrame_ = clickedFrame;

    if (HasSelection())
    {
        const int beginX = inner.left + viewport_.PixelForFrame(
            selection_->beginFrame,
            width
        );
        const int endX = inner.left + viewport_.PixelForFrame(
            selection_->endFrame,
            width
        );
        const bool nearBegin = std::abs(x - beginX) <= edgeTolerance;
        const bool nearEnd = std::abs(x - endX) <= edgeTolerance;

        if (nearBegin || nearEnd || extendSelection)
        {
            const bool adjustBegin = nearBegin ||
                (!nearEnd && extendSelection &&
                 clickedFrame <= selection_->beginFrame +
                    selection_->FrameCount() / 2U);
            selectionDragMode_ = adjustBegin
                ? SelectionDragMode::AdjustBegin
                : SelectionDragMode::AdjustEnd;
            selectionAnchorFrame_ = adjustBegin
                ? selection_->endFrame
                : selection_->beginFrame;
        }
    }

    SetFocus(window_);
    SetCapture(window_);
    selecting_ = true;
    selectionDragged_ = selectionDragMode_ != SelectionDragMode::NewSelection;
    selectionAnchorX_ = x;

    if (selectionDragMode_ == SelectionDragMode::NewSelection)
    {
        selection_ = AudioFrameRange{
            clickedFrame,
            std::min(document_->FrameCount(), clickedFrame + 1U)
        };
        InvalidateSelectionTransition(previousSelection, selection_);
    }
    else
    {
        UpdateSelectionFromPoint(x);
    }

    UpdateEditControls();
}

void AudioEditorWindow::HandleWaveformMouseMove(
    const int x,
    const int,
    const WPARAM keyState
)
{
    if (!selecting_ || (keyState & MK_LBUTTON) == 0)
    {
        return;
    }

    if (std::abs(x - selectionAnchorX_) >= Scale(3))
    {
        selectionDragged_ = true;
    }
    UpdateSelectionFromPoint(x);
}

void AudioEditorWindow::HandleWaveformMouseUp(const int x, const int y)
{
    if (!selecting_)
    {
        return;
    }

    UpdateSelectionFromPoint(x);
    const SelectionDragMode completedMode = selectionDragMode_;
    selecting_ = false;
    selectionDragMode_ = SelectionDragMode::None;
    if (GetCapture() == window_)
    {
        ReleaseCapture();
    }

    if (!selectionDragged_ && completedMode == SelectionDragMode::NewSelection)
    {
        ClearSelection();
        const RECT inner = WaveformInnerRectangle();
        const POINT point{x, y};
        if (PtInRect(&inner, point) != FALSE)
        {
            SeekToFrame(viewport_.FrameAtPixel(
                x - inner.left,
                inner.right - inner.left
            ));
        }
        return;
    }

    if (selection_.has_value())
    {
        const double seconds = static_cast<double>(selection_->FrameCount()) /
            static_cast<double>(document_->SampleRate());
        std::wstring status = Localization::Text(L"Seçim: ", L"Selection: ");
        status.append(FormatDuration(seconds));
        SetStatusText(status);
    }
    UpdateEditControls();
}

void AudioEditorWindow::UpdateSelectionFromPoint(const int x)
{
    if (!document_.has_value())
    {
        return;
    }

    const RECT inner = WaveformInnerRectangle();
    const int clampedX = std::clamp(
        x,
        static_cast<int>(inner.left),
        static_cast<int>(inner.right) - 1
    );
    std::size_t frame = viewport_.FrameAtPixel(
        clampedX - inner.left,
        inner.right - inner.left
    );
    if (selectionDragMode_ != SelectionDragMode::NewSelection &&
        clampedX == inner.right - 1)
    {
        frame = viewport_.VisibleRange().endFrame;
    }

    const std::optional<AudioFrameRange> previousSelection = selection_;
    const std::size_t begin = std::min(selectionAnchorFrame_, frame);
    const std::size_t end = std::max(selectionAnchorFrame_, frame);
    const std::size_t boundedEnd = std::min(
        document_->FrameCount(),
        end + (selectionDragMode_ == SelectionDragMode::NewSelection
            ? 1U
            : 0U)
    );

    selection_ = AudioFrameRange{begin, boundedEnd};
    if (selection_->IsEmpty())
    {
        selection_.reset();
    }
    UpdateEditControls();
    InvalidateSelectionTransition(previousSelection, selection_);
}

void AudioEditorWindow::ClearSelection()
{
    const std::optional<AudioFrameRange> previousSelection = selection_;
    if (window_ != nullptr && GetCapture() == window_)
    {
        ReleaseCapture();
    }
    selection_.reset();
    selecting_ = false;
    selectionDragMode_ = SelectionDragMode::None;
    UpdateEditControls();
    InvalidateSelectionTransition(previousSelection, selection_);
}

void AudioEditorWindow::SelectAllAudio()
{
    if (!document_.has_value() || document_->Empty())
    {
        return;
    }

    const std::optional<AudioFrameRange> previousSelection = selection_;
    selection_ = AudioFrameRange{0U, document_->FrameCount()};
    playheadFrame_ = 0U;
    UpdateEditControls();
    UpdatePlaybackTimeText();
    SetStatusText(Localization::Text(
        L"Tüm ses seçildi.",
        L"All audio selected."
    ));
    InvalidateSelectionTransition(previousSelection, selection_);
}

void AudioEditorWindow::SetSelectionBoundaryAtPlayhead(
    const AudioSelectionBoundary boundary
)
{
    if (!document_.has_value() || document_->Empty())
    {
        return;
    }

    const std::size_t frame = std::min(
        previewPlayer_.State() == AudioPreviewState::Playing
            ? previewPlayer_.CurrentFrame()
            : CurrentPlayheadFrame(),
        document_->FrameCount()
    );
    const std::optional<AudioFrameRange> nextSelection =
        SetAudioSelectionBoundary(
            selection_,
            frame,
            document_->FrameCount(),
            boundary
        );

    if (!nextSelection.has_value())
    {
        SetStatusText(
            boundary == AudioSelectionBoundary::Begin
                ? Localization::Text(
                    L"Kırpma başlangıcı sesin sonuna ayarlanamaz.",
                    L"The trim start cannot be set at the end of the audio."
                )
                : Localization::Text(
                    L"Kırpma bitişi sesin başına ayarlanamaz.",
                    L"The trim end cannot be set at the start of the audio."
                )
        );
        return;
    }

    const std::optional<AudioFrameRange> previousSelection = selection_;
    selection_ = nextSelection;
    UpdateEditControls();
    UpdateSelectionControls(true);
    SetStatusText(
        boundary == AudioSelectionBoundary::Begin
            ? Localization::Text(
                L"Kırpma başlangıcı oynatma imlecine ayarlandı (I).",
                L"The trim start was set to the playhead (I)."
            )
            : Localization::Text(
                L"Kırpma bitişi oynatma imlecine ayarlandı (O).",
                L"The trim end was set to the playhead (O)."
            )
    );
    InvalidateSelectionTransition(previousSelection, selection_);
}

void AudioEditorWindow::ApplySelectionTimes()
{
    if (!document_.has_value())
    {
        return;
    }

    wchar_t startBuffer[64]{};
    wchar_t endBuffer[64]{};
    GetWindowTextW(selectionStartEdit_, startBuffer, static_cast<int>(std::size(startBuffer)));
    GetWindowTextW(selectionEndEdit_, endBuffer, static_cast<int>(std::size(endBuffer)));

    const auto start = ParseAudioFrameTime(
        WideToUtf8(startBuffer),
        document_->SampleRate(),
        document_->FrameCount()
    );
    const auto end = ParseAudioFrameTime(
        WideToUtf8(endBuffer),
        document_->SampleRate(),
        document_->FrameCount()
    );

    if (!start.has_value() || !end.has_value())
    {
        SetStatusText(Localization::Text(
            L"Zamanı saniye, dk:sn.msn veya sa:dk:sn.msn biçiminde gir.",
            L"Enter time as seconds, mm:ss.mmm, or hh:mm:ss.mmm."
        ));
        const HWND invalidEdit = !start.has_value()
            ? selectionStartEdit_
            : selectionEndEdit_;
        SetFocus(invalidEdit);
        SendMessageW(invalidEdit, EM_SETSEL, 0, -1);
        return;
    }

    if (end->frame < start->frame)
    {
        SetStatusText(Localization::Text(
            L"Bitiş zamanı başlangıçtan önce olamaz.",
            L"The end time cannot be before the start time."
        ));
        SetFocus(selectionEndEdit_);
        SendMessageW(selectionEndEdit_, EM_SETSEL, 0, -1);
        return;
    }

    const std::optional<AudioFrameRange> previousSelection = selection_;
    StopPlayback();
    playheadFrame_ = start->frame;
    if (start->frame == end->frame)
    {
        selection_.reset();
        SetStatusText(Localization::Text(
            L"Oynatma imleci ayarlandı.",
            L"The playhead was positioned."
        ));
    }
    else
    {
        selection_ = AudioFrameRange{start->frame, end->frame};
        SetStatusText(Localization::Text(
            L"Hassas seçim uygulandı.",
            L"Precise selection applied."
        ));
    }

    UpdateEditControls();
    UpdatePlaybackTimeText();
    InvalidateSelectionTransition(previousSelection, selection_);
}

void AudioEditorWindow::SnapSelectionToZeroCrossings()
{
    if (!document_.has_value() || !HasSelection())
    {
        return;
    }

    const std::size_t searchFrames = std::max<std::size_t>(
        1U,
        static_cast<std::size_t>(document_->SampleRate()) / 100U
    );
    const AudioFrameRange snapped = SnapAudioRangeToZeroCrossings(
        *document_,
        *selection_,
        searchFrames
    );

    if (snapped.IsEmpty() || !document_->IsValidRange(snapped))
    {
        return;
    }

    const std::optional<AudioFrameRange> previousSelection = selection_;
    selection_ = snapped;
    playheadFrame_ = snapped.beginFrame;
    UpdateEditControls();
    UpdatePlaybackTimeText();
    SetStatusText(Localization::Text(
        L"Seçim sınırları en yakın sıfır geçişlerine taşındı.",
        L"Selection boundaries snapped to nearby zero crossings."
    ));
    InvalidateSelectionTransition(previousSelection, selection_);
}

void AudioEditorWindow::ZoomToSelection()
{
    if (!document_.has_value() || !HasSelection())
    {
        return;
    }

    const std::size_t padding = std::max<std::size_t>(
        1U,
        selection_->FrameCount() / 20U
    );
    const AudioFrameRange padded{
        selection_->beginFrame > padding
            ? selection_->beginFrame - padding
            : 0U,
        selection_->endFrame > document_->FrameCount() -
            std::min(padding, document_->FrameCount())
            ? document_->FrameCount()
            : selection_->endFrame + padding
    };

    if (viewport_.SetVisibleRange(padded))
    {
        UpdateWaveformScrollBar();
        UpdateTransportControls();
        MarkWaveformBaseDirty();
        InvalidateRect(window_, &waveformRectangle_, FALSE);
    }
}

void AudioEditorWindow::ApplySelectionEdit(const SelectionEdit edit)
{
    if (!document_.has_value() || !HasSelection()) return;
    if (edit == SelectionEdit::Delete && selection_->FrameCount() >= document_->FrameCount())
    {
        SetStatusText(Localization::Text(
            L"Tüm ses silinemez. Bunun yerine başka bir bölüm seç.",
            L"The entire audio cannot be deleted. Select a smaller range."
        ));
        return;
    }
    if (!editHistory_.CanStore(*document_))
    {
        MessageBoxW(window_, Localization::Text(L"Bu dosya geri alma belleği sınırını aşıyor; güvenli düzenleme uygulanmadı.", L"This file exceeds the undo memory limit; no edit was applied."),
            L"SoundBoardFasaFiso", MB_OK | MB_ICONWARNING);
        return;
    }

    StopPlayback();
    std::optional<AudioDocument> before;
    try
    {
        before.emplace(*document_);
    }
    catch (const std::bad_alloc&)
    {
        MessageBoxW(window_, Localization::Text(
            L"Geri alma kopyası için yeterli bellek yok; düzenleme uygulanmadı.",
            L"There is not enough memory for the undo snapshot; no edit was applied."
        ), L"SoundBoardFasaFiso", MB_OK | MB_ICONWARNING);
        return;
    }
    const std::uint64_t beforeState = currentStateIdentifier_;
    const AudioEditResult result = edit == SelectionEdit::Crop
        ? document_->CropTo(*selection_)
        : document_->Delete(*selection_);
    if (result != AudioEditResult::Applied) return;

    std::string cacheError;
    auto cache = AudioWaveformCache::Build(*document_, cacheError);
    if (!cache.has_value())
    {
        *document_ = std::move(*before);
        waveformCache_ = AudioWaveformCache::Build(*document_, cacheError);
        MessageBoxW(window_, Localization::Text(L"Düzenleme sonrası waveform oluşturulamadı; değişiklik geri alındı.", L"The waveform could not be rebuilt; the edit was rolled back."),
            L"SoundBoardFasaFiso", MB_OK | MB_ICONERROR);
        return;
    }

    const bool historyRecorded = editHistory_.Record(
        std::move(*before),
        beforeState
    );
    currentStateIdentifier_ = nextStateIdentifier_++;
    waveformCache_ = std::move(cache);
    previewPlayer_.Shutdown();
    selection_.reset();
    playheadFrame_ = 0U;
    viewport_.Reset(document_->FrameCount());
    UpdateWindowTitle();
    UpdateMetadataText();
    UpdateTransportControls();
    UpdateEditControls();
    UpdatePlaybackTimeText();
    UpdateWaveformScrollBar();
    if (!historyRecorded)
    {
        SetStatusText(Localization::Text(
            L"Düzenleme uygulandı ancak geri alma kaydı oluşturulamadı.",
            L"The edit was applied, but an undo record could not be created."
        ));
    }
    else
    {
        SetStatusText(edit == SelectionEdit::Crop
            ? Localization::Text(
                L"Seçim dışındaki ses kırpıldı.",
                L"Audio outside the selection was cropped."
            )
            : Localization::Text(
                L"Seçili ses silindi.",
                L"Selected audio was deleted."
            ));
    }
    MarkWaveformBaseDirty();
    InvalidateRect(window_, nullptr, TRUE);
}

bool AudioEditorWindow::CopySelection()
{
    if (!document_.has_value() || !HasSelection())
    {
        return false;
    }

    std::string errorMessage;
    std::optional<AudioDocument> copied = document_->CopyRange(
        *selection_,
        errorMessage
    );
    if (!copied.has_value())
    {
        const std::wstring detail = Utf8ToWide(errorMessage);
        MessageBoxW(
            window_,
            detail.empty()
                ? Localization::Text(
                    L"Seçim kopyalanamadı.",
                    L"The selection could not be copied."
                )
                : detail.c_str(),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return false;
    }

    clipboard_ = std::move(copied);
    UpdateEditControls();
    SetStatusText(Localization::Text(
        L"Seçim editör panosuna kopyalandı.",
        L"Selection copied to the editor clipboard."
    ));
    return true;
}

void AudioEditorWindow::CutSelection()
{
    if (!document_.has_value() || !HasSelection() ||
        selection_->FrameCount() >= document_->FrameCount())
    {
        return;
    }

    if (!CopySelection())
    {
        return;
    }

    ApplySelectionEdit(SelectionEdit::Delete);
    SetStatusText(Localization::Text(
        L"Seçim kesildi ve editör panosuna kopyalandı.",
        L"Selection cut to the editor clipboard."
    ));
}

void AudioEditorWindow::PasteClipboard()
{
    if (!document_.has_value() || !clipboard_.has_value() ||
        clipboard_->Empty())
    {
        return;
    }

    if (clipboard_->SampleRate() != document_->SampleRate() ||
        clipboard_->ChannelCount() != document_->ChannelCount())
    {
        SetStatusText(Localization::Text(
            L"Panodaki sesin örnekleme hızı veya kanal sayısı açık dosyayla uyuşmuyor.",
            L"The clipboard sample rate or channel count does not match the open file."
        ));
        return;
    }

    if (!editHistory_.CanStore(*document_))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Bu dosya geri alma belleği sınırını aşıyor; yapıştırma uygulanmadı.",
                L"This file exceeds the undo memory limit; paste was not applied."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    StopPlayback();
    std::optional<AudioDocument> before;
    try
    {
        before.emplace(*document_);
    }
    catch (const std::bad_alloc&)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Geri alma kopyası için yeterli bellek yok; yapıştırma uygulanmadı.",
                L"There is not enough memory for the undo snapshot; paste was not applied."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    const std::uint64_t beforeState = currentStateIdentifier_;
    const std::size_t insertionFrame = HasSelection()
        ? selection_->beginFrame
        : std::min(CurrentPlayheadFrame(), document_->FrameCount());

    if (HasSelection())
    {
        const AudioEditResult deleteResult = document_->Delete(*selection_);
        if (deleteResult != AudioEditResult::Applied)
        {
            return;
        }
    }

    const AudioEditResult insertResult = document_->Insert(
        insertionFrame,
        *clipboard_
    );
    if (insertResult != AudioEditResult::Applied)
    {
        *document_ = std::move(*before);
        MessageBoxW(
            window_,
            Localization::Text(
                L"Ses yapıştırılamadı. Yeterli bellek olmayabilir.",
                L"The audio could not be pasted. There may not be enough memory."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return;
    }

    const AudioFrameRange pastedRange{
        insertionFrame,
        insertionFrame + clipboard_->FrameCount()
    };
    FinalizeDocumentEdit(
        std::move(*before),
        beforeState,
        pastedRange,
        pastedRange.endFrame,
        true,
        L"Ses yapıştırıldı.",
        L"Audio pasted."
    );
}

void AudioEditorWindow::SilenceSelection()
{
    if (!document_.has_value() || !HasSelection())
    {
        return;
    }

    if (!editHistory_.CanStore(*document_))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Bu dosya geri alma belleği sınırını aşıyor; sessize alma uygulanmadı.",
                L"This file exceeds the undo memory limit; silence was not applied."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    StopPlayback();
    std::optional<AudioDocument> before;
    try
    {
        before.emplace(*document_);
    }
    catch (const std::bad_alloc&)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Geri alma kopyası için yeterli bellek yok; sessize alma uygulanmadı.",
                L"There is not enough memory for the undo snapshot; silence was not applied."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    const AudioFrameRange target = *selection_;
    const std::uint64_t beforeState = currentStateIdentifier_;
    const AudioEditResult result = document_->Silence(target);
    if (result == AudioEditResult::NoChange)
    {
        SetStatusText(Localization::Text(
            L"Seçim zaten sessiz.",
            L"The selection is already silent."
        ));
        return;
    }
    if (result != AudioEditResult::Applied)
    {
        return;
    }

    FinalizeDocumentEdit(
        std::move(*before),
        beforeState,
        target,
        target.beginFrame,
        false,
        L"Seçili alan sessize alındı.",
        L"Selected audio silenced."
    );
}

void AudioEditorWindow::TrimBoundarySilence()
{
    if (!document_.has_value() || document_->Empty())
    {
        return;
    }

    const std::size_t paddingFrames = std::max<std::size_t>(
        1U,
        static_cast<std::size_t>(document_->SampleRate()) / 200U
    );
    const std::optional<AudioFrameRange> audibleRange = FindAudibleAudioRange(
        *document_,
        DefaultSilenceTrimThresholdDecibels,
        paddingFrames
    );
    if (!audibleRange.has_value())
    {
        SetStatusText(Localization::Text(
            L"Dosyada -50 dBFS eşiğinin üzerinde ses bulunamadı.",
            L"No audio above the -50 dBFS threshold was found."
        ));
        return;
    }

    if (audibleRange->beginFrame == 0U &&
        audibleRange->endFrame == document_->FrameCount())
    {
        SetStatusText(Localization::Text(
            L"Kırpılacak başlangıç veya bitiş sessizliği bulunamadı.",
            L"No leading or trailing silence was found to trim."
        ));
        return;
    }

    if (!editHistory_.CanStore(*document_))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Bu dosya geri alma belleği sınırını aşıyor; sessizlik kırpılmadı.",
                L"This file exceeds the undo memory limit; silence was not trimmed."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    StopPlayback();
    std::optional<AudioDocument> before;
    try
    {
        before.emplace(*document_);
    }
    catch (const std::bad_alloc&)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Geri alma kopyası için yeterli bellek yok; sessizlik kırpılmadı.",
                L"There is not enough memory for the undo snapshot; silence was not trimmed."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    const std::uint64_t beforeState = currentStateIdentifier_;
    if (document_->CropTo(*audibleRange) != AudioEditResult::Applied)
    {
        return;
    }

    FinalizeDocumentEdit(
        std::move(*before),
        beforeState,
        std::nullopt,
        0U,
        true,
        L"Başlangıç ve bitiş sessizliği kırpıldı (-50 dBFS, 5 ms koruma).",
        L"Leading and trailing silence trimmed (-50 dBFS, 5 ms padding)."
    );
}

bool AudioEditorWindow::FinalizeDocumentEdit(
    AudioDocument before,
    const std::uint64_t beforeState,
    std::optional<AudioFrameRange> nextSelection,
    const std::size_t nextPlayheadFrame,
    const bool fitWaveform,
    const wchar_t* const turkishStatus,
    const wchar_t* const englishStatus
)
{
    if (!document_.has_value())
    {
        return false;
    }

    std::string cacheError;
    std::optional<AudioWaveformCache> cache = AudioWaveformCache::Build(
        *document_,
        cacheError
    );
    if (!cache.has_value())
    {
        *document_ = std::move(before);
        waveformCache_ = AudioWaveformCache::Build(*document_, cacheError);
        MessageBoxW(
            window_,
            Localization::Text(
                L"Düzenleme sonrası waveform oluşturulamadı; değişiklik geri alındı.",
                L"The waveform could not be rebuilt after the edit; the change was rolled back."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return false;
    }

    const bool historyRecorded = editHistory_.Record(
        std::move(before),
        beforeState
    );
    currentStateIdentifier_ = nextStateIdentifier_++;
    waveformCache_ = std::move(cache);
    previewPlayer_.Shutdown();

    if (nextSelection.has_value() &&
        !document_->IsValidRange(*nextSelection))
    {
        nextSelection.reset();
    }
    selection_ = nextSelection;
    playheadFrame_ = std::min(nextPlayheadFrame, document_->FrameCount());
    if (fitWaveform)
    {
        viewport_.Reset(document_->FrameCount());
    }

    UpdateWindowTitle();
    UpdateMetadataText();
    UpdateTransportControls();
    UpdateEditControls();
    UpdatePlaybackTimeText();
    UpdateWaveformScrollBar();
    SetStatusText(
        historyRecorded
            ? Localization::Text(turkishStatus, englishStatus)
            : Localization::Text(
                L"Düzenleme uygulandı ancak geri alma kaydı oluşturulamadı.",
                L"The edit was applied, but an undo record could not be created."
            )
    );
    MarkWaveformBaseDirty();
    InvalidateRect(window_, nullptr, TRUE);
    return true;
}

void AudioEditorWindow::ToggleEffectScope()
{
    if (!HasSelection())
    {
        effectScope_ = AudioEffectScope::WholeDocument;
        UpdateEffectControls();
        return;
    }

    effectScope_ = effectScope_ == AudioEffectScope::Selection
        ? AudioEffectScope::WholeDocument
        : AudioEffectScope::Selection;
    UpdateEffectControls();
}

void AudioEditorWindow::ApplyAudioEffect(const AudioEffect effect)
{
    if (!document_.has_value() || document_->Empty())
    {
        return;
    }

    const std::optional<AudioFrameRange> targetRange = ResolveAudioEffectRange(
        document_->FrameCount(),
        selection_,
        effectScope_
    );
    if (!targetRange.has_value())
    {
        return;
    }

    float gainDecibels = 0.0f;
    bool gainWouldClip = false;
    if (effect == AudioEffect::Gain)
    {
        wchar_t buffer[32]{};
        GetWindowTextW(
            gainEdit_,
            buffer,
            static_cast<int>(std::size(buffer))
        );
        const std::optional<float> parsed = ParseAudioEffectGainDecibels(
            WideToUtf8(buffer)
        );
        if (!parsed.has_value())
        {
            SetStatusText(Localization::Text(
                L"Kazancı -60 ile +24 dB arasında gir. Nokta veya virgül kullanabilirsin.",
                L"Enter gain between -60 and +24 dB. You can use a dot or comma."
            ));
            SetFocus(gainEdit_);
            SendMessageW(gainEdit_, EM_SETSEL, 0, -1);
            return;
        }
        gainDecibels = *parsed;
        gainWouldClip = WouldAudioEffectGainExceedUnitPeak(
            *document_,
            *targetRange,
            gainDecibels
        );
    }

    if (!editHistory_.CanStore(*document_))
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Bu dosya geri alma belleği sınırını aşıyor; efekt uygulanmadı.",
                L"This file exceeds the undo memory limit; the effect was not applied."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    StopPlayback();
    std::optional<AudioDocument> before;
    try
    {
        before.emplace(*document_);
    }
    catch (const std::bad_alloc&)
    {
        MessageBoxW(
            window_,
            Localization::Text(
                L"Geri alma kopyası için yeterli bellek yok; efekt uygulanmadı.",
                L"There is not enough memory for the undo snapshot; the effect was not applied."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    const std::uint64_t beforeState = currentStateIdentifier_;
    AudioEditResult result = AudioEditResult::NoChange;
    switch (effect)
    {
        case AudioEffect::Gain:
            result = document_->ApplyGainDecibels(
                *targetRange,
                gainDecibels
            );
            break;
        case AudioEffect::Normalize:
            result = document_->NormalizePeak(*targetRange);
            break;
        case AudioEffect::FadeIn:
            result = document_->FadeIn(*targetRange);
            break;
        case AudioEffect::FadeOut:
            result = document_->FadeOut(*targetRange);
            break;
        case AudioEffect::ConvertToMono:
            result = document_->ConvertToMono();
            break;
    }

    if (result != AudioEditResult::Applied)
    {
        SetStatusText(
            result == AudioEditResult::NoChange
                ? Localization::Text(
                    L"Efekt ses üzerinde bir değişiklik oluşturmadı.",
                    L"The effect did not change the audio."
                )
                : Localization::Text(
                    L"Efekt uygulanamadı.",
                    L"The effect could not be applied."
                )
        );
        return;
    }

    std::string cacheError;
    std::optional<AudioWaveformCache> cache = AudioWaveformCache::Build(
        *document_,
        cacheError
    );
    if (!cache.has_value())
    {
        *document_ = std::move(*before);
        waveformCache_ = AudioWaveformCache::Build(*document_, cacheError);
        MessageBoxW(
            window_,
            Localization::Text(
                L"Efekt sonrası waveform oluşturulamadı; değişiklik geri alındı.",
                L"The waveform could not be rebuilt after the effect; the change was rolled back."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return;
    }

    const bool historyRecorded = editHistory_.Record(
        std::move(*before),
        beforeState
    );
    currentStateIdentifier_ = nextStateIdentifier_++;
    waveformCache_ = std::move(cache);
    previewPlayer_.Shutdown();
    playheadFrame_ = std::min(playheadFrame_, document_->FrameCount());

    UpdateWindowTitle();
    UpdateMetadataText();
    UpdateTransportControls();
    UpdateEditControls();
    UpdatePlaybackTimeText();
    UpdateWaveformScrollBar();

    if (!historyRecorded)
    {
        SetStatusText(Localization::Text(
            L"Efekt uygulandı ancak geri alma kaydı oluşturulamadı.",
            L"The effect was applied, but an undo record could not be created."
        ));
    }
    else
    {
        switch (effect)
        {
            case AudioEffect::Gain:
                SetStatusText(
                    gainWouldClip
                        ? Localization::Text(
                            L"Kazanç uygulandı. 0 dBFS üstü örnekler PCM16 kaydında kırpılır.",
                            L"Gain applied. Samples above 0 dBFS will clip when saved as PCM16."
                        )
                        : Localization::Text(
                            L"Kazanç uygulandı.",
                            L"Gain applied."
                        )
                );
                break;
            case AudioEffect::Normalize:
                SetStatusText(Localization::Text(
                    L"Ses tepe seviyesine göre normalize edildi.",
                    L"Audio normalized by peak level."
                ));
                break;
            case AudioEffect::FadeIn:
                SetStatusText(Localization::Text(
                    L"Fade-in uygulandı.",
                    L"Fade in applied."
                ));
                break;
            case AudioEffect::FadeOut:
                SetStatusText(Localization::Text(
                    L"Fade-out uygulandı.",
                    L"Fade out applied."
                ));
                break;
            case AudioEffect::ConvertToMono:
                SetStatusText(Localization::Text(
                    L"Ses mono biçimine dönüştürüldü.",
                    L"Audio converted to mono."
                ));
                break;
        }
    }

    MarkWaveformBaseDirty();
    InvalidateRect(window_, nullptr, TRUE);
}

void AudioEditorWindow::UndoEdit()
{
    if (!document_.has_value() || !editHistory_.CanUndo()) return;
    StopPlayback();
    if (!editHistory_.Undo(*document_, currentStateIdentifier_)) return;
    if (!RebuildAfterDocumentChange(true))
    {
        editHistory_.Redo(*document_, currentStateIdentifier_);
        RebuildAfterDocumentChange(true);
        return;
    }
    SetStatusText(Localization::Text(L"Düzenleme geri alındı.", L"Edit undone."));
}

void AudioEditorWindow::RedoEdit()
{
    if (!document_.has_value() || !editHistory_.CanRedo()) return;
    StopPlayback();
    if (!editHistory_.Redo(*document_, currentStateIdentifier_)) return;
    if (!RebuildAfterDocumentChange(true))
    {
        editHistory_.Undo(*document_, currentStateIdentifier_);
        RebuildAfterDocumentChange(true);
        return;
    }
    SetStatusText(Localization::Text(L"Düzenleme yinelendi.", L"Edit redone."));
}

bool AudioEditorWindow::RebuildAfterDocumentChange(const bool fitWaveform)
{
    if (!document_.has_value()) return false;
    std::string cacheError;
    auto cache = AudioWaveformCache::Build(*document_, cacheError);
    if (!cache.has_value())
    {
        const std::wstring detail = Utf8ToWide(cacheError);
        MessageBoxW(window_, detail.empty() ? Localization::Text(L"Waveform yeniden oluşturulamadı.", L"The waveform could not be rebuilt.") : detail.c_str(),
            L"SoundBoardFasaFiso", MB_OK | MB_ICONERROR);
        return false;
    }
    waveformCache_ = std::move(cache);
    previewPlayer_.Shutdown();
    selection_.reset();
    playheadFrame_ = 0U;
    if (fitWaveform) viewport_.Reset(document_->FrameCount());
    UpdateWindowTitle();
    UpdateMetadataText();
    UpdateTransportControls();
    UpdateEditControls();
    UpdatePlaybackTimeText();
    UpdateWaveformScrollBar();
    MarkWaveformBaseDirty();
    InvalidateRect(window_, nullptr, TRUE);
    return true;
}

RECT AudioEditorWindow::WaveformInnerRectangle() const noexcept
{
    RECT inner = waveformRectangle_;
    const int padding = Scale(12);
    InflateRect(&inner, -padding, -padding);
    return inner;
}

std::size_t AudioEditorWindow::CurrentPlayheadFrame() const noexcept
{
    return playheadFrame_;
}

void AudioEditorWindow::SetStatusText(const std::wstring& text)
{
    if (statusLabel_ != nullptr)
    {
        SetWindowTextW(statusLabel_, text.c_str());
    }
}

bool AudioEditorWindow::HasSelection() const noexcept
{
    return document_.has_value() && selection_.has_value() &&
        document_->IsValidRange(*selection_) && !selection_->IsEmpty();
}

bool AudioEditorWindow::IsModified() const noexcept
{
    return document_.has_value() && currentStateIdentifier_ != savedStateIdentifier_;
}

bool AudioEditorWindow::IsLoadRunning() const noexcept
{
    return loadRunning_.load(std::memory_order_acquire);
}

bool AudioEditorWindow::IsSaveRunning() const noexcept
{
    return saveRunning_.load(std::memory_order_acquire);
}

bool AudioEditorWindow::IsBusy() const noexcept
{
    return IsLoadRunning() || IsSaveRunning();
}

int AudioEditorWindow::Scale(const int value) const noexcept
{
    return MulDiv(
        value,
        static_cast<int>(currentDpi_),
        USER_DEFAULT_SCREEN_DPI
    );
}

std::wstring AudioEditorWindow::Utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    if (required <= 0)
    {
        return std::wstring{value.begin(), value.end()};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required
    );
    return result;
}

std::string AudioEditorWindow::WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (required <= 0)
    {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required,
        nullptr,
        nullptr
    );
    return result;
}

std::wstring AudioEditorWindow::FormatDuration(const double seconds)
{
    const double safeSeconds = std::isfinite(seconds)
        ? std::max(0.0, seconds)
        : 0.0;
    const auto totalMilliseconds = static_cast<unsigned long long>(
        std::llround(safeSeconds * 1000.0)
    );
    const auto minutes = totalMilliseconds / 60000ULL;
    const auto remainingSeconds = (totalMilliseconds / 1000ULL) % 60ULL;
    const auto milliseconds = totalMilliseconds % 1000ULL;

    std::wostringstream text;
    text << minutes << L':' << std::setfill(L'0')
        << std::setw(2) << remainingSeconds << L'.'
        << std::setw(3) << milliseconds;
    return text.str();
}
