#include "editor/AudioEditorViewport.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

void AudioEditorViewport::Reset(const std::size_t totalFrames) noexcept
{
    totalFrames_ = totalFrames;
    beginFrame_ = 0U;
    endFrame_ = totalFrames_;
}

bool AudioEditorViewport::Empty() const noexcept
{
    return totalFrames_ == 0U;
}

AudioFrameRange AudioEditorViewport::VisibleRange() const noexcept
{
    return AudioFrameRange{beginFrame_, endFrame_};
}

double AudioEditorViewport::VisibleRatio() const noexcept
{
    if (totalFrames_ == 0U)
    {
        return 1.0;
    }

    return static_cast<double>(endFrame_ - beginFrame_) /
        static_cast<double>(totalFrames_);
}

double AudioEditorViewport::ScrollRatio() const noexcept
{
    const std::size_t visibleFrames = endFrame_ - beginFrame_;
    if (totalFrames_ <= visibleFrames)
    {
        return 0.0;
    }

    return static_cast<double>(beginFrame_) /
        static_cast<double>(totalFrames_ - visibleFrames);
}

bool AudioEditorViewport::IsFit() const noexcept
{
    return totalFrames_ == 0U ||
        (beginFrame_ == 0U && endFrame_ == totalFrames_);
}

bool AudioEditorViewport::ZoomAt(
    const std::size_t anchorFrame,
    const double magnification
) noexcept
{
    if (totalFrames_ == 0U || !std::isfinite(magnification) ||
        magnification <= 0.0)
    {
        return false;
    }

    const std::size_t currentFrames = endFrame_ - beginFrame_;
    if (currentFrames == 0U)
    {
        Reset(totalFrames_);
        return false;
    }

    const std::size_t minimumFrames = std::min(
        totalFrames_,
        MinimumVisibleFrames
    );
    const long double desiredFramesValue =
        static_cast<long double>(currentFrames) /
        static_cast<long double>(magnification);
    std::size_t desiredFrames = static_cast<std::size_t>(std::llround(
        std::clamp(
            desiredFramesValue,
            static_cast<long double>(minimumFrames),
            static_cast<long double>(totalFrames_)
        )
    ));
    desiredFrames = std::clamp(desiredFrames, minimumFrames, totalFrames_);

    if (desiredFrames == currentFrames)
    {
        return false;
    }

    const std::size_t clampedAnchor = std::min(anchorFrame, totalFrames_);
    const long double anchorPosition = std::clamp(
        static_cast<long double>(clampedAnchor) -
            static_cast<long double>(beginFrame_),
        0.0L,
        static_cast<long double>(currentFrames)
    );
    const long double anchorRatio = anchorPosition /
        static_cast<long double>(currentFrames);
    const long double desiredBeginValue =
        static_cast<long double>(clampedAnchor) -
        anchorRatio * static_cast<long double>(desiredFrames);

    const std::size_t maximumBegin = totalFrames_ - desiredFrames;
    beginFrame_ = static_cast<std::size_t>(std::llround(std::clamp(
        desiredBeginValue,
        0.0L,
        static_cast<long double>(maximumBegin)
    )));
    endFrame_ = beginFrame_ + desiredFrames;
    ClampRange();
    return true;
}

bool AudioEditorViewport::SetVisibleRange(
    const AudioFrameRange range
) noexcept
{
    if (totalFrames_ == 0U || range.beginFrame >= range.endFrame ||
        range.endFrame > totalFrames_)
    {
        return false;
    }

    const std::size_t minimumFrames = std::min(
        totalFrames_,
        MinimumVisibleFrames
    );
    const std::size_t desiredFrames = std::max(
        minimumFrames,
        range.FrameCount()
    );
    const std::size_t centerFrame = range.beginFrame +
        range.FrameCount() / 2U;
    const std::size_t halfFrames = desiredFrames / 2U;
    std::size_t beginFrame = centerFrame > halfFrames
        ? centerFrame - halfFrames
        : 0U;

    if (beginFrame + desiredFrames > totalFrames_)
    {
        beginFrame = totalFrames_ - desiredFrames;
    }

    const std::size_t endFrame = beginFrame + desiredFrames;
    if (beginFrame == beginFrame_ && endFrame == endFrame_)
    {
        return false;
    }

    beginFrame_ = beginFrame;
    endFrame_ = endFrame;
    ClampRange();
    return true;
}

bool AudioEditorViewport::PanFrames(const std::int64_t deltaFrames) noexcept
{
    const std::size_t visibleFrames = endFrame_ - beginFrame_;
    if (totalFrames_ <= visibleFrames || deltaFrames == 0)
    {
        return false;
    }

    const std::size_t maximumBegin = totalFrames_ - visibleFrames;
    std::size_t newBegin = beginFrame_;

    if (deltaFrames > 0)
    {
        const auto positiveDelta = static_cast<std::uint64_t>(deltaFrames);
        const std::size_t available = maximumBegin - beginFrame_;
        const std::size_t applied = static_cast<std::size_t>(std::min<
            std::uint64_t
        >(positiveDelta, available));
        newBegin += applied;
    }
    else
    {
        const std::uint64_t magnitude =
            static_cast<std::uint64_t>(-(deltaFrames + 1)) + 1U;
        const std::size_t applied = static_cast<std::size_t>(std::min<
            std::uint64_t
        >(magnitude, beginFrame_));
        newBegin -= applied;
    }

    if (newBegin == beginFrame_)
    {
        return false;
    }

    beginFrame_ = newBegin;
    endFrame_ = beginFrame_ + visibleFrames;
    return true;
}

bool AudioEditorViewport::SetScrollRatio(const double ratio) noexcept
{
    if (totalFrames_ == 0U || !std::isfinite(ratio))
    {
        return false;
    }

    const std::size_t visibleFrames = endFrame_ - beginFrame_;
    if (totalFrames_ <= visibleFrames)
    {
        return false;
    }

    const std::size_t maximumBegin = totalFrames_ - visibleFrames;
    const long double clampedRatio = std::clamp(
        static_cast<long double>(ratio),
        0.0L,
        1.0L
    );
    const std::size_t newBegin = static_cast<std::size_t>(std::llround(
        clampedRatio * static_cast<long double>(maximumBegin)
    ));

    if (newBegin == beginFrame_)
    {
        return false;
    }

    beginFrame_ = newBegin;
    endFrame_ = beginFrame_ + visibleFrames;
    return true;
}

std::size_t AudioEditorViewport::FrameAtPixel(
    const int x,
    const int pixelWidth
) const noexcept
{
    if (totalFrames_ == 0U || pixelWidth <= 1)
    {
        return beginFrame_;
    }

    const int clampedX = std::clamp(x, 0, pixelWidth - 1);
    const std::size_t visibleFrames = endFrame_ - beginFrame_;
    if (visibleFrames <= 1U)
    {
        return beginFrame_;
    }

    const long double ratio = static_cast<long double>(clampedX) /
        static_cast<long double>(pixelWidth - 1);
    const std::size_t offset = static_cast<std::size_t>(std::llround(
        ratio * static_cast<long double>(visibleFrames - 1U)
    ));
    return std::min(beginFrame_ + offset, endFrame_ - 1U);
}

int AudioEditorViewport::PixelForFrame(
    const std::size_t frame,
    const int pixelWidth
) const noexcept
{
    if (pixelWidth <= 1 || totalFrames_ == 0U)
    {
        return 0;
    }

    if (frame <= beginFrame_)
    {
        return 0;
    }
    if (frame >= endFrame_)
    {
        return pixelWidth - 1;
    }

    const std::size_t visibleFrames = endFrame_ - beginFrame_;
    if (visibleFrames <= 1U)
    {
        return 0;
    }

    const long double ratio =
        static_cast<long double>(frame - beginFrame_) /
        static_cast<long double>(visibleFrames - 1U);
    return static_cast<int>(std::llround(
        ratio * static_cast<long double>(pixelWidth - 1)
    ));
}

void AudioEditorViewport::ClampRange() noexcept
{
    if (totalFrames_ == 0U)
    {
        beginFrame_ = 0U;
        endFrame_ = 0U;
        return;
    }

    beginFrame_ = std::min(beginFrame_, totalFrames_ - 1U);
    endFrame_ = std::clamp(endFrame_, beginFrame_ + 1U, totalFrames_);
}
