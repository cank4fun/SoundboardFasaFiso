#pragma once

#include "audio/MicrophoneProcessingSettings.hpp"
#include "audio/RnNoiseSuppressor.hpp"

#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
#include "audio/WebRtcAec3Processor.hpp"
#endif

#include <array>
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
    bool noiseSuppressionRequested = false;
    bool noiseSuppressionActive = false;
    bool noiseSuppressionFailed = false;
    bool echoCancellationRequested = false;
    bool echoCancellationActive = false;
    bool echoCancellationFailed = false;
    bool agcActive = false;
    bool bypassed = true;
    bool configurationValid = true;

    float voiceActivityProbability = 0.0f;
    float agcGainDb = 0.0f;
    int echoCancellationError = 0;
};

class MicrophoneProcessor
{
public:
    static constexpr unsigned int ProcessingSampleRate = 48000;
    static constexpr std::size_t SamplesPerBlock = 480;
    static constexpr float MinimumAgcGainDb = -12.0f;
    static constexpr float MaximumAgcGainDb = 18.0f;
    static constexpr float AgcSilenceThresholdDbfs = -55.0f;

    MicrophoneProcessor() = default;

    MicrophoneProcessor(const MicrophoneProcessor&) = delete;
    MicrophoneProcessor& operator=(const MicrophoneProcessor&) = delete;

    MicrophoneProcessor(MicrophoneProcessor&&) = delete;
    MicrophoneProcessor& operator=(MicrophoneProcessor&&) = delete;

    bool Initialize(
        const MicrophoneProcessingSettings& settings,
        bool echoCancellationRequested = false
    );
    bool UpdateSettings(const MicrophoneProcessingSettings& settings);

    bool ProcessBlock(
        std::span<const float> input,
        std::span<float> output
#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
        ,
        std::span<const float> renderReference = {},
        int streamDelayMilliseconds = 0
#endif
    );

    [[nodiscard]] MicrophoneProcessingSnapshot GetSnapshot() const;
    [[nodiscard]] bool IsInitialized() const noexcept;

    void Reset();

private:
    void ConfigureProcessingState();
    void ResetProcessingState();

    [[nodiscard]] float ProcessHighPass(float sample);
    void UpdateAgcGain(float blockRms);
    [[nodiscard]] float ProcessCompressor(float sample);
    [[nodiscard]] float ProcessLimiter(float sample) const;
    [[nodiscard]] float NoiseSuppressionMix() const noexcept;

    void PublishSnapshot(
        const MicrophoneProcessingSnapshot& snapshot
    );

    MicrophoneProcessingSettings settings_{};
    bool initialized_ = false;
    bool configurationValid_ = true;

    float highPassB0_ = 1.0f;
    float highPassB1_ = 0.0f;
    float highPassB2_ = 0.0f;
    float highPassA1_ = 0.0f;
    float highPassA2_ = 0.0f;
    float highPassState1_ = 0.0f;
    float highPassState2_ = 0.0f;

    float agcGainReductionCoefficient_ = 0.0f;
    float agcGainIncreaseCoefficient_ = 0.0f;
    float agcSilenceReturnCoefficient_ = 0.0f;
    float agcGainDb_ = 0.0f;

    float compressorAttackCoefficient_ = 0.0f;
    float compressorReleaseCoefficient_ = 0.0f;
    float compressorEnvelope_ = 0.0f;

    float limiterCeilingLinear_ = 1.0f;

#if defined(SOUNDBOARD_ENABLE_WEBRTC_AEC3)
    WebRtcAec3Processor echoCanceller_;
#endif
    bool echoCancellationRequested_ = false;
    bool echoCancellationAvailable_ = false;
    bool echoCancellationFailed_ = false;
    int echoCancellationError_ = 0;
    std::array<float, SamplesPerBlock> sanitizedInputBuffer_{};
    std::array<float, SamplesPerBlock> echoCancelledBuffer_{};

    RnNoiseSuppressor noiseSuppressor_;
    std::array<float, SamplesPerBlock> preNoiseSuppressionBuffer_{};
    std::array<float, SamplesPerBlock> noiseSuppressedBuffer_{};
    bool noiseSuppressionAvailable_ = false;
    bool noiseSuppressionFailed_ = false;

    std::atomic<std::uint64_t> snapshotSequence_{0};
    std::atomic<float> rawPeak_{0.0f};
    std::atomic<float> rawRms_{0.0f};
    std::atomic<float> processedPeak_{0.0f};
    std::atomic<float> processedRms_{0.0f};
    std::atomic_bool inputClipped_{false};
    std::atomic_bool invalidSampleDetected_{false};
    std::atomic_bool noiseSuppressionRequested_{false};
    std::atomic_bool noiseSuppressionActive_{false};
    std::atomic_bool noiseSuppressionFailedSnapshot_{false};
    std::atomic_bool echoCancellationRequestedSnapshot_{false};
    std::atomic_bool echoCancellationActive_{false};
    std::atomic_bool echoCancellationFailedSnapshot_{false};
    std::atomic<int> echoCancellationErrorSnapshot_{0};
    std::atomic_bool agcActive_{false};
    std::atomic_bool bypassed_{true};
    std::atomic_bool snapshotConfigurationValid_{true};
    std::atomic<float> voiceActivityProbability_{0.0f};
    std::atomic<float> agcGainDbSnapshot_{0.0f};
};
