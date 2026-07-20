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

## Planned milestones

### Alpha 1 — localization foundation

- Embedded Turkish and English messages
- `language=tr|en`
- Existing config compatibility
- No external language files required

### Alpha 2 — control-panel foundation

- Native Win32 control panel without an external GUI runtime
- Live runtime status and configuration summary
- Sound-binding overview and command dispatch to the existing runtime
- Tray-first lifecycle where closing the panel keeps the soundboard running

### Alpha 3 — core settings editor

- Device enumeration and main/monitor selectors
- Volume, language, sample-rate and buffer controls
- Pending-config validation, atomic save and runtime rollback

### Alpha 4 — sound and hotkey editor

- Add, edit and remove sound bindings from the native control panel
- Capture supported hotkey combinations and edit control hotkeys
- Select WAV/MP3/FLAC files and optionally copy them into `sounds`
- Keep edits pending until full config and runtime validation succeeds

### Alpha 5 — microphone mixer

- Physical microphone capture
- Soundboard + microphone gain and mute controls
- Monitor and broadcast sink selection

### Beta — packaging and migration

- Console-free normal mode with optional diagnostics window
- v1 config migration
- Portable release and clean-machine testing

## Driver boundary

A development-only virtual microphone can be built from Microsoft audio driver samples,
but ordinary 64-bit Windows systems require trusted signing for public deployment. The
main v2 application must not depend on test-signing mode or weakened Secure Boot settings.
