#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct AudioFrameRange final
{
    std::size_t beginFrame = 0;
    std::size_t endFrame = 0;

    [[nodiscard]] std::size_t FrameCount() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
};

enum class AudioEditResult
{
    Applied,
    NoChange,
    InvalidRange,
    InvalidValue
};

class AudioDocument final
{
public:
    static std::optional<AudioDocument> Create(
        std::uint32_t sampleRate,
        std::uint32_t channelCount,
        std::vector<float> interleavedSamples,
        std::string& errorMessage
    );

    [[nodiscard]] std::uint32_t SampleRate() const noexcept;
    [[nodiscard]] std::uint32_t ChannelCount() const noexcept;
    [[nodiscard]] std::size_t FrameCount() const noexcept;
    [[nodiscard]] double DurationSeconds() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    [[nodiscard]] std::span<const float> Samples() const noexcept;

    [[nodiscard]] bool IsValidRange(
        AudioFrameRange range
    ) const noexcept;

    AudioEditResult CropTo(AudioFrameRange range);
    AudioEditResult Delete(AudioFrameRange range);

    AudioEditResult ApplyGainDecibels(
        AudioFrameRange range,
        float decibels
    );

    AudioEditResult NormalizePeak(
        AudioFrameRange range,
        float targetPeak = 1.0f
    );

    AudioEditResult FadeIn(AudioFrameRange range);
    AudioEditResult FadeOut(AudioFrameRange range);
    AudioEditResult ConvertToMono();

private:
    AudioDocument(
        std::uint32_t sampleRate,
        std::uint32_t channelCount,
        std::vector<float> interleavedSamples
    );

    [[nodiscard]] std::size_t SampleOffset(
        std::size_t frame
    ) const noexcept;

    AudioEditResult ApplyFade(
        AudioFrameRange range,
        bool fadeIn
    );

    void MarkChanged() noexcept;

    std::uint32_t sampleRate_ = 0;
    std::uint32_t channelCount_ = 0;
    std::vector<float> samples_;
    std::uint64_t revision_ = 0U;
};
