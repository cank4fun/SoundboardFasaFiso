#include "audio/AecRenderReferenceMixer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

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

    if (!mixer.ReadMonoBlock(mono))
    {
        return Fail(9, "Initialized mixer failed to produce a block.");
    }

    if (!std::ranges::all_of(mono, [](const float sample)
        {
            return sample == 0.0f;
        }))
    {
        return Fail(10, "Idle mixer did not produce silence.");
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
        return Fail(11, "Failed to initialize the test audio buffer.");
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
        return Fail(12, "Failed to initialize the test sound.");
    }

    if (ma_sound_start(&sound) != MA_SUCCESS)
    {
        ma_sound_uninit(&sound);
        ma_audio_buffer_uninit(&buffer);
        return Fail(13, "Failed to start the test sound.");
    }

    if (!mixer.ReadMonoBlock(mono))
    {
        ma_sound_uninit(&sound);
        ma_audio_buffer_uninit(&buffer);
        return Fail(14, "Mixer failed to render the test sound.");
    }

    for (const float sample : mono)
    {
        if (!NearlyEqual(sample, 0.5f))
        {
            ma_sound_uninit(&sound);
            ma_audio_buffer_uninit(&buffer);
            return Fail(15, "Stereo-to-mono downmix produced an unexpected sample.");
        }
    }

    ma_sound_uninit(&sound);
    ma_audio_buffer_uninit(&buffer);

    if (!mixer.SetVolume(0.5f))
    {
        return Fail(16, "Mixer rejected a valid volume.");
    }

    if (mixer.SetVolume(1.1f))
    {
        return Fail(17, "Mixer accepted an out-of-range volume.");
    }

    mixer.Reset();
    if (mixer.IsInitialized() || mixer.GetEngine() != nullptr ||
        mixer.IsLoopbackInitialized() ||
        mixer.LoopbackIncludesCurrentProcess() ||
        mixer.GetLoopbackMode() !=
            AecRenderReferenceMixer::LoopbackMode::None)
    {
        return Fail(18, "Mixer reset did not clear its initialized state.");
    }

    std::cout
        << "AEC render-reference mixer tests passed: "
        << AecRenderReferenceMixer::FramesPerBlock
        << " stereo frames downmixed to one 10 ms mono block.\n";

    return 0;
}
