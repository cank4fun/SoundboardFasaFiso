#pragma once

#include <string_view>

enum class PlaybackMode
{
    Restart,
    Overlap,
    Toggle,
    Loop
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
    }

    return "restart";
}
