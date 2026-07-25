#pragma once

#include "editor/AudioDocument.hpp"
#include "miniaudio/miniaudio.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

enum class AudioPreviewState
{
    Stopped,
    Playing,
    Paused,
    Finished
};

class AudioPreviewPlayer final
{
public:
    AudioPreviewPlayer() = default;
    ~AudioPreviewPlayer();

    AudioPreviewPlayer(const AudioPreviewPlayer&) = delete;
    AudioPreviewPlayer& operator=(const AudioPreviewPlayer&) = delete;

    bool Prepare(
        const AudioDocument& document,
        std::string_view requestedDevice,
        std::string& errorMessage
    );
    [[nodiscard]] bool Matches(
        const AudioDocument& document,
        std::string_view requestedDevice
    ) const;

    bool PlayFrom(std::size_t frame, std::string& errorMessage);
    bool Pause(std::string& errorMessage);
    bool Resume(std::string& errorMessage);
    bool Stop(std::string& errorMessage);
    bool Seek(std::size_t frame, std::string& errorMessage);
    bool FinalizeFinished(std::string& errorMessage);

    void SetVolume(float volume) noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] AudioPreviewState State() const noexcept;
    [[nodiscard]] std::size_t CurrentFrame() const noexcept;
    [[nodiscard]] std::size_t FrameCount() const noexcept;
    [[nodiscard]] bool IsPrepared() const noexcept;

private:
    static void DataCallback(
        ma_device* device,
        void* outputFrames,
        const void* inputFrames,
        ma_uint32 frameCount
    ) noexcept;

    void Render(float* outputFrames, ma_uint32 frameCount) noexcept;
    bool StopDevice(std::string& errorMessage) noexcept;
    static std::string DescribeResult(ma_result result);

    ma_context context_{};
    ma_device device_{};
    ma_device_id playbackDeviceId_{};
    const float* samples_ = nullptr;
    std::size_t sampleCount_ = 0U;
    std::size_t frameCount_ = 0U;
    std::uint32_t channelCount_ = 0U;
    std::uint32_t sampleRate_ = 0U;
    std::uint64_t documentRevision_ = 0U;
    std::string deviceRequestKey_;
    bool contextInitialized_ = false;
    bool initialized_ = false;
    bool deviceStarted_ = false;
    std::atomic<std::size_t> currentFrame_{0U};
    std::atomic<float> volume_{1.0f};
    std::atomic<AudioPreviewState> state_{AudioPreviewState::Stopped};
};
