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
to preserve their existing hotkey behavior. A stopped or restarted voice
invalidates its previous session ID.

## Planned follow-up

1. Add the active-playback panel, progress bars and seek interaction.
2. Add configurable fade-in and fade-out envelopes.
3. Add richer retrigger policies and queue controls.
4. Add categories, search, favorites and profiles.
5. Add Auto-PTT, recording, editing, TTS and external control APIs.

## Configuration durability

Config saves now write and flush a sibling temporary file before Windows
atomically replaces the destination. When a previous config exists, Windows
keeps it as `config.txt.bak`. A successful save removes the temporary file but
retains the last known-good backup for manual recovery.
