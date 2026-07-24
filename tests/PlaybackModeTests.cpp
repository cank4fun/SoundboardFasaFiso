#include "sound/PlaybackMode.hpp"

#include <iostream>
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
}

int main()
{
    Expect(
        ResolvePlaybackTrigger(PlaybackMode::Restart, false) ==
            PlaybackTriggerAction::Start,
        "restart starts an inactive sound"
    );
    Expect(
        ResolvePlaybackTrigger(PlaybackMode::Restart, true) ==
            PlaybackTriggerAction::Restart,
        "restart restarts an active sound"
    );
    Expect(
        ResolvePlaybackTrigger(PlaybackMode::Overlap, false) ==
            PlaybackTriggerAction::StartOverlap,
        "overlap starts a separate inactive voice"
    );
    Expect(
        ResolvePlaybackTrigger(PlaybackMode::Overlap, true) ==
            PlaybackTriggerAction::StartOverlap,
        "overlap starts a separate active voice"
    );
    Expect(
        ResolvePlaybackTrigger(PlaybackMode::Toggle, false) ==
            PlaybackTriggerAction::Start,
        "toggle starts an inactive sound"
    );
    Expect(
        ResolvePlaybackTrigger(PlaybackMode::Toggle, true) ==
            PlaybackTriggerAction::Stop,
        "toggle stops an active sound"
    );
    Expect(
        ResolvePlaybackTrigger(PlaybackMode::Loop, false) ==
            PlaybackTriggerAction::Start,
        "loop starts an inactive sound"
    );
    Expect(
        ResolvePlaybackTrigger(PlaybackMode::Loop, true) ==
            PlaybackTriggerAction::Stop,
        "loop stops an active sound"
    );
    Expect(
        ResolvePlaybackTrigger(PlaybackMode::Ignore, false) ==
            PlaybackTriggerAction::Start,
        "ignore starts an inactive sound"
    );
    Expect(
        ResolvePlaybackTrigger(PlaybackMode::Ignore, true) ==
            PlaybackTriggerAction::Ignore,
        "ignore leaves an active sound untouched"
    );
    Expect(
        PlaybackModeName(PlaybackMode::Ignore) == "ignore",
        "ignore has a stable serialized name"
    );
    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Playback mode tests passed.\n";
    return 0;
}
