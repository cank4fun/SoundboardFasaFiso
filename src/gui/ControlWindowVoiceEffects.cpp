#include "gui/ControlWindow.hpp"

#include "audio/Audio.hpp"
#include "localization/Localization.hpp"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <optional>
#include <sstream>

namespace
{
    constexpr std::array VoiceEffectPresetOrder{
        VoiceEffectPreset::DeepHeavy,
        VoiceEffectPreset::HighNasalRap,
        VoiceEffectPreset::DarkVocal,
        VoiceEffectPreset::Radio,
        VoiceEffectPreset::Robot,
        VoiceEffectPreset::TinyHighVoice,
        VoiceEffectPreset::Custom
    };

    constexpr int BuiltInVoiceEffectPresetCount =
        static_cast<int>(VoiceEffectPresetOrder.size());

    int VoiceEffectPresetIndex(const VoiceEffectPreset preset)
    {
        const auto iterator = std::find(
            VoiceEffectPresetOrder.begin(),
            VoiceEffectPresetOrder.end(),
            preset
        );

        return iterator == VoiceEffectPresetOrder.end()
            ? static_cast<int>(VoiceEffectPresetOrder.size() - 1)
            : static_cast<int>(std::distance(
                VoiceEffectPresetOrder.begin(),
                iterator
            ));
    }

    std::optional<VoiceEffectPreset> VoiceEffectPresetFromIndex(
        const int index
    )
    {
        if (index < 0 ||
            static_cast<std::size_t>(index) >= VoiceEffectPresetOrder.size())
        {
            return std::nullopt;
        }

        return VoiceEffectPresetOrder[static_cast<std::size_t>(index)];
    }

    std::wstring VoiceEffectPresetDisplayName(
        const VoiceEffectPreset preset
    )
    {
        switch (preset)
        {
            case VoiceEffectPreset::DeepHeavy:
                return Localization::Text(L"Derin / Ağır", L"Deep / Heavy");
            case VoiceEffectPreset::HighNasalRap:
                return Localization::Text(
                    L"Yüksek / Nazal Rap",
                    L"High / Nasal Rap"
                );
            case VoiceEffectPreset::DarkVocal:
                return Localization::Text(L"Karanlık Vokal", L"Dark Vocal");
            case VoiceEffectPreset::Radio:
                return L"Radio";
            case VoiceEffectPreset::Robot:
                return L"Robot";
            case VoiceEffectPreset::TinyHighVoice:
                return Localization::Text(
                    L"Minik / Yüksek Ses",
                    L"Tiny / High Voice"
                );
            case VoiceEffectPreset::Custom:
                return Localization::Text(L"Özel", L"Custom");
        }

        return Localization::Text(L"Özel", L"Custom");
    }

    int ToTenths(const float value)
    {
        return static_cast<int>(std::lround(value * 10.0f));
    }

    int ToPercent(const float value)
    {
        return static_cast<int>(std::lround(value * 100.0f));
    }

    std::wstring FormatSignedTenths(
        const float value,
        const wchar_t* const suffix
    )
    {
        std::wostringstream stream;
        stream << std::showpos << std::fixed << std::setprecision(1)
            << value << suffix;
        return stream.str();
    }
}

bool ControlWindow::CreateVoiceEffectsControls()
{
    if (window_ == nullptr)
    {
        return false;
    }

    const auto createControl = [this](
        const wchar_t* const className,
        const wchar_t* const text,
        const DWORD style,
        const int id,
        const DWORD extendedStyle = 0
    )
    {
        HWND control = CreateWindowExW(
            extendedStyle,
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

        ApplyDefaultFont(control);
        return control;
    };

    voiceEffectsGroup_ = createControl(L"STATIC", L"", SS_OWNERDRAW, 0);
    voiceEffectsEnabledCheck_ = createControl(
        L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP, 0
    );
    voiceEffectsBypassCheck_ = createControl(
        L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP, 0
    );
    voiceEffectsStatusCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    voiceEffectsStatusValue_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    voiceEffectsPresetCaption_ = createControl(L"STATIC", L"", SS_LEFT, 0);
    voiceEffectsPresetCombo_ = createControl(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        IdVoiceEffectsPreset,
        WS_EX_CLIENTEDGE
    );

    voiceEffectsPresetNameCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );
    voiceEffectsPresetNameEdit_ = createControl(
        L"EDIT",
        L"",
        ES_AUTOHSCROLL | WS_TABSTOP,
        IdVoiceEffectsPresetName,
        WS_EX_CLIENTEDGE
    );
    SendMessageW(
        voiceEffectsPresetNameEdit_,
        EM_SETLIMITTEXT,
        static_cast<WPARAM>(VoiceEffectLimits::MaximumUserPresetNameBytes),
        0
    );

    const auto createSliderRow = [&createControl](
        HWND& caption,
        HWND& slider,
        HWND& value,
        const int sliderId,
        const int minimum,
        const int maximum,
        const int pageSize
    )
    {
        caption = createControl(L"STATIC", L"", SS_LEFT, 0);
        slider = createControl(
            TRACKBAR_CLASSW,
            L"",
            TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
            sliderId
        );
        value = createControl(L"STATIC", L"", SS_RIGHT, 0);
        SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(minimum, maximum));
        SendMessageW(slider, TBM_SETPAGESIZE, 0, pageSize);
    };

    createSliderRow(
        voiceEffectsPitchCaption_,
        voiceEffectsPitchSlider_,
        voiceEffectsPitchValue_,
        IdVoiceEffectsPitchSlider,
        -120,
        120,
        10
    );
    createSliderRow(
        voiceEffectsFormantCaption_,
        voiceEffectsFormantSlider_,
        voiceEffectsFormantValue_,
        IdVoiceEffectsFormantSlider,
        -60,
        60,
        5
    );
    createSliderRow(
        voiceEffectsBodyCaption_,
        voiceEffectsBodySlider_,
        voiceEffectsBodyValue_,
        IdVoiceEffectsBodySlider,
        0,
        100,
        5
    );
    createSliderRow(
        voiceEffectsCharacterCaption_,
        voiceEffectsCharacterSlider_,
        voiceEffectsCharacterValue_,
        IdVoiceEffectsCharacterSlider,
        0,
        100,
        5
    );
    createSliderRow(
        voiceEffectsDriveCaption_,
        voiceEffectsDriveSlider_,
        voiceEffectsDriveValue_,
        IdVoiceEffectsDriveSlider,
        0,
        100,
        5
    );
    createSliderRow(
        voiceEffectsDryWetCaption_,
        voiceEffectsDryWetSlider_,
        voiceEffectsDryWetValue_,
        IdVoiceEffectsDryWetSlider,
        0,
        100,
        5
    );
    createSliderRow(
        voiceEffectsOutputGainCaption_,
        voiceEffectsOutputGainSlider_,
        voiceEffectsOutputGainValue_,
        IdVoiceEffectsOutputGainSlider,
        -240,
        120,
        10
    );

    voiceEffectsResetButton_ = createControl(
        L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, IdVoiceEffectsReset
    );
    voiceEffectsSavePresetButton_ = createControl(
        L"BUTTON",
        L"",
        BS_OWNERDRAW | WS_TABSTOP,
        IdVoiceEffectsSavePreset
    );
    voiceEffectsDeletePresetButton_ = createControl(
        L"BUTTON",
        L"",
        BS_OWNERDRAW | WS_TABSTOP,
        IdVoiceEffectsDeletePreset
    );
    voiceEffectsInfoCaption_ = createControl(
        L"STATIC", L"", SS_LEFT, 0
    );

    const HWND controls[]{
        voiceEffectsGroup_, voiceEffectsEnabledCheck_,
        voiceEffectsBypassCheck_, voiceEffectsStatusCaption_,
        voiceEffectsStatusValue_, voiceEffectsPresetCaption_,
        voiceEffectsPresetCombo_, voiceEffectsPresetNameCaption_,
        voiceEffectsPresetNameEdit_, voiceEffectsPitchCaption_,
        voiceEffectsPitchSlider_, voiceEffectsPitchValue_,
        voiceEffectsFormantCaption_, voiceEffectsFormantSlider_,
        voiceEffectsFormantValue_, voiceEffectsBodyCaption_,
        voiceEffectsBodySlider_, voiceEffectsBodyValue_,
        voiceEffectsCharacterCaption_, voiceEffectsCharacterSlider_,
        voiceEffectsCharacterValue_,
        voiceEffectsDriveCaption_, voiceEffectsDriveSlider_,
        voiceEffectsDriveValue_, voiceEffectsDryWetCaption_,
        voiceEffectsDryWetSlider_, voiceEffectsDryWetValue_,
        voiceEffectsOutputGainCaption_, voiceEffectsOutputGainSlider_,
        voiceEffectsOutputGainValue_, voiceEffectsResetButton_,
        voiceEffectsSavePresetButton_, voiceEffectsDeletePresetButton_,
        voiceEffectsInfoCaption_
    };

    return std::all_of(
        std::begin(controls),
        std::end(controls),
        [](const HWND control)
        {
            return control != nullptr;
        }
    );
}

void ControlWindow::LayoutVoiceEffectsControls(
    const int contentX,
    const int pageY,
    const int contentWidth,
    const int pageHeight
)
{
    const auto moveWindow = [this](
        const HWND control,
        const int x,
        const int y,
        const int width,
        const int height
    )
    {
        ::MoveWindow(
            control,
            Scale(x),
            Scale(y),
            Scale(width),
            Scale(height),
            TRUE
        );
    };

    moveWindow(
        voiceEffectsGroup_, contentX, pageY, contentWidth, pageHeight
    );

    const int innerX = contentX + 18;
    const int innerWidth = contentWidth - 36;
    const int columnGap = 28;
    const int columnWidth = (innerWidth - columnGap) / 2;
    const int rightX = innerX + columnWidth + columnGap;
    const int valueWidth = 72;
    const int captionWidth = 118;
    const int sliderWidth = std::max(
        90,
        columnWidth - captionWidth - valueWidth - 10
    );
    int rowY = pageY + 34;

    moveWindow(
        voiceEffectsEnabledCheck_, innerX, rowY, 210, 22
    );
    moveWindow(
        voiceEffectsBypassCheck_, innerX + 220, rowY, 130, 22
    );
    moveWindow(
        voiceEffectsStatusCaption_, rightX, rowY + 2, 76, 20
    );
    moveWindow(
        voiceEffectsStatusValue_, rightX + 76, rowY + 2,
        columnWidth - 76, 20
    );
    rowY += 36;

    moveWindow(
        voiceEffectsPresetCaption_, innerX, rowY + 4, 90, 20
    );
    moveWindow(
        voiceEffectsPresetCombo_, innerX + 90, rowY,
        std::max(220, columnWidth - 90), 220
    );
    moveWindow(
        voiceEffectsPresetNameCaption_, rightX, rowY + 4, 96, 20
    );
    moveWindow(
        voiceEffectsPresetNameEdit_, rightX + 96, rowY,
        std::max(180, columnWidth - 96), 26
    );
    rowY += 44;

    const auto layoutSlider = [&](
        const HWND caption,
        const HWND slider,
        const HWND value,
        const int x,
        const int y
    )
    {
        moveWindow(caption, x, y + 5, captionWidth, 20);
        moveWindow(slider, x + captionWidth, y, sliderWidth, 26);
        moveWindow(
            value,
            x + captionWidth + sliderWidth + 10,
            y + 5,
            valueWidth,
            20
        );
    };

    layoutSlider(
        voiceEffectsPitchCaption_, voiceEffectsPitchSlider_,
        voiceEffectsPitchValue_, innerX, rowY
    );
    layoutSlider(
        voiceEffectsFormantCaption_, voiceEffectsFormantSlider_,
        voiceEffectsFormantValue_, rightX, rowY
    );
    rowY += 48;

    layoutSlider(
        voiceEffectsBodyCaption_, voiceEffectsBodySlider_,
        voiceEffectsBodyValue_, innerX, rowY
    );
    layoutSlider(
        voiceEffectsCharacterCaption_, voiceEffectsCharacterSlider_,
        voiceEffectsCharacterValue_, rightX, rowY
    );
    rowY += 48;

    layoutSlider(
        voiceEffectsDriveCaption_, voiceEffectsDriveSlider_,
        voiceEffectsDriveValue_, innerX, rowY
    );
    layoutSlider(
        voiceEffectsDryWetCaption_, voiceEffectsDryWetSlider_,
        voiceEffectsDryWetValue_, rightX, rowY
    );
    rowY += 48;

    layoutSlider(
        voiceEffectsOutputGainCaption_, voiceEffectsOutputGainSlider_,
        voiceEffectsOutputGainValue_, innerX, rowY
    );

    const int buttonY = pageY + pageHeight - 48;
    const int buttonGap = 8;
    const int buttonWidth = 150;
    moveWindow(
        voiceEffectsResetButton_, innerX, buttonY, buttonWidth, 34
    );
    moveWindow(
        voiceEffectsSavePresetButton_,
        innerX + buttonWidth + buttonGap,
        buttonY,
        buttonWidth,
        34
    );
    moveWindow(
        voiceEffectsDeletePresetButton_,
        innerX + (buttonWidth + buttonGap) * 2,
        buttonY,
        buttonWidth,
        34
    );
    moveWindow(
        applySettingsButton_,
        contentX + contentWidth - 18 - 180,
        buttonY,
        180,
        34
    );
    moveWindow(
        voiceEffectsInfoCaption_,
        innerX,
        buttonY - 43,
        innerWidth,
        34
    );
}

void ControlWindow::PopulateVoiceEffectsPresetCombo()
{
    if (voiceEffectsPresetCombo_ == nullptr)
    {
        return;
    }

    const int previousSelection = static_cast<int>(SendMessageW(
        voiceEffectsPresetCombo_, CB_GETCURSEL, 0, 0
    ));
    const auto previousPreset = VoiceEffectPresetFromIndex(
        previousSelection
    );

    std::optional<std::string> previousUserPresetName;
    if (const auto userIndex = SelectedVoiceEffectUserPresetIndex();
        userIndex.has_value())
    {
        previousUserPresetName = currentConfig_
            .GetVoiceEffectUserPresets()[*userIndex].name;
    }

    SendMessageW(voiceEffectsPresetCombo_, CB_RESETCONTENT, 0, 0);

    for (const VoiceEffectPreset preset : VoiceEffectPresetOrder)
    {
        AddComboItem(
            voiceEffectsPresetCombo_,
            VoiceEffectPresetDisplayName(preset)
        );
    }

    const std::wstring userPrefix = Localization::Text(
        L"Kullanıcı • ",
        L"User • "
    );
    const auto& userPresets = currentConfig_.GetVoiceEffectUserPresets();
    for (const VoiceEffectUserPreset& preset : userPresets)
    {
        AddComboItem(
            voiceEffectsPresetCombo_,
            userPrefix + Utf8ToWide(preset.name)
        );
    }

    int selection = VoiceEffectPresetIndex(
        previousPreset.value_or(VoiceEffectPreset::Custom)
    );

    if (previousUserPresetName.has_value())
    {
        const auto iterator = std::find_if(
            userPresets.begin(),
            userPresets.end(),
            [&previousUserPresetName](const VoiceEffectUserPreset& preset)
            {
                return VoiceEffectUserPresetNamesEqual(
                    preset.name,
                    *previousUserPresetName
                );
            }
        );

        if (iterator != userPresets.end())
        {
            selection = BuiltInVoiceEffectPresetCount +
                static_cast<int>(std::distance(
                    userPresets.begin(),
                    iterator
                ));
        }
    }

    SendMessageW(
        voiceEffectsPresetCombo_,
        CB_SETCURSEL,
        static_cast<WPARAM>(selection),
        0
    );
}

void ControlWindow::PopulateVoiceEffectsControls()
{
    if (voiceEffectsEnabledCheck_ == nullptr)
    {
        return;
    }

    populatingVoiceEffectsControls_ = true;
    PopulateVoiceEffectsPresetCombo();

    const VoiceEffectSettings& settings =
        currentConfig_.GetVoiceEffectSettings();

    SendMessageW(
        voiceEffectsEnabledCheck_,
        BM_SETCHECK,
        settings.enabled ? BST_CHECKED : BST_UNCHECKED,
        0
    );
    SendMessageW(
        voiceEffectsBypassCheck_,
        BM_SETCHECK,
        settings.bypassed ? BST_CHECKED : BST_UNCHECKED,
        0
    );

    const VoiceEffectPreset displayedPreset =
        (VoiceEffectSettingsMatchPreset(settings, settings.preset) ||
            VoiceEffectPresetHasDedicatedStage(settings.preset))
            ? settings.preset
            : VoiceEffectPreset::Custom;
    SendMessageW(
        voiceEffectsPresetCombo_,
        CB_SETCURSEL,
        static_cast<WPARAM>(VoiceEffectPresetIndex(displayedPreset)),
        0
    );

    SendMessageW(
        voiceEffectsPitchSlider_, TBM_SETPOS, TRUE,
        ToTenths(settings.pitchSemitones)
    );
    SendMessageW(
        voiceEffectsFormantSlider_, TBM_SETPOS, TRUE,
        ToTenths(settings.formantSemitones)
    );
    SendMessageW(
        voiceEffectsBodySlider_, TBM_SETPOS, TRUE,
        ToPercent(settings.body)
    );
    SendMessageW(
        voiceEffectsCharacterSlider_, TBM_SETPOS, TRUE,
        ToPercent(settings.character)
    );
    SendMessageW(
        voiceEffectsDriveSlider_, TBM_SETPOS, TRUE,
        ToPercent(settings.drive)
    );
    SendMessageW(
        voiceEffectsDryWetSlider_, TBM_SETPOS, TRUE,
        ToPercent(settings.dryWet)
    );
    SendMessageW(
        voiceEffectsOutputGainSlider_, TBM_SETPOS, TRUE,
        ToTenths(settings.outputGainDb)
    );

    SetControlText(voiceEffectsPresetNameEdit_, L"");
    pendingVoiceEffectPreview_.reset();
    pendingVoiceEffectPresetDeleteName_.reset();
    UpdateVoiceEffectsValueLabels();
    populatingVoiceEffectsControls_ = false;
    UpdateVoiceEffectsPresetButtons();
}

void ControlWindow::ApplySelectedVoiceEffectsPreset()
{
    if (populatingVoiceEffectsControls_)
    {
        return;
    }

    CancelPendingVoiceEffectPresetDelete();

    const int selectedIndex = static_cast<int>(SendMessageW(
        voiceEffectsPresetCombo_, CB_GETCURSEL, 0, 0
    ));
    const auto preset = VoiceEffectPresetFromIndex(selectedIndex);
    const auto userPresetIndex = SelectedVoiceEffectUserPresetIndex();

    const bool enabled = SendMessageW(
        voiceEffectsEnabledCheck_, BM_GETCHECK, 0, 0
    ) == BST_CHECKED;
    const bool bypassed = SendMessageW(
        voiceEffectsBypassCheck_, BM_GETCHECK, 0, 0
    ) == BST_CHECKED;

    std::optional<VoiceEffectSettings> settings;
    if (userPresetIndex.has_value())
    {
        const VoiceEffectUserPreset& userPreset = currentConfig_
            .GetVoiceEffectUserPresets()[*userPresetIndex];
        settings = ApplyVoiceEffectUserPreset(
            userPreset,
            enabled,
            bypassed
        );
        SetControlText(
            voiceEffectsPresetNameEdit_,
            Utf8ToWide(userPreset.name)
        );
    }
    else
    {
        SetControlText(voiceEffectsPresetNameEdit_, L"");

        if (!preset.has_value() || *preset == VoiceEffectPreset::Custom)
        {
            UpdateVoiceEffectsPresetButtons();
            SubmitVoiceEffectsPreview();
            return;
        }

        settings = BuildVoiceEffectPreset(*preset, enabled);
        if (settings.has_value())
        {
            settings->bypassed = bypassed;
        }
    }

    if (!settings.has_value())
    {
        return;
    }

    populatingVoiceEffectsControls_ = true;
    SendMessageW(
        voiceEffectsPitchSlider_, TBM_SETPOS, TRUE,
        ToTenths(settings->pitchSemitones)
    );
    SendMessageW(
        voiceEffectsFormantSlider_, TBM_SETPOS, TRUE,
        ToTenths(settings->formantSemitones)
    );
    SendMessageW(
        voiceEffectsBodySlider_, TBM_SETPOS, TRUE,
        ToPercent(settings->body)
    );
    SendMessageW(
        voiceEffectsCharacterSlider_, TBM_SETPOS, TRUE,
        ToPercent(settings->character)
    );
    SendMessageW(
        voiceEffectsDriveSlider_, TBM_SETPOS, TRUE,
        ToPercent(settings->drive)
    );
    SendMessageW(
        voiceEffectsDryWetSlider_, TBM_SETPOS, TRUE,
        ToPercent(settings->dryWet)
    );
    SendMessageW(
        voiceEffectsOutputGainSlider_, TBM_SETPOS, TRUE,
        ToTenths(settings->outputGainDb)
    );
    UpdateVoiceEffectsValueLabels();
    populatingVoiceEffectsControls_ = false;
    UpdateVoiceEffectsPresetButtons();
    SubmitVoiceEffectsPreview();
}

void ControlWindow::MarkVoiceEffectsPresetCustom()
{
    if (populatingVoiceEffectsControls_ ||
        voiceEffectsPresetCombo_ == nullptr)
    {
        return;
    }

    CancelPendingVoiceEffectPresetDelete();

    const int selectedIndex = static_cast<int>(SendMessageW(
        voiceEffectsPresetCombo_, CB_GETCURSEL, 0, 0
    ));
    const auto selectedPreset = VoiceEffectPresetFromIndex(selectedIndex);

    VoiceEffectPreset effectivePreset = selectedPreset.value_or(
        VoiceEffectPreset::Custom
    );
    if (const auto userPresetIndex = SelectedVoiceEffectUserPresetIndex();
        userPresetIndex.has_value())
    {
        effectivePreset = currentConfig_
            .GetVoiceEffectUserPresets()[*userPresetIndex]
            .settings.preset;
    }

    if (VoiceEffectPresetHasDedicatedStage(effectivePreset))
    {
        return;
    }

    SendMessageW(
        voiceEffectsPresetCombo_,
        CB_SETCURSEL,
        static_cast<WPARAM>(VoiceEffectPresetIndex(
            VoiceEffectPreset::Custom
        )),
        0
    );
    UpdateVoiceEffectsPresetButtons();
}

void ControlWindow::UpdateVoiceEffectsRuntimeDiagnostics(
    const AudioLevelSnapshot& snapshot
)
{
    if (voiceEffectsInfoCaption_ == nullptr)
    {
        return;
    }

    if (!snapshot.microphoneAvailable)
    {
        SetControlText(
            voiceEffectsInfoCaption_,
            Localization::Text(
                L"Mikrofon kullanılamıyor. Voice Effects fiziksel testi başlatılamaz.",
                L"Microphone unavailable. The Voice Effects physical test cannot start."
            )
        );
        return;
    }

    if (!snapshot.microphoneProcessingActive ||
        !snapshot.microphoneProcessingDiagnosticsAvailable)
    {
        SetControlText(
            voiceEffectsInfoCaption_,
            Localization::Text(
                L"48 kHz işleme runtime'ı kapalı. Voice Effects'i etkinleştirip Kaydet ve uygula'ya basın.",
                L"The 48 kHz processing runtime is inactive. Enable Voice Effects and select Save and apply."
            )
        );
        return;
    }

    const double averageMilliseconds =
        snapshot.microphoneProcessedBlockCount == 0
            ? 0.0
            : static_cast<double>(
                snapshot.microphoneTotalProcessingTimeNanoseconds
            ) / static_cast<double>(snapshot.microphoneProcessedBlockCount) /
                1'000'000.0;
    const double maximumMilliseconds = static_cast<double>(
        snapshot.microphoneMaximumProcessingTimeNanoseconds
    ) / 1'000'000.0;
    const double peakQueueMilliseconds = static_cast<double>(
        snapshot.microphonePeakQueuedInputFrames
    ) * 1000.0 /
        static_cast<double>(MicrophoneProcessingRuntime::RequiredSampleRate);
    const double dspLatencyMilliseconds = static_cast<double>(
        VoiceEffectsProcessor::ProcessingLatencySamples
    ) * 1000.0 /
        static_cast<double>(VoiceEffectsProcessor::ProcessingSampleRate);

    const bool warning =
        snapshot.microphoneProcessingDeadlineMissCount != 0 ||
        snapshot.microphoneDroppedInputFrames != 0;
    const bool effectsEnabled = SendMessageW(
        voiceEffectsEnabledCheck_, BM_GETCHECK, 0, 0
    ) == BST_CHECKED;
    const bool effectsBypassed = SendMessageW(
        voiceEffectsBypassCheck_, BM_GETCHECK, 0, 0
    ) == BST_CHECKED;

    std::wostringstream stream;
    if (!effectsEnabled)
    {
        stream << Localization::Text(
            L"Effects kapalı",
            L"Effects disabled"
        );
    }
    else if (effectsBypassed)
    {
        stream << Localization::Text(L"Bypass aktif", L"Bypass active");
    }
    else
    {
        stream << Localization::Text(L"Effects canlı", L"Effects live");
    }

    stream << L" • " << (warning
        ? Localization::Text(L"runtime uyarısı", L"runtime warning")
        : Localization::Text(L"runtime sağlıklı", L"runtime healthy"));
    stream << L" • DSP " << std::fixed << std::setprecision(1)
        << dspLatencyMilliseconds << L" ms • "
        << Localization::Text(L"ortalama ", L"average ")
        << std::setprecision(2) << averageMilliseconds
        << L" ms • "
        << Localization::Text(L"maksimum ", L"maximum ")
        << maximumMilliseconds << L" ms • "
        << Localization::Text(L"deadline kaçırma ", L"deadline misses ")
        << snapshot.microphoneProcessingDeadlineMissCount << L" • "
        << Localization::Text(L"kuyruk tepe ", L"queue peak ")
        << std::setprecision(1) << peakQueueMilliseconds << L" ms • "
        << Localization::Text(L"düşen ", L"dropped ")
        << snapshot.microphoneDroppedInputFrames << L" • "
        << Localization::Text(L"reddedilen ayar ", L"rejected updates ")
        << snapshot.microphoneRejectedVoiceEffectUpdateCount;

    SetControlText(voiceEffectsInfoCaption_, stream.str());
}

void ControlWindow::UpdateVoiceEffectsValueLabels()
{
    if (voiceEffectsPitchSlider_ == nullptr)
    {
        return;
    }

    const float pitch = static_cast<float>(SendMessageW(
        voiceEffectsPitchSlider_, TBM_GETPOS, 0, 0
    )) / 10.0f;
    const float formant = static_cast<float>(SendMessageW(
        voiceEffectsFormantSlider_, TBM_GETPOS, 0, 0
    )) / 10.0f;
    const int body = static_cast<int>(SendMessageW(
        voiceEffectsBodySlider_, TBM_GETPOS, 0, 0
    ));
    const int character = static_cast<int>(SendMessageW(
        voiceEffectsCharacterSlider_, TBM_GETPOS, 0, 0
    ));
    const int drive = static_cast<int>(SendMessageW(
        voiceEffectsDriveSlider_, TBM_GETPOS, 0, 0
    ));
    const int dryWet = static_cast<int>(SendMessageW(
        voiceEffectsDryWetSlider_, TBM_GETPOS, 0, 0
    ));
    const float outputGain = static_cast<float>(SendMessageW(
        voiceEffectsOutputGainSlider_, TBM_GETPOS, 0, 0
    )) / 10.0f;

    SetControlText(
        voiceEffectsPitchValue_, FormatSignedTenths(pitch, L" st")
    );
    SetControlText(
        voiceEffectsFormantValue_, FormatSignedTenths(formant, L" st")
    );
    SetControlText(
        voiceEffectsBodyValue_, std::to_wstring(body) + L"%"
    );
    SetControlText(
        voiceEffectsCharacterValue_, std::to_wstring(character) + L"%"
    );
    SetControlText(
        voiceEffectsDriveValue_, std::to_wstring(drive) + L"%"
    );
    SetControlText(
        voiceEffectsDryWetValue_, std::to_wstring(dryWet) + L"%"
    );
    SetControlText(
        voiceEffectsOutputGainValue_,
        FormatSignedTenths(outputGain, L" dB")
    );
}

bool ControlWindow::ReadVoiceEffectSettingsFromControls(
    VoiceEffectSettings& settings,
    const bool showErrors
) const
{
    const int presetIndex = static_cast<int>(SendMessageW(
        voiceEffectsPresetCombo_, CB_GETCURSEL, 0, 0
    ));
    const auto preset = VoiceEffectPresetFromIndex(presetIndex);
    const auto userPresetIndex = SelectedVoiceEffectUserPresetIndex();

    if (!preset.has_value() && !userPresetIndex.has_value())
    {
        if (showErrors)
        {
            MessageBoxW(
                window_,
                Localization::Text(
                    L"Geçerli bir Voice Effects preset'i seçin.",
                    L"Select a valid Voice Effects preset."
                ),
                L"SoundBoardFasaFiso",
                MB_OK | MB_ICONWARNING
            );
        }
        return false;
    }

    settings.enabled = SendMessageW(
        voiceEffectsEnabledCheck_, BM_GETCHECK, 0, 0
    ) == BST_CHECKED;
    settings.bypassed = SendMessageW(
        voiceEffectsBypassCheck_, BM_GETCHECK, 0, 0
    ) == BST_CHECKED;
    settings.preset = userPresetIndex.has_value()
        ? currentConfig_.GetVoiceEffectUserPresets()[*userPresetIndex]
            .settings.preset
        : *preset;
    settings.pitchSemitones = static_cast<float>(SendMessageW(
        voiceEffectsPitchSlider_, TBM_GETPOS, 0, 0
    )) / 10.0f;
    settings.formantSemitones = static_cast<float>(SendMessageW(
        voiceEffectsFormantSlider_, TBM_GETPOS, 0, 0
    )) / 10.0f;
    settings.body = static_cast<float>(SendMessageW(
        voiceEffectsBodySlider_, TBM_GETPOS, 0, 0
    )) / 100.0f;
    settings.character = static_cast<float>(SendMessageW(
        voiceEffectsCharacterSlider_, TBM_GETPOS, 0, 0
    )) / 100.0f;
    settings.drive = static_cast<float>(SendMessageW(
        voiceEffectsDriveSlider_, TBM_GETPOS, 0, 0
    )) / 100.0f;
    settings.dryWet = static_cast<float>(SendMessageW(
        voiceEffectsDryWetSlider_, TBM_GETPOS, 0, 0
    )) / 100.0f;
    settings.outputGainDb = static_cast<float>(SendMessageW(
        voiceEffectsOutputGainSlider_, TBM_GETPOS, 0, 0
    )) / 10.0f;

    if (!IsValidVoiceEffectSettings(settings))
    {
        if (showErrors)
        {
            MessageBoxW(
                window_,
                Localization::Text(
                    L"Voice Effects değerlerinden biri desteklenen aralığın dışında.",
                    L"One of the Voice Effects values is outside the supported range."
                ),
                L"SoundBoardFasaFiso",
                MB_OK | MB_ICONWARNING
            );
        }
        return false;
    }

    return true;
}

void ControlWindow::SubmitVoiceEffectsPreview(const bool showStatus)
{
    if (populatingVoiceEffectsControls_ || audio_ == nullptr)
    {
        return;
    }

    VoiceEffectSettings settings;
    if (!ReadVoiceEffectSettingsFromControls(settings, false))
    {
        SetControlText(
            voiceEffectsStatusValue_,
            Localization::Text(L"Geçersiz ayar", L"Invalid settings")
        );
        return;
    }

    const VoiceEffectSettingsUpdateResult result =
        audio_->UpdateVoiceEffectSettings(settings);

    switch (result)
    {
        case VoiceEffectSettingsUpdateResult::Applied:
            pendingVoiceEffectPreview_.reset();
            SetControlText(
                voiceEffectsStatusValue_,
                Localization::Text(
                    L"Canlı • DSP slotu hazır",
                    L"Live • DSP slot ready"
                )
            );
            if (showStatus)
            {
                SetStatus(Localization::Text(
                    L"Voice Effects önizlemesi güncellendi. Kalıcı olması "
                    L"için Kaydet ve uygula'ya basın.",
                    L"Voice Effects preview updated. Select Save and apply to keep it."
                ));
            }
            break;

        case VoiceEffectSettingsUpdateResult::RuntimeUnavailable:
            pendingVoiceEffectPreview_.reset();
            SetControlText(
                voiceEffectsStatusValue_,
                Localization::Text(
                    L"Kaydet ve uygula gerekli",
                    L"Save and apply required"
                )
            );
            if (showStatus)
            {
                SetStatus(Localization::Text(
                    L"48 kHz mikrofon işleme runtime'ını başlatmak için "
                    L"Kaydet ve uygula'ya basın.",
                    L"Select Save and apply to start the 48 kHz microphone-processing runtime."
                ));
            }
            break;

        case VoiceEffectSettingsUpdateResult::QueueBusy:
            pendingVoiceEffectPreview_ = settings;
            SetControlText(
                voiceEffectsStatusValue_,
                Localization::Text(
                    L"Son değişiklik bekliyor...",
                    L"Latest change pending..."
                )
            );
            break;

        case VoiceEffectSettingsUpdateResult::Invalid:
            pendingVoiceEffectPreview_.reset();
            SetControlText(
                voiceEffectsStatusValue_,
                Localization::Text(L"Geçersiz ayar", L"Invalid settings")
            );
            break;
    }
}

void ControlWindow::RetryPendingVoiceEffectsPreview()
{
    if (!pendingVoiceEffectPreview_.has_value() || audio_ == nullptr)
    {
        return;
    }

    const VoiceEffectSettingsUpdateResult result =
        audio_->UpdateVoiceEffectSettings(*pendingVoiceEffectPreview_);

    if (result == VoiceEffectSettingsUpdateResult::Applied)
    {
        pendingVoiceEffectPreview_.reset();
        SetControlText(
            voiceEffectsStatusValue_,
            Localization::Text(
                L"Canlı • DSP slotu hazır",
                L"Live • DSP slot ready"
            )
        );
    }
    else if (result != VoiceEffectSettingsUpdateResult::QueueBusy)
    {
        pendingVoiceEffectPreview_.reset();
    }
}

void ControlWindow::ResetVoiceEffectsControls()
{
    if (voiceEffectsPresetCombo_ == nullptr)
    {
        return;
    }

    CancelPendingVoiceEffectPresetDelete();

    const bool enabled = SendMessageW(
        voiceEffectsEnabledCheck_, BM_GETCHECK, 0, 0
    ) == BST_CHECKED;
    const bool bypassed = SendMessageW(
        voiceEffectsBypassCheck_, BM_GETCHECK, 0, 0
    ) == BST_CHECKED;
    const int selectedIndex = static_cast<int>(SendMessageW(
        voiceEffectsPresetCombo_, CB_GETCURSEL, 0, 0
    ));
    const auto selectedPreset = VoiceEffectPresetFromIndex(selectedIndex);
    const auto userPresetIndex = SelectedVoiceEffectUserPresetIndex();

    VoiceEffectSettings settings;
    int resultingSelection = VoiceEffectPresetIndex(
        VoiceEffectPreset::Custom
    );

    if (userPresetIndex.has_value())
    {
        settings = ApplyVoiceEffectUserPreset(
            currentConfig_.GetVoiceEffectUserPresets()[*userPresetIndex],
            enabled,
            bypassed
        );
        resultingSelection = selectedIndex;
    }
    else if (selectedPreset.has_value() &&
        *selectedPreset != VoiceEffectPreset::Custom)
    {
        const auto presetSettings = BuildVoiceEffectPreset(
            *selectedPreset,
            enabled
        );
        if (!presetSettings.has_value())
        {
            return;
        }
        settings = *presetSettings;
        settings.bypassed = bypassed;
        resultingSelection = VoiceEffectPresetIndex(*selectedPreset);
    }
    else
    {
        settings.enabled = enabled;
        settings.bypassed = bypassed;
        settings.preset = VoiceEffectPreset::Custom;
        settings.pitchSemitones = 0.0f;
        settings.formantSemitones = 0.0f;
        settings.body = 0.0f;
        settings.character = 0.0f;
        settings.drive = 0.0f;
        settings.dryWet = 1.0f;
        settings.outputGainDb = 0.0f;
    }

    populatingVoiceEffectsControls_ = true;
    SendMessageW(
        voiceEffectsPresetCombo_,
        CB_SETCURSEL,
        static_cast<WPARAM>(resultingSelection),
        0
    );
    SendMessageW(
        voiceEffectsPitchSlider_, TBM_SETPOS, TRUE,
        ToTenths(settings.pitchSemitones)
    );
    SendMessageW(
        voiceEffectsFormantSlider_, TBM_SETPOS, TRUE,
        ToTenths(settings.formantSemitones)
    );
    SendMessageW(
        voiceEffectsBodySlider_, TBM_SETPOS, TRUE,
        ToPercent(settings.body)
    );
    SendMessageW(
        voiceEffectsCharacterSlider_, TBM_SETPOS, TRUE,
        ToPercent(settings.character)
    );
    SendMessageW(
        voiceEffectsDriveSlider_, TBM_SETPOS, TRUE,
        ToPercent(settings.drive)
    );
    SendMessageW(
        voiceEffectsDryWetSlider_, TBM_SETPOS, TRUE,
        ToPercent(settings.dryWet)
    );
    SendMessageW(
        voiceEffectsOutputGainSlider_, TBM_SETPOS, TRUE,
        ToTenths(settings.outputGainDb)
    );
    UpdateVoiceEffectsValueLabels();
    populatingVoiceEffectsControls_ = false;
    UpdateVoiceEffectsPresetButtons();
    SubmitVoiceEffectsPreview();
}

std::optional<std::size_t>
    ControlWindow::SelectedVoiceEffectUserPresetIndex() const
{
    if (voiceEffectsPresetCombo_ == nullptr)
    {
        return std::nullopt;
    }

    const int selectedIndex = static_cast<int>(SendMessageW(
        voiceEffectsPresetCombo_, CB_GETCURSEL, 0, 0
    ));
    const int userIndex = selectedIndex - BuiltInVoiceEffectPresetCount;
    const auto& userPresets = currentConfig_.GetVoiceEffectUserPresets();

    if (userIndex < 0 ||
        static_cast<std::size_t>(userIndex) >= userPresets.size())
    {
        return std::nullopt;
    }

    return static_cast<std::size_t>(userIndex);
}

void ControlWindow::CancelPendingVoiceEffectPresetDelete()
{
    if (!pendingVoiceEffectPresetDeleteName_.has_value())
    {
        return;
    }

    pendingVoiceEffectPresetDeleteName_.reset();
    UpdateVoiceEffectsPresetButtons();
}

void ControlWindow::UpdateVoiceEffectsPresetButtons()
{
    if (voiceEffectsSavePresetButton_ == nullptr ||
        voiceEffectsDeletePresetButton_ == nullptr ||
        voiceEffectsPresetNameEdit_ == nullptr)
    {
        return;
    }

    const std::string name = WideToUtf8(
        GetControlText(voiceEffectsPresetNameEdit_)
    );
    const auto& userPresets = currentConfig_.GetVoiceEffectUserPresets();
    const auto existing = std::find_if(
        userPresets.begin(),
        userPresets.end(),
        [&name](const VoiceEffectUserPreset& preset)
        {
            return VoiceEffectUserPresetNamesEqual(preset.name, name);
        }
    );

    const bool validName = IsValidVoiceEffectUserPresetName(name);
    const bool canAdd = existing != userPresets.end() ||
        userPresets.size() < VoiceEffectLimits::MaximumUserPresetCount;
    EnableWindow(
        voiceEffectsSavePresetButton_,
        validName && canAdd ? TRUE : FALSE
    );

    SetControlText(
        voiceEffectsSavePresetButton_,
        existing != userPresets.end()
            ? Localization::Text(L"Preset'i güncelle", L"Update preset")
            : Localization::Text(L"Preset kaydet", L"Save preset")
    );

    const auto selectedUserIndex = SelectedVoiceEffectUserPresetIndex();
    EnableWindow(
        voiceEffectsDeletePresetButton_,
        selectedUserIndex.has_value() ? TRUE : FALSE
    );

    bool confirmDelete = false;
    if (selectedUserIndex.has_value() &&
        pendingVoiceEffectPresetDeleteName_.has_value())
    {
        confirmDelete = VoiceEffectUserPresetNamesEqual(
            userPresets[*selectedUserIndex].name,
            *pendingVoiceEffectPresetDeleteName_
        );
    }

    SetControlText(
        voiceEffectsDeletePresetButton_,
        confirmDelete
            ? Localization::Text(L"Silmeyi onayla", L"Confirm delete")
            : Localization::Text(L"Preset sil", L"Delete preset")
    );
}

void ControlWindow::SaveVoiceEffectUserPreset()
{
    VoiceEffectSettings settings;
    if (!ReadVoiceEffectSettingsFromControls(settings, false))
    {
        SetControlText(
            voiceEffectsStatusValue_,
            Localization::Text(L"Preset ayarları geçersiz", L"Invalid preset settings")
        );
        return;
    }

    const std::string name = WideToUtf8(
        GetControlText(voiceEffectsPresetNameEdit_)
    );
    VoiceEffectUserPreset preset;
    preset.name = name;
    preset.settings = settings;
    preset.settings.enabled = false;
    preset.settings.bypassed = false;

    if (!IsValidVoiceEffectUserPreset(preset))
    {
        SetControlText(
            voiceEffectsStatusValue_,
            Localization::Text(
                L"Preset adı boş, çok uzun veya geçersiz karakter içeriyor",
                L"Preset name is empty, too long, or contains invalid characters"
            )
        );
        UpdateVoiceEffectsPresetButtons();
        return;
    }

    const auto& existingPresets = currentConfig_
        .GetVoiceEffectUserPresets();
    const bool updating = std::any_of(
        existingPresets.begin(),
        existingPresets.end(),
        [&name](const VoiceEffectUserPreset& existing)
        {
            return VoiceEffectUserPresetNamesEqual(existing.name, name);
        }
    );

    Config candidate = currentConfig_;
    if (!candidate.AddOrUpdateVoiceEffectUserPreset(preset))
    {
        SetControlText(
            voiceEffectsStatusValue_,
            Localization::Text(
                L"En fazla 32 kullanıcı preset'i kaydedilebilir",
                L"A maximum of 32 user presets can be saved"
            )
        );
        UpdateVoiceEffectsPresetButtons();
        return;
    }

    if (!candidate.Save(configPath_))
    {
        SetControlText(
            voiceEffectsStatusValue_,
            Localization::Text(
                L"Preset config dosyasına yazılamadı",
                L"Preset could not be written to the config file"
            )
        );
        return;
    }

    currentConfig_ = candidate;
    pendingVoiceEffectPresetDeleteName_.reset();
    PopulateVoiceEffectsPresetCombo();

    const auto& savedPresets = currentConfig_.GetVoiceEffectUserPresets();
    const auto saved = std::find_if(
        savedPresets.begin(),
        savedPresets.end(),
        [&name](const VoiceEffectUserPreset& existing)
        {
            return VoiceEffectUserPresetNamesEqual(existing.name, name);
        }
    );
    if (saved != savedPresets.end())
    {
        SendMessageW(
            voiceEffectsPresetCombo_,
            CB_SETCURSEL,
            static_cast<WPARAM>(
                BuiltInVoiceEffectPresetCount +
                static_cast<int>(std::distance(
                    savedPresets.begin(),
                    saved
                ))
            ),
            0
        );
        SetControlText(
            voiceEffectsPresetNameEdit_,
            Utf8ToWide(saved->name)
        );
    }

    UpdateVoiceEffectsPresetButtons();
    SetControlText(
        voiceEffectsStatusValue_,
        updating
            ? Localization::Text(L"Kullanıcı preset'i güncellendi", L"User preset updated")
            : Localization::Text(L"Kullanıcı preset'i kaydedildi", L"User preset saved")
    );
    SetStatus(
        updating
            ? Localization::Text(
                L"Voice Effects kullanıcı preset'i güncellendi.",
                L"The Voice Effects user preset was updated."
            )
            : Localization::Text(
                L"Voice Effects kullanıcı preset'i kaydedildi.",
                L"The Voice Effects user preset was saved."
            )
    );
}

void ControlWindow::DeleteVoiceEffectUserPreset()
{
    const auto selectedIndex = SelectedVoiceEffectUserPresetIndex();
    if (!selectedIndex.has_value())
    {
        SetControlText(
            voiceEffectsStatusValue_,
            Localization::Text(
                L"Silmek için bir kullanıcı preset'i seçin",
                L"Select a user preset to delete"
            )
        );
        UpdateVoiceEffectsPresetButtons();
        return;
    }

    const VoiceEffectUserPreset selectedPreset = currentConfig_
        .GetVoiceEffectUserPresets()[*selectedIndex];

    if (!pendingVoiceEffectPresetDeleteName_.has_value() ||
        !VoiceEffectUserPresetNamesEqual(
            *pendingVoiceEffectPresetDeleteName_,
            selectedPreset.name
        ))
    {
        pendingVoiceEffectPresetDeleteName_ = selectedPreset.name;
        UpdateVoiceEffectsPresetButtons();
        SetControlText(
            voiceEffectsStatusValue_,
            Localization::Text(
                L"Silmek için Silmeyi onayla düğmesine tekrar basın",
                L"Select Confirm delete again to remove this preset"
            )
        );
        return;
    }

    VoiceEffectSettings currentSettings;
    if (!ReadVoiceEffectSettingsFromControls(currentSettings, false))
    {
        currentSettings = ApplyVoiceEffectUserPreset(
            selectedPreset,
            false,
            false
        );
    }

    Config candidate = currentConfig_;
    if (!candidate.RemoveVoiceEffectUserPreset(selectedPreset.name) ||
        !candidate.Save(configPath_))
    {
        pendingVoiceEffectPresetDeleteName_.reset();
        UpdateVoiceEffectsPresetButtons();
        SetControlText(
            voiceEffectsStatusValue_,
            Localization::Text(
                L"Preset silinemedi veya config yazılamadı",
                L"Preset could not be deleted or the config could not be written"
            )
        );
        return;
    }

    currentConfig_ = candidate;
    pendingVoiceEffectPresetDeleteName_.reset();
    PopulateVoiceEffectsPresetCombo();

    const VoiceEffectPreset displayedPreset =
        (VoiceEffectSettingsMatchPreset(
                currentSettings,
                currentSettings.preset
            ) ||
            VoiceEffectPresetHasDedicatedStage(currentSettings.preset))
            ? currentSettings.preset
            : VoiceEffectPreset::Custom;
    SendMessageW(
        voiceEffectsPresetCombo_,
        CB_SETCURSEL,
        static_cast<WPARAM>(VoiceEffectPresetIndex(displayedPreset)),
        0
    );
    SetControlText(voiceEffectsPresetNameEdit_, L"");
    UpdateVoiceEffectsPresetButtons();
    SetControlText(
        voiceEffectsStatusValue_,
        Localization::Text(L"Kullanıcı preset'i silindi", L"User preset deleted")
    );
    SetStatus(Localization::Text(
        L"Voice Effects kullanıcı preset'i silindi.",
        L"The Voice Effects user preset was deleted."
    ));
}

bool ControlWindow::IsVoiceEffectsSlider(const HWND control) const noexcept
{
    return control == voiceEffectsPitchSlider_ ||
        control == voiceEffectsFormantSlider_ ||
        control == voiceEffectsBodySlider_ ||
        control == voiceEffectsCharacterSlider_ ||
        control == voiceEffectsDriveSlider_ ||
        control == voiceEffectsDryWetSlider_ ||
        control == voiceEffectsOutputGainSlider_;
}
