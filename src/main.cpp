#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app/Version.hpp"
#include "audio/Audio.hpp"
#include "config/Config.hpp"
#include "diagnostics/Logger.hpp"
#include "gui/ControlWindow.hpp"
#include "hotkeys/HotkeyManager.hpp"
#include "localization/Localization.hpp"
#include "platform/DebugConsole.hpp"
#include "platform/SingleInstance.hpp"
#include "platform/StartupManager.hpp"
#include "platform/Utf8Path.hpp"
#include "tray/TrayIcon.hpp"

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    constexpr int FirstSoundHotkeyId = 100;
    constexpr int ApplySettingsCommandId = 992;
    constexpr int OpenControlPanelCommandId = 993;
    constexpr int ToggleConsoleCommandId = 994;
    constexpr int OutputMuteHotkeyId = 995;
    constexpr int MonitorMuteHotkeyId = 996;
    constexpr int ReloadHotkeyId = 997;
    constexpr int StopHotkeyId = 998;
    constexpr int ExitHotkeyId = 999;

    std::optional<std::filesystem::path> GetExecutablePath()
    {
        constexpr std::size_t MaximumPathCharacters = 32768;

        std::vector<wchar_t> pathBuffer(512);

        while (true)
        {
            const DWORD pathLength = GetModuleFileNameW(
                nullptr,
                pathBuffer.data(),
                static_cast<DWORD>(pathBuffer.size())
            );

            if (pathLength == 0)
            {
                std::cerr
                    << Localization::Text(
                        "EXE yolu alınamadı. Windows hata kodu: ",
                        "The executable path could not be read. Windows error code: "
                    )
                    << GetLastError()
                    << '\n';

                return std::nullopt;
            }

            if (static_cast<std::size_t>(pathLength) < pathBuffer.size())
            {
                const std::filesystem::path executablePath{
                    std::wstring(
                        pathBuffer.data(),
                        static_cast<std::size_t>(pathLength)
                    )
                };

                if (!executablePath.has_parent_path())
                {
                    std::cerr << Localization::Text(
                        "EXE klasörü belirlenemedi.\n",
                        "The executable folder could not be determined.\n"
                    );
                    return std::nullopt;
                }

                return executablePath;
            }

            if (pathBuffer.size() >= MaximumPathCharacters)
            {
                std::cerr << Localization::Text(
                    "EXE yolu desteklenen uzunluğu aşıyor.\n",
                    "The executable path exceeds the supported length.\n"
                );
                return std::nullopt;
            }

            const std::size_t nextBufferSize =
                pathBuffer.size() * 2 > MaximumPathCharacters
                    ? MaximumPathCharacters
                    : pathBuffer.size() * 2;

            pathBuffer.resize(nextBufferSize);
        }
    }

    bool ApplyStartupSetting(
        const Config& config,
        const std::filesystem::path& executablePath
    )
    {
        const LSTATUS result = StartupManager::SetEnabled(
            config.GetStartWithWindows(),
            executablePath
        );

        if (result == ERROR_SUCCESS)
        {
            return true;
        }

        std::cerr
            << Localization::Text(
                "Windows başlangıç ayarı güncellenemedi. Hata kodu: ",
                "The Windows startup setting could not be updated. Error code: "
            )
            << result
            << '\n';

        return false;
    }

    void PrintConfigSummary(const Config& config)
    {
        std::cout
            << Localization::Text(
                "\nİstenen ana çıkış: ",
                "\nRequested main output: "
            )
            << config.GetOutputDevice()
            << '\n';

        std::cout
            << Localization::Text(
                "Ana çıkış seviyesi: ",
                "Main output volume: "
            )
            << static_cast<int>(config.GetOutputVolume() * 100.0f)
            << "%\n";

        std::cout
            << Localization::Text(
                "İstenen monitör çıkışı: ",
                "Requested monitor output: "
            )
            << config.GetMonitorDevice()
            << '\n';

        std::cout
            << Localization::Text(
                "Monitör seviyesi: ",
                "Monitor volume: "
            )
            << static_cast<int>(config.GetMonitorVolume() * 100.0f)
            << "%\n";

        std::cout
            << Localization::Text(
                "Mikrofon miksi: ",
                "Microphone mix: "
            )
            << (config.GetMicrophoneEnabled()
                ? Localization::Text("Açık", "Enabled")
                : Localization::Text("Kapalı", "Disabled"))
            << '\n';

        if (config.GetMicrophoneEnabled())
        {
            std::cout
                << Localization::Text(
                    "İstenen mikrofon: ",
                    "Requested microphone: "
                )
                << config.GetMicrophoneDevice()
                << '\n'
                << Localization::Text(
                    "Mikrofon seviyesi: ",
                    "Microphone volume: "
                )
                << static_cast<int>(
                    config.GetMicrophoneVolume() * 100.0f
                )
                << "%\n"
                << Localization::Text(
                    "Mikrofon yönlendirmesi: ",
                    "Microphone routing: "
                );

            if (config.GetMicrophoneToOutput())
            {
                std::cout << Localization::Text(
                    "ana çıkış",
                    "main output"
                );
            }

            if (config.GetMicrophoneToOutput() &&
                config.GetMicrophoneToMonitor())
            {
                std::cout << " + ";
            }

            if (config.GetMicrophoneToMonitor())
            {
                std::cout << Localization::Text(
                    "monitör",
                    "monitor"
                );
            }

            std::cout << '\n';
        }

        std::cout
            << Localization::Text(
                "İstenen örnekleme hızı: ",
                "Requested sample rate: "
            )
            << (config.GetAudioSampleRate() == 0
                ? std::string{Localization::Text(
                    "cihaz doğal hızı",
                    "device native rate"
                )}
                : std::to_string(config.GetAudioSampleRate()) + " Hz")
            << '\n';

        std::cout
            << Localization::Text(
                "İstenen audio buffer: ",
                "Requested audio buffer: "
            )
            << (config.GetAudioBufferMilliseconds() == 0
                ? std::string{Localization::Text(
                    "Windows/miniaudio varsayılanı",
                    "Windows/miniaudio default"
                )}
                : std::to_string(
                    config.GetAudioBufferMilliseconds()
                ) + " ms")
            << '\n';

        std::cout
            << Localization::Text(
                "Windows ile başlat: ",
                "Start with Windows: "
            )
            << (config.GetStartWithWindows()
                ? Localization::Text("Açık", "Enabled")
                : Localization::Text("Kapalı", "Disabled"))
            << '\n'
            << Localization::Text(
                "Hata ayıklama konsolu: yalnızca elle açılır\n",
                "Debug console: manual only\n"
            )
            << Localization::Text(
                "Başlangıçta güncelleme denetimi: ",
                "Update check on startup: "
            )
            << (config.GetCheckUpdatesOnStart()
                ? Localization::Text("Açık", "Enabled")
                : Localization::Text("Kapalı", "Disabled"))
            << '\n';
    }

    bool BuildRuntime(
        const Config& config,
        const std::filesystem::path& soundsFolder,
        Audio& audio,
        HotkeyManager& hotkeys,
        std::vector<SoundBinding>& activeBindings,
        const bool requireAllBindings = false
    )
    {
        activeBindings.clear();
        bool bindingError = false;

        PrintConfigSummary(config);

        if (!audio.Initialize(
            config.GetOutputDevice(),
            config.GetMonitorDevice(),
            config.GetOutputVolume(),
            config.GetMonitorVolume(),
            config.GetMicrophoneEnabled(),
            config.GetMicrophoneDevice(),
            config.GetMicrophoneVolume(),
            config.GetMicrophoneToOutput(),
            config.GetMicrophoneToMonitor(),
            config.GetAudioSampleRate(),
            config.GetAudioBufferMilliseconds()
        ))
        {
            std::cerr << Localization::Text(
                "Ses sistemi başlatılamadı.\n",
                "The audio system could not be initialized.\n"
            );
            return false;
        }

        std::cout
            << Localization::Text(
                "\nSesler ana ve monitör çıkışları için RAM'e yükleniyor...\n",
                "\nLoading sounds into RAM for the main and monitor outputs...\n"
            );

        std::cout << Localization::Text(
                "\nHotkey atamaları:\n",
                "\nHotkey assignments:\n"
            );

        for (const SoundBinding& binding : config.GetBindings())
        {
            std::filesystem::path soundPath =
                soundsFolder / binding.soundFile;

            soundPath = soundPath.lexically_normal();

            if (!std::filesystem::exists(soundPath) ||
                !std::filesystem::is_regular_file(soundPath))
            {
                std::cerr
                    << Localization::Text(
                        "Ses dosyası bulunamadı: ",
                        "Sound file not found: "
                    )
                    << PathToUtf8(soundPath)
                    << '\n';

                bindingError = true;
                continue;
            }

            if (!audio.LoadSound(
                binding.keyName,
                soundPath,
                binding.volume,
                binding.mode
            ))
            {
                std::cerr
                    << Localization::Text(
                        "Ses yüklenemedi: ",
                        "Sound could not be loaded: "
                    )
                    << PathToUtf8(binding.soundFile)
                    << '\n';

                bindingError = true;
                continue;
            }

            const int hotkeyId =
                FirstSoundHotkeyId +
                static_cast<int>(activeBindings.size());

            if (!hotkeys.Register(
                hotkeyId,
                binding.modifiers,
                binding.virtualKey
            ))
            {
                std::cerr
                    << binding.keyName
                    << Localization::Text(
                        " hotkey olarak kaydedilemedi.\n",
                        " could not be registered as a hotkey.\n"
                    );

                bindingError = true;
                continue;
            }

            SoundBinding activeBinding = binding;
            activeBinding.soundFile = std::move(soundPath);

            activeBindings.push_back(std::move(activeBinding));

            std::cout
                << binding.keyName
                << " -> "
                << PathToUtf8(binding.soundFile)
                << " [RAM, volume="
                << static_cast<int>(binding.volume * 100.0f)
                << "%, mode="
                << PlaybackModeName(binding.mode)
                << "]\n";

        }

        if (requireAllBindings && bindingError)
        {
            std::cerr
                << Localization::Text(
                    "Yeni config tamamen uygulanamadı. Eski ayarlara dönülecek.\n",
                    "The new config could not be applied completely. The previous settings will be restored.\n"
                );

            return false;
        }

        if (activeBindings.empty())
        {
            std::cerr
                << Localization::Text(
                    "Kullanılabilir ses veya hotkey bulunamadı.\n",
                    "No usable sound or hotkey was found.\n"
                );

            return false;
        }

        if (!hotkeys.Register(
            StopHotkeyId,
            config.GetStopModifiers(),
            config.GetStopVirtualKey()
        ))
        {
            std::cerr
                << config.GetStopKeyName()
                << Localization::Text(
                    " durdurma hotkey'i kaydedilemedi.\n",
                    " could not be registered as the stop hotkey.\n"
                );

            return false;
        }

        if (!hotkeys.Register(
            OutputMuteHotkeyId,
            config.GetOutputMuteModifiers(),
            config.GetOutputMuteVirtualKey()
        ))
        {
            std::cerr
                << config.GetOutputMuteKeyName()
                << Localization::Text(
                    " ana çıkış mute hotkey'i kaydedilemedi.\n",
                    " could not be registered as the main output mute hotkey.\n"
                );

            return false;
        }

        if (!hotkeys.Register(
            MonitorMuteHotkeyId,
            config.GetMonitorMuteModifiers(),
            config.GetMonitorMuteVirtualKey()
        ))
        {
            std::cerr
                << config.GetMonitorMuteKeyName()
                << Localization::Text(
                    " monitör mute hotkey'i kaydedilemedi.\n",
                    " could not be registered as the monitor mute hotkey.\n"
                );

            return false;
        }

        if (!hotkeys.Register(
            ReloadHotkeyId,
            config.GetReloadModifiers(),
            config.GetReloadVirtualKey()
        ))
        {
            std::cerr
                << config.GetReloadKeyName()
                << Localization::Text(
                    " reload hotkey'i kaydedilemedi.\n",
                    " could not be registered as the reload hotkey.\n"
                );

            return false;
        }

        if (!hotkeys.Register(
            ExitHotkeyId,
            config.GetExitModifiers(),
            config.GetExitVirtualKey()
        ))
        {
            std::cerr
                << config.GetExitKeyName()
                << Localization::Text(
                    " çıkış hotkey'i kaydedilemedi.\n",
                    " could not be registered as the exit hotkey.\n"
                );

            return false;
        }

        std::cout
            << config.GetStopKeyName()
            << Localization::Text(
                " -> Tüm sesleri durdur\n",
                " -> Stop all sounds\n"
            );

        std::cout
            << config.GetOutputMuteKeyName()
            << Localization::Text(
                " -> Ana çıkışı mute/unmute\n",
                " -> Mute/unmute main output\n"
            );

        std::cout
            << config.GetMonitorMuteKeyName()
            << Localization::Text(
                " -> Monitör çıkışını mute/unmute\n",
                " -> Mute/unmute monitor output\n"
            );

        std::cout
            << config.GetReloadKeyName()
            << Localization::Text(
                " -> Config'i yeniden yükle\n",
                " -> Reload config\n"
            );

        std::cout
            << config.GetExitKeyName()
            << Localization::Text(
                " -> Programı kapat\n",
                " -> Exit the program\n"
            );

        return true;
    }

    void ClearRuntime(
        Audio& audio,
        HotkeyManager& hotkeys,
        std::vector<SoundBinding>& activeBindings
    )
    {
        audio.StopAll();
        hotkeys.UnregisterAll();
        audio.Shutdown();
        activeBindings.clear();
    }
}

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR,
    int
)
{

    const auto executablePath = GetExecutablePath();

    if (!executablePath.has_value())
    {
        MessageBoxW(
            nullptr,
            L"The executable path could not be determined.",
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }

    const std::filesystem::path programFolder =
        executablePath->parent_path();
    const std::filesystem::path soundsFolder =
        programFolder / "sounds";
    const std::filesystem::path logsFolder =
        programFolder / "logs";
    const std::filesystem::path configPath =
        programFolder / "config.txt";

    std::filesystem::path pendingConfigPath = configPath;
    pendingConfigPath += L".pending";

    Logger logger;

    if (!logger.Initialize(logsFolder))
    {
        MessageBoxW(
            nullptr,
            L"Persistent log system could not be initialized.",
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONWARNING
        );
    }

    Config config;

    if (!config.Load(configPath))
    {
        MessageBoxW(
            nullptr,
            Localization::Text(
                L"Config yüklenemedi. Ayrıntılar logs\\latest.log dosyasında.",
                L"The config could not be loaded. Details are in logs\\latest.log."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }

    SingleInstance singleInstance;
    const SingleInstanceResult instanceResult =
        singleInstance.Acquire(
            L"Local\\SoundBoardFasaFiso.SingleInstance"
        );

    if (instanceResult == SingleInstanceResult::AlreadyRunning)
    {
        MessageBoxW(
            nullptr,
            Localization::Text(
                L"SoundBoardFasaFiso zaten çalışıyor.",
                L"SoundBoardFasaFiso is already running."
            ),
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONINFORMATION
        );

        return 0;
    }

    if (instanceResult == SingleInstanceResult::Failed)
    {
        std::cerr
            << Localization::Text(
                "Tek uygulama kilidi oluşturulamadı. Windows hata kodu: ",
                "The single-instance lock could not be created. Windows error code: "
            )
            << GetLastError()
            << '\n';

        return 1;
    }

    if (!ApplyStartupSetting(config, *executablePath))
    {
        std::cerr << Localization::Text(
            "Program çalışmaya devam edecek; başlangıç ayarını GUI'den tekrar deneyebilirsin.\n",
            "The program will continue; you can retry the startup setting from the GUI.\n"
        );
    }

    std::cout << "================================\n";
    std::cout << "SoundBoardFasaFiso v" << AppVersion::String << '\n';
    std::cout << "================================\n";

    std::cout
        << Localization::Text(
            "Kullanılan config: ",
            "Config in use: "
        )
        << PathToUtf8(configPath)
        << '\n';

    if (!logger.GetLogPath().empty())
    {
        std::cout
            << Localization::Text(
                "Oturum logu: ",
                "Session log: "
            )
            << PathToUtf8(logger.GetLogPath())
            << '\n';
    }

    Audio audio;
    HotkeyManager hotkeys;
    std::vector<SoundBinding> activeBindings;

    if (!BuildRuntime(
        config,
        soundsFolder,
        audio,
        hotkeys,
        activeBindings
    ))
    {
        ClearRuntime(audio, hotkeys, activeBindings);
        return 1;
    }

    ControlWindow controlWindow;

    const ControlWindowCommandIds controlWindowCommandIds{
        .applySettings = ApplySettingsCommandId,
        .stop = StopHotkeyId,
        .outputMute = OutputMuteHotkeyId,
        .monitorMute = MonitorMuteHotkeyId,
        .reload = ReloadHotkeyId,
        .toggleConsole = ToggleConsoleCommandId,
        .exit = ExitHotkeyId
    };

    if (controlWindow.Initialize(
        config,
        configPath,
        soundsFolder,
        Audio::EnumeratePlaybackDevices(),
        Audio::EnumerateCaptureDevices(),
        controlWindowCommandIds,
        &audio
    ))
    {
        controlWindow.Show();

        if (config.GetCheckUpdatesOnStart())
        {
            controlWindow.CheckForUpdates(false);
        }

        std::cout
            << Localization::Text(
                "Kontrol paneli hazır. Pencereyi kapatmak uygulamayı kapatmaz; tray'e gizler.\n",
                "The control panel is ready. Closing the window hides it to the tray without exiting.\n"
            );
    }
    else
    {
        std::cerr
            << Localization::Text(
                "Kontrol paneli başlatılamadı. Soundboard tray üzerinden çalışmaya devam edecek. Ayrıntılar log dosyasında.\n",
                "The control panel could not be initialized. The soundboard will continue through the tray. Details are in the log file.\n"
            );
    }

    TrayIcon trayIcon;

    const TrayCommandIds trayCommandIds{
        .controlPanel = OpenControlPanelCommandId,
        .stop = StopHotkeyId,
        .outputMute = OutputMuteHotkeyId,
        .monitorMute = MonitorMuteHotkeyId,
        .reload = ReloadHotkeyId,
        .toggleConsole = ToggleConsoleCommandId,
        .exit = ExitHotkeyId
    };

    if (trayIcon.Initialize(
        L"SoundBoardFasaFiso",
        trayCommandIds
    ))
    {
        std::cout
            << Localization::Text(
                "Tray icon hazır. Sağ tıklayarak menüyü açabilirsin.\nTray icon'a çift tıklayarak kontrol panelini açabilirsin.\n",
                "Tray icon is ready. Right-click it to open the menu.\nDouble-click the tray icon to open the control panel.\n"
            );
    }
    else
    {
        std::cerr
            << Localization::Text(
                "Tray icon başlatılamadı. Kontrol panelini kapatma; ayrıntılar log dosyasında.\n",
                "The tray icon could not be initialized. Keep the control panel open; details are in the log file.\n"
            );
    }

    DebugConsole::Hide();

    std::cout
        << Localization::Text(
            "\nSoundboard hazır.\nSesler ana çıkışa ve monitör çıkışına aynı anda gönderilecek.\n",
            "\nSoundboard ready.\nSounds will be sent to the main and monitor outputs at the same time.\n"
        );

    bool running = true;
    bool audioRecoveryWarningShown = false;

    auto nextAudioConnectionCheck =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(1);

    while (running)
    {
        const int hotkeyId = hotkeys.WaitForPress(
            250,
            controlWindow.NativeHandle(),
            controlWindow.AcceleratorTable()
        );

        if (hotkeyId < 0)
        {
            break;
        }

        const auto currentTime =
            std::chrono::steady_clock::now();

        if (currentTime >= nextAudioConnectionCheck)
        {
            nextAudioConnectionCheck =
                currentTime + std::chrono::seconds(3);

            const AudioRecoveryResult recoveryResult =
                audio.MaintainDeviceConnection();

            if (recoveryResult == AudioRecoveryResult::Failed)
            {
                if (!audioRecoveryWarningShown)
                {
                    std::cerr
                        << Localization::Text(
                            "\nSes cihazı bağlantısı koptu. Program açık kalacak ve 3 saniyede bir yeniden deneyecek.\n",
                            "\nThe audio device connection was lost. The program will stay open and retry every 3 seconds.\n"
                        );

                    controlWindow.SetStatus(
                        Localization::Text(
                            L"Ses cihazı bağlantısı koptu; yeniden bağlanma bekleniyor.",
                            L"Audio device connection lost; waiting to reconnect."
                        )
                    );

                    audioRecoveryWarningShown = true;
                }
            }
            else if (
                recoveryResult == AudioRecoveryResult::Recovered
            )
            {
                std::cout
                    << Localization::Text(
                        "\nSes sistemi yeniden başlatıldı.\nSesler RAM'e tekrar yüklendi.\nSoundboard hazır.\n",
                        "\nThe audio system was restarted.\nSounds were loaded into RAM again.\nSoundboard ready.\n"
                    );

                controlWindow.SetStatus(
                    Localization::Text(
                        L"Ses sistemi yeniden bağlandı. Soundboard hazır.",
                        L"Audio system reconnected. Soundboard ready."
                    )
                );

                audioRecoveryWarningShown = false;
            }
        }

        if (hotkeyId == 0)
        {
            continue;
        }

        if (hotkeyId == ExitHotkeyId)
        {
            running = false;
            continue;
        }

        if (hotkeyId == OpenControlPanelCommandId)
        {
            controlWindow.Show();
            continue;
        }

        if (hotkeyId == ToggleConsoleCommandId)
        {
            if (!DebugConsole::ToggleVisibility())
            {
                std::cerr
                    << Localization::Text(
                        "Hata ayıklama konsolu açılamadı.\n",
                        "The debug console could not be opened.\n"
                    );
            }

            continue;
        }

        if (hotkeyId == ApplySettingsCommandId)
        {
            std::cout << Localization::Text(
                "\nGUI ayarları doğrulanıyor ve uygulanıyor...\n",
                "\nValidating and applying GUI settings...\n"
            );

            const Language oldLanguage = config.GetLanguage();
            const Config oldConfig = config;
            Config newConfig;

            if (!newConfig.Load(pendingConfigPath))
            {
                Localization::SetLanguage(oldLanguage);

                std::error_code removeError;
                std::filesystem::remove(
                    pendingConfigPath,
                    removeError
                );

                std::cerr << Localization::Text(
                    "GUI ayarları geçersiz. Önceki ayarlar korunuyor.\n",
                    "The GUI settings are invalid. Previous settings are being kept.\n"
                );

                controlWindow.UpdateConfig(config);
                controlWindow.SetStatus(Localization::Text(
                    L"Yeni ayarlar geçersiz; önceki ayarlar korunuyor.",
                    L"New settings are invalid; previous settings kept."
                ));
                continue;
            }

            ClearRuntime(audio, hotkeys, activeBindings);

            const bool runtimeStarted = BuildRuntime(
                newConfig,
                soundsFolder,
                audio,
                hotkeys,
                activeBindings,
                true
            );

            const bool startupSettingApplied =
                runtimeStarted &&
                ApplyStartupSetting(newConfig, *executablePath);

            const bool configSaved =
                startupSettingApplied && newConfig.Save(configPath);

            std::error_code removeError;
            std::filesystem::remove(
                pendingConfigPath,
                removeError
            );

            if (runtimeStarted && startupSettingApplied && configSaved)
            {
                config = std::move(newConfig);
                audioRecoveryWarningShown = false;
                nextAudioConnectionCheck =
                    std::chrono::steady_clock::now() +
                    std::chrono::seconds(1);

                std::cout << Localization::Text(
                    "GUI ayarları kaydedildi ve uygulandı.\nSoundboard hazır.\n",
                    "GUI settings were saved and applied.\nSoundboard ready.\n"
                );

                controlWindow.UpdateConfig(config);
                controlWindow.SetStatus(Localization::Text(
                    L"Ayarlar kaydedildi ve uygulandı. Soundboard hazır.",
                    L"Settings saved and applied. Soundboard ready."
                ));
                continue;
            }

            Localization::SetLanguage(oldLanguage);

            if (runtimeStarted && !startupSettingApplied)
            {
                std::cerr << Localization::Text(
                    "Windows başlangıç ayarı uygulanamadı. Önceki ayarlar geri yükleniyor...\n",
                    "The Windows startup setting could not be applied. Restoring previous settings...\n"
                );
            }
            else if (runtimeStarted && !configSaved)
            {
                std::cerr << Localization::Text(
                    "Yeni ayarlar çalıştı ancak config dosyasına kaydedilemedi. Önceki ayarlar geri yükleniyor...\n",
                    "The new settings worked but could not be saved to the config file. Restoring previous settings...\n"
                );
            }
            else
            {
                std::cerr << Localization::Text(
                    "Yeni ayarlar başlatılamadı. Önceki ayarlar geri yükleniyor...\n",
                    "The new settings could not be initialized. Restoring previous settings...\n"
                );
            }

            ClearRuntime(audio, hotkeys, activeBindings);
            ApplyStartupSetting(oldConfig, *executablePath);

            if (!BuildRuntime(
                oldConfig,
                soundsFolder,
                audio,
                hotkeys,
                activeBindings
            ))
            {
                std::cerr << Localization::Text(
                    "Önceki ayarlar geri yüklenemedi. Program kapatılıyor.\n",
                    "The previous settings could not be restored. The program is shutting down.\n"
                );

                controlWindow.SetStatus(Localization::Text(
                    L"Ayarlar geri yüklenemedi; program kapatılıyor.",
                    L"Settings could not be restored; shutting down."
                ));
                running = false;
                continue;
            }

            config = oldConfig;
            audioRecoveryWarningShown = false;
            nextAudioConnectionCheck =
                std::chrono::steady_clock::now() +
                std::chrono::seconds(1);

            controlWindow.UpdateConfig(config);
            controlWindow.SetStatus(
                runtimeStarted
                    ? Localization::Text(
                        L"Config kaydedilemedi; önceki ayarlar geri yüklendi.",
                        L"Config could not be saved; previous settings restored."
                    )
                    : Localization::Text(
                        L"Yeni ayarlar uygulanamadı; önceki ayarlar geri yüklendi.",
                        L"New settings failed; previous settings restored."
                    )
            );
            continue;
        }

        if (hotkeyId == ReloadHotkeyId)
        {
            std::cout << Localization::Text(
                "\nConfig yeniden yükleniyor...\n",
                "\nReloading config...\n"
            );

            controlWindow.SetStatus(
                Localization::Text(
                    L"Config yeniden yükleniyor...",
                    L"Reloading config..."
                )
            );

            const Language oldLanguage = config.GetLanguage();
            Config newConfig;

            if (!newConfig.Load(configPath))
            {
                Localization::SetLanguage(oldLanguage);
                std::cerr
                    << Localization::Text(
                        "Reload başarısız. Eski ayarlar kullanılmaya devam ediyor.\n",
                        "Reload failed. The previous settings will continue to be used.\n"
                    );

                controlWindow.SetStatus(
                    Localization::Text(
                        L"Reload başarısız; önceki ayarlar korunuyor.",
                        L"Reload failed; previous settings kept."
                    )
                );

                continue;
            }

            const Config oldConfig = config;

            ClearRuntime(audio, hotkeys, activeBindings);

            const bool runtimeStarted = BuildRuntime(
                newConfig,
                soundsFolder,
                audio,
                hotkeys,
                activeBindings,
                true
            );
            const bool startupSettingApplied =
                runtimeStarted &&
                ApplyStartupSetting(newConfig, *executablePath);

            if (runtimeStarted && startupSettingApplied)
            {
                config = std::move(newConfig);
                audioRecoveryWarningShown = false;
                nextAudioConnectionCheck =
                    std::chrono::steady_clock::now() +
                    std::chrono::seconds(1);

                std::cout
                    << Localization::Text(
                        "\nConfig başarıyla yeniden yüklendi.\nSoundboard hazır.\n",
                        "\nConfig reloaded successfully.\nSoundboard ready.\n"
                    );

                controlWindow.UpdateConfig(config);
                controlWindow.SetStatus(
                    Localization::Text(
                        L"Config başarıyla yenilendi. Soundboard hazır.",
                        L"Config reloaded successfully. Soundboard ready."
                    )
                );

                continue;
            }

            Localization::SetLanguage(oldLanguage);

            std::cerr
                << (runtimeStarted && !startupSettingApplied
                    ? Localization::Text(
                        "Windows başlangıç ayarı uygulanamadı. Eski ayarlar geri yükleniyor...\n",
                        "The Windows startup setting could not be applied. Restoring the previous settings...\n"
                    )
                    : Localization::Text(
                        "Yeni ayarlar başlatılamadı. Eski ayarlar geri yükleniyor...\n",
                        "The new settings could not be initialized. Restoring the previous settings...\n"
                    ));

            controlWindow.SetStatus(
                Localization::Text(
                    L"Yeni ayarlar uygulanamadı; önceki ayarlar geri yükleniyor...",
                    L"New settings failed; restoring previous settings..."
                )
            );

            ClearRuntime(audio, hotkeys, activeBindings);
            ApplyStartupSetting(oldConfig, *executablePath);

            if (!BuildRuntime(
                oldConfig,
                soundsFolder,
                audio,
                hotkeys,
                activeBindings
            ))
            {
                std::cerr
                    << Localization::Text(
                        "Eski ayarlar da geri yüklenemedi. Program kapatılıyor.\n",
                        "The previous settings could not be restored either. The program is shutting down.\n"
                    );

                controlWindow.SetStatus(
                    Localization::Text(
                        L"Ayarlar geri yüklenemedi; program kapatılıyor.",
                        L"Settings could not be restored; shutting down."
                    )
                );

                running = false;
                continue;
            }

            config = oldConfig;
            audioRecoveryWarningShown = false;
            nextAudioConnectionCheck =
                std::chrono::steady_clock::now() +
                std::chrono::seconds(1);

            std::cout
                << Localization::Text(
                    "Eski ayarlar geri yüklendi.\nSoundboard hazır.\n",
                    "The previous settings were restored.\nSoundboard ready.\n"
                );

            controlWindow.UpdateConfig(config);
            controlWindow.SetStatus(
                Localization::Text(
                    L"Önceki ayarlar geri yüklendi. Soundboard hazır.",
                    L"Previous settings restored. Soundboard ready."
                )
            );

            continue;
        }

        if (hotkeyId == OutputMuteHotkeyId)
        {
            const MuteToggleResult result =
                audio.ToggleOutputMute();

            if (result == MuteToggleResult::Muted)
            {
                std::cout << Localization::Text("Ana çıkış susturuldu.\n", "Main output muted.\n");
                controlWindow.SetStatus(Localization::Text(L"Ana çıkış susturuldu.", L"Main output muted."));
            }
            else if (result == MuteToggleResult::Unmuted)
            {
                std::cout << Localization::Text("Ana çıkış sesi açıldı.\n", "Main output unmuted.\n");
                controlWindow.SetStatus(Localization::Text(L"Ana çıkış sesi açıldı.", L"Main output unmuted."));
            }
            else if (result == MuteToggleResult::Unavailable)
            {
                std::cerr << Localization::Text("Ana çıkış şu anda kullanılamıyor.\n", "Main output is currently unavailable.\n");
                controlWindow.SetStatus(Localization::Text(L"Ana çıkış kullanılamıyor.", L"Main output unavailable."));
            }
            else
            {
                std::cerr
                    << Localization::Text("Ana çıkış mute durumu değiştirilemedi.\n", "Main output mute state could not be changed.\n");
                controlWindow.SetStatus(Localization::Text(L"Ana çıkış mute durumu değiştirilemedi.", L"Main output mute state could not be changed."));
            }

            continue;
        }

        if (hotkeyId == MonitorMuteHotkeyId)
        {
            const MuteToggleResult result =
                audio.ToggleMonitorMute();

            if (result == MuteToggleResult::Muted)
            {
                std::cout << Localization::Text("Monitör çıkışı susturuldu.\n", "Monitor output muted.\n");
                controlWindow.SetStatus(Localization::Text(L"Monitör çıkışı susturuldu.", L"Monitor output muted."));
            }
            else if (result == MuteToggleResult::Unmuted)
            {
                std::cout << Localization::Text("Monitör çıkışı sesi açıldı.\n", "Monitor output unmuted.\n");
                controlWindow.SetStatus(Localization::Text(L"Monitör çıkışı sesi açıldı.", L"Monitor output unmuted."));
            }
            else if (result == MuteToggleResult::Unavailable)
            {
                std::cerr
                    << Localization::Text("Monitör çıkışı kapalı veya kullanılamıyor.\n", "Monitor output is disabled or unavailable.\n");
                controlWindow.SetStatus(Localization::Text(L"Monitör çıkışı kapalı veya kullanılamıyor.", L"Monitor output disabled or unavailable."));
            }
            else
            {
                std::cerr
                    << Localization::Text("Monitör mute durumu değiştirilemedi.\n", "Monitor mute state could not be changed.\n");
                controlWindow.SetStatus(Localization::Text(L"Monitör mute durumu değiştirilemedi.", L"Monitor mute state could not be changed."));
            }

            continue;
        }

        if (hotkeyId == StopHotkeyId)
        {
            if (audio.StopAll())
            {
                std::cout << Localization::Text("Tüm sesler durduruldu.\n", "All sounds stopped.\n");
                controlWindow.SetStatus(Localization::Text(L"Tüm sesler durduruldu.", L"All sounds stopped."));
            }
            else
            {
                std::cerr << Localization::Text("Bazı sesler durdurulamadı.\n", "Some sounds could not be stopped.\n");
                controlWindow.SetStatus(Localization::Text(L"Bazı sesler durdurulamadı.", L"Some sounds could not be stopped."));
            }

            continue;
        }

        const int bindingIndex =
            hotkeyId - FirstSoundHotkeyId;

        if (bindingIndex < 0)
        {
            continue;
        }

        const std::size_t index =
            static_cast<std::size_t>(bindingIndex);

        if (index >= activeBindings.size())
        {
            continue;
        }

        const SoundBinding& binding = activeBindings[index];

        const PlaybackResult playbackResult =
            audio.PlayLoaded(binding.keyName);

        if (playbackResult == PlaybackResult::Started)
        {
            std::cout
                << Localization::Text("Çalınıyor: ", "Playing: ")
                << PathToUtf8(binding.soundFile.stem())
                << " ["
                << PlaybackModeName(binding.mode)
                << "]\n";

            controlWindow.SetStatus(
                Localization::Text(L"Ses çalınıyor.", L"Sound is playing.")
            );
        }
        else if (playbackResult == PlaybackResult::Stopped)
        {
            std::cout
                << Localization::Text("Durduruldu: ", "Stopped: ")
                << PathToUtf8(binding.soundFile.stem())
                << '\n';

            controlWindow.SetStatus(
                Localization::Text(L"Ses durduruldu.", L"Sound stopped.")
            );
        }
        else if (playbackResult == PlaybackResult::Failed)
        {
            std::cerr
                << Localization::Text("Ses çalınamadı: ", "Sound could not be played: ")
                << PathToUtf8(binding.soundFile.stem())
                << '\n';

            controlWindow.SetStatus(
                Localization::Text(
                    L"Ses çalınamadı.",
                    L"Sound could not be played."
                )
            );
        }
    }

    controlWindow.Shutdown();
    trayIcon.Shutdown();
    ClearRuntime(audio, hotkeys, activeBindings);

    std::cout << Localization::Text("Program kapatıldı.\n", "Program closed.\n");
    return 0;
}
