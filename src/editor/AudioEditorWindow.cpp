#include "editor/AudioEditorWindow.hpp"

#include "editor/AudioDocumentWav.hpp"
#include "localization/Localization.hpp"
#include "ResourceIds.h"

#include <commdlg.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <iterator>
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
    fileLabel_ = nullptr;
    metadataLabel_ = nullptr;
    statusLabel_ = nullptr;
    owner_ = nullptr;
    instance_ = nullptr;
    currentDpi_ = USER_DEFAULT_SCREEN_DPI;
    classRegistered_ = false;
    waveformRectangle_ = {};
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
        SetStatusText(Localization::Text(
            L"Waveform önizlemesi hazır.",
            L"Waveform preview is ready."
        ));
    }

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

        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                switch (LOWORD(wParam))
                {
                    case IdOpenFile:
                        BrowseForWav();
                        return 0;

                    case IdClose:
                        ShowWindow(window_, SW_HIDE);
                        return 0;

                    default:
                        break;
                }
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
            ShowWindow(window_, SW_HIDE);
            return 0;

        case WM_NCDESTROY:
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
    fileLabel_ = createControl(
        L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS, 0
    );
    metadataLabel_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    statusLabel_ = createControl(
        L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS, 0
    );

    return openButton_ != nullptr && closeButton_ != nullptr &&
        fileLabel_ != nullptr && metadataLabel_ != nullptr &&
        statusLabel_ != nullptr;
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

    const int waveformTop = margin + headerRowHeight + Scale(10);
    const int statusTop = std::max(
        waveformTop,
        clientHeight - margin - statusRowHeight
    );
    waveformRectangle_ = RECT{
        margin,
        waveformTop,
        std::max(margin + 1, clientWidth - margin),
        std::max(waveformTop + 1, statusTop - Scale(8))
    };

    MoveWindow(
        statusLabel_,
        margin,
        statusTop,
        std::max(1, clientWidth - margin * 2),
        statusRowHeight,
        TRUE
    );

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
    UpdateMetadataText();
    SetStatusText(Localization::Text(
        L"WAV yüklendi. Bu aşamada yalnızca waveform önizlemesi etkin.",
        L"WAV loaded. Only waveform preview is enabled in this stage."
    ));
    InvalidateRect(window_, nullptr, TRUE);
    return true;
}

void AudioEditorWindow::ClearDocument()
{
    waveformCache_.reset();
    document_.reset();
    loadedFile_.clear();
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

        RECT inner = waveformRectangle_;
        const int padding = Scale(12);
        InflateRect(&inner, -padding, -padding);
        DrawWaveform(deviceContext, inner);
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
            AudioFrameRange{0U, document_->FrameCount()},
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
            const int x = rectangle.left + static_cast<int>(column);
            const int yTop = centerY - static_cast<int>(
                std::lround(maximum * static_cast<float>(halfHeight))
            );
            const int yBottom = centerY - static_cast<int>(
                std::lround(minimum * static_cast<float>(halfHeight))
            );

            MoveToEx(deviceContext, x, yTop, nullptr);
            LineTo(deviceContext, x, yBottom + 1);
        }

        SelectObject(deviceContext, centerPen);
    }

    SelectObject(deviceContext, oldPen);
    DeleteObject(centerPen);
    DeleteObject(waveformPen);
}

void AudioEditorWindow::DrawButton(
    const DRAWITEMSTRUCT& item
) const
{
    RECT rectangle = item.rcItem;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    COLORREF fillColor = item.hwndItem == openButton_
        ? accentColor_
        : panelColor_;

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

    const HWND labels[]{fileLabel_, metadataLabel_, statusLabel_};
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

    const HWND buttons[]{openButton_, closeButton_};
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
