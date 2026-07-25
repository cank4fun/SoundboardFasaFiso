#include "editor/AudioPreviewPlayer.hpp"
#include "editor/AudioPreviewDeviceSelection.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

AudioPreviewPlayer::~AudioPreviewPlayer()
{
    Shutdown();
}

bool AudioPreviewPlayer::Prepare(
    const AudioDocument& document,
    const std::string_view requestedDevice,
    std::string& errorMessage
)
{
    errorMessage.clear();
    Shutdown();

    if (document.Empty() || document.SampleRate() == 0U ||
        document.ChannelCount() == 0U)
    {
        errorMessage = "The audio document is empty or invalid.";
        return false;
    }

    const AudioPreviewDeviceRequestKind requestKind =
        ClassifyAudioPreviewDeviceRequest(requestedDevice);
    if (requestKind == AudioPreviewDeviceRequestKind::Disabled)
    {
        errorMessage =
            "The configured monitor output is disabled. Select a monitor "
            "output before previewing audio.";
        return false;
    }

    const ma_backend preferredBackends[]{ma_backend_wasapi};
    ma_result result = ma_context_init(
        preferredBackends,
        1U,
        nullptr,
        &context_
    );
    if (result != MA_SUCCESS)
    {
        std::memset(&context_, 0, sizeof(context_));
        result = ma_context_init(nullptr, 0U, nullptr, &context_);
    }

    if (result != MA_SUCCESS)
    {
        errorMessage = "The preview audio context could not be initialized: " +
            DescribeResult(result);
        return false;
    }
    contextInitialized_ = true;

    ma_device_config configuration = ma_device_config_init(
        ma_device_type_playback
    );
    configuration.playback.format = ma_format_f32;
    configuration.playback.channels = 2U;
    configuration.sampleRate = document.SampleRate();
    configuration.dataCallback = &AudioPreviewPlayer::DataCallback;
    configuration.pUserData = this;

    if (requestKind == AudioPreviewDeviceRequestKind::Named)
    {
        ma_device_info* playbackDevices = nullptr;
        ma_uint32 playbackDeviceCount = 0U;
        result = ma_context_get_devices(
            &context_,
            &playbackDevices,
            &playbackDeviceCount,
            nullptr,
            nullptr
        );
        if (result != MA_SUCCESS)
        {
            errorMessage = "Playback devices could not be enumerated: " +
                DescribeResult(result);
            Shutdown();
            return false;
        }

        std::vector<std::string> deviceNames;
        deviceNames.reserve(static_cast<std::size_t>(playbackDeviceCount));
        for (ma_uint32 index = 0U; index < playbackDeviceCount; ++index)
        {
            deviceNames.emplace_back(playbackDevices[index].name);
        }

        const AudioPreviewDeviceMatchResult match =
            FindAudioPreviewDeviceMatch(
                requestedDevice,
                std::span<const std::string>{deviceNames}
            );
        if (match.status != AudioPreviewDeviceMatchStatus::Found ||
            !match.index.has_value())
        {
            errorMessage = match.status ==
                    AudioPreviewDeviceMatchStatus::Ambiguous
                ? "The configured monitor output matched more than one "
                    "playback device."
                : "The configured monitor output was not found.";
            Shutdown();
            return false;
        }

        const std::size_t matchedIndex = *match.index;
        if (matchedIndex >= static_cast<std::size_t>(playbackDeviceCount))
        {
            errorMessage = "The matched monitor output index is invalid.";
            Shutdown();
            return false;
        }

        playbackDeviceId_ = playbackDevices[matchedIndex].id;
        configuration.playback.pDeviceID = &playbackDeviceId_;
    }

    const std::span<const float> samples = document.Samples();
    if (samples.empty())
    {
        errorMessage = "The audio document does not contain samples.";
        Shutdown();
        return false;
    }

    samples_ = samples.data();
    sampleCount_ = samples.size();
    frameCount_ = document.FrameCount();
    channelCount_ = document.ChannelCount();
    sampleRate_ = document.SampleRate();
    documentRevision_ = document.Revision();
    deviceRequestKey_ = NormalizeAudioPreviewDeviceRequest(requestedDevice);

    result = ma_device_init(&context_, &configuration, &device_);
    if (result != MA_SUCCESS)
    {
        errorMessage = "The preview audio device could not be initialized: " +
            DescribeResult(result);
        Shutdown();
        return false;
    }

    initialized_ = true;
    currentFrame_.store(0U, std::memory_order_release);
    state_.store(AudioPreviewState::Stopped, std::memory_order_release);
    return true;
}

bool AudioPreviewPlayer::Matches(
    const AudioDocument& document,
    const std::string_view requestedDevice
) const
{
    const std::span<const float> samples = document.Samples();
    return initialized_ && samples_ == samples.data() &&
        sampleCount_ == samples.size() &&
        frameCount_ == document.FrameCount() &&
        channelCount_ == document.ChannelCount() &&
        sampleRate_ == document.SampleRate() &&
        documentRevision_ == document.Revision() &&
        deviceRequestKey_ ==
            NormalizeAudioPreviewDeviceRequest(requestedDevice);
}

bool AudioPreviewPlayer::PlayFrom(
    std::size_t frame,
    std::string& errorMessage
)
{
    errorMessage.clear();
    if (!initialized_)
    {
        errorMessage = "The preview audio device is not prepared.";
        return false;
    }

    if (!StopDevice(errorMessage))
    {
        return false;
    }

    if (frame >= frameCount_)
    {
        frame = 0U;
    }

    currentFrame_.store(frame, std::memory_order_release);
    state_.store(AudioPreviewState::Playing, std::memory_order_release);

    const ma_result result = ma_device_start(&device_);
    if (result != MA_SUCCESS)
    {
        state_.store(AudioPreviewState::Stopped, std::memory_order_release);
        errorMessage = "The preview audio device could not be started: " +
            DescribeResult(result);
        return false;
    }

    deviceStarted_ = true;
    return true;
}

bool AudioPreviewPlayer::Pause(std::string& errorMessage)
{
    errorMessage.clear();
    if (State() != AudioPreviewState::Playing)
    {
        return true;
    }

    if (!StopDevice(errorMessage))
    {
        return false;
    }

    const AudioPreviewState nextState = CurrentFrame() >= frameCount_
        ? AudioPreviewState::Finished
        : AudioPreviewState::Paused;
    state_.store(nextState, std::memory_order_release);
    return true;
}

bool AudioPreviewPlayer::Resume(std::string& errorMessage)
{
    const AudioPreviewState currentState = State();
    if (currentState == AudioPreviewState::Playing)
    {
        errorMessage.clear();
        return true;
    }

    const std::size_t frame = currentState == AudioPreviewState::Finished
        ? 0U
        : CurrentFrame();
    return PlayFrom(frame, errorMessage);
}

bool AudioPreviewPlayer::Stop(std::string& errorMessage)
{
    errorMessage.clear();
    if (!StopDevice(errorMessage))
    {
        return false;
    }

    currentFrame_.store(0U, std::memory_order_release);
    state_.store(AudioPreviewState::Stopped, std::memory_order_release);
    return true;
}

bool AudioPreviewPlayer::Seek(
    const std::size_t frame,
    std::string& errorMessage
)
{
    errorMessage.clear();
    if (!initialized_)
    {
        errorMessage = "The preview audio device is not prepared.";
        return false;
    }

    const AudioPreviewState previousState = State();
    if (!StopDevice(errorMessage))
    {
        return false;
    }

    const std::size_t clampedFrame = std::min(frame, frameCount_);
    currentFrame_.store(clampedFrame, std::memory_order_release);

    if (clampedFrame >= frameCount_)
    {
        state_.store(AudioPreviewState::Finished, std::memory_order_release);
        return true;
    }

    if (previousState == AudioPreviewState::Playing)
    {
        state_.store(AudioPreviewState::Playing, std::memory_order_release);
        const ma_result result = ma_device_start(&device_);
        if (result != MA_SUCCESS)
        {
            state_.store(AudioPreviewState::Paused, std::memory_order_release);
            errorMessage = "The preview audio device could not resume after seeking: " +
                DescribeResult(result);
            return false;
        }

        deviceStarted_ = true;
        return true;
    }

    state_.store(
        previousState == AudioPreviewState::Paused
            ? AudioPreviewState::Paused
            : AudioPreviewState::Stopped,
        std::memory_order_release
    );
    return true;
}

bool AudioPreviewPlayer::FinalizeFinished(std::string& errorMessage)
{
    errorMessage.clear();
    if (State() != AudioPreviewState::Finished)
    {
        return true;
    }

    return StopDevice(errorMessage);
}

void AudioPreviewPlayer::SetVolume(const float volume) noexcept
{
    const float safeVolume = std::isfinite(volume)
        ? std::clamp(volume, 0.0f, 1.0f)
        : 1.0f;
    volume_.store(safeVolume, std::memory_order_release);
}

void AudioPreviewPlayer::Shutdown() noexcept
{
    if (deviceStarted_)
    {
        ma_device_stop(&device_);
        deviceStarted_ = false;
    }

    if (initialized_)
    {
        ma_device_uninit(&device_);
    }

    if (contextInitialized_)
    {
        ma_context_uninit(&context_);
    }

    initialized_ = false;
    contextInitialized_ = false;
    samples_ = nullptr;
    sampleCount_ = 0U;
    frameCount_ = 0U;
    channelCount_ = 0U;
    sampleRate_ = 0U;
    documentRevision_ = 0U;
    deviceRequestKey_.clear();
    std::memset(&playbackDeviceId_, 0, sizeof(playbackDeviceId_));
    currentFrame_.store(0U, std::memory_order_release);
    state_.store(AudioPreviewState::Stopped, std::memory_order_release);
}

AudioPreviewState AudioPreviewPlayer::State() const noexcept
{
    return state_.load(std::memory_order_acquire);
}

std::size_t AudioPreviewPlayer::CurrentFrame() const noexcept
{
    return std::min(
        currentFrame_.load(std::memory_order_acquire),
        frameCount_
    );
}

std::size_t AudioPreviewPlayer::FrameCount() const noexcept
{
    return frameCount_;
}

bool AudioPreviewPlayer::IsPrepared() const noexcept
{
    return initialized_;
}

void AudioPreviewPlayer::DataCallback(
    ma_device* const device,
    void* const outputFrames,
    const void*,
    const ma_uint32 frameCount
) noexcept
{
    if (device == nullptr || outputFrames == nullptr)
    {
        return;
    }

    auto* player = static_cast<AudioPreviewPlayer*>(device->pUserData);
    if (player == nullptr)
    {
        return;
    }

    player->Render(static_cast<float*>(outputFrames), frameCount);
}

void AudioPreviewPlayer::Render(
    float* const outputFrames,
    const ma_uint32 frameCount
) noexcept
{
    const std::size_t outputSampleCount =
        static_cast<std::size_t>(frameCount) * 2U;
    std::fill_n(outputFrames, outputSampleCount, 0.0f);

    if (state_.load(std::memory_order_acquire) !=
            AudioPreviewState::Playing ||
        samples_ == nullptr || channelCount_ == 0U)
    {
        return;
    }

    const std::size_t startFrame = currentFrame_.load(
        std::memory_order_acquire
    );
    if (startFrame >= frameCount_)
    {
        state_.store(AudioPreviewState::Finished, std::memory_order_release);
        return;
    }

    const std::size_t requestedFrames = static_cast<std::size_t>(frameCount);
    const std::size_t availableFrames = frameCount_ - startFrame;
    const std::size_t framesToRender = std::min(
        requestedFrames,
        availableFrames
    );

    const float volume = volume_.load(std::memory_order_relaxed);
    for (std::size_t index = 0U; index < framesToRender; ++index)
    {
        const std::size_t sourceOffset =
            (startFrame + index) * static_cast<std::size_t>(channelCount_);
        if (sourceOffset >= sampleCount_)
        {
            break;
        }

        const float left = samples_[sourceOffset];
        const float right = channelCount_ == 1U ||
            sourceOffset + 1U >= sampleCount_
            ? left
            : samples_[sourceOffset + 1U];
        outputFrames[index * 2U] = left * volume;
        outputFrames[index * 2U + 1U] = right * volume;
    }

    const std::size_t nextFrame = startFrame + framesToRender;
    currentFrame_.store(nextFrame, std::memory_order_release);
    if (nextFrame >= frameCount_)
    {
        state_.store(AudioPreviewState::Finished, std::memory_order_release);
    }
}

bool AudioPreviewPlayer::StopDevice(std::string& errorMessage) noexcept
{
    if (!deviceStarted_)
    {
        return true;
    }

    const ma_result result = ma_device_stop(&device_);
    if (result != MA_SUCCESS)
    {
        errorMessage = "The preview audio device could not be stopped: " +
            DescribeResult(result);
        return false;
    }

    deviceStarted_ = false;
    return true;
}

std::string AudioPreviewPlayer::DescribeResult(const ma_result result)
{
    const char* const description = ma_result_description(result);
    return description == nullptr ? "unknown miniaudio error" : description;
}
