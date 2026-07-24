# v2.1 Standalone Runtime Contract

SoundBoardFasaFiso is distributed as one self-contained folder. Users do not
need a Visual C++ redistributable, .NET, Python, CMake, vcpkg, yt-dlp, FFmpeg,
or administrator rights to run the official portable package.

## Storage modes

The official portable archive includes `portable.flag`. When the marker is
present, all mutable data remains inside the extracted folder:

- `config.txt` and `config.txt.bak`
- `logs/`
- `sounds/Imported/`
- `tools/`

Portable mode is intentionally strict. If the folder is read-only, incomplete,
or placed under a protected location such as Program Files, startup stops with
a clear error instead of silently writing files elsewhere. Moving the complete
folder to a writable user location preserves portability.

When `portable.flag` is absent, the executable uses
`%LOCALAPPDATA%\SoundBoardFasaFiso`. This fallback supports future installer or
single-EXE deployment experiments. Packaged `config.txt` and example sounds are
copied only when the user data does not already exist.

## Privilege model

The embedded application manifest requests `asInvoker`. The program does not
ask for elevation. Running it manually as administrator can prevent normal
Explorer windows from sending drag-and-drop messages across the Windows
integrity boundary, so the application detects elevation and warns the user.

## Runtime dependency policy

- The application and its C/C++ runtime use static linking.
- RNNoise and WebRTC AEC3 are built into the executable or static libraries.
- Media tools will be private files below `tools/`, never system installations.
- Official packaging must pin and verify every downloaded media-tool binary and
  ship the corresponding license notices.
- The app must remain usable for local playback when optional media-tool files
  are missing or damaged; only URL import and conversion are disabled.
