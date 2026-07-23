#include "audio/WebRtcAec3Processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>

#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/environment/environment_factory.h"

struct WebRtcAec3Processor::Impl
{
    webrtc::scoped_refptr<webrtc::AudioProcessing> audioProcessing;
    webrtc::StreamConfig mono48k{
        WebRtcAec3Processor::ProcessingSampleRate,
        1
    };
    std::array<float, WebRtcAec3Processor::SamplesPerBlock>
        renderOutput{};
};

WebRtcAec3Processor::WebRtcAec3Processor() = default;

WebRtcAec3Processor::~WebRtcAec3Processor()
{
    Reset();
}

bool WebRtcAec3Processor::Initialize()
{
    Reset();

    auto implementation = std::unique_ptr<Impl>(
        new (std::nothrow) Impl()
    );

    if (!implementation)
    {
        lastError_ = -1;
        return false;
    }

    webrtc::AudioProcessing::Config config{};
    config.echo_canceller.enabled = true;

    implementation->audioProcessing =
        webrtc::BuiltinAudioProcessingBuilder(config).Build(
            webrtc::CreateEnvironment()
        );

    if (!implementation->audioProcessing)
    {
        lastError_ = -2;
        return false;
    }

    implementation_ = std::move(implementation);
    lastError_ = webrtc::AudioProcessing::kNoError;
    return true;
}

bool WebRtcAec3Processor::ProcessBlock(
    const std::span<const float> renderReference,
    const std::span<const float> microphoneInput,
    const std::span<float> microphoneOutput,
    const int streamDelayMilliseconds
) noexcept
{
    if (microphoneInput.size() == SamplesPerBlock &&
        microphoneOutput.size() == SamplesPerBlock)
    {
        std::copy(
            microphoneInput.begin(),
            microphoneInput.end(),
            microphoneOutput.begin()
        );
    }

    if (!implementation_ ||
        !implementation_->audioProcessing ||
        renderReference.size() != SamplesPerBlock ||
        microphoneInput.size() != SamplesPerBlock ||
        microphoneOutput.size() != SamplesPerBlock ||
        streamDelayMilliseconds < 0)
    {
        lastError_ = -3;
        return false;
    }

    const float* renderInput[]{renderReference.data()};
    float* renderOutput[]{implementation_->renderOutput.data()};
    const float* captureInput[]{microphoneInput.data()};
    float* captureOutput[]{microphoneOutput.data()};

    int result = implementation_->audioProcessing->ProcessReverseStream(
        renderInput,
        implementation_->mono48k,
        implementation_->mono48k,
        renderOutput
    );

    if (result != webrtc::AudioProcessing::kNoError)
    {
        lastError_ = result;
        return false;
    }

    result = implementation_->audioProcessing->set_stream_delay_ms(
        streamDelayMilliseconds
    );

    if (result != webrtc::AudioProcessing::kNoError)
    {
        lastError_ = result;
        return false;
    }

    result = implementation_->audioProcessing->ProcessStream(
        captureInput,
        implementation_->mono48k,
        implementation_->mono48k,
        captureOutput
    );

    lastError_ = result;
    return result == webrtc::AudioProcessing::kNoError;
}

bool WebRtcAec3Processor::IsInitialized() const noexcept
{
    return implementation_ && implementation_->audioProcessing;
}

int WebRtcAec3Processor::LastError() const noexcept
{
    return lastError_;
}

void WebRtcAec3Processor::Reset() noexcept
{
    implementation_.reset();
    lastError_ = 0;
}
