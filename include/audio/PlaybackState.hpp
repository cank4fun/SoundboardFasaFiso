#pragma once

#include "sound/PlaybackMode.hpp"

#include <cstdint>
#include <string>

using PlaybackId = std::uint64_t;

inline constexpr PlaybackId InvalidPlaybackId = 0;

enum class PlaybackStatus
{
    Playing,
    Paused
};

struct PlaybackSnapshot
{
    PlaybackId id = InvalidPlaybackId;
    std::string soundId;
    PlaybackStatus status = PlaybackStatus::Playing;
    PlaybackMode mode = PlaybackMode::Restart;
    float positionSeconds = 0.0f;
    float durationSeconds = 0.0f;
    float volume = 1.0f;
};
