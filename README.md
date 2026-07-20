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

**v2 development status:** this branch is an alpha foundation and is not a stable replacement for v1.0.0 yet.

SoundBoardFasaFiso sends each sound to two independent Windows playback devices. A common setup routes the main output to **VB-CABLE** for voice chat or streaming and sends the monitor output to headphones.

There is no installer, database or GUI framework. Runtime behavior is controlled by `config.txt`, and changes can be reloaded without closing the program.

## Features

- Global hotkeys through Win32 `RegisterHotKey`
- Separate main and monitor outputs with independent volume and mute controls
- WAV, MP3 and FLAC playback
- Audio preloaded into RAM
- Per-sound volume and playback mode
- `restart`, `overlap`, `toggle` and `loop` modes
- Bounded eight-voice overlap pool
- Live config reload with rollback on failure
- Automatic recovery after audio-device disconnects
- WASAPI with configurable sample rate and buffer target
- Turkish and English runtime messages selected from `config.txt`
- UTF-8 console and Unicode file-path support
- Tray menu and single-instance protection
- Portable Release ZIP generation with CMake

## Quick start

1. Download and extract the latest portable ZIP.
2. Put audio files in the `sounds` folder.
3. Edit `config.txt`.
4. Run `SoundBoardFasaFiso.exe`.
5. Press `CTRL+SHIFT+F11` after changing the config.

```text
SoundBoardFasaFiso/
├── SoundBoardFasaFiso.exe
├── config.txt
├── README.txt
└── sounds/
```

## Configuration

```ini
# LANGUAGE: tr or en
language=tr

# AUDIO OUTPUTS
output=CABLE Input
output_volume=1.00

monitor=default
monitor_volume=0.30

# LOW-LATENCY AUDIO
audio_sample_rate=48000
audio_buffer_ms=5

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

`language=tr` selects Turkish messages and `language=en` selects English messages. The setting is applied during startup and live reload.

`default` selects the current Windows default playback device. `none` disables the monitor output. Device names also support unique partial matches.

Sound paths are relative to the `sounds` folder. Subfolders are supported, but absolute paths and `..` traversal are rejected.

### Playback modes

| Mode | Behavior |
|---|---|
| `restart` | Restarts the sound from the beginning on every press. |
| `overlap` | Starts another voice without stopping the previous one. Uses up to eight voices. |
| `toggle` | Starts on the first press and stops on the next. |
| `loop` | Loops until the same hotkey is pressed again. |

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

## Tray menu

Right-click the tray icon to:

- Reload the config
- Stop all sounds
- Mute or unmute either output
- Show or hide the console
- Exit

Double-click the tray icon to show or hide the console.

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

The default CMake output is normally:

```text
out/build/x64-Debug/SoundBoardFasaFiso.exe
```

### Portable Release build

Open **x64 Native Tools Command Prompt for VS 2022** in the repository root:

```cmd
cmake -S . -B out/build/x64-Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/x64-Release --target PortableRelease
```

The distributable archive is created at:

```text
out/build/x64-Release/SoundBoardFasaFiso-portable.zip
```

## Repository layout

```text
assets/          Repository artwork
include/         Project headers and generated-file templates
resources/       Windows icon and resource templates
sounds/          Neutral example tones
src/             Application sources
third_party/     Vendored third-party code
config.txt       Default runtime configuration
CMakeLists.txt   Build and packaging rules
```

## Third-party software

This repository vendors [miniaudio](https://github.com/mackron/miniaudio). Its license notice remains in the original header.

VB-CABLE is optional and is not bundled with this project. Local playback works without it. Presenting the mixed signal to other applications as a microphone requires a virtual audio endpoint; the v2 architecture keeps this as a separate bridge layer. See [docs/V2_ARCHITECTURE.md](docs/V2_ARCHITECTURE.md).

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for release notes.

## License

Released under the [MIT License](LICENSE).
