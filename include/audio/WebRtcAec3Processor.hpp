#pragma once

#include <cstddef>
#include <memory>
#include <span>

class WebRtcAec3Processor
{
public:
    static constexpr int ProcessingSampleRate = 48'000;
    static constexpr std::size_t SamplesPerBlock = 480;

    WebRtcAec3Processor();
    ~WebRtcAec3Processor();

    WebRtcAec3Processor(const WebRtcAec3Processor&) = delete;
    WebRtcAec3Processor& operator=(const WebRtcAec3Processor&) = delete;

    WebRtcAec3Processor(WebRtcAec3Processor&&) = delete;
    WebRtcAec3Processor& operator=(WebRtcAec3Processor&&) = delete;

    bool Initialize();

    bool ProcessBlock(
        std::span<const float> renderReference,
        std::span<const float> microphoneInput,
        std::span<float> microphoneOutput,
        int streamDelayMilliseconds
    ) noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] int LastError() const noexcept;

    void Reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
    int lastError_ = 0;
};
