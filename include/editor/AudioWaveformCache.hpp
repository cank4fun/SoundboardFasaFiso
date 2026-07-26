#pragma once

#include "editor/AudioDocument.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct AudioWaveformPeak final
{
    float minimum = 0.0f;
    float maximum = 0.0f;
};

class AudioWaveformView final
{
public:
    [[nodiscard]] AudioFrameRange Range() const noexcept;
    [[nodiscard]] std::size_t ColumnCount() const noexcept;
    [[nodiscard]] std::uint32_t ChannelCount() const noexcept;

    [[nodiscard]] std::span<const AudioWaveformPeak> PeaksForChannel(
        std::uint32_t channel
    ) const noexcept;

private:
    friend class AudioWaveformCache;

    AudioFrameRange range_{};
    std::size_t columnCount_ = 0U;
    std::uint32_t channelCount_ = 0U;
    std::vector<AudioWaveformPeak> channelMajorPeaks_;
};

class AudioWaveformCache final
{
public:
    static constexpr std::size_t BaseFramesPerPeak = 64U;
    static constexpr std::size_t MaximumViewColumns = 16384U;

    static std::optional<AudioWaveformCache> Build(
        const AudioDocument& document,
        std::string& errorMessage,
        const std::atomic_bool* cancellationRequested = nullptr
    );

    [[nodiscard]] bool Matches(
        const AudioDocument& document
    ) const noexcept;

    [[nodiscard]] std::size_t LevelCount() const noexcept;
    [[nodiscard]] std::size_t MemoryBytes() const noexcept;

    std::optional<AudioWaveformView> CreateView(
        const AudioDocument& document,
        AudioFrameRange range,
        std::size_t requestedColumnCount,
        std::string& errorMessage
    ) const;

private:
    struct Level final
    {
        std::size_t framesPerPeak = 0U;
        std::size_t peakCount = 0U;
        std::vector<AudioWaveformPeak> channelMajorPeaks;
    };

    std::uint32_t sampleRate_ = 0U;
    std::uint32_t channelCount_ = 0U;
    std::size_t frameCount_ = 0U;
    std::uint64_t revision_ = 0U;
    const float* sampleData_ = nullptr;
    std::vector<Level> levels_;
};
