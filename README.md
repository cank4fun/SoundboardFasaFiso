<p align="center">
  <img src="assets/fasafisotray.png" alt="SoundBoardFasaFiso logo" width="180">
</p>

<h1 align="center">SoundBoardFasaFiso</h1>

<p align="center">
  A lightweight, portable Windows soundboard built with C++23, miniaudio and native Win32 APIs.
</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D4">
  <img alt="Language" src="https://img.shields.io/badge/C%2B%2B-23-00599C">
  <img alt="Audio" src="https://img.shields.io/badge/audio-WASAPI-6A5ACD">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-green">
</p>

**Development version:** `2.1.0-alpha.1` adds the opt-in microphone-processing pipeline, RNNoise, AGC and optional WebRTC AEC3 integration while the user-mode playback engine is expanded.

**Latest stable release:** `2.0.0` is the first stable v2 release. It includes the native control panel, independent main and monitor routing, microphone mixing, resilient live configuration, device recovery and portable Windows packaging.

SoundBoardFasaFiso sends each sound to two independent Windows playback devices. A common setup routes the main output to **VB-CABLE** for voice chat or streaming and sends the monitor output to headphones.

There is no installer, database or GUI framework. Audio settings, control hotkeys, and sound bindings can be edited from the native control panel; `config.txt` remains available for manual workflows.

## Features

- Global hotkeys through Win32 `RegisterHotKey`
- Separate main and monitor outputs with independent volume and mute controls
- Physical microphone capture with device selection, gain and output/monitor routing
- Local microphone processing with HPF, RNNoise, AGC, compression, limiting and optional WebRTC AEC3
- Persistent session logs with one-click log-folder access
- Manual and optional startup update checks through the public GitHub Releases API
- Optional Windows startup registration and console-free launch
- WAV, MP3 and FLAC playback
- Audio preloaded into RAM
- Per-sound volume and playback mode
- Active Playbacks tab with live progress, pause/resume, seek, per-session stop and temporary session volume
- `restart`, `overlap`, `toggle`, `loop` and `ignore` retrigger modes
- Bounded eight-voice overlap pool
- Live config reload with rollback on failure and atomic saves that retain `config.txt.bak`
- Automatic recovery after audio-device disconnects
- WASAPI with configurable sample rate and buffer target
- Turkish and English runtime messages selected from `config.txt`
- Modern native Win32 control panel with persistent light/dark themes, live signal meters, device selectors, hotkey capture and sound-binding editing
- UTF-8 console and Unicode file-path support
- Tray menu and single-instance protection
- Portable Release ZIP generation with CMake

## Quick start

1. Download and extract the latest portable ZIP.
2. Put audio files in the `sounds` folder.
3. Run `SoundBoardFasaFiso.exe`; the control panel opens automatically.
4. Open the **Hotkeys** tab and create a binding for each sound. Copying a file into `sounds` does not assign a hotkey automatically.
5. Configure devices and control hotkeys, then click **Save and apply**. Invalid changes are rejected and the previous runtime is restored.

The control panel remains available even when no sound binding is active or previously configured files are missing, so a fresh portable folder can be repaired without editing `config.txt` manually.

The packaged default starts on the Windows default output with monitor routing disabled, so VB-CABLE is optional.

### Windows security notice

GitHub portable builds are currently unsigned. Microsoft Defender SmartScreen may show an unknown-publisher warning, and Windows 11 Smart App Control can block a new unsigned executable without offering a per-application exception. This also applies to locally compiled unsigned Release builds.

Download releases only from this repository and compare the ZIP against the attached `.sha256` file:

```powershell
Get-FileHash .\SoundBoardFasaFiso-v*-windows-x64-portable.zip -Algorithm SHA256
```

A matching checksum confirms that the archive is identical to the release asset; it is not a substitute for code signing. Do not weaken system security solely to run the application. Systems that enforce trusted signing require a signed build or an application-control policy that explicitly permits the executable.

```text
SoundBoardFasaFiso/
├── SoundBoardFasaFiso.exe
├── config.txt
├── README.txt
├── LICENSE
├── THIRD_PARTY_NOTICES.txt
├── WEBRTC_THIRD_PARTY_NOTICES.txt  # AEC-enabled official builds
├── logs/          # created automatically
└── sounds/
```

## Configuration

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

# MICROPHONE PROCESSING
microphone_processing_enabled=false
microphone_processing_preset=natural
microphone_echo_cancellation_enabled=false
microphone_noise_suppression_enabled=false
microphone_agc_enabled=false

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
F5=example.wav|volume=1.00|mode=ignore
```

`language=tr` selects Turkish messages and `language=en` selects English messages. `theme=dark` and `theme=light` select the persistent interface theme. Both settings are applied during startup and live reload.

`default` selects the current Windows default playback or capture device. `none` disables the monitor output. Device names also support unique partial matches.

When `microphone_enabled=true`, the selected physical microphone is captured and mixed into every enabled route. `microphone_to_output=true` sends it to the main output; `microphone_to_monitor=true` also lets the user hear it through the monitor device. Enabling microphone monitoring can create feedback when speakers are used, so headphones are recommended.

Microphone processing is opt-in. AEC-enabled builds can remove soundboard audio that returns through the physical monitor and microphone by using WebRTC AEC3. The render reference excludes microphone monitoring and the virtual main-output route. Builds made without WebRTC preserve the setting but show echo cancellation as unavailable.

Sound paths are relative to the `sounds` folder. Subfolders are supported, but absolute paths and `..` traversal are rejected.

### Playback modes

| Mode | Behavior |
|---|---|
| `restart` | Restarts the sound from the beginning on every press. |
| `overlap` | Starts another voice without stopping the previous one. Uses up to eight voices. |
| `toggle` | Starts on the first press and stops on the next. |
| `loop` | Loops until the same hotkey is pressed again. |
| `ignore` | Starts only when inactive; repeated presses leave the current session untouched. |

Defaults when options are omitted:

```text
volume=1.00
mode=restart
```

### Hotkeys

Supported modifiers are `CTRL`, `SHIFT` and `ALT`. Function keys `F1` through `F24` and `NUMPAD0` through `NUMPAD9` are supported.

```ini
CTRL+F2=example.wav
SHIFT+F3=example.mp3
CTRL+ALT+F4=example.flac
NUMPAD1=example.wav
```

Windows rejects a hotkey already owned by another application. SoundBoardFasaFiso reports the conflict and keeps the last working configuration during a failed reload.

## Control panel

The native Win32 control panel uses a single-window, tabbed layout with light/dark themes, live microphone and output meters, keyboard navigation, per-monitor DPI scaling, device selectors, hotkey capture and sound-binding editing. It does not require Qt, .NET or another GUI runtime.

`Save and apply` writes a pending configuration, validates it, rebuilds the complete playback/capture runtime and only replaces the active config after every device and hotkey succeeds. A failure restores the previous working runtime. Closing the control-panel window only hides it, so the soundboard continues running in the tray.

Keyboard shortcuts inside the panel:

- `Ctrl+1`, `Ctrl+2`, `Ctrl+3` switch tabs
- `Ctrl+S` saves and applies pending changes
- `Esc` cancels hotkey capture

## Startup and diagnostics

`start_with_windows=true` registers the current executable under the current user's Windows startup key, so no administrator permission is required. Moving the portable folder and launching the application again refreshes the registered path.

The application starts without a console window. The diagnostic console remains available on demand from the control panel or tray menu. The legacy `show_console_on_start` key is retained for config compatibility and is normalized to `false` when settings are saved.

`check_updates_on_start=true` performs a background check against the latest published stable GitHub Release. The control panel also provides a manual check. The updater never downloads, replaces, or executes files automatically; it only offers to open the official Release page.

Every successful launch writes `logs/latest.log`; the previous session is kept as `logs/previous.log`. The control panel can open the logs folder directly.

## Tray menu

Right-click the tray icon to:

- Open the control panel
- Reload the config
- Stop all sounds
- Mute or unmute either output
- Show or hide the console
- Exit

Double-click the tray icon to reopen the control panel.

## Device recovery and latency

If a configured device disappears, the application remains open and periodically rebuilds the audio engine. Loaded sounds are restored after the device becomes available.

`audio_sample_rate=0` uses the device's native rate. `audio_buffer_ms=0` leaves buffer selection to Windows/miniaudio. Very small buffer targets can crackle on some drivers; increase the value when needed.

The console prints the actual sample rate, period count and effective buffer reported by the device.

## Building from source

### Requirements

- Windows 10 or Windows 11
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.25 or newer
- Ninja

### Visual Studio debug build

Open the repository folder in Visual Studio and press `CTRL+SHIFT+B`.

The build copies the default `config.txt` only when the build directory does not already contain one, so local test settings are not overwritten. Delete the build-directory copy to restore the packaged defaults.

The default CMake output is normally:

```text
out/build/x64-Debug/SoundBoardFasaFiso.exe
```

### Portable Release build

The official v2.1 release path enables WebRTC AEC3 through the pinned vcpkg manifest. Follow [Building with WebRTC AEC3](docs/BUILDING_WITH_WEBRTC_AEC3.md) for the one-time vcpkg setup and complete configure command.

A smaller dependency-free build remains available when echo cancellation is not needed:

```cmd
cmake -S . -B out/build/x64-Release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DSOUNDBOARD_ENABLE_WEBRTC_AEC3=OFF
cmake --build out/build/x64-Release --target PortableRelease
```

The distributable archive is created at:

```text
out/build/x64-Release/SoundBoardFasaFiso-v2.1.0-alpha.1-windows-x64-portable.zip
```

## Repository layout

```text
assets/          Repository artwork
cmake/           Small build helper scripts
docs/            Architecture, build and release notes
include/         Project headers and generated-file templates
resources/       Windows icon and resource templates
sounds/          Neutral example tones
src/             Application sources
third_party/     Vendored third-party code
config.txt       Default runtime configuration
CMakeLists.txt   Build and packaging rules
THIRD_PARTY_NOTICES.txt  Vendored dependency notices
vcpkg.json       Pinned optional WebRTC AEC3 dependency manifest
```

## Third-party software

This repository vendors [miniaudio](https://github.com/mackron/miniaudio) and RNNoise. Their notices are summarized in `THIRD_PARTY_NOTICES.txt`. AEC-enabled portable builds additionally include generated `WEBRTC_THIRD_PARTY_NOTICES.txt` covering WebRTC and the transitive static dependencies installed by the pinned vcpkg manifest.

VB-CABLE is optional and is not bundled with this project. Local playback works without it. Presenting the mixed signal to other applications as a microphone requires a virtual audio endpoint; the v2 architecture keeps this as a separate bridge layer. See [docs/V2_ARCHITECTURE.md](docs/V2_ARCHITECTURE.md).

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for release notes.

## License

Released under the [MIT License](LICENSE).

## Code signing policy

Official GitHub release artifacts are currently unsigned. The project does not currently use a SignPath Foundation certificate or another public code-signing certificate. Signing is not a release blocker; official artifacts are built by GitHub Actions and published with SHA-256 checksums.

Build provenance, project roles, privacy rules, checksum verification and the requirements for any future signed release are documented in [CODE_SIGNING_POLICY.md](CODE_SIGNING_POLICY.md).
