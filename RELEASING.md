# Creating a release

The public version is defined by `SOUNDBOARD_VERSION` in `CMakeLists.txt`. The
tag must match it exactly, including prerelease suffixes such as `-rc.1`.

Official builds are currently unsigned. Code signing is not a release blocker;
every release must instead preserve the GitHub Actions build record, immutable
tag and published SHA-256 checksum. See `CODE_SIGNING_POLICY.md`.

## Pre-release checks

Official v2.1 builds include WebRTC AEC3 and use the pinned vcpkg manifest.
Prepare the standalone vcpkg checkout exactly as documented in
`docs/BUILDING_WITH_WEBRTC_AEC3.md`, then run from **x64 Native Tools Command
Prompt for VS 2022**:

```cmd
cmake -S . -B out/build/x64-Release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:\Dev\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DVCPKG_MANIFEST_FEATURES=webrtc-aec3 ^
  -DSOUNDBOARD_ENABLE_WEBRTC_AEC3=ON ^
  -DSOUNDBOARD_BUILD_WEBRTC_AEC3_TESTS=ON ^
  -DSOUNDBOARD_WARNINGS_AS_ERRORS=ON

cmake --build out/build/x64-Release --parallel
ctest --test-dir out/build/x64-Release --output-on-failure
cmake --build out/build/x64-Release --target PortableRelease --parallel
```

Confirm that:

- the Release build has no warnings and every ordinary/AEC test passes;
- the portable ZIP opens on a clean Windows 10/11 machine;
- no developer config, logs, build files or personal paths are present;
- audio playback, microphone routing, AEC, RNNoise, AGC, tray behavior,
  hotkeys and device recovery work;
- AEC safely bypasses when the physical monitor reference is unavailable;
- the update check opens only the official GitHub Release page;
- the unsigned-build and Smart App Control limitation is present in both
  readmes;
- `THIRD_PARTY_NOTICES.txt` and generated
  `WEBRTC_THIRD_PARTY_NOTICES.txt` are present;
- the generated checksum matches the exact portable ZIP attached to the
  release.

The package name is versioned:

```text
SoundBoardFasaFiso-v<version>-windows-x64-portable.zip
```

## Publish a release candidate

After committing the release-candidate version:

```powershell
git status
git push
git tag v2.1.0-rc.1
git push origin v2.1.0-rc.1
```

Tags containing a prerelease suffix are published as GitHub prereleases.

## Publish a stable release

Change `SOUNDBOARD_VERSION` to the intended stable version, update the
changelog, commit, and then create the matching immutable tag.

The **Windows Build and Release** workflow checks out the pinned vcpkg
baseline, builds WebRTC AEC3 with MSVC and warnings as errors, runs the complete
test suite, validates the portable package, creates a SHA-256 checksum, uploads
both files as workflow artifacts, and attaches both files to a tagged GitHub
Release.

Never move or reuse an existing release tag. Fix the issue, increment the
version, and create a new tag.
