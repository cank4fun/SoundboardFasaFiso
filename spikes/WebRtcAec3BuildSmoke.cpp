#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>

#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/environment/environment_factory.h"

namespace {

constexpr int kSampleRate = 48'000;
constexpr std::size_t kFramesPerBlock = kSampleRate / 100;
constexpr int kBlockCount = 200;
constexpr float kPi = 3.14159265358979323846F;

bool IsFinite(const std::array<float, kFramesPerBlock>& samples) noexcept {
    for (const float sample : samples) {
        if (!std::isfinite(sample)) {
            return false;
        }
    }

    return true;
}

} // namespace

int main() {
    webrtc::AudioProcessing::Config config{};
    config.echo_canceller.enabled = true;

    webrtc::scoped_refptr<webrtc::AudioProcessing> audioProcessing =
        webrtc::BuiltinAudioProcessingBuilder(config).Build(
            webrtc::CreateEnvironment()
        );

    if (!audioProcessing) {
        std::cerr << "WebRTC AudioProcessing creation failed.\n";
        return 1;
    }

    const webrtc::StreamConfig mono48k{kSampleRate, 1};

    std::array<float, kFramesPerBlock> render{};
    std::array<float, kFramesPerBlock> renderOutput{};
    std::array<float, kFramesPerBlock> capture{};
    std::array<float, kFramesPerBlock> captureOutput{};

    const float* renderInput[] = {render.data()};
    float* renderDestination[] = {renderOutput.data()};
    const float* captureInput[] = {capture.data()};
    float* captureDestination[] = {captureOutput.data()};

    std::size_t absoluteSample = 0;

    for (int block = 0; block < kBlockCount; ++block) {
        for (std::size_t frame = 0; frame < kFramesPerBlock; ++frame) {
            const float phase =
                2.0F * kPi * 440.0F *
                static_cast<float>(absoluteSample) /
                static_cast<float>(kSampleRate);
            const float farEnd = 0.20F * std::sin(phase);

            render[frame] = farEnd;
            capture[frame] = 0.35F * farEnd;
            ++absoluteSample;
        }

        const int reverseResult = audioProcessing->ProcessReverseStream(
            renderInput,
            mono48k,
            mono48k,
            renderDestination
        );

        if (reverseResult != webrtc::AudioProcessing::kNoError) {
            std::cerr
                << "ProcessReverseStream failed with code "
                << reverseResult
                << ".\n";
            return 2;
        }

        const int delayResult = audioProcessing->set_stream_delay_ms(10);
        if (delayResult != webrtc::AudioProcessing::kNoError) {
            std::cerr
                << "set_stream_delay_ms failed with code "
                << delayResult
                << ".\n";
            return 3;
        }

        const int captureResult = audioProcessing->ProcessStream(
            captureInput,
            mono48k,
            mono48k,
            captureDestination
        );

        if (captureResult != webrtc::AudioProcessing::kNoError) {
            std::cerr
                << "ProcessStream failed with code "
                << captureResult
                << ".\n";
            return 4;
        }

        if (!IsFinite(renderOutput) || !IsFinite(captureOutput)) {
            std::cerr << "WebRTC produced a non-finite sample.\n";
            return 5;
        }
    }

    std::cout
        << "WebRTC AEC3 smoke test passed: "
        << kBlockCount
        << " render/capture blocks at 48 kHz mono.\n";
    return 0;
}
