#pragma once

#include <array>
#include <cstddef>
#include <span>

class RnNoiseSuppressor
{
public:
    static constexpr unsigned int RequiredSampleRate = 48000;
    static constexpr std::size_t SamplesPerFrame = 480;

    RnNoiseSuppressor() = default;
    ~RnNoiseSuppressor();

    RnNoiseSuppressor(const RnNoiseSuppressor&) = delete;
    RnNoiseSuppressor& operator=(const RnNoiseSuppressor&) = delete;
    RnNoiseSuppressor(RnNoiseSuppressor&&) = delete;
    RnNoiseSuppressor& operator=(RnNoiseSuppressor&&) = delete;

    bool Initialize();
    bool ProcessFrame(
        std::span<const float> normalizedInput,
        std::span<float> normalizedOutput
    ) noexcept;

    void Reset() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] float GetLastVadProbability() const noexcept;

private:
    void* state_ = nullptr;
    std::array<float, SamplesPerFrame> scaledInput_{};
    std::array<float, SamplesPerFrame> scaledOutput_{};
    float lastVadProbability_ = 0.0f;
};
