#pragma once

#include "audio/MicrophoneProcessingRuntime.hpp"
#include "audio/PlaybackState.hpp"

#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
#include "audio/AecRenderReferenceMixer.hpp"
#endif
#include "miniaudio/miniaudio.h"
#include "sound/PlaybackMode.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
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
    Ignored,
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

enum class MicrophoneTestMonitorResult
{
    Enabled,
    Disabled,
    AlreadyRouted,
    Unavailable,
    Failed
};

struct AudioLevelSnapshot
{
    float output = 0.0f;
    float monitor = 0.0f;
    float microphone = 0.0f;
    float microphoneRaw = 0.0f;
    float microphoneProcessed = 0.0f;
    float microphoneRawRms = 0.0f;
    float microphoneProcessedRms = 0.0f;
    float microphoneVoiceActivityProbability = 0.0f;
    float microphoneAgcGainDb = 0.0f;
    int microphoneEchoCancellationError = 0;

    bool outputAvailable = false;
    bool monitorAvailable = false;
    bool microphoneAvailable = false;
    bool microphoneProcessingActive = false;
    bool microphoneTestMonitorActive = false;
    bool microphoneNoiseSuppressionActive = false;
    bool microphoneNoiseSuppressionFailed = false;
    bool microphoneEchoCancellationRequested = false;
    bool microphoneEchoCancellationReady = false;
    bool microphoneEchoCancellationReferenceAvailable = false;
    bool microphoneEchoCancellationActive = false;
    bool microphoneEchoCancellationFailed = false;
    bool microphoneAgcActive = false;
    bool microphoneInputClipped = false;
    bool microphoneInvalidSampleDetected = false;

    std::uint64_t microphoneDroppedInputFrames = 0;
    std::uint64_t microphoneEchoCancellationReferenceUnderruns = 0;
    std::uint64_t microphoneEchoCancellationFailures = 0;
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

    static std::vector<std::string> EnumeratePlaybackDevices();
    static std::vector<std::string> EnumerateCaptureDevices();

    bool Initialize(
        const std::string& requestedOutputDevice,
        const std::string& requestedMonitorDevice,
        float outputVolume,
        float monitorVolume,
        bool microphoneEnabled,
        const std::string& requestedMicrophoneDevice,
        float microphoneVolume,
        bool microphoneToOutput,
        bool microphoneToMonitor,
        const MicrophoneProcessingSettings& microphoneProcessingSettings,
        unsigned int sampleRate,
        unsigned int bufferMilliseconds
    );

    bool LoadSound(
        const std::string& soundId,
        const std::filesystem::path& soundPath,
        float volume,
        PlaybackMode mode,
        unsigned int fadeInMilliseconds,
        unsigned int fadeOutMilliseconds
    );

    PlaybackResult PlayLoaded(const std::string& soundId);
    bool StopAll(bool immediate = false);

    [[nodiscard]] std::vector<PlaybackSnapshot>
        GetPlaybackSnapshots() const;
    bool PausePlayback(PlaybackId playbackId);
    bool ResumePlayback(PlaybackId playbackId);
    bool StopPlayback(PlaybackId playbackId);
    bool SeekPlayback(PlaybackId playbackId, float positionSeconds);
    bool SetPlaybackVolume(PlaybackId playbackId, float volume);

    MuteToggleResult ToggleOutputMute();
    MuteToggleResult ToggleMonitorMute();

    MicrophoneTestMonitorResult SetMicrophoneTestMonitorEnabled(
        bool enabled
    );
    bool IsMicrophoneTestMonitorEnabled() const noexcept;

    AudioRecoveryResult MaintainDeviceConnection();
    AudioLevelSnapshot GetLevelSnapshot() const;

    void Shutdown();

private:
    struct EngineState
    {
        ma_engine engine{};
        ma_device_id deviceId{};

        std::string deviceName;

        bool initialized = false;
    };

    struct CaptureState
    {
        ma_device device{};
        ma_device_id deviceId{};

        std::string deviceName;

        bool initialized = false;
    };

    struct MicrophoneRoute
    {
        ma_pcm_rb ringBuffer{};
        ma_sound sound{};

        bool ringBufferInitialized = false;
        bool soundInitialized = false;
    };

    struct SoundDefinition
    {
        std::filesystem::path path;
        float volume = 1.0f;
        PlaybackMode mode = PlaybackMode::Restart;
        unsigned int fadeInMilliseconds = 0;
        unsigned int fadeOutMilliseconds = 0;
    };

    struct Voice
    {
        std::unique_ptr<ma_sound> outputSound;
        std::unique_ptr<ma_sound> monitorSound;

#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
        std::unique_ptr<ma_sound> aecReferenceSound;
#endif

        PlaybackId playbackId = InvalidPlaybackId;
        float volume = 1.0f;
        unsigned int fadeInMilliseconds = 0;
        unsigned int fadeOutMilliseconds = 0;
        bool paused = false;
        bool stopping = false;
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

    static void MicrophoneDataCallback(
        ma_device* device,
        void* outputFrames,
        const void* inputFrames,
        ma_uint32 frameCount
    );

    static void ProcessedMicrophoneOutputCallback(
        void* context,
        const float* interleavedStereoFrames,
        ma_uint32 frameCount
    ) noexcept;

#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    static bool ReadAecRenderReferenceCallback(
        void* context,
        float* interleavedStereoFrames,
        ma_uint32 frameCount
    ) noexcept;

    [[nodiscard]] int EstimateAecStreamDelayMilliseconds() const noexcept;
#endif

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

    bool InitializeMicrophone(
        ma_device_info* captureDevices,
        ma_uint32 captureDeviceCount
    );

    bool InitializeMicrophoneRoute(
        EngineState& engineState,
        ma_uint32 sampleRate,
        MicrophoneRoute& route
    );

    static void WriteMicrophoneFrames(
        MicrophoneRoute& route,
        const void* inputFrames,
        ma_uint32 frameCount
    );

    static void DestroyMicrophoneRoute(MicrophoneRoute& route);

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
        const std::string& soundId,
        bool beginNewPlayback = true
    );

    bool StopVoice(
        Voice& voice,
        const std::string& soundId,
        bool immediate
    );

    [[nodiscard]] PlaybackId AllocatePlaybackId() noexcept;
    Voice* FindVoiceByPlaybackId(
        PlaybackId playbackId,
        const std::string** soundId = nullptr
    );

    static bool IsVoicePlaying(const Voice& voice);
    static bool IsVoiceActive(const Voice& voice);
    static void DestroyVoice(Voice& voice);
    static void DestroyLoadedSound(LoadedSound& loadedSound);

    MuteToggleResult ToggleEngineMute(
        EngineState& state,
        float configuredVolume,
        bool& muted,
        const std::string& engineLabel
    );

    bool IsEngineRunning(EngineState& state) const;
    bool IsMicrophoneRunning() const;
    bool IsRuntimeHealthy();

    void DestroyRuntime();

    static std::atomic<Audio*> activeInstance_;

    ma_context context_{};

    EngineState outputEngine_;
    EngineState monitorEngine_;
    CaptureState microphoneCapture_;
    MicrophoneRoute microphoneOutputRoute_;
    MicrophoneRoute microphoneMonitorRoute_;
    MicrophoneRoute microphoneTestMonitorRoute_;
    MicrophoneProcessingRuntime microphoneProcessingRuntime_;

#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    AecRenderReferenceMixer aecRenderReferenceMixer_;
#endif

    std::unordered_map<std::string, LoadedSound> loadedSounds_;
    std::unordered_map<std::string, SoundDefinition> soundDefinitions_;
    std::atomic<PlaybackId> nextPlaybackId_{1};

    std::string requestedOutputDevice_;
    std::string requestedMonitorDevice_;
    std::string requestedMicrophoneDevice_;

    float outputVolume_ = 1.0f;
    float monitorVolume_ = 0.30f;
    float microphoneVolume_ = 1.0f;

    bool microphoneEnabled_ = false;
    bool microphoneToOutput_ = true;
    bool microphoneToMonitor_ = false;
    MicrophoneProcessingSettings microphoneProcessingSettings_{};

    ma_uint32 sampleRate_ = 48000;
    ma_uint32 bufferMilliseconds_ = 5;

    bool outputMuted_ = false;
    bool monitorMuted_ = false;

    std::atomic_bool recoveryRequested_{false};
    std::atomic_bool ignoreDeviceNotifications_{false};
    std::atomic<float> microphonePeak_{0.0f};
    std::atomic_bool microphoneProcessingActive_{false};
    std::atomic_bool microphoneTestMonitorEnabled_{false};

    bool contextInitialized_ = false;
    bool desiredConfigurationSet_ = false;
};
