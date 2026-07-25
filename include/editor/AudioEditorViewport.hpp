#pragma once

#include "editor/AudioDocument.hpp"

#include <cstddef>
#include <cstdint>

class AudioEditorViewport final
{
public:
    static constexpr std::size_t MinimumVisibleFrames = 64U;

    void Reset(std::size_t totalFrames) noexcept;

    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t TotalFrames() const noexcept;
    [[nodiscard]] AudioFrameRange VisibleRange() const noexcept;
    [[nodiscard]] double VisibleRatio() const noexcept;
    [[nodiscard]] double ScrollRatio() const noexcept;
    [[nodiscard]] bool IsFit() const noexcept;

    bool ZoomAt(std::size_t anchorFrame, double magnification) noexcept;
    bool PanFrames(std::int64_t deltaFrames) noexcept;
    bool SetScrollRatio(double ratio) noexcept;

    [[nodiscard]] std::size_t FrameAtPixel(
        int x,
        int pixelWidth
    ) const noexcept;

    [[nodiscard]] int PixelForFrame(
        std::size_t frame,
        int pixelWidth
    ) const noexcept;

private:
    void ClampRange() noexcept;

    std::size_t totalFrames_ = 0U;
    std::size_t beginFrame_ = 0U;
    std::size_t endFrame_ = 0U;
};
