#pragma once

#include "audio/MicrophoneProcessingSettings.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

struct MicrophoneProcessingSnapshot
{
    float rawPeak = 0.0f;
    float rawRms = 0.0f;
    float processedPeak = 0.0f;
    float processedRms = 0.0f;

    bool inputClipped = false;
    bool invalidSampleDetected = false;
    bool bypassed = true;
    bool configurationValid = true;
};

class MicrophoneProcessor
{
public:
    static constexpr unsigned int ProcessingSampleRate = 48000;
    static constexpr std::size_t SamplesPerBlock = 480;

    MicrophoneProcessor() = default;

    MicrophoneProcessor(const MicrophoneProcessor&) = delete;
    MicrophoneProcessor& operator=(const MicrophoneProcessor&) = delete;

    MicrophoneProcessor(MicrophoneProcessor&&) = delete;
    MicrophoneProcessor& operator=(MicrophoneProcessor&&) = delete;

    bool Initialize(const MicrophoneProcessingSettings& settings);
    bool UpdateSettings(const MicrophoneProcessingSettings& settings);

    bool ProcessBlock(
        std::span<const float> input,
        std::span<float> output
    );

    [[nodiscard]] MicrophoneProcessingSnapshot GetSnapshot() const;
    [[nodiscard]] bool IsInitialized() const noexcept;

    void Reset();

private:
    void PublishSnapshot(
        const MicrophoneProcessingSnapshot& snapshot
    );

    MicrophoneProcessingSettings settings_{};
    bool initialized_ = false;
    bool configurationValid_ = true;

    std::atomic<std::uint64_t> snapshotSequence_{0};
    std::atomic<float> rawPeak_{0.0f};
    std::atomic<float> rawRms_{0.0f};
    std::atomic<float> processedPeak_{0.0f};
    std::atomic<float> processedRms_{0.0f};
    std::atomic_bool inputClipped_{false};
    std::atomic_bool invalidSampleDetected_{false};
    std::atomic_bool bypassed_{true};
    std::atomic_bool snapshotConfigurationValid_{true};
};
