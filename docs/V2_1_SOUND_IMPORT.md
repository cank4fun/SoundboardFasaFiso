# v2.1 Sound Import Pipeline

## Local drag and drop

The control panel accepts files and folders dropped anywhere on the window.
Supported audio files are imported through one shared pipeline used by drag and
drop and the multi-select file picker.

- WAV, MP3 and FLAC files are accepted directly because these are the formats
  decoded by the current miniaudio build.
- External files are copied into `sounds/Imported`.
- Files already below `sounds` stay in place and are referenced by their
  portable relative path.
- Name collisions use `_2`, `_3`, and later suffixes without overwriting an
  existing file.
- Folder drops are recursive, skip inaccessible entries and stop at a bounded
  4096-file scan limit.
- Duplicate input paths are imported once.
- Copies are written to a sibling temporary file and renamed only after the
  copy succeeds, so an interrupted import does not leave a partial playable
  file under its final name.

After a successful import, the Hotkeys page opens and the first imported path
is placed into the binding editor. Importing a file does not invent or activate
a global hotkey; the user still chooses the hotkey and selects **Save and
apply**.

## Planned URL import

Internet import will build on the same pipeline rather than adding a second
library path. The planned flow runs `yt-dlp` and FFmpeg through `CreateProcessW`,
keeps command arguments separate from shell parsing, downloads into a temporary
workspace, converts the result to a directly supported audio format, and then
passes the finished file to `SoundImporter`.

The URL workflow will include tool discovery, progress and cancellation,
playlist opt-in, collision-safe naming, cleanup of partial downloads and clear
error output. `yt-dlp.exe` and `ffmpeg.exe` will remain optional external tools;
they are not bundled into the portable application without their own packaging
and license review.
