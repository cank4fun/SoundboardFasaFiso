#include "audio/RnNoiseSuppressor.hpp"

#include "rnnoise.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float PcmScale = 32768.0f;
}

RnNoiseSuppressor::~RnNoiseSuppressor()
{
    Reset();
}

bool RnNoiseSuppressor::Initialize()
{
    Reset();

    if (rnnoise_get_frame_size() !=
        static_cast<int>(SamplesPerFrame))
    {
        return false;
    }

    state_ = rnnoise_create(nullptr);
    lastVadProbability_ = 0.0f;
    return state_ != nullptr;
}

bool RnNoiseSuppressor::ProcessFrame(
    const std::span<const float> normalizedInput,
    const std::span<float> normalizedOutput
) noexcept
{
    if (state_ == nullptr ||
        normalizedInput.size() != SamplesPerFrame ||
        normalizedOutput.size() != SamplesPerFrame)
    {
        return false;
    }

    for (std::size_t index = 0; index < SamplesPerFrame; ++index)
    {
        const float input = std::isfinite(normalizedInput[index])
            ? std::clamp(normalizedInput[index], -1.0f, 1.0f)
            : 0.0f;
        scaledInput_[index] = input * PcmScale;
    }

    auto* const state = static_cast<DenoiseState*>(state_);
    const float vadProbability = rnnoise_process_frame(
        state,
        scaledOutput_.data(),
        scaledInput_.data()
    );

    if (!std::isfinite(vadProbability))
    {
        std::fill(normalizedOutput.begin(), normalizedOutput.end(), 0.0f);
        lastVadProbability_ = 0.0f;
        return false;
    }

    bool outputValid = true;

    for (std::size_t index = 0; index < SamplesPerFrame; ++index)
    {
        const float output = scaledOutput_[index] / PcmScale;

        if (!std::isfinite(output))
        {
            normalizedOutput[index] = 0.0f;
            outputValid = false;
            continue;
        }

        normalizedOutput[index] = std::clamp(output, -1.0f, 1.0f);
    }

    lastVadProbability_ = std::clamp(vadProbability, 0.0f, 1.0f);
    return outputValid;
}

void RnNoiseSuppressor::Reset() noexcept
{
    if (state_ != nullptr)
    {
        rnnoise_destroy(static_cast<DenoiseState*>(state_));
        state_ = nullptr;
    }

    scaledInput_.fill(0.0f);
    scaledOutput_.fill(0.0f);
    lastVadProbability_ = 0.0f;
}

bool RnNoiseSuppressor::IsInitialized() const noexcept
{
    return state_ != nullptr;
}

float RnNoiseSuppressor::GetLastVadProbability() const noexcept
{
    return lastVadProbability_;
}
