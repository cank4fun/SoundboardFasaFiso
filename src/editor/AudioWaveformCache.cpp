#include "editor/AudioWaveformCache.hpp"

#include <algorithm>
#include <limits>

namespace
{
    bool IsCancellationRequested(
        const std::atomic_bool* cancellationRequested
    ) noexcept
    {
        return cancellationRequested != nullptr &&
            cancellationRequested->load(std::memory_order_relaxed);
    }

    std::size_t DivideRoundingUp(
        const std::size_t value,
        const std::size_t divisor
    ) noexcept
    {
        return value / divisor + (value % divisor != 0U ? 1U : 0U);
    }

    void IncludeSample(
        AudioWaveformPeak& peak,
        bool& hasValue,
        const float sample
    ) noexcept
    {
        if (!hasValue)
        {
            peak.minimum = sample;
            peak.maximum = sample;
            hasValue = true;
            return;
        }

        peak.minimum = std::min(peak.minimum, sample);
        peak.maximum = std::max(peak.maximum, sample);
    }

    void IncludePeak(
        AudioWaveformPeak& destination,
        bool& hasValue,
        const AudioWaveformPeak source
    ) noexcept
    {
        if (!hasValue)
        {
            destination = source;
            hasValue = true;
            return;
        }

        destination.minimum = std::min(
            destination.minimum,
            source.minimum
        );
        destination.maximum = std::max(
            destination.maximum,
            source.maximum
        );
    }

    void IncludeRawFrames(
        const AudioDocument& document,
        const std::size_t beginFrame,
        const std::size_t endFrame,
        const std::uint32_t channel,
        AudioWaveformPeak& peak,
        bool& hasValue
    ) noexcept
    {
        const std::span<const float> samples = document.Samples();
        const std::size_t channelCount = static_cast<std::size_t>(
            document.ChannelCount()
        );
        const std::size_t channelOffset = static_cast<std::size_t>(channel);

        for (std::size_t frame = beginFrame; frame < endFrame; ++frame)
        {
            IncludeSample(
                peak,
                hasValue,
                samples[frame * channelCount + channelOffset]
            );
        }
    }
}

AudioFrameRange AudioWaveformView::Range() const noexcept
{
    return range_;
}

std::size_t AudioWaveformView::ColumnCount() const noexcept
{
    return columnCount_;
}

std::uint32_t AudioWaveformView::ChannelCount() const noexcept
{
    return channelCount_;
}

std::span<const AudioWaveformPeak> AudioWaveformView::PeaksForChannel(
    const std::uint32_t channel
) const noexcept
{
    if (channel >= channelCount_ || columnCount_ == 0U)
    {
        return {};
    }

    const std::size_t begin =
        static_cast<std::size_t>(channel) * columnCount_;

    return std::span<const AudioWaveformPeak>{
        channelMajorPeaks_.data() + begin,
        columnCount_
    };
}

std::optional<AudioWaveformCache> AudioWaveformCache::Build(
    const AudioDocument& document,
    std::string& errorMessage,
    const std::atomic_bool* cancellationRequested
)
{
    errorMessage.clear();

    if (IsCancellationRequested(cancellationRequested))
    {
        errorMessage = "The waveform cache build was cancelled.";
        return std::nullopt;
    }

    AudioWaveformCache cache;
    cache.sampleRate_ = document.SampleRate();
    cache.channelCount_ = document.ChannelCount();
    cache.frameCount_ = document.FrameCount();
    cache.revision_ = document.Revision();
    cache.sampleData_ = document.Samples().data();

    if (document.Empty())
    {
        return cache;
    }

    const std::size_t channelCount = static_cast<std::size_t>(
        document.ChannelCount()
    );
    const std::size_t basePeakCount = DivideRoundingUp(
        document.FrameCount(),
        BaseFramesPerPeak
    );

    if (basePeakCount >
        std::vector<AudioWaveformPeak>{}.max_size() / channelCount)
    {
        errorMessage = "The waveform cache is too large.";
        return std::nullopt;
    }

    Level baseLevel;
    baseLevel.framesPerPeak = BaseFramesPerPeak;
    baseLevel.peakCount = basePeakCount;
    baseLevel.channelMajorPeaks.resize(basePeakCount * channelCount);

    for (std::uint32_t channel = 0U;
         channel < document.ChannelCount();
         ++channel)
    {
        for (std::size_t peakIndex = 0U;
             peakIndex < basePeakCount;
             ++peakIndex)
        {
            if ((peakIndex & 1023U) == 0U &&
                IsCancellationRequested(cancellationRequested))
            {
                errorMessage = "The waveform cache build was cancelled.";
                return std::nullopt;
            }
            const std::size_t beginFrame = peakIndex * BaseFramesPerPeak;
            const std::size_t endFrame = std::min(
                beginFrame + BaseFramesPerPeak,
                document.FrameCount()
            );
            AudioWaveformPeak peak;
            bool hasValue = false;

            IncludeRawFrames(
                document,
                beginFrame,
                endFrame,
                channel,
                peak,
                hasValue
            );

            baseLevel.channelMajorPeaks[
                static_cast<std::size_t>(channel) * basePeakCount + peakIndex
            ] = peak;
        }
    }

    cache.levels_.push_back(std::move(baseLevel));

    while (cache.levels_.back().peakCount > 1U)
    {
        if (IsCancellationRequested(cancellationRequested))
        {
            errorMessage = "The waveform cache build was cancelled.";
            return std::nullopt;
        }
        const Level& previous = cache.levels_.back();
        Level next;
        next.framesPerPeak = previous.framesPerPeak * 2U;
        next.peakCount = DivideRoundingUp(previous.peakCount, 2U);

        if (next.peakCount >
            std::vector<AudioWaveformPeak>{}.max_size() / channelCount)
        {
            errorMessage = "The waveform cache is too large.";
            return std::nullopt;
        }

        next.channelMajorPeaks.resize(next.peakCount * channelCount);

        for (std::uint32_t channel = 0U;
             channel < document.ChannelCount();
             ++channel)
        {
            const std::size_t previousChannelOffset =
                static_cast<std::size_t>(channel) * previous.peakCount;
            const std::size_t nextChannelOffset =
                static_cast<std::size_t>(channel) * next.peakCount;

            for (std::size_t peakIndex = 0U;
                 peakIndex < next.peakCount;
                 ++peakIndex)
            {
                if ((peakIndex & 4095U) == 0U &&
                    IsCancellationRequested(cancellationRequested))
                {
                    errorMessage = "The waveform cache build was cancelled.";
                    return std::nullopt;
                }
                const std::size_t firstChild = peakIndex * 2U;
                AudioWaveformPeak peak = previous.channelMajorPeaks[
                    previousChannelOffset + firstChild
                ];

                const std::size_t secondChild = firstChild + 1U;
                if (secondChild < previous.peakCount)
                {
                    const AudioWaveformPeak other =
                        previous.channelMajorPeaks[
                            previousChannelOffset + secondChild
                        ];
                    peak.minimum = std::min(peak.minimum, other.minimum);
                    peak.maximum = std::max(peak.maximum, other.maximum);
                }

                next.channelMajorPeaks[nextChannelOffset + peakIndex] = peak;
            }
        }

        cache.levels_.push_back(std::move(next));
    }

    return cache;
}

bool AudioWaveformCache::Matches(
    const AudioDocument& document
) const noexcept
{
    return sampleRate_ == document.SampleRate() &&
        channelCount_ == document.ChannelCount() &&
        frameCount_ == document.FrameCount() &&
        revision_ == document.Revision() &&
        sampleData_ == document.Samples().data();
}

std::size_t AudioWaveformCache::LevelCount() const noexcept
{
    return levels_.size();
}

std::size_t AudioWaveformCache::MemoryBytes() const noexcept
{
    std::size_t total = 0U;

    for (const Level& level : levels_)
    {
        const std::size_t bytes =
            level.channelMajorPeaks.size() * sizeof(AudioWaveformPeak);

        if (bytes > std::numeric_limits<std::size_t>::max() - total)
        {
            return std::numeric_limits<std::size_t>::max();
        }

        total += bytes;
    }

    return total;
}

std::optional<AudioWaveformView> AudioWaveformCache::CreateView(
    const AudioDocument& document,
    const AudioFrameRange range,
    const std::size_t requestedColumnCount,
    std::string& errorMessage
) const
{
    errorMessage.clear();

    if (!Matches(document))
    {
        errorMessage = "The waveform cache is out of date.";
        return std::nullopt;
    }

    if (!document.IsValidRange(range) || range.IsEmpty())
    {
        errorMessage = "The waveform range is invalid.";
        return std::nullopt;
    }

    if (requestedColumnCount == 0U ||
        requestedColumnCount > MaximumViewColumns)
    {
        errorMessage = "The waveform column count is invalid.";
        return std::nullopt;
    }

    const std::size_t columnCount = std::min(
        requestedColumnCount,
        range.FrameCount()
    );
    const std::size_t channelCount = static_cast<std::size_t>(
        document.ChannelCount()
    );

    if (columnCount >
        std::vector<AudioWaveformPeak>{}.max_size() / channelCount)
    {
        errorMessage = "The waveform view is too large.";
        return std::nullopt;
    }

    AudioWaveformView view;
    view.range_ = range;
    view.columnCount_ = columnCount;
    view.channelCount_ = document.ChannelCount();
    view.channelMajorPeaks_.resize(columnCount * channelCount);

    const std::size_t frameCount = range.FrameCount();
    const std::size_t baseSpan = frameCount / columnCount;
    const std::size_t extraFrames = frameCount % columnCount;

    for (std::size_t column = 0U; column < columnCount; ++column)
    {
        const std::size_t beginFrame = range.beginFrame +
            column * baseSpan + std::min(column, extraFrames);
        const std::size_t span = baseSpan +
            (column < extraFrames ? 1U : 0U);
        const std::size_t endFrame = beginFrame + span;

        const Level* selectedLevel = nullptr;
        for (const Level& level : levels_)
        {
            if (level.framesPerPeak > span)
            {
                break;
            }

            selectedLevel = &level;
        }

        for (std::uint32_t channel = 0U;
             channel < document.ChannelCount();
             ++channel)
        {
            AudioWaveformPeak peak;
            bool hasValue = false;

            if (selectedLevel == nullptr)
            {
                IncludeRawFrames(
                    document,
                    beginFrame,
                    endFrame,
                    channel,
                    peak,
                    hasValue
                );
            }
            else
            {
                const std::size_t blockSize =
                    selectedLevel->framesPerPeak;
                const std::size_t firstFullBlock = DivideRoundingUp(
                    beginFrame,
                    blockSize
                );
                const std::size_t lastFullBlock = endFrame / blockSize;
                const std::size_t rawPrefixEnd = std::min(
                    endFrame,
                    firstFullBlock * blockSize
                );

                IncludeRawFrames(
                    document,
                    beginFrame,
                    rawPrefixEnd,
                    channel,
                    peak,
                    hasValue
                );

                const std::size_t channelOffset =
                    static_cast<std::size_t>(channel) *
                    selectedLevel->peakCount;

                for (std::size_t block = firstFullBlock;
                     block < lastFullBlock;
                     ++block)
                {
                    IncludePeak(
                        peak,
                        hasValue,
                        selectedLevel->channelMajorPeaks[
                            channelOffset + block
                        ]
                    );
                }

                const std::size_t rawSuffixBegin = std::max(
                    rawPrefixEnd,
                    lastFullBlock * blockSize
                );
                IncludeRawFrames(
                    document,
                    rawSuffixBegin,
                    endFrame,
                    channel,
                    peak,
                    hasValue
                );
            }

            view.channelMajorPeaks_[
                static_cast<std::size_t>(channel) * columnCount + column
            ] = peak;
        }
    }

    return view;
}
