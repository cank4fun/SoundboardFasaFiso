#pragma once

#include "miniaudio/miniaudio.h"
#include "sound/PlaybackMode.hpp"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class AudioRecoveryResult
{
    NotNeeded,
    Recovered,
    Failed
};

enum class PlaybackResult
{
    Started,
    Stopped,
    Unsupported,
    Failed
};

enum class MuteToggleResult
{
    Muted,
    Unmuted,
    Unavailable,
    Failed
};

class Audio
{
public:
    Audio() = default;
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    Audio(Audio&&) = delete;
    Audio& operator=(Audio&&) = delete;

    bool Initialize(
        const std::string& requestedOutputDevice,
        const std::string& requestedMonitorDevice,
        float outputVolume,
        float monitorVolume,
        unsigned int sampleRate,
        unsigned int bufferMilliseconds
    );

    bool LoadSound(
        const std::string& soundId,
        const std::filesystem::path& soundPath,
        float volume,
        PlaybackMode mode
    );

    PlaybackResult PlayLoaded(const std::string& soundId);
    bool StopAll();

    MuteToggleResult ToggleOutputMute();
    MuteToggleResult ToggleMonitorMute();

    AudioRecoveryResult MaintainDeviceConnection();

    void Shutdown();

private:
    struct EngineState
    {
        ma_engine engine{};
        ma_device_id deviceId{};

        std::string deviceName;

        bool initialized = false;
    };

    struct SoundDefinition
    {
        std::filesystem::path path;
        float volume = 1.0f;
        PlaybackMode mode = PlaybackMode::Restart;
    };

    struct Voice
    {
        std::unique_ptr<ma_sound> outputSound;
        std::unique_ptr<ma_sound> monitorSound;
    };

    struct LoadedSound
    {
        std::vector<Voice> voices;
        PlaybackMode mode = PlaybackMode::Restart;
        std::size_t nextOverlapVoice = 0;
    };

    static void DeviceNotificationCallback(
        const ma_device_notification* notification
    );

    bool InitializeRuntime();

    bool InitializeEngine(
        const std::string& requestedDevice,
        const std::string& engineLabel,
        ma_device_info* playbackDevices,
        ma_uint32 playbackDeviceCount,
        float volume,
        bool muted,
        ma_uint32 sampleRate,
        ma_uint32 bufferMilliseconds,
        EngineState& state
    );

    bool LoadSoundIntoRuntime(
        const std::string& soundId,
        const SoundDefinition& definition
    );

    bool InitializeVoiceFromFile(
        const SoundDefinition& definition,
        Voice& voice
    );

    bool InitializeVoiceCopy(
        const Voice& sourceVoice,
        const SoundDefinition& definition,
        Voice& voice
    );

    bool PrepareVoiceForPlayback(
        Voice& voice,
        const std::string& soundId
    );

    bool StartVoice(
        Voice& voice,
        const std::string& soundId
    );

    static bool IsVoicePlaying(const Voice& voice);
    static void DestroyVoice(Voice& voice);
    static void DestroyLoadedSound(LoadedSound& loadedSound);

    MuteToggleResult ToggleEngineMute(
        EngineState& state,
        float configuredVolume,
        bool& muted,
        const std::string& engineLabel
    );

    bool IsEngineRunning(EngineState& state) const;
    bool IsRuntimeHealthy();

    void DestroyRuntime();

    static std::atomic<Audio*> activeInstance_;

    ma_context context_{};

    EngineState outputEngine_;
    EngineState monitorEngine_;

    std::unordered_map<std::string, LoadedSound> loadedSounds_;
    std::unordered_map<std::string, SoundDefinition> soundDefinitions_;

    std::string requestedOutputDevice_;
    std::string requestedMonitorDevice_;

    float outputVolume_ = 1.0f;
    float monitorVolume_ = 0.30f;

    ma_uint32 sampleRate_ = 48000;
    ma_uint32 bufferMilliseconds_ = 5;

    bool outputMuted_ = false;
    bool monitorMuted_ = false;

    std::atomic_bool recoveryRequested_{false};
    std::atomic_bool ignoreDeviceNotifications_{false};

    bool contextInitialized_ = false;
    bool desiredConfigurationSet_ = false;
};
