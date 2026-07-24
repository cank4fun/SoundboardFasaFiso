# v2.1 User-Mode Playback Engine

## Scope

This work remains entirely in user mode. It does not create a kernel driver,
virtual audio endpoint or physical-microphone injection path. Existing main and
monitor routing continues to use normal Windows playback devices.

## Alpha 1 foundation

The audio runtime assigns a monotonic `PlaybackId` whenever a voice starts.
Active sessions can be queried through `GetPlaybackSnapshots()`, which reports:

- sound ID;
- playing or paused state;
- playback mode;
- current position and decoded duration; and
- the current per-session volume.

The runtime also exposes session-scoped pause, resume, stop, seek and volume
operations. These APIs deliberately arrive before GUI controls so progress
bars, an active-playback list and remote-control integrations can share one
stable runtime contract.

Overlap voices receive independent IDs. Restart, toggle and loop modes continue
to preserve their existing hotkey behavior. The `ignore` policy starts an
inactive sound but leaves an existing playing or paused session untouched.
A stopped or restarted voice invalidates its previous session ID.

## Active-playback controls

The native control panel now includes an **Active Playbacks** tab. It refreshes
from the session snapshot API without changing the existing hotkey paths and
keeps the selected `PlaybackId` stable while positions advance. The selected
session can be paused or resumed, stopped independently, seeked within decoded
duration and given a temporary runtime volume. Double-clicking a session also
toggles pause/resume.

The controls operate on a running session only. They do not rewrite the
binding's saved volume or playback mode; persistent binding edits remain in the
Home editor and are applied through **Save and apply**.

Playback time keeps fractional seconds, using centiseconds for clips below ten
seconds. The seek thumb is not overwritten by the periodic runtime refresh while
the user has mouse capture, and the final seek is committed once when dragging
ends. This is especially important for the bundled example clips, which are only
about half a second long. After a seek is committed, the UI keeps the target
position until the audio engine reports the updated cursor; this prevents the
thumb from briefly jumping back to the pre-seek position.

## Retrigger policies

Playback behavior is resolved from a small deterministic policy table shared by
the runtime and unit tests:

- `restart`: start when inactive, restart from the beginning when active;
- `overlap`: always start a separate voice from the bounded overlap pool;
- `toggle`: start when inactive, stop when active;
- `loop`: start looping when inactive, stop when active;
- `ignore`: start when inactive and ignore repeated presses while active.

Paused sessions count as active. This prevents `ignore` from accidentally
restarting a paused clip. The existing `toggle` mode remains the
start-when-inactive, stop-when-active policy.

## Planned follow-up

1. Add configurable fade-in and fade-out envelopes.
2. Add queue controls and bounded per-binding concurrency.
3. Add categories, search, favorites and profiles.
4. Add Auto-PTT, recording, editing, TTS and external control APIs.

## Configuration durability

Config saves now write and flush a sibling temporary file before Windows
atomically replaces the destination. When a previous config exists, Windows
keeps it as `config.txt.bak`. A successful save removes the temporary file but
retains the last known-good backup for manual recovery.
