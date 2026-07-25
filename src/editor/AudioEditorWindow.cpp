#include "editor/AudioEditorWindow.hpp"

#include "editor/AudioDocumentWav.hpp"
#include "localization/Localization.hpp"
#include "ResourceIds.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr wchar_t AudioEditorWindowClassName[] =
        L"SoundBoardFasaFiso.AudioEditorWindow";
    constexpr int Margin = 18;
    constexpr int ButtonHeight = 34;
    constexpr int ButtonWidth = 116;
    constexpr int ButtonGap = 8;
    constexpr int HeaderRowHeight = 38;
    constexpr int TransportRowHeight = 38;
    constexpr int TransportButtonWidth = 88;
    constexpr int CompactButtonWidth = 58;
    constexpr int ScrollBarHeight = 18;
    constexpr int StatusRowHeight = 24;
    constexpr int PanelRadius = 12;

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
        const int radius
    )
    {
        HPEN pen = CreatePen(PS_SOLID, 1, color);
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
}

AudioEditorWindow::~AudioEditorWindow()
{
    Shutdown();
}

bool AudioEditorWindow::Show(
    const HINSTANCE instance,
    const HWND owner,
    const AppTheme theme,
    const std::optional<std::filesystem::path>& initialFile
)
{
    theme_ = theme;

    if (!EnsureWindow(instance, owner))
    {
        return false;
    }

    ApplyTheme();
    RefreshLocalizedText();

    if (initialFile.has_value())
    {
        LoadFile(*initialFile);
    }

    ShowWindow(window_, SW_SHOW);
    ShowWindow(window_, SW_RESTORE);
    SetForegroundWindow(window_);
    return true;
}

void AudioEditorWindow::Shutdown()
{
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
    closeButton_ = nullptr;
    playPauseButton_ = nullptr;
    stopButton_ = nullptr;
    zoomOutButton_ = nullptr;
    zoomFitButton_ = nullptr;
    zoomInButton_ = nullptr;
    waveformScrollBar_ = nullptr;
    fileLabel_ = nullptr;
    metadataLabel_ = nullptr;
    timeLabel_ = nullptr;
    statusLabel_ = nullptr;
    owner_ = nullptr;
    instance_ = nullptr;
    currentDpi_ = USER_DEFAULT_SCREEN_DPI;
    classRegistered_ = false;
    waveformRectangle_ = {};
    playheadFrame_ = 0U;
    viewport_.Reset(0U);
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
        window_,
        Localization::Text(
            L"SoundBoardFasaFiso Ses Editörü",
            L"SoundBoardFasaFiso Audio Editor"
        )
    );
    SetWindowTextW(
        openButton_,
        Localization::Text(L"WAV aç", L"Open WAV")
    );
    SetWindowTextW(closeButton_, Localization::Text(L"Kapat", L"Close"));
    SetWindowTextW(stopButton_, Localization::Text(L"Durdur", L"Stop"));
    SetWindowTextW(zoomOutButton_, L"−");
    SetWindowTextW(zoomFitButton_, Localization::Text(L"Sığdır", L"Fit"));
    SetWindowTextW(zoomInButton_, L"+");

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
            L"WAV aç; oynat, waveform'a tıkla veya fare tekerleğiyle yakınlaştır.",
            L"Open a WAV, play it, click the waveform, or zoom with the mouse wheel."
        ));
    }

    UpdateTransportControls();
    UpdatePlaybackTimeText();
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
            SetTextColor(
                deviceContext,
                control == fileLabel_ ? textColor_ : mutedTextColor_
            );
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        }

        case WM_CTLCOLORSCROLLBAR:
            return reinterpret_cast<LRESULT>(panelBrush_);

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                switch (LOWORD(wParam))
                {
                    case IdOpenFile:
                        BrowseForWav();
                        return 0;

                    case IdClose:
                        StopPlayback();
                        ShowWindow(window_, SW_HIDE);
                        return 0;

                    case IdPlayPause:
                        TogglePlayback();
                        return 0;

                    case IdStop:
                        StopPlayback();
                        return 0;

                    case IdZoomOut:
                        if (document_.has_value())
                        {
                            ZoomAtFrame(
                                viewport_.VisibleRange().beginFrame +
                                    viewport_.VisibleRange().FrameCount() / 2U,
                                0.5
                            );
                        }
                        return 0;

                    case IdZoomFit:
                        FitWaveform();
                        return 0;

                    case IdZoomIn:
                        if (document_.has_value())
                        {
                            ZoomAtFrame(
                                viewport_.VisibleRange().beginFrame +
                                    viewport_.VisibleRange().FrameCount() / 2U,
                                2.0
                            );
                        }
                        return 0;

                    default:
                        break;
                }
            }
            break;

        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == waveformScrollBar_)
            {
                HandleHorizontalScroll(wParam);
                return 0;
            }
            break;

        case WM_MOUSEWHEEL:
            HandleMouseWheel(wParam, lParam);
            return 0;

        case WM_LBUTTONDOWN:
            HandleWaveformClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_TIMER:
            if (wParam == PlaybackTimerId)
            {
                HandlePlaybackTimer();
                return 0;
            }
            break;

        case WM_KEYDOWN:
            if (wParam == VK_SPACE)
            {
                TogglePlayback();
                return 0;
            }
            if (wParam == VK_HOME)
            {
                SeekToFrame(0U);
                return 0;
            }
            break;

        case WM_SIZE:
            LayoutControls(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_DPICHANGED:
            HandleDpiChanged(
                HIWORD(wParam),
                *reinterpret_cast<const RECT*>(lParam)
            );
            return 0;

        case WM_GETMINMAXINFO:
            if (lParam != 0)
            {
                auto* minimumMaximum =
                    reinterpret_cast<MINMAXINFO*>(lParam);
                RECT minimumRectangle{
                    0,
                    0,
                    Scale(MinimumClientWidth),
                    Scale(MinimumClientHeight)
                };
                AdjustWindowRectEx(
                    &minimumRectangle,
                    WS_OVERLAPPEDWINDOW,
                    FALSE,
                    0
                );
                minimumMaximum->ptMinTrackSize.x =
                    minimumRectangle.right - minimumRectangle.left;
                minimumMaximum->ptMinTrackSize.y =
                    minimumRectangle.bottom - minimumRectangle.top;
                return 0;
            }
            break;

        case WM_CLOSE:
            StopPlayback();
            ShowWindow(window_, SW_HIDE);
            return 0;

        case WM_NCDESTROY:
            KillTimer(window, PlaybackTimerId);
            previewPlayer_.Shutdown();
            if (window_ == window)
            {
                window_ = nullptr;
            }
            return DefWindowProcW(window, message, wParam, lParam);

        default:
            break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool AudioEditorWindow::EnsureWindow(
    const HINSTANCE instance,
    const HWND owner
)
{
    if (window_ != nullptr)
    {
        owner_ = owner;
        return true;
    }

    instance_ = instance;
    owner_ = owner;

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

    window_ = CreateWindowExW(
        0,
        AudioEditorWindowClassName,
        L"SoundBoardFasaFiso Audio Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
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
        return CreateWindowExW(
            0,
            className,
            text,
            WS_CHILD | WS_VISIBLE | style,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_,
            nullptr
        );
    };

    openButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdOpenFile
    );
    closeButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdClose
    );
    playPauseButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdPlayPause
    );
    stopButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdStop
    );
    zoomOutButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdZoomOut
    );
    zoomFitButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdZoomFit
    );
    zoomInButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdZoomIn
    );
    waveformScrollBar_ = createControl(
        L"SCROLLBAR", L"", SBS_HORZ | WS_TABSTOP, IdWaveformScroll
    );
    fileLabel_ = createControl(
        L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS, 0
    );
    metadataLabel_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    timeLabel_ = createControl(
        L"STATIC", L"", SS_RIGHT | SS_PATHELLIPSIS, 0
    );
    statusLabel_ = createControl(
        L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS, 0
    );

    return openButton_ != nullptr && closeButton_ != nullptr &&
        playPauseButton_ != nullptr && stopButton_ != nullptr &&
        zoomOutButton_ != nullptr && zoomFitButton_ != nullptr &&
        zoomInButton_ != nullptr && waveformScrollBar_ != nullptr &&
        fileLabel_ != nullptr && metadataLabel_ != nullptr &&
        timeLabel_ != nullptr && statusLabel_ != nullptr;
}

void AudioEditorWindow::LayoutControls(
    const int clientWidth,
    const int clientHeight
)
{
    if (window_ == nullptr)
    {
        return;
    }

    const int margin = Scale(Margin);
    const int buttonWidth = Scale(ButtonWidth);
    const int buttonHeight = Scale(ButtonHeight);
    const int buttonGap = Scale(ButtonGap);
    const int headerRowHeight = Scale(HeaderRowHeight);
    const int transportRowHeight = Scale(TransportRowHeight);
    const int transportButtonWidth = Scale(TransportButtonWidth);
    const int compactButtonWidth = Scale(CompactButtonWidth);
    const int scrollBarHeight = Scale(ScrollBarHeight);
    const int statusRowHeight = Scale(StatusRowHeight);

    MoveWindow(
        openButton_,
        margin,
        margin,
        buttonWidth,
        buttonHeight,
        TRUE
    );
    MoveWindow(
        closeButton_,
        clientWidth - margin - buttonWidth,
        margin,
        buttonWidth,
        buttonHeight,
        TRUE
    );

    const int labelX = margin + buttonWidth + buttonGap;
    const int labelWidth = std::max(
        80,
        clientWidth - labelX - margin - buttonWidth - buttonGap
    );
    MoveWindow(
        fileLabel_,
        labelX,
        margin + Scale(2),
        labelWidth,
        Scale(22),
        TRUE
    );
    MoveWindow(
        metadataLabel_,
        labelX,
        margin + Scale(23),
        labelWidth,
        Scale(18),
        TRUE
    );

    const int transportTop = margin + headerRowHeight + Scale(6);
    int transportX = margin;
    MoveWindow(
        playPauseButton_,
        transportX,
        transportTop,
        transportButtonWidth,
        buttonHeight,
        TRUE
    );
    transportX += transportButtonWidth + buttonGap;
    MoveWindow(
        stopButton_,
        transportX,
        transportTop,
        transportButtonWidth,
        buttonHeight,
        TRUE
    );
    transportX += transportButtonWidth + Scale(18);
    MoveWindow(
        zoomOutButton_,
        transportX,
        transportTop,
        compactButtonWidth,
        buttonHeight,
        TRUE
    );
    transportX += compactButtonWidth + buttonGap;
    MoveWindow(
        zoomFitButton_,
        transportX,
        transportTop,
        transportButtonWidth,
        buttonHeight,
        TRUE
    );
    transportX += transportButtonWidth + buttonGap;
    MoveWindow(
        zoomInButton_,
        transportX,
        transportTop,
        compactButtonWidth,
        buttonHeight,
        TRUE
    );

    const int timeLabelX = transportX + compactButtonWidth + buttonGap;
    MoveWindow(
        timeLabel_,
        timeLabelX,
        transportTop + Scale(8),
        std::max(1, clientWidth - margin - timeLabelX),
        Scale(22),
        TRUE
    );

    const int waveformTop = transportTop + transportRowHeight + Scale(8);
    const int statusTop = std::max(
        waveformTop,
        clientHeight - margin - statusRowHeight
    );
    const int scrollTop = std::max(
        waveformTop,
        statusTop - Scale(8) - scrollBarHeight
    );
    waveformRectangle_ = RECT{
        margin,
        waveformTop,
        std::max(margin + 1, clientWidth - margin),
        std::max(waveformTop + 1, scrollTop - Scale(8))
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
        statusRowHeight,
        TRUE
    );

    UpdateWaveformScrollBar();
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

bool AudioEditorWindow::LoadFile(
    const std::filesystem::path& filePath
)
{
    StopPlayback();
    previewPlayer_.Shutdown();

    SetStatusText(Localization::Text(
        L"WAV yükleniyor...",
        L"Loading WAV..."
    ));

    AudioWavLoadResult loadResult = AudioDocumentWav::Load(filePath);
    if (!loadResult.Succeeded() || !loadResult.document.has_value())
    {
        const std::wstring detail = Utf8ToWide(loadResult.errorMessage);
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
        return false;
    }

    std::optional<AudioDocument> loadedDocument{
        std::move(*loadResult.document)
    };
    std::string cacheError;
    std::optional<AudioWaveformCache> cache = AudioWaveformCache::Build(
        *loadedDocument,
        cacheError
    );

    if (!cache.has_value())
    {
        SetStatusText(Localization::Text(
            L"Waveform önbelleği oluşturulamadı.",
            L"The waveform cache could not be created."
        ));
        const std::wstring detail = Utf8ToWide(cacheError);
        MessageBoxW(
            window_,
            detail.empty()
                ? Localization::Text(
                    L"Waveform önbelleği oluşturulamadı.",
                    L"The waveform cache could not be created."
                )
                : detail.c_str(),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return false;
    }

    loadedFile_ = filePath;
    document_ = std::move(loadedDocument);
    waveformCache_ = std::move(cache);
    viewport_.Reset(document_->FrameCount());
    playheadFrame_ = 0U;
    UpdateMetadataText();
    UpdateTransportControls();
    UpdatePlaybackTimeText();
    UpdateWaveformScrollBar();
    SetStatusText(Localization::Text(
        L"WAV yüklendi. Oynat düğmesini kullan veya waveform'a tıklayarak konum seç.",
        L"WAV loaded. Use Play or click the waveform to seek."
    ));
    InvalidateRect(window_, nullptr, TRUE);
    return true;
}

void AudioEditorWindow::ClearDocument()
{
    if (window_ != nullptr)
    {
        KillTimer(window_, PlaybackTimerId);
    }
    previewPlayer_.Shutdown();
    waveformCache_.reset();
    document_.reset();
    loadedFile_.clear();
    viewport_.Reset(0U);
    playheadFrame_ = 0U;
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

    if (waveformRectangle_.right > waveformRectangle_.left &&
        waveformRectangle_.bottom > waveformRectangle_.top)
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

        DrawWaveform(deviceContext, WaveformInnerRectangle());
    }

    EndPaint(window_, &paint);
}

void AudioEditorWindow::DrawWaveform(
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

    const AudioFrameRange visibleRange = viewport_.VisibleRange();
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
    RECT rectangle = item.rcItem;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const bool primaryButton = item.hwndItem == openButton_ ||
        item.hwndItem == playPauseButton_;
    COLORREF fillColor = primaryButton ? accentColor_ : panelColor_;

    if (disabled)
    {
        fillColor = panelColor_;
    }
    else if (pressed)
    {
        fillColor = RGB(
            GetRValue(fillColor) * 82 / 100,
            GetGValue(fillColor) * 82 / 100,
            GetBValue(fillColor) * 82 / 100
        );
    }

    FillRoundedRectangle(
        item.hDC,
        rectangle,
        fillColor,
        Scale(9)
    );
    DrawRoundedBorder(
        item.hDC,
        rectangle,
        focused ? accentColor_ : borderColor_,
        Scale(9)
    );

    wchar_t text[128]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(
        item.hDC,
        disabled ? mutedTextColor_ : textColor_
    );
    const HGDIOBJ oldFont = SelectObject(item.hDC, buttonFont_);
    DrawTextW(
        item.hDC,
        text,
        -1,
        &rectangle,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
    SelectObject(item.hDC, oldFont);
}

void AudioEditorWindow::ApplyTheme()
{
    if (theme_ == AppTheme::Dark)
    {
        backgroundColor_ = RGB(15, 17, 23);
        panelColor_ = RGB(24, 28, 36);
        textColor_ = RGB(244, 246, 250);
        mutedTextColor_ = RGB(154, 164, 178);
        borderColor_ = RGB(42, 49, 64);
        accentColor_ = RGB(124, 92, 255);
        waveformColor_ = RGB(139, 108, 255);
        centerLineColor_ = RGB(70, 78, 96);
        playheadColor_ = RGB(255, 93, 115);
    }
    else
    {
        backgroundColor_ = RGB(242, 245, 250);
        panelColor_ = RGB(255, 255, 255);
        textColor_ = RGB(27, 31, 40);
        mutedTextColor_ = RGB(91, 101, 117);
        borderColor_ = RGB(207, 214, 225);
        accentColor_ = RGB(98, 70, 230);
        waveformColor_ = RGB(98, 70, 230);
        centerLineColor_ = RGB(207, 214, 225);
        playheadColor_ = RGB(220, 55, 82);
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
        InvalidateRect(window_, nullptr, TRUE);
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

    bodyFont_ = CreateFontW(
        -Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    buttonFont_ = CreateFontW(
        -Scale(15), 0, 0, 0, 600, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    const HWND labels[]{fileLabel_, metadataLabel_, timeLabel_, statusLabel_};
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
        closeButton_,
        playPauseButton_,
        stopButton_,
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
    if (!document_.has_value())
    {
        return;
    }

    SetWindowTextW(fileLabel_, loadedFile_.filename().c_str());

    std::wostringstream metadata;
    metadata << document_->SampleRate() << L" Hz · "
        << document_->ChannelCount() << L" "
        << (document_->ChannelCount() == 1U
            ? Localization::Text(L"kanal", L"channel")
            : Localization::Text(L"kanal", L"channels"))
        << L" · " << FormatDuration(document_->DurationSeconds())
        << L" · " << document_->FrameCount() << L" "
        << Localization::Text(L"frame", L"frames");
    SetWindowTextW(metadataLabel_, metadata.str().c_str());
}

void AudioEditorWindow::UpdateTransportControls()
{
    const bool hasDocument = document_.has_value() && !document_->Empty();
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

    const bool scrollable = document_.has_value() && !viewport_.IsFit();
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

    std::string errorMessage;
    if (!previewPlayer_.Matches(*document_) &&
        !previewPlayer_.Prepare(*document_, errorMessage))
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
    InvalidateRect(window_, &waveformRectangle_, FALSE);
}

void AudioEditorWindow::StopPlayback()
{
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
    if (window_ != nullptr)
    {
        InvalidateRect(window_, &waveformRectangle_, FALSE);
    }
}

void AudioEditorWindow::SeekToFrame(const std::size_t frame)
{
    if (!document_.has_value())
    {
        return;
    }

    const std::size_t clampedFrame = std::min(frame, document_->FrameCount());
    playheadFrame_ = clampedFrame;

    std::string errorMessage;
    if (previewPlayer_.Matches(*document_) &&
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
    InvalidateRect(window_, &waveformRectangle_, FALSE);
}

void AudioEditorWindow::HandlePlaybackTimer()
{
    if (!document_.has_value())
    {
        KillTimer(window_, PlaybackTimerId);
        return;
    }

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
    InvalidateRect(window_, &waveformRectangle_, FALSE);
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

void AudioEditorWindow::HandleWaveformClick(const int x, const int y)
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

    SetFocus(window_);
    SeekToFrame(viewport_.FrameAtPixel(
        x - inner.left,
        inner.right - inner.left
    ));
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
    return previewPlayer_.State() == AudioPreviewState::Playing
        ? previewPlayer_.CurrentFrame()
        : playheadFrame_;
}

void AudioEditorWindow::SetStatusText(const std::wstring& text)
{
    if (statusLabel_ != nullptr)
    {
        SetWindowTextW(statusLabel_, text.c_str());
    }
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
