#include "audio/WebRtcAec3Processor.hpp"

#include "audio/StereoCrosstalkCanceller.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <new>
#include <optional>

#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio/echo_canceller3_config.h"
#include "api/environment/environment_factory.h"

namespace
{
    webrtc::EchoCanceller3Config MakeAggressiveAec3Config()
    {
        webrtc::EchoCanceller3Config config;

        // Headset USB codecs can leak a clean, high-level copy of playback
        // directly into the capture endpoint. Tighten residual-echo masking
        // while leaving near-end tuning at WebRTC defaults so double-talk is
        // still protected.
        config.suppressor.normal_tuning.mask_lf.enr_transparent = 0.12f;
        config.suppressor.normal_tuning.mask_lf.enr_suppress = 0.20f;
        config.suppressor.normal_tuning.mask_lf.emr_transparent = 0.20f;
        config.suppressor.normal_tuning.mask_hf.enr_transparent = 0.03f;
        config.suppressor.normal_tuning.mask_hf.enr_suppress = 0.08f;
        config.suppressor.normal_tuning.mask_hf.emr_transparent = 0.20f;
        config.suppressor.normal_tuning.max_inc_factor = 1.5f;
        config.suppressor.conservative_hf_suppression = true;
        config.suppressor.high_bands_suppression.max_gain_during_echo =
            0.05f;
        config.suppressor.floor_first_increase = 0.000001f;
        config.filter.conservative_initial_phase = true;
        // The endpoint reference is intentionally stereo. Do not wait for
        // WebRTC's long-lived content detector before using both channels;
        // USB headset leakage can be strongly channel-dependent.
        config.multi_channel.detect_stereo_content = false;

        static_cast<void>(webrtc::EchoCanceller3Config::Validate(&config));
        return config;
    }
}

struct WebRtcAec3Processor::Impl
{
    webrtc::scoped_refptr<webrtc::AudioProcessing> audioProcessing;
    webrtc::StreamConfig mono48k{
        WebRtcAec3Processor::ProcessingSampleRate,
        1
    };
    webrtc::StreamConfig stereo48k{
        WebRtcAec3Processor::ProcessingSampleRate,
        static_cast<int>(WebRtcAec3Processor::RenderChannelCount)
    };
    std::array<float, WebRtcAec3Processor::SamplesPerBlock>
        renderLeft{};
    std::array<float, WebRtcAec3Processor::SamplesPerBlock>
        renderRight{};
    std::array<float, WebRtcAec3Processor::SamplesPerBlock>
        renderOutputLeft{};
    std::array<float, WebRtcAec3Processor::SamplesPerBlock>
        renderOutputRight{};
    std::array<float, WebRtcAec3Processor::SamplesPerBlock>
        crosstalkReducedInput{};
    StereoCrosstalkCanceller crosstalkCanceller;
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
    config.pipeline.multi_channel_render = true;

    webrtc::BuiltinAudioProcessingBuilder builder(config);
    builder.SetEchoCancellerConfig(
        MakeAggressiveAec3Config(),
        std::nullopt
    );

    implementation->audioProcessing = builder.Build(
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
        renderReference.size() != RenderSamplesPerBlock ||
        microphoneInput.size() != SamplesPerBlock ||
        microphoneOutput.size() != SamplesPerBlock ||
        streamDelayMilliseconds < 0)
    {
        lastError_ = -3;
        return false;
    }

    for (std::size_t frame = 0; frame < SamplesPerBlock; ++frame)
    {
        const std::size_t sampleIndex = frame * RenderChannelCount;
        const float left = renderReference[sampleIndex];
        const float right = renderReference[sampleIndex + 1];
        implementation_->renderLeft[frame] =
            std::isfinite(left) ? std::clamp(left, -1.0f, 1.0f) : 0.0f;
        implementation_->renderRight[frame] =
            std::isfinite(right) ? std::clamp(right, -1.0f, 1.0f) : 0.0f;
    }

    const float* renderInput[]{
        implementation_->renderLeft.data(),
        implementation_->renderRight.data()
    };
    float* renderOutput[]{
        implementation_->renderOutputLeft.data(),
        implementation_->renderOutputRight.data()
    };
    if (!implementation_->crosstalkCanceller.ProcessBlock(
            renderReference,
            microphoneInput,
            implementation_->crosstalkReducedInput
        ))
    {
        std::copy(
            microphoneInput.begin(),
            microphoneInput.end(),
            implementation_->crosstalkReducedInput.begin()
        );
    }

    const float* captureInput[]{
        implementation_->crosstalkReducedInput.data()
    };
    float* captureOutput[]{microphoneOutput.data()};

    int result = implementation_->audioProcessing->ProcessReverseStream(
        renderInput,
        implementation_->stereo48k,
        implementation_->stereo48k,
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

    if (result == webrtc::AudioProcessing::kNoError)
    {
        implementation_->crosstalkCanceller.ApplyResidualSuppression(
            microphoneOutput
        );
    }

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
