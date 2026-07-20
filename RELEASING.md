# Creating a release

The public version is defined by `SOUNDBOARD_VERSION` in `CMakeLists.txt`. The tag must match it exactly, including prerelease suffixes such as `-rc.1`.

## Pre-release checks

From **x64 Native Tools Command Prompt for VS 2022**:

```cmd
cmake -S . -B out/build/x64-Release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DSOUNDBOARD_WARNINGS_AS_ERRORS=ON

cmake --build out/build/x64-Release --target PortableRelease --parallel
```

Confirm that:

- the Release build has no warnings
- the portable ZIP opens on a clean Windows 10/11 machine
- no developer config, logs, build files or personal paths are present
- audio playback, microphone routing, tray behavior, hotkeys and device recovery work
- the update check opens only the official GitHub Release page

The package name is versioned:

```text
SoundBoardFasaFiso-v<version>-windows-x64-portable.zip
```

## Publish a release candidate

After committing the release-candidate version:

```powershell
git status
git push
git tag v2.0.0-rc.1
git push origin v2.0.0-rc.1
```

Tags containing a prerelease suffix are published as GitHub prereleases.

## Publish the stable release

Change `SOUNDBOARD_VERSION` to `2.0.0`, update the changelog, commit, and then:

```powershell
git status
git push
git tag v2.0.0
git push origin v2.0.0
```

The **Windows Build and Release** workflow builds with MSVC and warnings as errors, validates the portable package, produces a SHA-256 checksum, uploads both files as workflow artifacts, and attaches both files to the tagged GitHub Release.

Never move or reuse an existing release tag. Fix the issue, increment the version, and create a new tag.
