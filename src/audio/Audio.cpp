#include "audio/Audio.hpp"
#include "sound/SoundFileFormat.hpp"
#include "platform/Utf8Path.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

std::atomic<Audio*> Audio::activeInstance_{nullptr};

namespace
{
    constexpr std::size_t OverlapVoiceCount = 8;

    std::string ToLower(std::string text)
    {
        std::transform(
            text.begin(),
            text.end(),
            text.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(
                    std::tolower(character)
                );
            }
        );

        return text;
    }

    bool IsDefaultDeviceRequest(const std::string& deviceName)
    {
        const std::string loweredName =
            ToLower(deviceName);

        return loweredName.empty() ||
            loweredName == "default" ||
            loweredName == "varsayilan";
    }

    bool IsDisabledDeviceRequest(const std::string& deviceName)
    {
        const std::string loweredName =
            ToLower(deviceName);

        return loweredName.empty() ||
            loweredName == "none" ||
            loweredName == "off" ||
            loweredName == "disabled" ||
            loweredName == "kapali";
    }

    const char* BackendName(const ma_backend backend)
    {
        switch (backend)
        {
        case ma_backend_wasapi:
            return "WASAPI";
        case ma_backend_dsound:
            return "DirectSound";
        case ma_backend_winmm:
            return "WinMM";
        case ma_backend_null:
            return "Null";
        default:
            return "Diger";
        }
    }

    std::string DeviceTimingDescription(ma_engine& engine)
    {
        ma_device* const device =
            ma_engine_get_device(&engine);

        if (device == nullptr || device->sampleRate == 0)
        {
            return {};
        }

        std::ostringstream description;

        description
            << " | Ornekleme: "
            << device->sampleRate
            << " Hz";

        const ma_uint32 periodFrames =
            device->playback.internalPeriodSizeInFrames;

        const ma_uint32 periodCount =
            device->playback.internalPeriods;

        if (periodFrames != 0)
        {
            const double periodMilliseconds =
                static_cast<double>(periodFrames) * 1000.0 /
                static_cast<double>(device->sampleRate);

            description
                << " | Period: "
                << std::fixed
                << std::setprecision(2)
                << periodMilliseconds
                << " ms";

            if (periodCount != 0)
            {
                description
                    << " x "
                    << periodCount
                    << " | Buffer: "
                    << periodMilliseconds *
                        static_cast<double>(periodCount)
                    << " ms";
            }
        }

        const ma_uint32 nativeSampleRate =
            device->playback.internalSampleRate;

        if (nativeSampleRate != 0 &&
            nativeSampleRate != device->sampleRate)
        {
            description
                << " | Cihaz dogal hizi: "
                << nativeSampleRate
                << " Hz";
        }

        return description.str();
    }

    std::optional<ma_uint32> FindPlaybackDevice(
        const std::string& requestedDevice,
        ma_device_info* playbackDevices,
        const ma_uint32 playbackDeviceCount
    )
    {
        const std::string requestedLower =
            ToLower(requestedDevice);

        std::vector<ma_uint32> exactMatches;
        std::vector<ma_uint32> partialMatches;

        for (
            ma_uint32 index = 0;
            index < playbackDeviceCount;
            ++index
        )
        {
            const std::string deviceName =
                playbackDevices[index].name;

            const std::string deviceNameLower =
                ToLower(deviceName);

            if (deviceNameLower == requestedLower)
            {
                exactMatches.push_back(index);
            }
            else if (
                deviceNameLower.find(requestedLower) !=
                std::string::npos
            )
            {
                partialMatches.push_back(index);
            }
        }

        const auto& matches =
            !exactMatches.empty()
                ? exactMatches
                : partialMatches;

        if (matches.empty())
        {
            std::cerr
                << "Cikis cihazi bulunamadi: "
                << requestedDevice
                << '\n';

            return std::nullopt;
        }

        if (matches.size() > 1)
        {
            std::cerr
                << "Cihaz adi birden fazla cihazla eslesti: "
                << requestedDevice
                << "\nEslesen cihazlar:\n";

            for (const ma_uint32 index : matches)
            {
                std::cerr
                    << "- "
                    << playbackDevices[index].name
                    << '\n';
            }

            return std::nullopt;
        }

        return matches.front();
    }

    bool PrepareSoundForPlayback(ma_sound* sound)
    {
        ma_result result = ma_sound_stop(sound);

        if (result != MA_SUCCESS)
        {
            return false;
        }

        result = ma_sound_seek_to_pcm_frame(
            sound,
            0
        );

        return result == MA_SUCCESS;
    }
}

Audio::~Audio()
{
    Shutdown();
}

void Audio::DeviceNotificationCallback(
    const ma_device_notification* notification
)
{
    if (notification == nullptr)
    {
        return;
    }

    Audio* const instance =
        activeInstance_.load(std::memory_order_acquire);

    if (instance == nullptr ||
        instance->ignoreDeviceNotifications_.load(
            std::memory_order_relaxed
        ))
    {
        return;
    }

    if (notification->type == ma_device_notification_type_stopped ||
        notification->type == ma_device_notification_type_rerouted ||
        notification->type ==
            ma_device_notification_type_interruption_began)
    {
        instance->recoveryRequested_.store(
            true,
            std::memory_order_release
        );
    }
}

bool Audio::InitializeEngine(
    const std::string& requestedDevice,
    const std::string& engineLabel,
    ma_device_info* playbackDevices,
    const ma_uint32 playbackDeviceCount,
    const float volume,
    const bool muted,
    const ma_uint32 sampleRate,
    const ma_uint32 bufferMilliseconds,
    EngineState& state
)
{
    state.deviceName.clear();
    state.initialized = false;

    ma_engine_config engineConfig =
        ma_engine_config_init();

    engineConfig.pContext = &context_;
    engineConfig.notificationCallback =
        &Audio::DeviceNotificationCallback;

    if (IsDefaultDeviceRequest(requestedDevice))
    {
        state.deviceName =
            "Windows varsayilan cikisi";
    }
    else
    {
        const auto selectedIndex =
            FindPlaybackDevice(
                requestedDevice,
                playbackDevices,
                playbackDeviceCount
            );

        if (!selectedIndex.has_value())
        {
            return false;
        }

        state.deviceId =
            playbackDevices[*selectedIndex].id;

        state.deviceName =
            playbackDevices[*selectedIndex].name;

        engineConfig.pPlaybackDeviceID =
            &state.deviceId;
    }

    const auto tryInitializeEngine =
        [&state, &engineConfig](
            const ma_uint32 attemptSampleRate,
            const ma_uint32 attemptBufferMilliseconds
        )
        {
            std::memset(&state.engine, 0, sizeof(state.engine));

            engineConfig.sampleRate = attemptSampleRate;
            engineConfig.periodSizeInFrames = 0;
            engineConfig.periodSizeInMilliseconds =
                attemptBufferMilliseconds;

            return ma_engine_init(
                &engineConfig,
                &state.engine
            );
        };

    ma_uint32 activeSampleRate = sampleRate;
    ma_uint32 activeBufferMilliseconds =
        bufferMilliseconds;

    ma_result engineResult =
        tryInitializeEngine(
            activeSampleRate,
            activeBufferMilliseconds
        );

    if (engineResult != MA_SUCCESS &&
        activeBufferMilliseconds != 0)
    {
        std::cerr
            << "Uyari: "
            << engineLabel
            << " icin "
            << activeBufferMilliseconds
            << " ms dusuk gecikme buffer'i acilamadi. "
            << "Windows varsayilan buffer'i ile tekrar deneniyor.\n";

        activeBufferMilliseconds = 0;

        engineResult =
            tryInitializeEngine(
                activeSampleRate,
                activeBufferMilliseconds
            );
    }

    if (engineResult != MA_SUCCESS &&
        activeSampleRate != 0)
    {
        std::cerr
            << "Uyari: "
            << engineLabel
            << " icin "
            << activeSampleRate
            << " Hz acilamadi. Cihazin dogal ornekleme "
            << "hizi ile tekrar deneniyor.\n";

        activeSampleRate = 0;
        activeBufferMilliseconds = 0;

        engineResult =
            tryInitializeEngine(
                activeSampleRate,
                activeBufferMilliseconds
            );
    }

    if (engineResult != MA_SUCCESS)
    {
        std::cerr
            << engineLabel
            << " audio engine baslatilamadi. Hata: "
            << engineResult
            << '\n';

        state.deviceName.clear();

        return false;
    }

    const float effectiveVolume =
        muted ? 0.0f : volume;

    const ma_result volumeResult =
        ma_engine_set_volume(
            &state.engine,
            effectiveVolume
        );

    if (volumeResult != MA_SUCCESS)
    {
        std::cerr
            << engineLabel
            << " ses seviyesi ayarlanamadi. Hata: "
            << volumeResult
            << '\n';

        ma_engine_uninit(&state.engine);

        state.deviceName.clear();

        return false;
    }

    state.initialized = true;

    std::cout
        << engineLabel
        << ": "
        << state.deviceName
        << " | Ses: "
        << static_cast<int>(volume * 100.0f)
        << '%';

    if (muted)
    {
        std::cout << " [MUTE]";
    }

    std::cout
        << DeviceTimingDescription(state.engine)
        << '\n';

    return true;
}

bool Audio::InitializeRuntime()
{
    if (contextInitialized_)
    {
        return true;
    }

    const ma_backend preferredBackends[]{
        ma_backend_wasapi
    };

    ma_result contextResult =
        ma_context_init(
            preferredBackends,
            1,
            nullptr,
            &context_
        );

    if (contextResult != MA_SUCCESS)
    {
        std::cerr
            << "Uyari: WASAPI baslatilamadi. Miniaudio'nun "
            << "Windows yedek backend sirasi deneniyor. Hata: "
            << contextResult
            << '\n';

        std::memset(&context_, 0, sizeof(context_));

        contextResult =
            ma_context_init(
                nullptr,
                0,
                nullptr,
                &context_
            );
    }

    if (contextResult != MA_SUCCESS)
    {
        std::cerr
            << "Audio context baslatilamadi. Hata: "
            << contextResult
            << '\n';

        return false;
    }

    contextInitialized_ = true;

    std::cout
        << "Audio backend: "
        << BackendName(context_.backend)
        << '\n';

    ma_device_info* playbackDevices = nullptr;
    ma_uint32 playbackDeviceCount = 0;

    const ma_result deviceResult =
        ma_context_get_devices(
            &context_,
            &playbackDevices,
            &playbackDeviceCount,
            nullptr,
            nullptr
        );

    if (deviceResult != MA_SUCCESS)
    {
        std::cerr
            << "Ses cihazlari alinamadi. Hata: "
            << deviceResult
            << '\n';

        DestroyRuntime();
        return false;
    }

    if (!InitializeEngine(
        requestedOutputDevice_,
        "Ana cikis",
        playbackDevices,
        playbackDeviceCount,
        outputVolume_,
        outputMuted_,
        sampleRate_,
        bufferMilliseconds_,
        outputEngine_
    ))
    {
        DestroyRuntime();
        return false;
    }

    if (!IsDisabledDeviceRequest(requestedMonitorDevice_))
    {
        if (!InitializeEngine(
            requestedMonitorDevice_,
            "Monitor cikisi",
            playbackDevices,
            playbackDeviceCount,
            monitorVolume_,
            monitorMuted_,
            sampleRate_,
            bufferMilliseconds_,
            monitorEngine_
        ))
        {
            DestroyRuntime();
            return false;
        }

        if (outputEngine_.deviceName ==
            monitorEngine_.deviceName)
        {
            std::cerr
                << "Uyari: Ana cikis ve monitor ayni cihaz. "
                << "Ses iki kez oynatilabilir.\n";
        }
    }
    else
    {
        std::cout << "Monitor cikisi: Kapali\n";
    }

    return true;
}

bool Audio::Initialize(
    const std::string& requestedOutputDevice,
    const std::string& requestedMonitorDevice,
    const float outputVolume,
    const float monitorVolume,
    const unsigned int sampleRate,
    const unsigned int bufferMilliseconds
)
{
    if (contextInitialized_)
    {
        return true;
    }

    Audio* const activeInstance =
        activeInstance_.load(std::memory_order_acquire);

    if (activeInstance != nullptr && activeInstance != this)
    {
        std::cerr
            << "Ayni anda birden fazla Audio nesnesi "
            << "baslatilamaz.\n";

        return false;
    }

    const bool sampleRateIsValid =
        sampleRate == 0 ||
        (sampleRate >= 8000 && sampleRate <= 192000);

    const bool bufferIsValid =
        bufferMilliseconds == 0 ||
        (bufferMilliseconds >= 2 &&
            bufferMilliseconds <= 100);

    if (!sampleRateIsValid || !bufferIsValid)
    {
        std::cerr
            << "Gecersiz audio gecikme ayari. "
            << "sampleRate="
            << sampleRate
            << ", bufferMilliseconds="
            << bufferMilliseconds
            << '\n';

        return false;
    }

    requestedOutputDevice_ = requestedOutputDevice;
    requestedMonitorDevice_ = requestedMonitorDevice;
    outputVolume_ = outputVolume;
    monitorVolume_ = monitorVolume;
    sampleRate_ = static_cast<ma_uint32>(sampleRate);
    bufferMilliseconds_ =
        static_cast<ma_uint32>(bufferMilliseconds);

    soundDefinitions_.clear();
    desiredConfigurationSet_ = true;

    ignoreDeviceNotifications_.store(
        true,
        std::memory_order_release
    );

    activeInstance_.store(
        this,
        std::memory_order_release
    );

    if (!InitializeRuntime())
    {
        desiredConfigurationSet_ = false;
        activeInstance_.store(nullptr, std::memory_order_release);

        ignoreDeviceNotifications_.store(
            false,
            std::memory_order_release
        );

        return false;
    }

    recoveryRequested_.store(false, std::memory_order_release);

    ignoreDeviceNotifications_.store(
        false,
        std::memory_order_release
    );

    return true;
}

bool Audio::InitializeVoiceFromFile(
    const SoundDefinition& definition,
    Voice& voice
)
{
    const std::wstring pathString =
        definition.path.wstring();

    const ma_uint32 flags =
        MA_SOUND_FLAG_DECODE |
        MA_SOUND_FLAG_NO_SPATIALIZATION |
        MA_SOUND_FLAG_NO_PITCH;

    voice.outputSound =
        std::make_unique<ma_sound>();

    ma_result result =
        ma_sound_init_from_file_w(
            &outputEngine_.engine,
            pathString.c_str(),
            flags,
            nullptr,
            nullptr,
            voice.outputSound.get()
        );

    if (result != MA_SUCCESS)
    {
        std::cerr
            << "Ses ana cikisa yuklenemedi: "
            << PathToUtf8(definition.path)
            << ". Hata: "
            << result
            << '\n';

        voice.outputSound.reset();
        return false;
    }

    if (monitorEngine_.initialized)
    {
        voice.monitorSound =
            std::make_unique<ma_sound>();

        result =
            ma_sound_init_from_file_w(
                &monitorEngine_.engine,
                pathString.c_str(),
                flags,
                nullptr,
                nullptr,
                voice.monitorSound.get()
            );

        if (result != MA_SUCCESS)
        {
            std::cerr
                << "Ses monitor cikisina yuklenemedi: "
                << PathToUtf8(definition.path)
                << ". Hata: "
                << result
                << '\n';

            DestroyVoice(voice);
            return false;
        }
    }

    const ma_bool32 shouldLoop =
        definition.mode == PlaybackMode::Loop
            ? MA_TRUE
            : MA_FALSE;

    ma_sound_set_volume(
        voice.outputSound.get(),
        definition.volume
    );

    ma_sound_set_looping(
        voice.outputSound.get(),
        shouldLoop
    );

    if (voice.monitorSound)
    {
        ma_sound_set_volume(
            voice.monitorSound.get(),
            definition.volume
        );

        ma_sound_set_looping(
            voice.monitorSound.get(),
            shouldLoop
        );
    }

    return true;
}

bool Audio::InitializeVoiceCopy(
    const Voice& sourceVoice,
    const SoundDefinition& definition,
    Voice& voice
)
{
    if (!sourceVoice.outputSound)
    {
        return false;
    }

    const ma_uint32 flags =
        MA_SOUND_FLAG_NO_SPATIALIZATION |
        MA_SOUND_FLAG_NO_PITCH;

    voice.outputSound =
        std::make_unique<ma_sound>();

    ma_result result =
        ma_sound_init_copy(
            &outputEngine_.engine,
            sourceVoice.outputSound.get(),
            flags,
            nullptr,
            voice.outputSound.get()
        );

    if (result != MA_SUCCESS)
    {
        std::cerr
            << "Overlap voice ana cikisa olusturulamadi: "
            << PathToUtf8(definition.path)
            << ". Hata: "
            << result
            << '\n';

        voice.outputSound.reset();
        return false;
    }

    if (sourceVoice.monitorSound)
    {
        voice.monitorSound =
            std::make_unique<ma_sound>();

        result =
            ma_sound_init_copy(
                &monitorEngine_.engine,
                sourceVoice.monitorSound.get(),
                flags,
                nullptr,
                voice.monitorSound.get()
            );

        if (result != MA_SUCCESS)
        {
            std::cerr
                << "Overlap voice monitor cikisina olusturulamadi: "
                << PathToUtf8(definition.path)
                << ". Hata: "
                << result
                << '\n';

            DestroyVoice(voice);
            return false;
        }
    }

    ma_sound_set_volume(
        voice.outputSound.get(),
        definition.volume
    );

    ma_sound_set_looping(
        voice.outputSound.get(),
        MA_FALSE
    );

    if (voice.monitorSound)
    {
        ma_sound_set_volume(
            voice.monitorSound.get(),
            definition.volume
        );

        ma_sound_set_looping(
            voice.monitorSound.get(),
            MA_FALSE
        );
    }

    return true;
}

bool Audio::LoadSoundIntoRuntime(
    const std::string& soundId,
    const SoundDefinition& definition
)
{
    if (!outputEngine_.initialized)
    {
        std::cerr << "Ana audio engine hazir degil.\n";
        return false;
    }

    if (soundId.empty())
    {
        std::cerr << "Ses kimligi bos olamaz.\n";
        return false;
    }

    if (loadedSounds_.contains(soundId))
    {
        std::cerr
            << "Ses zaten yuklenmis: "
            << soundId
            << '\n';

        return false;
    }

    if (!std::filesystem::exists(definition.path) ||
        !std::filesystem::is_regular_file(definition.path))
    {
        std::cerr
            << "Ses dosyasi bulunamadi: "
            << PathToUtf8(definition.path)
            << '\n';

        return false;
    }

    LoadedSound loadedSound;
    loadedSound.mode = definition.mode;

    Voice primaryVoice;

    if (!InitializeVoiceFromFile(
        definition,
        primaryVoice
    ))
    {
        return false;
    }

    loadedSound.voices.push_back(
        std::move(primaryVoice)
    );

    if (definition.mode == PlaybackMode::Overlap)
    {
        loadedSound.voices.reserve(
            OverlapVoiceCount
        );

        while (loadedSound.voices.size() <
            OverlapVoiceCount)
        {
            Voice copiedVoice;

            if (!InitializeVoiceCopy(
                loadedSound.voices.front(),
                definition,
                copiedVoice
            ))
            {
                DestroyLoadedSound(loadedSound);
                return false;
            }

            loadedSound.voices.push_back(
                std::move(copiedVoice)
            );
        }
    }

    loadedSounds_.emplace(
        soundId,
        std::move(loadedSound)
    );

    return true;
}

bool Audio::PrepareVoiceForPlayback(
    Voice& voice,
    const std::string& soundId
)
{
    bool success = true;

    if (!voice.outputSound ||
        !PrepareSoundForPlayback(
            voice.outputSound.get()
        ))
    {
        std::cerr
            << "Ana cikistaki ses hazirlanamadi: "
            << soundId
            << '\n';

        success = false;
    }

    if (voice.monitorSound &&
        !PrepareSoundForPlayback(
            voice.monitorSound.get()
        ))
    {
        std::cerr
            << "Monitor sesi hazirlanamadi: "
            << soundId
            << '\n';

        success = false;
    }

    return success;
}

bool Audio::StartVoice(
    Voice& voice,
    const std::string& soundId
)
{
    if (!voice.outputSound)
    {
        return false;
    }

    ma_result result =
        ma_sound_start(
            voice.outputSound.get()
        );

    if (result != MA_SUCCESS)
    {
        std::cerr
            << "Ana cikistaki ses baslatilamadi: "
            << soundId
            << ". Hata: "
            << result
            << '\n';

        return false;
    }

    if (voice.monitorSound)
    {
        result =
            ma_sound_start(
                voice.monitorSound.get()
            );

        if (result != MA_SUCCESS)
        {
            std::cerr
                << "Monitor sesi baslatilamadi: "
                << soundId
                << ". Hata: "
                << result
                << '\n';

            PrepareSoundForPlayback(
                voice.outputSound.get()
            );

            PrepareSoundForPlayback(
                voice.monitorSound.get()
            );

            return false;
        }
    }

    return true;
}

bool Audio::IsVoicePlaying(const Voice& voice)
{
    const bool outputIsPlaying =
        voice.outputSound &&
        ma_sound_is_playing(
            voice.outputSound.get()
        ) == MA_TRUE;

    const bool monitorIsPlaying =
        voice.monitorSound &&
        ma_sound_is_playing(
            voice.monitorSound.get()
        ) == MA_TRUE;

    return outputIsPlaying || monitorIsPlaying;
}

void Audio::DestroyVoice(Voice& voice)
{
    if (voice.monitorSound)
    {
        ma_sound_uninit(
            voice.monitorSound.get()
        );

        voice.monitorSound.reset();
    }

    if (voice.outputSound)
    {
        ma_sound_uninit(
            voice.outputSound.get()
        );

        voice.outputSound.reset();
    }
}

void Audio::DestroyLoadedSound(
    LoadedSound& loadedSound
)
{
    for (
        auto voiceIterator = loadedSound.voices.rbegin();
        voiceIterator != loadedSound.voices.rend();
        ++voiceIterator
    )
    {
        DestroyVoice(*voiceIterator);
    }

    loadedSound.voices.clear();
    loadedSound.nextOverlapVoice = 0;
}

bool Audio::LoadSound(
    const std::string& soundId,
    const std::filesystem::path& soundPath,
    const float volume,
    const PlaybackMode mode
)
{
    if (soundDefinitions_.contains(soundId))
    {
        std::cerr
            << "Ses zaten kayitli: "
            << soundId
            << '\n';

        return false;
    }

    if (!SoundFileFormat::IsSupported(soundPath))
    {
        const std::string extension =
            SoundFileFormat::NormalizedExtension(soundPath);

        std::cerr
            << "Desteklenmeyen ses dosyasi uzantisi: "
            << (extension.empty() ? "<uzanti yok>" : extension)
            << "\nDosya: "
            << PathToUtf8(soundPath)
            << "\nDesteklenen uzantilar: "
            << SoundFileFormat::SupportedExtensions()
            << '\n';

        return false;
    }

    if (volume < 0.0f || volume > 1.0f)
    {
        std::cerr
            << "Ses seviyesi 0.00 ile 1.00 arasinda olmali: "
            << soundId
            << '\n';

        return false;
    }

    const SoundDefinition definition{
        soundPath,
        volume,
        mode
    };

    if (!LoadSoundIntoRuntime(soundId, definition))
    {
        return false;
    }

    soundDefinitions_.emplace(
        soundId,
        definition
    );

    return true;
}

PlaybackResult Audio::PlayLoaded(const std::string& soundId)
{
    const auto iterator =
        loadedSounds_.find(soundId);

    if (iterator == loadedSounds_.end())
    {
        std::cerr
            << "Ses sistemi su anda hazir degil veya "
            << "yuklenmis ses bulunamadi: "
            << soundId
            << '\n';

        return PlaybackResult::Failed;
    }

    LoadedSound& loadedSound =
        iterator->second;

    if (loadedSound.voices.empty())
    {
        return PlaybackResult::Failed;
    }

    if (loadedSound.mode == PlaybackMode::Overlap)
    {
        const std::size_t voiceCount =
            loadedSound.voices.size();

        std::size_t selectedVoiceIndex =
            loadedSound.nextOverlapVoice % voiceCount;

        for (
            std::size_t offset = 0;
            offset < voiceCount;
            ++offset
        )
        {
            const std::size_t candidateIndex =
                (loadedSound.nextOverlapVoice + offset) %
                voiceCount;

            if (!IsVoicePlaying(
                loadedSound.voices[candidateIndex]
            ))
            {
                selectedVoiceIndex = candidateIndex;
                break;
            }
        }

        loadedSound.nextOverlapVoice =
            (selectedVoiceIndex + 1) % voiceCount;

        Voice& selectedVoice =
            loadedSound.voices[selectedVoiceIndex];

        if (!PrepareVoiceForPlayback(
            selectedVoice,
            soundId
        ) ||
            !StartVoice(
                selectedVoice,
                soundId
            ))
        {
            recoveryRequested_.store(
                true,
                std::memory_order_release
            );

            return PlaybackResult::Failed;
        }

        return PlaybackResult::Started;
    }

    Voice& voice =
        loadedSound.voices.front();

    const bool isToggleMode =
        loadedSound.mode == PlaybackMode::Toggle ||
        loadedSound.mode == PlaybackMode::Loop;

    if (isToggleMode && IsVoicePlaying(voice))
    {
        if (!PrepareVoiceForPlayback(
            voice,
            soundId
        ))
        {
            recoveryRequested_.store(
                true,
                std::memory_order_release
            );

            return PlaybackResult::Failed;
        }

        return PlaybackResult::Stopped;
    }

    if (!PrepareVoiceForPlayback(
        voice,
        soundId
    ) ||
        !StartVoice(
            voice,
            soundId
        ))
    {
        recoveryRequested_.store(
            true,
            std::memory_order_release
        );

        return PlaybackResult::Failed;
    }

    return PlaybackResult::Started;
}

bool Audio::StopAll()
{
    bool success = true;

    for (auto& [soundId, loadedSound] : loadedSounds_)
    {
        for (Voice& voice : loadedSound.voices)
        {
            if (!PrepareVoiceForPlayback(
                voice,
                soundId
            ))
            {
                success = false;
            }
        }
    }

    if (!success)
    {
        recoveryRequested_.store(
            true,
            std::memory_order_release
        );
    }

    return success;
}

MuteToggleResult Audio::ToggleEngineMute(
    EngineState& state,
    const float configuredVolume,
    bool& muted,
    const std::string& engineLabel
)
{
    if (!state.initialized)
    {
        return MuteToggleResult::Unavailable;
    }

    const bool newMutedState = !muted;
    const float newVolume =
        newMutedState ? 0.0f : configuredVolume;

    const ma_result result =
        ma_engine_set_volume(
            &state.engine,
            newVolume
        );

    if (result != MA_SUCCESS)
    {
        std::cerr
            << engineLabel
            << " mute durumu degistirilemedi. Hata: "
            << result
            << '\n';

        recoveryRequested_.store(
            true,
            std::memory_order_release
        );

        return MuteToggleResult::Failed;
    }

    muted = newMutedState;

    return muted
        ? MuteToggleResult::Muted
        : MuteToggleResult::Unmuted;
}

MuteToggleResult Audio::ToggleOutputMute()
{
    return ToggleEngineMute(
        outputEngine_,
        outputVolume_,
        outputMuted_,
        "Ana cikis"
    );
}

MuteToggleResult Audio::ToggleMonitorMute()
{
    return ToggleEngineMute(
        monitorEngine_,
        monitorVolume_,
        monitorMuted_,
        "Monitor cikisi"
    );
}

bool Audio::IsEngineRunning(EngineState& state) const
{
    if (!state.initialized)
    {
        return false;
    }

    ma_device* const device =
        ma_engine_get_device(&state.engine);

    if (device == nullptr)
    {
        return false;
    }

    const ma_device_state deviceState =
        ma_device_get_state(device);

    return deviceState == ma_device_state_started ||
        deviceState == ma_device_state_starting;
}

bool Audio::IsRuntimeHealthy()
{
    if (!contextInitialized_ ||
        !IsEngineRunning(outputEngine_))
    {
        return false;
    }

    if (!IsDisabledDeviceRequest(requestedMonitorDevice_) &&
        !IsEngineRunning(monitorEngine_))
    {
        return false;
    }

    return true;
}

AudioRecoveryResult Audio::MaintainDeviceConnection()
{
    if (!desiredConfigurationSet_)
    {
        return AudioRecoveryResult::NotNeeded;
    }

    const bool recoveryWasRequested =
        recoveryRequested_.load(std::memory_order_acquire);

    if (!recoveryWasRequested && IsRuntimeHealthy())
    {
        return AudioRecoveryResult::NotNeeded;
    }

    ignoreDeviceNotifications_.store(
        true,
        std::memory_order_release
    );

    DestroyRuntime();

    if (!InitializeRuntime())
    {
        recoveryRequested_.store(true, std::memory_order_release);

        ignoreDeviceNotifications_.store(
            false,
            std::memory_order_release
        );

        return AudioRecoveryResult::Failed;
    }

    for (const auto& [soundId, definition] : soundDefinitions_)
    {
        if (!LoadSoundIntoRuntime(soundId, definition))
        {
            DestroyRuntime();

            recoveryRequested_.store(true, std::memory_order_release);

            ignoreDeviceNotifications_.store(
                false,
                std::memory_order_release
            );

            return AudioRecoveryResult::Failed;
        }
    }

    recoveryRequested_.store(false, std::memory_order_release);

    ignoreDeviceNotifications_.store(
        false,
        std::memory_order_release
    );

    return AudioRecoveryResult::Recovered;
}

void Audio::DestroyRuntime()
{
    for (auto& [soundId, loadedSound] : loadedSounds_)
    {
        static_cast<void>(soundId);
        DestroyLoadedSound(loadedSound);
    }

    loadedSounds_.clear();

    if (monitorEngine_.initialized)
    {
        ma_engine_uninit(
            &monitorEngine_.engine
        );
    }

    monitorEngine_.initialized = false;
    monitorEngine_.deviceName.clear();

    if (outputEngine_.initialized)
    {
        ma_engine_uninit(
            &outputEngine_.engine
        );
    }

    outputEngine_.initialized = false;
    outputEngine_.deviceName.clear();

    if (contextInitialized_)
    {
        ma_context_uninit(&context_);
        contextInitialized_ = false;
    }
}

void Audio::Shutdown()
{
    ignoreDeviceNotifications_.store(
        true,
        std::memory_order_release
    );

    Audio* expectedInstance = this;

    activeInstance_.compare_exchange_strong(
        expectedInstance,
        nullptr,
        std::memory_order_acq_rel
    );

    DestroyRuntime();

    soundDefinitions_.clear();

    requestedOutputDevice_.clear();
    requestedMonitorDevice_.clear();

    sampleRate_ = 48000;
    bufferMilliseconds_ = 5;

    desiredConfigurationSet_ = false;

    recoveryRequested_.store(false, std::memory_order_release);

    ignoreDeviceNotifications_.store(
        false,
        std::memory_order_release
    );
}
