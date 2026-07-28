#include "audio/AecRenderReferenceMixer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <cstring>

class AecRenderReferenceMixerTestAccess
{
public:
    static bool InitializeLoopbackRing(AecRenderReferenceMixer& mixer)
    {
        const ma_result result = ma_pcm_rb_init(
            ma_format_f32,
            AecRenderReferenceMixer::ChannelCount,
            AecRenderReferenceMixer::LoopbackRingBufferFrames,
            nullptr,
            nullptr,
            &mixer.loopbackRingBuffer_
        );

        if (result != MA_SUCCESS)
        {
            return false;
        }

        mixer.loopbackRingBufferInitialized_ = true;
        mixer.loopbackDeviceInitialized_ = true;
        mixer.loopbackMode_ =
            AecRenderReferenceMixer::LoopbackMode::Endpoint;
        mixer.loopbackPrimed_ = false;
        ma_pcm_rb_set_sample_rate(
            &mixer.loopbackRingBuffer_,
            AecRenderReferenceMixer::SampleRate
        );
        return true;
    }

    static ma_uint32 Push(
        AecRenderReferenceMixer& mixer,
        const float* const frames,
        const ma_uint32 frameCount
    )
    {
        return mixer.PushLoopbackFrames(frames, frameCount);
    }

    static bool Read(
        AecRenderReferenceMixer& mixer,
        const std::span<float> output
    )
    {
        return mixer.ReadLoopbackStereoBlock(output);
    }

    static void Cleanup(AecRenderReferenceMixer& mixer)
    {
        mixer.loopbackDeviceInitialized_ = false;
        mixer.loopbackMode_ =
            AecRenderReferenceMixer::LoopbackMode::None;
        mixer.loopbackPrimed_ = false;

        if (mixer.loopbackRingBufferInitialized_)
        {
            ma_pcm_rb_uninit(&mixer.loopbackRingBuffer_);
            mixer.loopbackRingBufferInitialized_ = false;
        }

        std::memset(
            &mixer.loopbackRingBuffer_,
            0,
            sizeof(mixer.loopbackRingBuffer_)
        );
    }
};


namespace
{
    constexpr float Tolerance = 0.0005f;

    bool NearlyEqual(const float left, const float right)
    {
        return std::abs(left - right) <= Tolerance;
    }

    int Fail(const int code, const char* const message)
    {
        std::cerr << message << '\n';
        return code;
    }
}

int main()
{
    AecRenderReferenceMixer mixer;
    std::array<float, AecRenderReferenceMixer::FramesPerBlock> mono{};
    std::array<
        float,
        AecRenderReferenceMixer::FramesPerBlock *
            AecRenderReferenceMixer::ChannelCount
    > stereoReference{};

    if (mixer.IsLoopbackInitialized() ||
        mixer.LoopbackIncludesCurrentProcess() ||
        mixer.GetLoopbackMode() !=
            AecRenderReferenceMixer::LoopbackMode::None)
    {
        return Fail(1, "New mixer reported an active loopback reference.");
    }

    mono.fill(1.0f);
    if (mixer.ReadMonoBlock(mono))
    {
        return Fail(2, "Uninitialized mixer unexpectedly produced audio.");
    }

    if (!std::ranges::all_of(mono, [](const float sample)
        {
            return sample == 0.0f;
        }))
    {
        return Fail(3, "Uninitialized mixer did not return a silent block.");
    }

    if (mixer.Initialize(-0.1f))
    {
        return Fail(4, "Mixer accepted a negative volume.");
    }

    if (mixer.Initialize(std::numeric_limits<float>::quiet_NaN()))
    {
        return Fail(5, "Mixer accepted a non-finite volume.");
    }

    if (!mixer.Initialize(1.0f))
    {
        return Fail(6, "Mixer initialization failed.");
    }

    if (!mixer.IsInitialized() || mixer.GetEngine() == nullptr)
    {
        return Fail(7, "Mixer initialization state is inconsistent.");
    }

    if (mixer.IsLoopbackInitialized() ||
        mixer.GetLoopbackMode() !=
            AecRenderReferenceMixer::LoopbackMode::None)
    {
        return Fail(8, "Internal mixer initialization enabled loopback unexpectedly.");
    }

    if (!mixer.ReadStereoBlock(stereoReference))
    {
        return Fail(9, "Initialized mixer failed to produce a stereo block.");
    }

    if (!std::ranges::all_of(
            stereoReference,
            [](const float sample) { return sample == 0.0f; }
        ))
    {
        return Fail(10, "Idle mixer did not produce stereo silence.");
    }

    if (!mixer.ReadMonoBlock(mono))
    {
        return Fail(11, "Initialized mixer failed to produce a mono block.");
    }

    if (!std::ranges::all_of(mono, [](const float sample)
        {
            return sample == 0.0f;
        }))
    {
        return Fail(12, "Idle mixer did not produce mono silence.");
    }

    std::array<float,
        AecRenderReferenceMixer::FramesPerBlock *
            AecRenderReferenceMixer::ChannelCount> stereo{};

    for (std::size_t frame = 0;
        frame < AecRenderReferenceMixer::FramesPerBlock;
        ++frame)
    {
        const std::size_t sampleIndex =
            frame * AecRenderReferenceMixer::ChannelCount;
        stereo[sampleIndex] = 0.25f;
        stereo[sampleIndex + 1] = 0.75f;
    }

    ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
        ma_format_f32,
        AecRenderReferenceMixer::ChannelCount,
        AecRenderReferenceMixer::FramesPerBlock,
        stereo.data(),
        nullptr
    );
    bufferConfig.sampleRate = AecRenderReferenceMixer::SampleRate;

    ma_audio_buffer buffer{};
    if (ma_audio_buffer_init(&bufferConfig, &buffer) != MA_SUCCESS)
    {
        return Fail(15, "Failed to initialize the test audio buffer.");
    }

    ma_sound sound{};
    const ma_uint32 soundFlags =
        MA_SOUND_FLAG_NO_SPATIALIZATION |
        MA_SOUND_FLAG_NO_PITCH;

    if (ma_sound_init_from_data_source(
        mixer.GetEngine(),
        &buffer,
        soundFlags,
        nullptr,
        &sound
    ) != MA_SUCCESS)
    {
        ma_audio_buffer_uninit(&buffer);
        return Fail(16, "Failed to initialize the test sound.");
    }

    if (ma_sound_start(&sound) != MA_SUCCESS)
    {
        ma_sound_uninit(&sound);
        ma_audio_buffer_uninit(&buffer);
        return Fail(17, "Failed to start the test sound.");
    }

    if (!mixer.ReadStereoBlock(stereoReference))
    {
        ma_sound_uninit(&sound);
        ma_audio_buffer_uninit(&buffer);
        return Fail(18, "Mixer failed to render the stereo test sound.");
    }

    for (std::size_t frame = 0;
        frame < AecRenderReferenceMixer::FramesPerBlock;
        ++frame)
    {
        const std::size_t sampleIndex =
            frame * AecRenderReferenceMixer::ChannelCount;

        if (!NearlyEqual(stereoReference[sampleIndex], 0.25f) ||
            !NearlyEqual(stereoReference[sampleIndex + 1], 0.75f))
        {
            ma_sound_uninit(&sound);
            ma_audio_buffer_uninit(&buffer);
            return Fail(19, "Stereo render reference lost channel data.");
        }
    }

    if (ma_sound_seek_to_pcm_frame(&sound, 0) != MA_SUCCESS ||
        ma_sound_start(&sound) != MA_SUCCESS ||
        !mixer.ReadMonoBlock(mono))
    {
        ma_sound_uninit(&sound);
        ma_audio_buffer_uninit(&buffer);
        return Fail(20, "Mixer failed to render the mono compatibility block.");
    }

    for (const float sample : mono)
    {
        if (!NearlyEqual(sample, 0.5f))
        {
            ma_sound_uninit(&sound);
            ma_audio_buffer_uninit(&buffer);
            return Fail(21, "Stereo-to-mono downmix produced an unexpected sample.");
        }
    }

    ma_sound_uninit(&sound);
    ma_audio_buffer_uninit(&buffer);

    if (!mixer.SetVolume(0.5f))
    {
        return Fail(22, "Mixer rejected a valid volume.");
    }

    if (mixer.SetVolume(1.1f))
    {
        return Fail(23, "Mixer accepted an out-of-range volume.");
    }

    mixer.Reset();
    if (mixer.IsInitialized() || mixer.GetEngine() != nullptr ||
        mixer.IsLoopbackInitialized() ||
        mixer.LoopbackIncludesCurrentProcess() ||
        mixer.GetLoopbackMode() !=
            AecRenderReferenceMixer::LoopbackMode::None)
    {
        return Fail(24, "Mixer reset did not clear its initialized state.");
    }

    AecRenderReferenceMixer loopbackMixer;
    if (!AecRenderReferenceMixerTestAccess::InitializeLoopbackRing(
            loopbackMixer
        ))
    {
        return Fail(25, "Failed to initialize the loopback ring test.");
    }

    constexpr std::size_t StereoSamplesPerBlock =
        AecRenderReferenceMixer::FramesPerBlock *
        AecRenderReferenceMixer::ChannelCount;
    std::array<float, StereoSamplesPerBlock * 2> queuedStereo{};

    for (std::size_t frame = 0;
        frame < AecRenderReferenceMixer::FramesPerBlock;
        ++frame)
    {
        const std::size_t first =
            frame * AecRenderReferenceMixer::ChannelCount;
        const std::size_t second = StereoSamplesPerBlock + first;
        queuedStereo[first] = 0.10f;
        queuedStereo[first + 1] = 0.20f;
        queuedStereo[second] = 0.30f;
        queuedStereo[second + 1] = 0.40f;
    }

    if (AecRenderReferenceMixerTestAccess::Push(
            loopbackMixer,
            queuedStereo.data(),
            AecRenderReferenceMixer::FramesPerBlock * 2
        ) != AecRenderReferenceMixer::FramesPerBlock * 2)
    {
        AecRenderReferenceMixerTestAccess::Cleanup(loopbackMixer);
        return Fail(26, "Failed to queue the priming loopback blocks.");
    }

    if (!AecRenderReferenceMixerTestAccess::Read(
            loopbackMixer,
            stereoReference
        ) ||
        !NearlyEqual(stereoReference[0], 0.10f) ||
        !NearlyEqual(stereoReference[1], 0.20f))
    {
        AecRenderReferenceMixerTestAccess::Cleanup(loopbackMixer);
        return Fail(27, "Loopback priming did not return the first block.");
    }

    if (!AecRenderReferenceMixerTestAccess::Read(
            loopbackMixer,
            stereoReference
        ) ||
        !NearlyEqual(stereoReference[0], 0.30f) ||
        !NearlyEqual(stereoReference[1], 0.40f))
    {
        AecRenderReferenceMixerTestAccess::Cleanup(loopbackMixer);
        return Fail(28, "The primed loopback path incorrectly required two queued blocks for every read.");
    }

    std::array<float, StereoSamplesPerBlock> partialStereo{};
    for (std::size_t frame = 0;
        frame < AecRenderReferenceMixer::FramesPerBlock;
        ++frame)
    {
        const std::size_t sample =
            frame * AecRenderReferenceMixer::ChannelCount;
        partialStereo[sample] = 0.50f;
        partialStereo[sample + 1] = 0.60f;
    }

    constexpr ma_uint32 HalfBlock =
        AecRenderReferenceMixer::FramesPerBlock / 2;
    if (AecRenderReferenceMixerTestAccess::Push(
            loopbackMixer,
            partialStereo.data(),
            HalfBlock
        ) != HalfBlock)
    {
        AecRenderReferenceMixerTestAccess::Cleanup(loopbackMixer);
        return Fail(29, "Failed to queue the partial loopback block.");
    }

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (AecRenderReferenceMixerTestAccess::Read(
                loopbackMixer,
                stereoReference
            ))
        {
            AecRenderReferenceMixerTestAccess::Cleanup(loopbackMixer);
            return Fail(30, "An incomplete loopback block was returned as valid.");
        }
    }

    if (AecRenderReferenceMixerTestAccess::Push(
            loopbackMixer,
            partialStereo.data() +
                static_cast<std::size_t>(HalfBlock) *
                    AecRenderReferenceMixer::ChannelCount,
            HalfBlock
        ) != HalfBlock ||
        !AecRenderReferenceMixerTestAccess::Read(
            loopbackMixer,
            stereoReference
        ) ||
        !NearlyEqual(stereoReference[0], 0.50f) ||
        !NearlyEqual(stereoReference[1], 0.60f))
    {
        AecRenderReferenceMixerTestAccess::Cleanup(loopbackMixer);
        return Fail(31, "A temporary underrun discarded a partial loopback block.");
    }

    AecRenderReferenceMixerTestAccess::Cleanup(loopbackMixer);

    std::cout
        << "AEC render-reference mixer tests passed: "
        << AecRenderReferenceMixer::FramesPerBlock
        << " stereo frames preserved for AEC and downmixed for "
           "the compatibility path.\n";

    return 0;
}
