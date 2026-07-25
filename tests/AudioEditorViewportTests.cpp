#include "editor/AudioEditorViewport.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

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
        const double left,
        const double right,
        const double tolerance = 0.000001
    )
    {
        return std::abs(left - right) <= tolerance;
    }

    void TestResetAndMapping()
    {
        AudioEditorViewport viewport;
        viewport.Reset(1000U);

        Expect(!viewport.Empty(), "reset creates a non-empty viewport");
        Expect(viewport.IsFit(), "reset fits the complete document");
        Expect(
            viewport.VisibleRange().beginFrame == 0U &&
                viewport.VisibleRange().endFrame == 1000U,
            "fit range covers the complete document"
        );
        Expect(
            viewport.FrameAtPixel(0, 101) == 0U,
            "left pixel maps to the first frame"
        );
        Expect(
            viewport.FrameAtPixel(100, 101) == 999U,
            "right pixel maps to the last visible frame"
        );
        Expect(
            viewport.PixelForFrame(500U, 101) == 50,
            "middle frame maps to the middle pixel"
        );
    }

    void TestAnchoredZoom()
    {
        AudioEditorViewport viewport;
        viewport.Reset(1000U);

        Expect(viewport.ZoomAt(500U, 2.0), "zoom-in changes the viewport");
        const AudioFrameRange centered = viewport.VisibleRange();
        Expect(centered.FrameCount() == 500U, "2x zoom halves visible frames");
        Expect(
            centered.beginFrame == 250U && centered.endFrame == 750U,
            "center anchor stays centered"
        );

        Expect(viewport.ZoomAt(250U, 2.0), "second zoom changes viewport");
        const AudioFrameRange leftAnchored = viewport.VisibleRange();
        Expect(
            leftAnchored.beginFrame == 250U,
            "zooming at the left edge preserves the anchor"
        );
        Expect(
            leftAnchored.FrameCount() == 250U,
            "second 2x zoom halves visible frames again"
        );

        for (int index = 0; index < 20; ++index)
        {
            viewport.ZoomAt(250U, 2.0);
        }
        Expect(
            viewport.VisibleRange().FrameCount() ==
                AudioEditorViewport::MinimumVisibleFrames,
            "zoom-in stops at the minimum frame window"
        );

        Expect(viewport.ZoomAt(250U, 0.01), "zoom-out changes viewport");
        Expect(viewport.IsFit(), "large zoom-out returns to fit");
    }

    void TestPanAndScrollRatio()
    {
        AudioEditorViewport viewport;
        viewport.Reset(1000U);
        viewport.ZoomAt(500U, 2.0);

        Expect(viewport.PanFrames(100), "positive pan moves right");
        Expect(
            viewport.VisibleRange().beginFrame == 350U,
            "positive pan advances the beginning"
        );
        Expect(viewport.PanFrames(-500), "negative pan moves left");
        Expect(
            viewport.VisibleRange().beginFrame == 0U,
            "negative pan clamps at the start"
        );
        Expect(viewport.SetScrollRatio(1.0), "scroll ratio moves to the end");
        Expect(
            viewport.VisibleRange().endFrame == 1000U,
            "end scroll clamps at the document end"
        );
        Expect(
            NearlyEqual(viewport.ScrollRatio(), 1.0),
            "end scroll reports ratio one"
        );
        Expect(viewport.SetScrollRatio(0.5), "scroll ratio moves to middle");
        Expect(
            NearlyEqual(viewport.ScrollRatio(), 0.5, 0.002),
            "middle scroll reports approximately one half"
        );
        Expect(
            NearlyEqual(viewport.VisibleRatio(), 0.5),
            "visible ratio reflects zoom level"
        );
    }

    void TestInvalidAndSmallInputs()
    {
        AudioEditorViewport viewport;
        viewport.Reset(32U);

        Expect(
            !viewport.ZoomAt(16U, 2.0),
            "documents below the minimum zoom window stay fitted"
        );
        Expect(viewport.IsFit(), "small document remains fitted");
        Expect(
            !viewport.ZoomAt(16U, 0.0),
            "zero magnification is rejected"
        );
        Expect(
            !viewport.ZoomAt(
                16U,
                std::numeric_limits<double>::quiet_NaN()
            ),
            "non-finite magnification is rejected"
        );
        Expect(
            !viewport.SetScrollRatio(
                std::numeric_limits<double>::infinity()
            ),
            "non-finite scroll ratio is rejected"
        );

        viewport.Reset(0U);
        Expect(viewport.Empty(), "zero frames produce an empty viewport");
        Expect(viewport.IsFit(), "empty viewport is considered fitted");
        Expect(
            viewport.FrameAtPixel(10, 100) == 0U,
            "empty viewport maps every pixel to zero"
        );
    }
}

int main()
{
    TestResetAndMapping();
    TestAnchoredZoom();
    TestPanAndScrollRatio();
    TestInvalidAndSmallInputs();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " viewport assertion(s) failed.\n";
        return 1;
    }

    std::cout << "AudioEditorViewport tests passed.\n";
    return 0;
}
