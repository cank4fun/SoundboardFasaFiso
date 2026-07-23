#include "audio/WebRtcAec3Processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>

namespace
{
    constexpr float Pi = 3.14159265358979323846F;

    bool IsFinite(
        const std::array<float, WebRtcAec3Processor::SamplesPerBlock>&
            samples
    )
    {
        return std::all_of(
            samples.begin(),
            samples.end(),
            [](const float sample)
            {
                return std::isfinite(sample);
            }
        );
    }

    bool ApproximatelyEqual(const float left, const float right)
    {
        return std::abs(left - right) < 0.000001F;
    }
}

int main()
{
    WebRtcAec3Processor processor;

    if (!processor.Initialize() || !processor.IsInitialized())
    {
        std::cerr << "WebRTC AEC3 processor initialization failed.\n";
        return 1;
    }

    std::array<float, WebRtcAec3Processor::SamplesPerBlock> render{};
    std::array<float, WebRtcAec3Processor::SamplesPerBlock> capture{};
    std::array<float, WebRtcAec3Processor::SamplesPerBlock> output{};

    std::size_t absoluteSample = 0;

    for (int block = 0; block < 300; ++block)
    {
        for (std::size_t frame = 0;
            frame < WebRtcAec3Processor::SamplesPerBlock;
            ++frame)
        {
            const float time =
                static_cast<float>(absoluteSample) /
                static_cast<float>(
                    WebRtcAec3Processor::ProcessingSampleRate
                );
            const float farEnd =
                0.20F * std::sin(2.0F * Pi * 440.0F * time);
            const float nearEnd =
                0.08F * std::sin(2.0F * Pi * 880.0F * time);

            render[frame] = farEnd;
            capture[frame] = nearEnd + 0.35F * farEnd;
            ++absoluteSample;
        }

        if (!processor.ProcessBlock(render, capture, output, 10))
        {
            std::cerr
                << "WebRTC AEC3 block processing failed with code "
                << processor.LastError()
                << ".\n";
            return 2;
        }

        if (!IsFinite(output))
        {
            std::cerr << "WebRTC AEC3 produced a non-finite sample.\n";
            return 3;
        }
    }

    std::array<float, WebRtcAec3Processor::SamplesPerBlock - 1>
        shortRender{};
    output.fill(0.0F);

    if (processor.ProcessBlock(shortRender, capture, output, 10))
    {
        std::cerr << "Invalid render block was accepted.\n";
        return 4;
    }

    for (std::size_t index = 0; index < output.size(); ++index)
    {
        if (!ApproximatelyEqual(output[index], capture[index]))
        {
            std::cerr << "Failed AEC processing did not preserve bypass audio.\n";
            return 5;
        }
    }

    processor.Reset();

    if (processor.IsInitialized())
    {
        std::cerr << "WebRTC AEC3 processor reset failed.\n";
        return 6;
    }

    std::cout
        << "WebRTC AEC3 processor tests passed: 300 live-style blocks.\n";
    return 0;
}
