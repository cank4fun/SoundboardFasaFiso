#include "editor/AudioPreviewPlayer.hpp"

#include <algorithm>
#include <string>

AudioPreviewPlayer::~AudioPreviewPlayer()
{
    Shutdown();
}

bool AudioPreviewPlayer::Prepare(
    const AudioDocument& document,
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

    const std::span<const float> samples = document.Samples();
    if (samples.empty())
    {
        errorMessage = "The audio document does not contain samples.";
        return false;
    }

    samples_ = samples.data();
    sampleCount_ = samples.size();
    frameCount_ = document.FrameCount();
    channelCount_ = document.ChannelCount();
    sampleRate_ = document.SampleRate();
    documentRevision_ = document.Revision();

    ma_device_config configuration = ma_device_config_init(
        ma_device_type_playback
    );
    configuration.playback.format = ma_format_f32;
    configuration.playback.channels = 2U;
    configuration.sampleRate = sampleRate_;
    configuration.dataCallback = &AudioPreviewPlayer::DataCallback;
    configuration.pUserData = this;

    const ma_result result = ma_device_init(nullptr, &configuration, &device_);
    if (result != MA_SUCCESS)
    {
        errorMessage = "The preview audio device could not be initialized: " +
            DescribeResult(result);
        samples_ = nullptr;
        sampleCount_ = 0U;
        frameCount_ = 0U;
        channelCount_ = 0U;
        sampleRate_ = 0U;
        documentRevision_ = 0U;
        return false;
    }

    initialized_ = true;
    currentFrame_.store(0U, std::memory_order_release);
    state_.store(AudioPreviewState::Stopped, std::memory_order_release);
    return true;
}

bool AudioPreviewPlayer::Matches(
    const AudioDocument& document
) const noexcept
{
    const std::span<const float> samples = document.Samples();
    return initialized_ && samples_ == samples.data() &&
        sampleCount_ == samples.size() &&
        frameCount_ == document.FrameCount() &&
        channelCount_ == document.ChannelCount() &&
        sampleRate_ == document.SampleRate() &&
        documentRevision_ == document.Revision();
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

    initialized_ = false;
    samples_ = nullptr;
    sampleCount_ = 0U;
    frameCount_ = 0U;
    channelCount_ = 0U;
    sampleRate_ = 0U;
    documentRevision_ = 0U;
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
        outputFrames[index * 2U] = left;
        outputFrames[index * 2U + 1U] = right;
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
