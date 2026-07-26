#pragma once

#include <string_view>

enum class PlaybackMode
{
    Restart,
    Overlap,
    Toggle,
    Loop,
    Ignore
};

enum class PlaybackTriggerAction
{
    Start,
    Restart,
    StartOverlap,
    Stop,
    Ignore
};

constexpr std::string_view PlaybackModeName(const PlaybackMode mode)
{
    switch (mode)
    {
        case PlaybackMode::Restart:
            return "restart";

        case PlaybackMode::Overlap:
            return "overlap";

        case PlaybackMode::Toggle:
            return "toggle";

        case PlaybackMode::Loop:
            return "loop";

        case PlaybackMode::Ignore:
            return "ignore";
    }

    return "restart";
}

constexpr PlaybackTriggerAction ResolvePlaybackTrigger(
    const PlaybackMode mode,
    const bool active
)
{
    if (mode == PlaybackMode::Overlap)
    {
        return PlaybackTriggerAction::StartOverlap;
    }

    if (!active)
    {
        return PlaybackTriggerAction::Start;
    }

    switch (mode)
    {
        case PlaybackMode::Restart:
            return PlaybackTriggerAction::Restart;

        case PlaybackMode::Toggle:
        case PlaybackMode::Loop:
            return PlaybackTriggerAction::Stop;

        case PlaybackMode::Ignore:
            return PlaybackTriggerAction::Ignore;

        case PlaybackMode::Overlap:
            return PlaybackTriggerAction::StartOverlap;
    }

    return PlaybackTriggerAction::Restart;
}
