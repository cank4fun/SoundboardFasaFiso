#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app/Version.hpp"
#include "audio/Audio.hpp"
#include "config/Config.hpp"
#include "hotkeys/HotkeyManager.hpp"
#include "platform/SingleInstance.hpp"
#include "platform/Utf8Path.hpp"
#include "tray/TrayIcon.hpp"

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr int FirstSoundHotkeyId = 100;
    constexpr int ToggleConsoleCommandId = 994;
    constexpr int OutputMuteHotkeyId = 995;
    constexpr int MonitorMuteHotkeyId = 996;
    constexpr int ReloadHotkeyId = 997;
    constexpr int StopHotkeyId = 998;
    constexpr int ExitHotkeyId = 999;

    void ConfigureUtf8Console()
    {
        const bool outputCodePageSet =
            SetConsoleOutputCP(CP_UTF8) != 0;

        const bool inputCodePageSet =
            SetConsoleCP(CP_UTF8) != 0;

        if (!outputCodePageSet || !inputCodePageSet)
        {
            std::cerr
                << "Uyarı: Windows konsolu UTF-8 olarak "
                << "ayarlanamadı. Windows hata kodu: "
                << GetLastError()
                << '\n';
        }
    }

    std::optional<std::filesystem::path> GetExecutableFolder()
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
                    << "EXE yolu alınamadı. Windows hata kodu: "
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
                    std::cerr << "EXE klasörü belirlenemedi.\n";
                    return std::nullopt;
                }

                return executablePath.parent_path();
            }

            if (pathBuffer.size() >= MaximumPathCharacters)
            {
                std::cerr << "EXE yolu desteklenen uzunluğu aşıyor.\n";
                return std::nullopt;
            }

            const std::size_t nextBufferSize =
                pathBuffer.size() * 2 > MaximumPathCharacters
                    ? MaximumPathCharacters
                    : pathBuffer.size() * 2;

            pathBuffer.resize(nextBufferSize);
        }
    }

    void PrintConfigSummary(const Config& config)
    {
        std::cout
            << "\nİstenen ana çıkış: "
            << config.GetOutputDevice()
            << '\n';

        std::cout
            << "Ana çıkış seviyesi: "
            << static_cast<int>(config.GetOutputVolume() * 100.0f)
            << "%\n";

        std::cout
            << "İstenen monitör çıkışı: "
            << config.GetMonitorDevice()
            << '\n';

        std::cout
            << "Monitör seviyesi: "
            << static_cast<int>(config.GetMonitorVolume() * 100.0f)
            << "%\n";

        std::cout
            << "İstenen örnekleme hızı: "
            << (config.GetAudioSampleRate() == 0
                ? std::string{"cihaz doğal hızı"}
                : std::to_string(config.GetAudioSampleRate()) + " Hz")
            << '\n';

        std::cout
            << "İstenen audio buffer: "
            << (config.GetAudioBufferMilliseconds() == 0
                ? std::string{"Windows/miniaudio varsayılanı"}
                : std::to_string(
                    config.GetAudioBufferMilliseconds()
                ) + " ms")
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
            config.GetAudioSampleRate(),
            config.GetAudioBufferMilliseconds()
        ))
        {
            std::cerr << "Ses sistemi başlatılamadı.\n";
            return false;
        }

        std::cout
            << "\nSesler ana ve monitör çıkışları için "
            << "RAM'e yükleniyor...\n";

        std::cout << "\nHotkey atamaları:\n";

        for (const SoundBinding& binding : config.GetBindings())
        {
            std::filesystem::path soundPath =
                soundsFolder / binding.soundFile;

            soundPath = soundPath.lexically_normal();

            if (!std::filesystem::exists(soundPath) ||
                !std::filesystem::is_regular_file(soundPath))
            {
                std::cerr
                    << "Ses dosyası bulunamadı: "
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
                    << "Ses yüklenemedi: "
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
                    << " hotkey olarak kaydedilemedi.\n";

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
                << "Yeni config tamamen uygulanamadı. "
                << "Eski ayarlara dönülecek.\n";

            return false;
        }

        if (activeBindings.empty())
        {
            std::cerr
                << "Kullanılabilir ses veya hotkey bulunamadı.\n";

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
                << " durdurma hotkey'i kaydedilemedi.\n";

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
                << " ana çıkış mute hotkey'i kaydedilemedi.\n";

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
                << " monitör mute hotkey'i kaydedilemedi.\n";

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
                << " reload hotkey'i kaydedilemedi.\n";

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
                << " çıkış hotkey'i kaydedilemedi.\n";

            return false;
        }

        std::cout
            << config.GetStopKeyName()
            << " -> Tüm sesleri durdur\n";

        std::cout
            << config.GetOutputMuteKeyName()
            << " -> Ana çıkışı mute/unmute\n";

        std::cout
            << config.GetMonitorMuteKeyName()
            << " -> Monitör çıkışını mute/unmute\n";

        std::cout
            << config.GetReloadKeyName()
            << " -> Config'i yeniden yükle\n";

        std::cout
            << config.GetExitKeyName()
            << " -> Programı kapat\n";

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

int main()
{
    ConfigureUtf8Console();

    SingleInstance singleInstance;
    const SingleInstanceResult instanceResult =
        singleInstance.Acquire(
            L"Local\\SoundBoardFasaFiso.SingleInstance"
        );

    if (instanceResult == SingleInstanceResult::AlreadyRunning)
    {
        MessageBoxW(
            nullptr,
            L"SoundBoardFasaFiso zaten çalışıyor.",
            L"SoundBoardFasaFiso",
            MB_OK | MB_ICONINFORMATION
        );

        return 0;
    }

    if (instanceResult == SingleInstanceResult::Failed)
    {
        std::cerr
            << "Tek uygulama kilidi oluşturulamadı. "
            << "Windows hata kodu: "
            << GetLastError()
            << '\n';

        return 1;
    }

    std::cout << "================================\n";
    std::cout << "SoundBoardFasaFiso v" << AppVersion::String << '\n';
    std::cout << "================================\n";

    const auto programFolder = GetExecutableFolder();

    if (!programFolder.has_value())
    {
        return 1;
    }

    const std::filesystem::path soundsFolder =
        *programFolder / "sounds";

    const std::filesystem::path configPath =
        *programFolder / "config.txt";

    std::cout
        << "Kullanılan config: "
        << PathToUtf8(configPath)
        << '\n';

    Config config;

    if (!config.Load(configPath))
    {
        std::cerr << "Config yüklenemedi.\n";
        return 1;
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

    TrayIcon trayIcon;

    const TrayCommandIds trayCommandIds{
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
            << "Tray icon hazır. Sağ tıklayarak menüyü açabilirsin.\n"
            << "Tray icon'a çift tıklayarak konsolu "
            << "gösterip gizleyebilirsin.\n";
    }
    else
    {
        std::cerr
            << "Tray icon başlatılamadı. "
            << "Soundboard terminal üzerinden çalışmaya devam edecek.\n";
    }

    std::cout
        << "\nSoundboard hazır.\n"
        << "Sesler ana çıkışa ve monitör çıkışına "
        << "aynı anda gönderilecek.\n";

    bool running = true;
    bool audioRecoveryWarningShown = false;

    auto nextAudioConnectionCheck =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(1);

    while (running)
    {
        const int hotkeyId = hotkeys.WaitForPress(250);

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
                        << "\nSes cihazı bağlantısı koptu. "
                        << "Program açık kalacak ve "
                        << "3 saniyede bir yeniden deneyecek.\n";

                    audioRecoveryWarningShown = true;
                }
            }
            else if (
                recoveryResult == AudioRecoveryResult::Recovered
            )
            {
                std::cout
                    << "\nSes sistemi yeniden başlatıldı.\n"
                    << "Sesler RAM'e tekrar yüklendi.\n"
                    << "Soundboard hazır.\n";

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

        if (hotkeyId == ToggleConsoleCommandId)
        {
            if (!trayIcon.ToggleConsoleVisibility())
            {
                std::cerr
                    << "Konsol penceresi gösterilip gizlenemedi.\n";
            }

            continue;
        }

        if (hotkeyId == ReloadHotkeyId)
        {
            std::cout << "\nConfig yeniden yükleniyor...\n";

            Config newConfig;

            if (!newConfig.Load(configPath))
            {
                std::cerr
                    << "Reload başarısız. "
                    << "Eski ayarlar kullanılmaya devam ediyor.\n";

                continue;
            }

            const Config oldConfig = config;

            ClearRuntime(audio, hotkeys, activeBindings);

            if (BuildRuntime(
                newConfig,
                soundsFolder,
                audio,
                hotkeys,
                activeBindings,
                true
            ))
            {
                config = std::move(newConfig);
                audioRecoveryWarningShown = false;
                nextAudioConnectionCheck =
                    std::chrono::steady_clock::now() +
                    std::chrono::seconds(1);

                std::cout
                    << "\nConfig başarıyla yeniden yüklendi.\n"
                    << "Soundboard hazır.\n";

                continue;
            }

            std::cerr
                << "Yeni ayarlar başlatılamadı. "
                << "Eski ayarlar geri yükleniyor...\n";

            ClearRuntime(audio, hotkeys, activeBindings);

            if (!BuildRuntime(
                oldConfig,
                soundsFolder,
                audio,
                hotkeys,
                activeBindings
            ))
            {
                std::cerr
                    << "Eski ayarlar da geri yüklenemedi. "
                    << "Program kapatılıyor.\n";

                running = false;
                continue;
            }

            config = oldConfig;
            audioRecoveryWarningShown = false;
            nextAudioConnectionCheck =
                std::chrono::steady_clock::now() +
                std::chrono::seconds(1);

            std::cout
                << "Eski ayarlar geri yüklendi.\n"
                << "Soundboard hazır.\n";

            continue;
        }

        if (hotkeyId == OutputMuteHotkeyId)
        {
            const MuteToggleResult result =
                audio.ToggleOutputMute();

            if (result == MuteToggleResult::Muted)
            {
                std::cout << "Ana çıkış susturuldu.\n";
            }
            else if (result == MuteToggleResult::Unmuted)
            {
                std::cout << "Ana çıkış sesi açıldı.\n";
            }
            else if (result == MuteToggleResult::Unavailable)
            {
                std::cerr << "Ana çıkış şu anda kullanılamıyor.\n";
            }
            else
            {
                std::cerr
                    << "Ana çıkış mute durumu değiştirilemedi.\n";
            }

            continue;
        }

        if (hotkeyId == MonitorMuteHotkeyId)
        {
            const MuteToggleResult result =
                audio.ToggleMonitorMute();

            if (result == MuteToggleResult::Muted)
            {
                std::cout << "Monitör çıkışı susturuldu.\n";
            }
            else if (result == MuteToggleResult::Unmuted)
            {
                std::cout << "Monitör çıkışı sesi açıldı.\n";
            }
            else if (result == MuteToggleResult::Unavailable)
            {
                std::cerr
                    << "Monitör çıkışı kapalı veya kullanılamıyor.\n";
            }
            else
            {
                std::cerr
                    << "Monitör mute durumu değiştirilemedi.\n";
            }

            continue;
        }

        if (hotkeyId == StopHotkeyId)
        {
            if (audio.StopAll())
            {
                std::cout << "Tüm sesler durduruldu.\n";
            }
            else
            {
                std::cerr << "Bazı sesler durdurulamadı.\n";
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
                << "Çalınıyor: "
                << PathToUtf8(binding.soundFile.stem())
                << " ["
                << PlaybackModeName(binding.mode)
                << "]\n";
        }
        else if (playbackResult == PlaybackResult::Stopped)
        {
            std::cout
                << "Durduruldu: "
                << PathToUtf8(binding.soundFile.stem())
                << '\n';
        }
        else if (playbackResult == PlaybackResult::Failed)
        {
            std::cerr
                << "Ses çalınamadı: "
                << PathToUtf8(binding.soundFile.stem())
                << '\n';
        }
    }

    trayIcon.Shutdown();
    ClearRuntime(audio, hotkeys, activeBindings);

    std::cout << "Program kapatıldı.\n";
    return 0;
}
