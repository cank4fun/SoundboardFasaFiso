#include "editor/AudioWaveformCache.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    int failureCount = 0;

    void Expect(const bool condition, const std::string_view message)
    {
        if (condition)
        {
            return;
        }

        ++failureCount;
        std::cerr << "FAILED: " << message << '\n';
    }

    bool NearlyEqual(
        const float left,
        const float right,
        const float tolerance = 0.00001f
    )
    {
        return std::abs(left - right) <= tolerance;
    }

    AudioDocument RequireDocument(
        std::optional<AudioDocument> document,
        const std::string_view message
    )
    {
        if (!document.has_value())
        {
            Expect(false, message);
            std::exit(1);
        }

        return std::move(*document);
    }

    AudioWaveformCache RequireCache(
        std::optional<AudioWaveformCache> cache,
        const std::string_view message
    )
    {
        if (!cache.has_value())
        {
            Expect(false, message);
            std::exit(1);
        }

        return std::move(*cache);
    }

    AudioWaveformView RequireView(
        std::optional<AudioWaveformView> view,
        const std::string_view message
    )
    {
        if (!view.has_value())
        {
            Expect(false, message);
            std::exit(1);
        }

        return std::move(*view);
    }

    void ExpectPeak(
        const AudioWaveformPeak peak,
        const float minimum,
        const float maximum,
        const std::string_view message
    )
    {
        Expect(
            NearlyEqual(peak.minimum, minimum) &&
                NearlyEqual(peak.maximum, maximum),
            message
        );
    }

    AudioDocument CreateDocument(
        const std::uint32_t channels,
        std::vector<float> samples
    )
    {
        std::string errorMessage;
        return RequireDocument(
            AudioDocument::Create(
                48000U,
                channels,
                std::move(samples),
                errorMessage
            ),
            "waveform fixture is valid"
        );
    }

    void TestExactNearZoomPeaks()
    {
        AudioDocument document = CreateDocument(
            1U,
            {-0.8f, 0.2f, 0.5f, 1.4f, -1.2f, -0.1f, 0.3f, 0.9f}
        );
        std::string errorMessage;
        AudioWaveformCache cache = RequireCache(
            AudioWaveformCache::Build(document, errorMessage),
            "near-zoom cache is built"
        );
        AudioWaveformView view = RequireView(
            cache.CreateView(document, {0U, 8U}, 4U, errorMessage),
            "near-zoom view is built"
        );

        Expect(view.ColumnCount() == 4U, "requested columns are retained");
        Expect(view.ChannelCount() == 1U, "channel count is retained");
        Expect(view.Range().beginFrame == 0U, "view range start is retained");
        Expect(view.Range().endFrame == 8U, "view range end is retained");

        const std::span<const AudioWaveformPeak> peaks =
            view.PeaksForChannel(0U);
        Expect(peaks.size() == 4U, "mono peak span has one peak per column");
        ExpectPeak(peaks[0], -0.8f, 0.2f, "first near-zoom peak is exact");
        ExpectPeak(peaks[1], 0.5f, 1.4f, "headroom is not clipped");
        ExpectPeak(peaks[2], -1.2f, -0.1f, "negative peak is exact");
        ExpectPeak(peaks[3], 0.3f, 0.9f, "last near-zoom peak is exact");
        Expect(
            view.PeaksForChannel(1U).empty(),
            "out-of-range channel returns an empty span"
        );
    }

    void TestChannelSeparationAndColumnClamp()
    {
        AudioDocument document = CreateDocument(
            2U,
            {
                -1.0f, 0.1f,
                0.5f, 0.2f,
                -0.25f, 0.9f
            }
        );
        std::string errorMessage;
        AudioWaveformCache cache = RequireCache(
            AudioWaveformCache::Build(document, errorMessage),
            "stereo cache is built"
        );
        AudioWaveformView view = RequireView(
            cache.CreateView(document, {0U, 3U}, 100U, errorMessage),
            "column-clamped view is built"
        );

        Expect(
            view.ColumnCount() == 3U,
            "column count is clamped to available frames"
        );
        const auto left = view.PeaksForChannel(0U);
        const auto right = view.PeaksForChannel(1U);
        ExpectPeak(left[0], -1.0f, -1.0f, "left channel frame zero is isolated");
        ExpectPeak(left[2], -0.25f, -0.25f, "left channel frame two is isolated");
        ExpectPeak(right[0], 0.1f, 0.1f, "right channel frame zero is isolated");
        ExpectPeak(right[2], 0.9f, 0.9f, "right channel frame two is isolated");
    }

    void TestCachedFarZoomAndUnalignedRange()
    {
        std::vector<float> samples(1024U, 0.0f);
        for (std::size_t frame = 0U; frame < samples.size(); ++frame)
        {
            samples[frame] = static_cast<float>(
                static_cast<int>(frame % 101U) - 50
            ) / 10.0f;
        }
        samples[137U] = -9.0f;
        samples[741U] = 11.0f;

        AudioDocument document = CreateDocument(1U, std::move(samples));
        std::string errorMessage;
        AudioWaveformCache cache = RequireCache(
            AudioWaveformCache::Build(document, errorMessage),
            "far-zoom cache is built"
        );

        Expect(cache.LevelCount() == 5U, "power-of-two cache pyramid is built");
        Expect(cache.MemoryBytes() > 0U, "cache reports allocated peak memory");

        AudioWaveformView view = RequireView(
            cache.CreateView(document, {13U, 1013U}, 2U, errorMessage),
            "unaligned far-zoom view is built"
        );
        const auto peaks = view.PeaksForChannel(0U);
        ExpectPeak(peaks[0], -9.0f, 5.0f, "first far-zoom column includes exact edges");
        ExpectPeak(peaks[1], -5.0f, 11.0f, "second far-zoom column includes cached extrema");
    }

    void TestCacheInvalidation()
    {
        AudioDocument document = CreateDocument(1U, {0.25f, -0.5f, 0.75f});
        std::string errorMessage;
        AudioWaveformCache cache = RequireCache(
            AudioWaveformCache::Build(document, errorMessage),
            "invalidation cache is built"
        );

        Expect(cache.Matches(document), "fresh cache matches its document");
        AudioDocument other = CreateDocument(1U, {0.25f, -0.5f, 0.75f});
        Expect(
            !cache.Matches(other),
            "same-shaped independent document does not reuse the cache"
        );
        const std::uint64_t revision = document.Revision();
        Expect(
            document.ApplyGainDecibels({0U, 3U}, 6.0205999f) ==
                AudioEditResult::Applied,
            "document edit is applied"
        );
        Expect(document.Revision() == revision + 1U, "applied edit advances revision");
        Expect(!cache.Matches(document), "edit invalidates existing cache");
        Expect(
            !cache.CreateView(document, {0U, 3U}, 3U, errorMessage).has_value(),
            "stale cache refuses to create a view"
        );
        Expect(!errorMessage.empty(), "stale-cache error is reported");

        AudioWaveformCache rebuilt = RequireCache(
            AudioWaveformCache::Build(document, errorMessage),
            "cache can be rebuilt after an edit"
        );
        Expect(rebuilt.Matches(document), "rebuilt cache matches edited document");
    }

    void TestBuildCancellation()
    {
        AudioDocument document = CreateDocument(
            1U,
            {0.0f, 0.5f, -0.5f, 1.0f}
        );
        std::atomic_bool cancellationRequested{true};
        std::string errorMessage;

        Expect(
            !AudioWaveformCache::Build(
                document,
                errorMessage,
                &cancellationRequested
            ).has_value(),
            "pre-cancelled waveform build is rejected"
        );
        Expect(
            errorMessage == "The waveform cache build was cancelled.",
            "waveform cancellation is reported"
        );
    }

    void TestValidationAndEmptyDocument()
    {
        AudioDocument empty = CreateDocument(1U, {});
        std::string errorMessage;
        AudioWaveformCache cache = RequireCache(
            AudioWaveformCache::Build(empty, errorMessage),
            "empty document cache is valid"
        );

        Expect(cache.LevelCount() == 0U, "empty document allocates no levels");
        Expect(cache.MemoryBytes() == 0U, "empty cache allocates no peak memory");
        Expect(
            !cache.CreateView(empty, {0U, 0U}, 1U, errorMessage).has_value(),
            "empty range is rejected"
        );

        AudioDocument document = CreateDocument(1U, {0.0f, 1.0f});
        cache = RequireCache(
            AudioWaveformCache::Build(document, errorMessage),
            "validation cache is built"
        );
        Expect(
            !cache.CreateView(document, {0U, 2U}, 0U, errorMessage).has_value(),
            "zero columns are rejected"
        );
        Expect(
            !cache.CreateView(
                document,
                {0U, 2U},
                AudioWaveformCache::MaximumViewColumns + 1U,
                errorMessage
            ).has_value(),
            "excessive columns are rejected"
        );
        Expect(
            !cache.CreateView(document, {0U, 3U}, 2U, errorMessage).has_value(),
            "out-of-range frames are rejected"
        );
    }
}

int main()
{
    TestExactNearZoomPeaks();
    TestChannelSeparationAndColumnClamp();
    TestCachedFarZoomAndUnalignedRange();
    TestCacheInvalidation();
    TestBuildCancellation();
    TestValidationAndEmptyDocument();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " waveform cache test(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Audio waveform cache tests passed.\n";
    return EXIT_SUCCESS;
}
