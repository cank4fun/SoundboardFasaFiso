# SoundBoardFasaFiso v2 Architecture

## Goals

v2 keeps the portable, no-installer workflow while adding a GUI, editable profiles,
Turkish/English localization, microphone capture and a cleaner audio-routing model.

## What “standalone” means

The application itself will remain self-contained: no .NET, Java, Python, external
codec pack or Visual C++ Redistributable is required.

Local playback to physical speakers or headphones is already standalone. Capturing a
physical microphone and mixing it with soundboard audio can also be implemented inside
the application.

Exposing that mixed stream to Discord, Steam, games or OBS as a selectable microphone is
a different layer. Windows applications discover microphone endpoints through the audio
driver stack. A normal user-mode EXE cannot create a permanent system microphone endpoint
by itself.

For public builds, v2 therefore separates the system into:

1. **Soundboard core** — decoding, RAM cache, playback modes and profiles.
2. **Mixer** — microphone capture, soundboard mix, gain, mute and monitoring.
3. **Output sinks** — physical device, file/recording sink, or an installed virtual cable.
4. **Virtual microphone bridge** — optional driver-backed endpoint.

This separation allows the GUI and mixer to work without VB-CABLE while keeping VB-CABLE
or another installed virtual device as an optional bridge for voice-chat applications.
A first-party virtual microphone driver can be added later without rewriting the core.

## Current application structure

The v2 executable is divided into a small set of native components:

- **Audio runtime** — playback engines, RAM-backed sounds, voice pools, microphone capture and device recovery
- **Configuration** — UTF-8 parsing, validation, atomic writes and rollback
- **Control panel** — one native Win32 window with tabbed editing, DPI scaling and light/dark themes
- **Hotkeys and tray** — global commands, tray lifecycle and single-instance behavior
- **Diagnostics** — rotating session logs and an on-demand debug console
- **Update check** — a read-only background request to the public GitHub Releases API

All settings are applied as a transaction. A candidate config is written to a temporary file, validated, and used to rebuild the runtime. The active config is replaced only after devices, sounds and hotkeys initialize successfully.

## Driver boundary

A development-only virtual microphone can be built from Microsoft audio driver samples,
but ordinary 64-bit Windows systems require trusted signing for public deployment. The
main v2 application must not depend on test-signing mode or weakened Secure Boot settings.
