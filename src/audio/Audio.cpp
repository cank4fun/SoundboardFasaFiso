#include "audio/Audio.hpp"
#include "localization/Localization.hpp"
#include "platform/Utf8Path.hpp"
#include "sound/SoundFileFormat.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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
            return Localization::Text("Diğer", "Other");
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
            << Localization::Text(" | Örnekleme: ", " | Sample rate: ")
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
                << Localization::Text(" | Cihaz doğal hızı: ", " | Device native rate: ")
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
                << Localization::Text("Çıkış cihazı bulunamadı: ", "Output device not found: ")
                << requestedDevice
                << '\n';

            return std::nullopt;
        }

        if (matches.size() > 1)
        {
            std::cerr
                << Localization::Text(
                    "Cihaz adı birden fazla cihazla eşleşti: ",
                    "The device name matched more than one device: "
                )
                << requestedDevice
                << Localization::Text(
                    "\nEşleşen cihazlar:\n",
                    "\nMatching devices:\n"
                );

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

    std::optional<ma_uint32> FindCaptureDevice(
        const std::string& requestedDevice,
        ma_device_info* captureDevices,
        const ma_uint32 captureDeviceCount
    )
    {
        const std::string requestedLower = ToLower(requestedDevice);

        std::vector<ma_uint32> exactMatches;
        std::vector<ma_uint32> partialMatches;

        for (ma_uint32 index = 0; index < captureDeviceCount; ++index)
        {
            const std::string deviceName = captureDevices[index].name;
            const std::string deviceNameLower = ToLower(deviceName);

            if (deviceNameLower == requestedLower)
            {
                exactMatches.push_back(index);
            }
            else if (deviceNameLower.find(requestedLower) != std::string::npos)
            {
                partialMatches.push_back(index);
            }
        }

        const auto& matches =
            !exactMatches.empty() ? exactMatches : partialMatches;

        if (matches.empty())
        {
            std::cerr
                << Localization::Text(
                    "Mikrofon cihazı bulunamadı: ",
                    "Microphone device not found: "
                )
                << requestedDevice
                << '\n';

            return std::nullopt;
        }

        if (matches.size() > 1)
        {
            std::cerr
                << Localization::Text(
                    "Mikrofon adı birden fazla cihazla eşleşti: ",
                    "The microphone name matched more than one device: "
                )
                << requestedDevice
                << Localization::Text(
                    "\nEşleşen cihazlar:\n",
                    "\nMatching devices:\n"
                );

            for (const ma_uint32 index : matches)
            {
                std::cerr << "- " << captureDevices[index].name << '\n';
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


std::vector<std::string> Audio::EnumeratePlaybackDevices()
{
    ma_context context{};

    const ma_backend preferredBackends[]{
        ma_backend_wasapi
    };

    ma_result result = ma_context_init(
        preferredBackends,
        1,
        nullptr,
        &context
    );

    if (result != MA_SUCCESS)
    {
        std::memset(&context, 0, sizeof(context));
        result = ma_context_init(
            nullptr,
            0,
            nullptr,
            &context
        );
    }

    if (result != MA_SUCCESS)
    {
        return {};
    }

    ma_device_info* playbackDevices = nullptr;
    ma_uint32 playbackDeviceCount = 0;

    result = ma_context_get_devices(
        &context,
        &playbackDevices,
        &playbackDeviceCount,
        nullptr,
        nullptr
    );

    std::vector<std::string> deviceNames;

    if (result == MA_SUCCESS)
    {
        deviceNames.reserve(
            static_cast<std::size_t>(playbackDeviceCount)
        );

        for (
            ma_uint32 index = 0;
            index < playbackDeviceCount;
            ++index
        )
        {
            const std::string deviceName =
                playbackDevices[index].name;

            if (!deviceName.empty())
            {
                deviceNames.push_back(deviceName);
            }
        }
    }

    ma_context_uninit(&context);

    std::sort(deviceNames.begin(), deviceNames.end());
    deviceNames.erase(
        std::unique(deviceNames.begin(), deviceNames.end()),
        deviceNames.end()
    );

    return deviceNames;
}

std::vector<std::string> Audio::EnumerateCaptureDevices()
{
    ma_context context{};

    const ma_backend preferredBackends[]{
        ma_backend_wasapi
    };

    ma_result result = ma_context_init(
        preferredBackends,
        1,
        nullptr,
        &context
    );

    if (result != MA_SUCCESS)
    {
        std::memset(&context, 0, sizeof(context));
        result = ma_context_init(nullptr, 0, nullptr, &context);
    }

    if (result != MA_SUCCESS)
    {
        return {};
    }

    ma_device_info* captureDevices = nullptr;
    ma_uint32 captureDeviceCount = 0;

    result = ma_context_get_devices(
        &context,
        nullptr,
        nullptr,
        &captureDevices,
        &captureDeviceCount
    );

    std::vector<std::string> deviceNames;

    if (result == MA_SUCCESS)
    {
        deviceNames.reserve(static_cast<std::size_t>(captureDeviceCount));

        for (ma_uint32 index = 0; index < captureDeviceCount; ++index)
        {
            const std::string deviceName = captureDevices[index].name;

            if (!deviceName.empty())
            {
                deviceNames.push_back(deviceName);
            }
        }
    }

    ma_context_uninit(&context);

    std::sort(deviceNames.begin(), deviceNames.end());
    deviceNames.erase(
        std::unique(deviceNames.begin(), deviceNames.end()),
        deviceNames.end()
    );

    return deviceNames;
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

void Audio::MicrophoneDataCallback(
    ma_device* device,
    void* outputFrames,
    const void* inputFrames,
    const ma_uint32 frameCount
)
{
    static_cast<void>(outputFrames);

    if (device == nullptr || inputFrames == nullptr || frameCount == 0)
    {
        return;
    }

    auto* const instance = static_cast<Audio*>(device->pUserData);

    if (instance == nullptr)
    {
        return;
    }

    const auto* const samples =
        static_cast<const float*>(inputFrames);
    const std::size_t sampleCount =
        static_cast<std::size_t>(frameCount) * 2;

    float peak = 0.0f;

    for (std::size_t index = 0; index < sampleCount; ++index)
    {
        peak = std::max(peak, std::abs(samples[index]));
    }

    instance->microphonePeak_.store(
        std::clamp(peak, 0.0f, 1.0f),
        std::memory_order_relaxed
    );

    if (instance->microphoneToOutput_)
    {
        WriteMicrophoneFrames(
            instance->microphoneOutputRoute_,
            inputFrames,
            frameCount
        );
    }

    if (instance->microphoneToMonitor_)
    {
        WriteMicrophoneFrames(
            instance->microphoneMonitorRoute_,
            inputFrames,
            frameCount
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
            << Localization::Text("Uyarı: ", "Warning: ")
            << engineLabel
            << Localization::Text(" için ", " could not open a ")
            << activeBufferMilliseconds
            << Localization::Text(
                " ms düşük gecikme buffer'ı açılamadı. Windows varsayılan buffer'ı ile tekrar deneniyor.\n",
                " ms low-latency buffer. Retrying with the Windows default buffer.\n"
            );

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
            << Localization::Text("Uyarı: ", "Warning: ")
            << engineLabel
            << Localization::Text(" için ", " could not open at ")
            << activeSampleRate
            << Localization::Text(
                " Hz açılamadı. Cihazın doğal örnekleme hızı ile tekrar deneniyor.\n",
                " Hz. Retrying with the device's native sample rate.\n"
            );

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
            << Localization::Text(" audio engine başlatılamadı. Hata: ", " audio engine could not be initialized. Error: ")
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
            << Localization::Text(" ses seviyesi ayarlanamadı. Hata: ", " volume could not be set. Error: ")
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
        << Localization::Text(" | Ses: ", " | Volume: ")
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

bool Audio::InitializeMicrophoneRoute(
    EngineState& engineState,
    const ma_uint32 sampleRate,
    MicrophoneRoute& route
)
{
    if (!engineState.initialized || sampleRate == 0)
    {
        return false;
    }

    const ma_uint32 ringBufferFrames = std::max<ma_uint32>(
        sampleRate / 4,
        2048
    );

    ma_result result = ma_pcm_rb_init(
        ma_format_f32,
        2,
        ringBufferFrames,
        nullptr,
        nullptr,
        &route.ringBuffer
    );

    if (result != MA_SUCCESS)
    {
        return false;
    }

    route.ringBufferInitialized = true;
    ma_pcm_rb_set_sample_rate(&route.ringBuffer, sampleRate);

    result = ma_sound_init_from_data_source(
        &engineState.engine,
        &route.ringBuffer,
        MA_SOUND_FLAG_NO_SPATIALIZATION,
        nullptr,
        &route.sound
    );

    if (result != MA_SUCCESS)
    {
        DestroyMicrophoneRoute(route);
        return false;
    }

    route.soundInitialized = true;

    ma_sound_set_volume(
        &route.sound,
        microphoneVolume_
    );

    result = ma_sound_start(&route.sound);

    if (result != MA_SUCCESS)
    {
        DestroyMicrophoneRoute(route);
        return false;
    }

    return true;
}

void Audio::WriteMicrophoneFrames(
    MicrophoneRoute& route,
    const void* inputFrames,
    const ma_uint32 frameCount
)
{
    if (!route.ringBufferInitialized || inputFrames == nullptr)
    {
        return;
    }

    const auto* source = static_cast<const float*>(inputFrames);
    ma_uint32 remainingFrames = frameCount;
    ma_uint32 sourceOffset = 0;

    while (remainingFrames > 0)
    {
        ma_uint32 writableFrames = remainingFrames;
        void* destination = nullptr;

        const ma_result acquireResult = ma_pcm_rb_acquire_write(
            &route.ringBuffer,
            &writableFrames,
            &destination
        );

        if (acquireResult != MA_SUCCESS || writableFrames == 0 ||
            destination == nullptr)
        {
            break;
        }

        std::memcpy(
            destination,
            source + static_cast<std::size_t>(sourceOffset) * 2,
            static_cast<std::size_t>(writableFrames) * 2 * sizeof(float)
        );

        if (ma_pcm_rb_commit_write(
                &route.ringBuffer,
                writableFrames
            ) != MA_SUCCESS)
        {
            break;
        }

        sourceOffset += writableFrames;
        remainingFrames -= writableFrames;
    }
}

void Audio::DestroyMicrophoneRoute(MicrophoneRoute& route)
{
    if (route.soundInitialized)
    {
        ma_sound_uninit(&route.sound);
    }

    route.soundInitialized = false;
    std::memset(&route.sound, 0, sizeof(route.sound));

    if (route.ringBufferInitialized)
    {
        ma_pcm_rb_uninit(&route.ringBuffer);
    }

    route.ringBufferInitialized = false;
    std::memset(&route.ringBuffer, 0, sizeof(route.ringBuffer));
}

bool Audio::InitializeMicrophone(
    ma_device_info* captureDevices,
    const ma_uint32 captureDeviceCount
)
{
    microphoneCapture_.deviceName.clear();
    microphoneCapture_.initialized = false;

    ma_device_config deviceConfig =
        ma_device_config_init(ma_device_type_capture);

    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = 2;
    deviceConfig.sampleRate = sampleRate_;
    deviceConfig.periodSizeInMilliseconds = bufferMilliseconds_;
    deviceConfig.dataCallback = &Audio::MicrophoneDataCallback;
    deviceConfig.notificationCallback = &Audio::DeviceNotificationCallback;
    deviceConfig.pUserData = this;

    if (IsDefaultDeviceRequest(requestedMicrophoneDevice_))
    {
        microphoneCapture_.deviceName = Localization::Text(
            "Windows varsayılan mikrofonu",
            "Windows default microphone"
        );
    }
    else
    {
        const auto selectedIndex = FindCaptureDevice(
            requestedMicrophoneDevice_,
            captureDevices,
            captureDeviceCount
        );

        if (!selectedIndex.has_value())
        {
            return false;
        }

        microphoneCapture_.deviceId = captureDevices[*selectedIndex].id;
        microphoneCapture_.deviceName = captureDevices[*selectedIndex].name;
        deviceConfig.capture.pDeviceID = &microphoneCapture_.deviceId;
    }

    const auto tryInitialize = [this, &deviceConfig](
        const ma_uint32 attemptSampleRate,
        const ma_uint32 attemptBufferMilliseconds
    )
    {
        std::memset(
            &microphoneCapture_.device,
            0,
            sizeof(microphoneCapture_.device)
        );

        deviceConfig.sampleRate = attemptSampleRate;
        deviceConfig.periodSizeInMilliseconds = attemptBufferMilliseconds;

        return ma_device_init(
            &context_,
            &deviceConfig,
            &microphoneCapture_.device
        );
    };

    ma_uint32 activeSampleRate = sampleRate_;
    ma_uint32 activeBufferMilliseconds = bufferMilliseconds_;

    ma_result result = tryInitialize(
        activeSampleRate,
        activeBufferMilliseconds
    );

    if (result != MA_SUCCESS && activeBufferMilliseconds != 0)
    {
        std::cerr
            << Localization::Text(
                "Uyarı: Mikrofon düşük gecikme buffer'ı ile açılamadı. Windows varsayılan buffer'ı deneniyor.\n",
                "Warning: The microphone could not open with the low-latency buffer. Retrying with the Windows default buffer.\n"
            );

        activeBufferMilliseconds = 0;
        result = tryInitialize(activeSampleRate, activeBufferMilliseconds);
    }

    if (result != MA_SUCCESS && activeSampleRate != 0)
    {
        std::cerr
            << Localization::Text(
                "Uyarı: Mikrofon istenen örnekleme hızında açılamadı. Cihazın doğal hızı deneniyor.\n",
                "Warning: The microphone could not open at the requested sample rate. Retrying at the device's native rate.\n"
            );

        activeSampleRate = 0;
        activeBufferMilliseconds = 0;
        result = tryInitialize(activeSampleRate, activeBufferMilliseconds);
    }

    if (result != MA_SUCCESS)
    {
        std::cerr
            << Localization::Text(
                "Mikrofon başlatılamadı. Hata: ",
                "The microphone could not be initialized. Error: "
            )
            << result
            << '\n';

        microphoneCapture_.deviceName.clear();
        return false;
    }

    microphoneCapture_.initialized = true;

    const ma_uint32 captureSampleRate =
        microphoneCapture_.device.sampleRate;

    if (microphoneToOutput_ &&
        !InitializeMicrophoneRoute(
            outputEngine_,
            captureSampleRate,
            microphoneOutputRoute_
        ))
    {
        return false;
    }

    if (microphoneToMonitor_)
    {
        if (!monitorEngine_.initialized)
        {
            std::cerr << Localization::Text(
                "Mikrofon monitöre yönlendirildi ancak monitör çıkışı kapalı.\n",
                "The microphone is routed to the monitor, but the monitor output is disabled.\n"
            );
            return false;
        }

        if (!InitializeMicrophoneRoute(
                monitorEngine_,
                captureSampleRate,
                microphoneMonitorRoute_
            ))
        {
            return false;
        }
    }

    result = ma_device_start(&microphoneCapture_.device);

    if (result != MA_SUCCESS)
    {
        std::cerr
            << Localization::Text(
                "Mikrofon yakalama başlatılamadı. Hata: ",
                "Microphone capture could not be started. Error: "
            )
            << result
            << '\n';
        return false;
    }

    std::cout
        << Localization::Text("Mikrofon: ", "Microphone: ")
        << microphoneCapture_.deviceName
        << Localization::Text(" | Ses: ", " | Volume: ")
        << static_cast<int>(microphoneVolume_ * 100.0f)
        << "% | "
        << Localization::Text("Yönlendirme: ", "Routing: ");

    if (microphoneToOutput_)
    {
        std::cout << Localization::Text("ana çıkış", "main output");
    }

    if (microphoneToOutput_ && microphoneToMonitor_)
    {
        std::cout << " + ";
    }

    if (microphoneToMonitor_)
    {
        std::cout << Localization::Text("monitör", "monitor");
    }

    std::cout
        << Localization::Text(" | Örnekleme: ", " | Sample rate: ")
        << captureSampleRate
        << " Hz\n";

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
            << Localization::Text(
                "Uyarı: WASAPI başlatılamadı. Miniaudio'nun Windows yedek backend sırası deneniyor. Hata: ",
                "Warning: WASAPI could not be initialized. Trying miniaudio's Windows fallback backend order. Error: "
            )
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
            << Localization::Text("Audio context başlatılamadı. Hata: ", "Audio context could not be initialized. Error: ")
            << contextResult
            << '\n';

        return false;
    }

    contextInitialized_ = true;

    std::cout
        << Localization::Text("Audio backend: ", "Audio backend: ")
        << BackendName(context_.backend)
        << '\n';

    ma_device_info* playbackDevices = nullptr;
    ma_uint32 playbackDeviceCount = 0;
    ma_device_info* captureDevices = nullptr;
    ma_uint32 captureDeviceCount = 0;

    const ma_result deviceResult =
        ma_context_get_devices(
            &context_,
            &playbackDevices,
            &playbackDeviceCount,
            &captureDevices,
            &captureDeviceCount
        );

    if (deviceResult != MA_SUCCESS)
    {
        std::cerr
            << Localization::Text("Ses cihazları alınamadı. Hata: ", "Audio devices could not be enumerated. Error: ")
            << deviceResult
            << '\n';

        DestroyRuntime();
        return false;
    }

    if (!InitializeEngine(
        requestedOutputDevice_,
        Localization::Text("Ana çıkış", "Main output"),
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
            Localization::Text("Monitör çıkışı", "Monitor output"),
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
                << Localization::Text(
                    "Uyarı: Ana çıkış ve monitör aynı cihaz. Ses iki kez oynatılabilir.\n",
                    "Warning: The main output and monitor output use the same device. Audio may play twice.\n"
                );
        }
    }
    else
    {
        std::cout << Localization::Text("Monitör çıkışı: Kapalı\n", "Monitor output: Disabled\n");
    }

    if (microphoneEnabled_)
    {
        if (!InitializeMicrophone(
                captureDevices,
                captureDeviceCount
            ))
        {
            DestroyRuntime();
            return false;
        }
    }
    else
    {
        std::cout << Localization::Text(
            "Mikrofon miksi: Kapalı\n",
            "Microphone mix: Disabled\n"
        );
    }

    return true;
}

bool Audio::Initialize(
    const std::string& requestedOutputDevice,
    const std::string& requestedMonitorDevice,
    const float outputVolume,
    const float monitorVolume,
    const bool microphoneEnabled,
    const std::string& requestedMicrophoneDevice,
    const float microphoneVolume,
    const bool microphoneToOutput,
    const bool microphoneToMonitor,
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
            << Localization::Text(
                "Aynı anda birden fazla Audio nesnesi başlatılamaz.\n",
                "More than one Audio instance cannot be initialized at the same time.\n"
            );

        return false;
    }

    const bool sampleRateIsValid =
        sampleRate == 0 ||
        (sampleRate >= 8000 && sampleRate <= 192000);

    const bool bufferIsValid =
        bufferMilliseconds == 0 ||
        (bufferMilliseconds >= 2 &&
            bufferMilliseconds <= 100);

    const bool microphoneSettingsAreValid =
        !microphoneEnabled ||
        (!requestedMicrophoneDevice.empty() &&
            microphoneVolume >= 0.0f &&
            microphoneVolume <= 1.0f &&
            (microphoneToOutput || microphoneToMonitor));

    if (!sampleRateIsValid || !bufferIsValid ||
        !microphoneSettingsAreValid)
    {
        std::cerr
            << Localization::Text(
                "Geçersiz audio veya mikrofon ayarı. sampleRate=",
                "Invalid audio or microphone setting. sampleRate="
            )
            << sampleRate
            << ", bufferMilliseconds="
            << bufferMilliseconds
            << ", microphoneEnabled="
            << (microphoneEnabled ? "true" : "false")
            << ", microphoneVolume="
            << microphoneVolume
            << '\n';

        return false;
    }

    requestedOutputDevice_ = requestedOutputDevice;
    requestedMonitorDevice_ = requestedMonitorDevice;
    requestedMicrophoneDevice_ = requestedMicrophoneDevice;
    outputVolume_ = outputVolume;
    monitorVolume_ = monitorVolume;
    microphoneEnabled_ = microphoneEnabled;
    microphoneVolume_ = microphoneVolume;
    microphoneToOutput_ = microphoneToOutput;
    microphoneToMonitor_ = microphoneToMonitor;
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
            << Localization::Text("Ses ana çıkışa yüklenemedi: ", "Sound could not be loaded into the main output: ")
            << PathToUtf8(definition.path)
            << Localization::Text(". Hata: ", ". Error: ")
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
                << Localization::Text("Ses monitör çıkışına yüklenemedi: ", "Sound could not be loaded into the monitor output: ")
                << PathToUtf8(definition.path)
                << Localization::Text(". Hata: ", ". Error: ")
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
            << Localization::Text("Overlap voice ana çıkış için oluşturulamadı: ", "Overlap voice could not be created for the main output: ")
            << PathToUtf8(definition.path)
            << Localization::Text(". Hata: ", ". Error: ")
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
                << Localization::Text("Overlap voice monitör çıkışı için oluşturulamadı: ", "Overlap voice could not be created for the monitor output: ")
                << PathToUtf8(definition.path)
                << Localization::Text(". Hata: ", ". Error: ")
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
        std::cerr << Localization::Text("Ana audio engine hazır değil.\n", "The main audio engine is not ready.\n");
        return false;
    }

    if (soundId.empty())
    {
        std::cerr << Localization::Text("Ses kimliği boş olamaz.\n", "The sound ID cannot be empty.\n");
        return false;
    }

    if (loadedSounds_.contains(soundId))
    {
        std::cerr
            << Localization::Text("Ses zaten yüklenmiş: ", "Sound is already loaded: ")
            << soundId
            << '\n';

        return false;
    }

    if (!std::filesystem::exists(definition.path) ||
        !std::filesystem::is_regular_file(definition.path))
    {
        std::cerr
            << Localization::Text("Ses dosyası bulunamadı: ", "Sound file not found: ")
            << PathToUtf8(definition.path)
            << '\n';

        return false;
    }

    LoadedSound loadedSound;
    loadedSound.mode = definition.mode;

    if (definition.mode == PlaybackMode::Overlap)
    {
        loadedSound.voices.reserve(OverlapVoiceCount);
    }
    else
    {
        loadedSound.voices.reserve(1);
    }

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
            << Localization::Text("Ana çıkıştaki ses hazırlanamadı: ", "The sound on the main output could not be prepared: ")
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
            << Localization::Text("Monitör sesi hazırlanamadı: ", "The monitor sound could not be prepared: ")
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
            << Localization::Text("Ana çıkıştaki ses başlatılamadı: ", "The sound on the main output could not be started: ")
            << soundId
            << Localization::Text(". Hata: ", ". Error: ")
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
                << Localization::Text("Monitör sesi başlatılamadı: ", "The monitor sound could not be started: ")
                << soundId
                << Localization::Text(". Hata: ", ". Error: ")
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
            << Localization::Text("Ses zaten kayıtlı: ", "Sound is already registered: ")
            << soundId
            << '\n';

        return false;
    }

    if (!SoundFileFormat::IsSupported(soundPath))
    {
        const std::string extension =
            SoundFileFormat::NormalizedExtension(soundPath);

        std::cerr
            << Localization::Text("Desteklenmeyen ses dosyası uzantısı: ", "Unsupported sound file extension: ")
            << (extension.empty()
                ? Localization::Text("<uzantı yok>", "<no extension>")
                : extension)
            << Localization::Text("\nDosya: ", "\nFile: ")
            << PathToUtf8(soundPath)
            << Localization::Text("\nDesteklenen uzantılar: ", "\nSupported extensions: ")
            << SoundFileFormat::SupportedExtensions()
            << '\n';

        return false;
    }

    if (volume < 0.0f || volume > 1.0f)
    {
        std::cerr
            << Localization::Text("Ses seviyesi 0.00 ile 1.00 arasında olmalı: ", "Volume must be between 0.00 and 1.00: ")
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
            << Localization::Text(
                "Ses sistemi şu anda hazır değil veya yüklenmiş ses bulunamadı: ",
                "The audio system is not ready or the loaded sound could not be found: "
            )
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
            << Localization::Text(" mute durumu değiştirilemedi. Hata: ", " mute state could not be changed. Error: ")
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
        Localization::Text("Ana çıkış", "Main output")
    );
}

MuteToggleResult Audio::ToggleMonitorMute()
{
    return ToggleEngineMute(
        monitorEngine_,
        monitorVolume_,
        monitorMuted_,
        Localization::Text("Monitör çıkışı", "Monitor output")
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

bool Audio::IsMicrophoneRunning() const
{
    if (!microphoneEnabled_ || !microphoneCapture_.initialized)
    {
        return !microphoneEnabled_;
    }

    const ma_device_state deviceState =
        ma_device_get_state(&microphoneCapture_.device);

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

    if (!IsMicrophoneRunning())
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

AudioLevelSnapshot Audio::GetLevelSnapshot() const
{
    AudioLevelSnapshot snapshot;

    snapshot.outputAvailable = outputEngine_.initialized;
    snapshot.monitorAvailable = monitorEngine_.initialized;
    snapshot.microphoneAvailable =
        microphoneEnabled_ && microphoneCapture_.initialized;

    float activeOutputSoundLevel = 0.0f;
    float activeMonitorSoundLevel = 0.0f;

    for (const auto& [soundId, loadedSound] : loadedSounds_)
    {
        const auto definitionIterator =
            soundDefinitions_.find(soundId);

        const float configuredLevel = definitionIterator !=
            soundDefinitions_.end()
                ? definitionIterator->second.volume
                : 1.0f;

        for (const Voice& voice : loadedSound.voices)
        {
            if (voice.outputSound &&
                ma_sound_is_playing(voice.outputSound.get()) == MA_TRUE)
            {
                activeOutputSoundLevel = std::max(
                    activeOutputSoundLevel,
                    configuredLevel
                );
            }

            if (voice.monitorSound &&
                ma_sound_is_playing(voice.monitorSound.get()) == MA_TRUE)
            {
                activeMonitorSoundLevel = std::max(
                    activeMonitorSoundLevel,
                    configuredLevel
                );
            }
        }
    }

    const float microphonePeak = snapshot.microphoneAvailable
        ? std::clamp(
            microphonePeak_.load(std::memory_order_relaxed),
            0.0f,
            1.0f
        )
        : 0.0f;

    snapshot.microphone = microphonePeak;

    if (microphoneToOutput_ && snapshot.microphoneAvailable)
    {
        activeOutputSoundLevel = std::max(
            activeOutputSoundLevel,
            microphonePeak * microphoneVolume_
        );
    }

    if (microphoneToMonitor_ && snapshot.microphoneAvailable)
    {
        activeMonitorSoundLevel = std::max(
            activeMonitorSoundLevel,
            microphonePeak * microphoneVolume_
        );
    }

    snapshot.output = snapshot.outputAvailable && !outputMuted_
        ? std::clamp(
            activeOutputSoundLevel * outputVolume_,
            0.0f,
            1.0f
        )
        : 0.0f;

    snapshot.monitor = snapshot.monitorAvailable && !monitorMuted_
        ? std::clamp(
            activeMonitorSoundLevel * monitorVolume_,
            0.0f,
            1.0f
        )
        : 0.0f;

    return snapshot;
}

void Audio::DestroyRuntime()
{
    microphonePeak_.store(0.0f, std::memory_order_relaxed);

    if (microphoneCapture_.initialized)
    {
        ma_device_uninit(&microphoneCapture_.device);
    }

    microphoneCapture_.initialized = false;
    microphoneCapture_.deviceName.clear();
    std::memset(
        &microphoneCapture_.device,
        0,
        sizeof(microphoneCapture_.device)
    );

    DestroyMicrophoneRoute(microphoneOutputRoute_);
    DestroyMicrophoneRoute(microphoneMonitorRoute_);

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
    requestedMicrophoneDevice_.clear();

    microphoneEnabled_ = false;
    microphoneVolume_ = 1.0f;
    microphoneToOutput_ = true;
    microphoneToMonitor_ = false;

    sampleRate_ = 48000;
    bufferMilliseconds_ = 5;

    desiredConfigurationSet_ = false;

    recoveryRequested_.store(false, std::memory_order_release);

    ignoreDeviceNotifications_.store(
        false,
        std::memory_order_release
    );
}
