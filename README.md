<p align="center">
  <img src="assets/fasafisotray.png" alt="SoundBoardFasaFiso logo" width="180">
</p>

<h1 align="center">SoundBoardFasaFiso</h1>

<p align="center">
  A portable native Windows soundboard, microphone processor, media importer, and WAV editor built with C++23.
</p>

<p align="center">
  <a href="https://github.com/cank4fun/SoundboardFasaFiso/releases/latest">
    <img alt="Release" src="https://img.shields.io/github/v/release/cank4fun/SoundboardFasaFiso">
  </a>
  <img alt="Platform" src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D4">
  <img alt="Language" src="https://img.shields.io/badge/C%2B%2B-23-00599C">
  <img alt="Audio" src="https://img.shields.io/badge/audio-WASAPI-6A5ACD">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-green">
</p>

## About

SoundBoardFasaFiso is a standalone Windows audio toolkit designed for voice chat, streaming, gaming, and fast sound playback.

It combines a global-hotkey soundboard, independent main and monitor routing, physical microphone mixing, RNNoise noise suppression, optional WebRTC AEC3 echo cancellation, a low-latency Voice Effects / Voice Changer pipeline, local and URL media import, and an embedded waveform editor in one native Win32 application.

A common setup sends the main mix to **VB-CABLE** for voice chat or streaming while the monitor output plays through headphones. VB-CABLE is optional; the application also works as a normal local soundboard.

There is no installer, database, background service, .NET runtime or Qt dependency. The official build is distributed as a portable ZIP and keeps its configuration, sounds, logs, and imported media beside the executable.

**Current stable release:** `v2.3.0`

## Classic Voice Engine 2 in v2.3.0

- Rebuilt the local Voice Effects path around deterministic, allocation-free speech analysis, pitch, formant, vocal-weight and voice-polish engines.
- Uses no AI inference, model file, network service, installer or additional runtime dependency.
- Keeps independent pitch (`-12` to `+12` semitones), formant (`-6` to `+6` semitones), Vocal Weight, character, drive, dry/wet and output-gain controls.
- Adds an adjustable three-band parametric EQ, de-esser, speech-aware gate/expander and compressor.
- Adds a live-reorderable effect rack with independent module bypass and click-resistant order changes.
- Adds portable `.sbffvoice` preset import/export with validation, checksums and safe overwrite recovery.
- Adds a deterministic `7/7` inline self-test without opening a microphone or audio device.
- Keeps the Voice Effects stage after RNNoise and before AGC, with the existing fixed `16 ms` DSP latency.
- Preserves WebRTC AEC3, stereo crosstalk cancellation, output routing, lock-free settings delivery and the portable single-window design.

## AEC foundation from v2.1.2

- AEC3 receives the selected monitor endpoint as a true two-channel render reference instead of collapsing it to mono.
- An automatic stereo crosstalk layer measures each device path at runtime, validates its adaptive leakage model before mixing it in, and suppresses remaining far-end-only residue without a PC-specific fixed delay.
- Missing loopback blocks do not masquerade as valid silent references, and endpoint delay tracking remains with AEC3's internal estimator.

See [docs/releases/v2.3.0.md](docs/releases/v2.3.0.md) and [CHANGELOG.md](CHANGELOG.md) for the complete release notes.

## Features

### Soundboard and audio routing

- Global hotkeys through Win32 `RegisterHotKey`
- Separate main and monitor playback devices
- Independent output volume and mute controls
- Optional VB-CABLE routing for voice chat and streaming
- WAV, MP3, and FLAC playback
- Audio preloaded into RAM for responsive playback
- Per-sound volume
- `restart`, `overlap`, `toggle`, and `loop` playback modes
- Bounded eight-voice overlap pool
- Global stop, output mute, monitor mute, reload, and exit hotkeys
- Automatic recovery after playback-device disconnects
- Configurable WASAPI sample rate and buffer target

### Microphone processing

- Physical microphone capture with device selection and gain
- Independent routing to the main output and monitor output
- Microphone-processing filter chain
- RNNoise noise suppression
- Optional WebRTC AEC3 acoustic echo cancellation
- Embedded Voice Effects / Voice Changer after cleanup and before dynamics
- Classic Voice Engine 2 with independent pitch and formant controls and fixed 16 ms DSP latency
- Deep / Heavy, High / Nasal Rap, Dark Vocal, Radio, Robot and Tiny / High Voice presets
- Vocal Weight, character, drive, dry/wet and output gain
- Three-band parametric EQ, de-esser, gate/expander and compressor
- Reorderable effect rack, portable `.sbffvoice` preset import/export and configurable preset/bypass hotkeys
- Inline Voice Engine self-test plus real-time microphone, output and DSP telemetry
- Runtime rebuild and rollback when a new audio configuration fails
- Monitor routing for local listening; headphones are recommended to prevent feedback

### Local and URL media import

- Import local media files through the native control panel
- Import supported online media through a URL
- Background conversion keeps the main interface responsive
- Imported files can be placed directly in the portable `sounds` library
- Bundled standalone media tools:
  - yt-dlp
  - Deno
  - FFmpeg
  - ffprobe
- Tool versions are pinned and verified with SHA-256
- Required third-party license files are included in the portable package
- Media tools are self-contained; users do not need to install them separately

### Built-in WAV editor

- Embedded in the main application window
- Asynchronous WAV loading and saving
- Cancellable long-running load and save operations
- Buffered waveform rendering through `AudioWaveformCache`
- Playback, pause, seek, zoom, fit-to-window, and monitor preview
- Precise selection and trimming
- `I` sets the selection start at the playhead
- `O` sets the selection end at the playhead
- Cut, copy, and paste
- Undo and redo history
- Gain adjustment
- Peak normalization
- Fade-in and fade-out
- Stereo-to-mono conversion
- Trim leading and trailing silence
- Save, Save As, and overwrite workflows
- Inline validation for recoverable input errors
- Responsive toolbar layout across supported DPI scales

### Native control panel and reliability

- Single-window native Win32 interface
- Home, Settings, and Hotkeys tabs
- Embedded audio editor instead of a separate popup window
- Persistent light and dark themes
- Per-monitor DPI scaling
- Device selectors, hotkey capture, and sound-binding editor
- Live microphone and output meters
- Keyboard navigation
- Safe `Save and apply` workflow
- Live config reload with rollback on failure
- Portable paths and Unicode file-name support
- UTF-8 console output
- Tray menu and single-instance protection
- Optional current-user Windows startup registration
- Console-free normal startup with an on-demand diagnostic console
- Persistent `latest.log` and `previous.log` session logs
- Manual and optional startup update checks through GitHub Releases
- The updater never downloads or replaces the application automatically

### Portable release integrity

- Native x64 Windows GUI executable
- Portable ZIP generated through CMake
- SHA-256 checksum generated for the final archive
- Package allowlist verification
- Safe default-config verification
- Bundled media-tool checksum and license verification
- Rejection of hidden files, symbolic links, build artifacts, and debug residue
- PE verification for x64, PE32+, and Windows GUI subsystem
- Fresh build of the complete test suite before release validation

## Quick start

1. Download the latest portable ZIP from [GitHub Releases](https://github.com/cank4fun/SoundboardFasaFiso/releases).
2. Compare the ZIP with the attached `.sha256` file.
3. Extract the complete archive to a writable folder.
4. Run `SoundBoardFasaFiso.exe`.
5. Select the main, monitor, and optional microphone devices.
6. Add sound bindings in the **Hotkeys** tab.
7. Click **Save and apply**.

The packaged default uses the current Windows default output and keeps monitor routing disabled. Copying a file into `sounds` does not assign a hotkey automatically.

The control panel remains available even if no sound binding is active or a previously configured file is missing, so the portable folder can be repaired without editing `config.txt` manually.

## Recommended routing with VB-CABLE

A typical voice-chat or streaming setup is:

```text
Soundboard main output  -> CABLE Input
Soundboard monitor      -> Headphones
Voice-chat microphone   -> CABLE Output
```

The physical microphone can also be mixed into the main route, allowing the virtual endpoint to carry both microphone audio and soundboard playback.

VB-CABLE is not bundled with SoundBoardFasaFiso.

## Importing media

Use the control panel to import either:

- A local media file
- A supported media URL

The import service uses the verified tools included under `media-tools`. Imported audio is converted into a soundboard-compatible file and placed in the portable sound library.

The importer does not require a system-wide FFmpeg, yt-dlp, Deno, or Python installation.

## Audio editor

Open a WAV file from the control panel to use the embedded editor.

Core workflow:

1. Load or import a sound.
2. Play and seek through the waveform.
3. Zoom or fit the full file.
4. Drag to create a selection, or use `I` and `O` at the playhead.
5. Trim, cut, copy, paste, normalize, fade, change gain, convert to mono, or remove silence.
6. Preview through the configured monitor device.
7. Save asynchronously without blocking the main UI.

The editor preserves undo and redo history for document edits. Recoverable input errors are shown inline instead of opening unnecessary modal dialogs.

## Portable folder layout

```text
SoundBoardFasaFiso/
├── SoundBoardFasaFiso.exe
├── config.txt
├── README.txt
├── LICENSE
├── THIRD_PARTY_NOTICES.txt
├── media-tools/          # Verified import tools and their licenses
├── sounds/               # Sound library and neutral examples
└── logs/                 # Created automatically after launch
```

Configuration, logs, imported media, and edited sounds remain inside the portable folder.

## Configuration

Most settings can be edited from the control panel. `config.txt` remains available for manual and automated workflows.

```ini
# LANGUAGE AND APPEARANCE
language=tr
theme=dark

# AUDIO OUTPUTS
output=default
output_volume=1.00

monitor=none
monitor_volume=0.30

# MICROPHONE MIXER
microphone_enabled=false
microphone=default
microphone_volume=1.00
microphone_to_output=true
microphone_to_monitor=false

# LOW-LATENCY AUDIO
audio_sample_rate=48000
audio_buffer_ms=5

# APPLICATION
start_with_windows=false
show_console_on_start=false
check_updates_on_start=true

# CONTROLS
stop=F11
output_mute=CTRL+SHIFT+F9
monitor_mute=CTRL+SHIFT+F10
reload=CTRL+SHIFT+F11
exit=CTRL+SHIFT+F12

# SOUNDS
F1=example.wav|volume=0.80|mode=restart
F2=example.mp3|volume=1.00|mode=toggle
F3=example.flac|volume=0.60|mode=loop
F4=example.wav|volume=1.00|mode=overlap
```

`language=tr` selects Turkish runtime messages and `language=en` selects English runtime messages.

`theme=dark` and `theme=light` select the persistent interface theme.

`default` selects the current Windows default playback or capture device. `none` disables the monitor route. Device settings also support unique partial-name matches.

Sound paths are relative to `sounds`. Subfolders are supported; absolute paths and `..` traversal are rejected.

### Playback modes

| Mode | Behavior |
|---|---|
| `restart` | Restarts the sound from the beginning on every press. |
| `overlap` | Starts another voice without stopping the previous one, up to eight voices. |
| `toggle` | Starts on the first press and stops on the next. |
| `loop` | Loops until the same hotkey is pressed again. |

Defaults when options are omitted:

```text
volume=1.00
mode=restart
```

### Hotkeys

Supported modifiers are `CTRL`, `SHIFT`, and `ALT`. Function keys `F1` through `F24` and `NUMPAD0` through `NUMPAD9` are supported.

```ini
CTRL+F2=example.wav
SHIFT+F3=example.mp3
CTRL+ALT+F4=example.flac
NUMPAD1=example.wav
```

Windows rejects a global hotkey already owned by another application. SoundBoardFasaFiso reports the conflict and keeps the last working configuration.

### Control-panel shortcuts

- `Ctrl+1`, `Ctrl+2`, `Ctrl+3` switch tabs
- `Ctrl+S` saves and applies pending changes
- `Esc` cancels hotkey capture

### Audio-editor shortcuts

- `I` sets the selection start at the current playhead
- `O` sets the selection end at the current playhead

## Startup, tray, and diagnostics

`start_with_windows=true` registers the current executable for the current user without requiring administrator permission. Moving the portable folder and launching it again refreshes the registered path.

The application normally starts without a console window. The diagnostic console can be opened from the control panel or tray menu.

`check_updates_on_start=true` checks the latest stable GitHub Release in the background. The updater only offers to open the official release page; it does not download, replace, or execute an update.

Every successful launch writes `logs/latest.log`. The previous session is retained as `logs/previous.log`.

Right-click the tray icon to:

- Open the control panel
- Reload the configuration
- Stop all sounds
- Mute or unmute the main output
- Mute or unmute the monitor output
- Show the diagnostic console
- Exit the application

Double-click the tray icon to reopen the control panel.

## Device recovery and latency

If a configured device disappears, SoundBoardFasaFiso remains open and periodically attempts to rebuild the audio runtime. Loaded sounds are restored after the device becomes available.

`audio_sample_rate=0` uses the device's native rate. `audio_buffer_ms=0` leaves buffer selection to Windows and miniaudio. Increase the buffer target if a driver produces crackling or dropouts.

## Windows security notice

Official GitHub release artifacts are currently unsigned. Microsoft Defender SmartScreen may show an unknown-publisher warning, and Windows 11 Smart App Control may block a new unsigned executable.

Download the application only from this repository and compare the ZIP with its attached checksum:

```powershell
Get-FileHash .\SoundBoardFasaFiso-v2.3.0-windows-x64-portable.zip -Algorithm SHA256
```

A matching checksum confirms that the archive matches the published release asset. It does not replace code signing. Do not permanently weaken Windows security to run the application.

See [CODE_SIGNING_POLICY.md](CODE_SIGNING_POLICY.md) for the project policy.

## Building from source

### Requirements

- Windows 10 or Windows 11
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.25 or newer
- Ninja
- vcpkg
- Git

### Development build

Open the repository folder in Visual Studio, or configure a Ninja build from an **x64 Native Tools Command Prompt for VS 2022**.

The project uses C++23 and treats supported warning configurations as errors.

### Verified Release build

Set `VCPKG_ROOT` to your vcpkg installation:

```cmd
set "VCPKG_ROOT=C:\path\to\vcpkg"

cmake -S . -B out/build/x64-Release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DVCPKG_MANIFEST_FEATURES=webrtc-aec3 ^
  -DSOUNDBOARD_ENABLE_WEBRTC_AEC3=ON ^
  -DSOUNDBOARD_BUILD_WEBRTC_AEC3_TESTS=ON ^
  -DSOUNDBOARD_WARNINGS_AS_ERRORS=ON
```

Build the application and fresh test binaries:

```cmd
cmake --build out/build/x64-Release ^
  --target SoundBoardFasaFiso SoundBoardFasaFisoTests --parallel
```

Run the complete test suite:

```cmd
ctest --test-dir out/build/x64-Release --output-on-failure
```

Create and verify the portable release:

```cmd
cmake --build out/build/x64-Release ^
  --target VerifyPortableRelease --parallel
```

The verified files are created under:

```text
out/build/x64-Release/
├── SoundBoardFasaFiso-v2.3.0-windows-x64-portable.zip
└── SoundBoardFasaFiso-v2.3.0-windows-x64-portable.zip.sha256
```

## Repository layout

```text
.github/         GitHub Actions workflows
assets/          Repository artwork
cmake/           Build and portable-verification scripts
docs/            Architecture, build, development, and release documentation
include/         Project headers
resources/       Windows icon and resource templates
sounds/          Neutral example sounds
src/             Application sources
tests/           Automated tests
third_party/     Vendored third-party source and notices
tools/           Development and import helper scripts
config.txt       Default runtime configuration
CMakeLists.txt   Build, test, and packaging rules
vcpkg.json       Manifest dependencies and optional features
```

## Third-party software

The repository vendors or packages third-party components including miniaudio and RNNoise. Optional WebRTC AEC3 support is resolved through the vcpkg manifest feature.

Official portable releases also include pinned standalone builds of yt-dlp, Deno, FFmpeg, and ffprobe together with the required license material. See [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) and the license files inside `media-tools`.

VB-CABLE is optional and is not bundled.

## Changelog

See [CHANGELOG.md](CHANGELOG.md) and [docs/releases/v2.3.0.md](docs/releases/v2.3.0.md).

## License

Released under the [MIT License](LICENSE).

## Code signing policy

Official GitHub release artifacts are currently unsigned.

Build provenance, privacy rules, checksum verification, and requirements for any future signed release are documented in [CODE_SIGNING_POLICY.md](CODE_SIGNING_POLICY.md).
