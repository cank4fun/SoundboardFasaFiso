# Creating a release

The public version is defined by `SOUNDBOARD_VERSION` in `CMakeLists.txt`. The
Git tag must match it exactly, including prerelease suffixes such as `-rc.1`.
The `version-string` in `vcpkg.json` must carry the same value; CMake rejects a
configuration when those two release metadata sources drift. Existing release
tags are immutable and must never be moved or reused.

Official builds are currently unsigned. Code signing is not a release blocker;
every release must preserve the GitHub Actions build record, immutable tag and
published SHA-256 checksum. See `CODE_SIGNING_POLICY.md`.

## Release branches

Prepare each release on a dedicated branch created from the intended base. Keep
the branch limited to the version being released and review its complete diff
before opening a pull request.

For example:

```powershell
git status --short --branch
git diff main..HEAD --stat
git log --oneline main..HEAD
```

The release diff should contain only intentional feature, fix, version,
documentation and workflow changes for that release. Existing release tags and
published assets must not be rewritten.

## Pre-release checks

Official v2.1 and later builds include WebRTC AEC3 and use the pinned vcpkg manifest.
Prepare the standalone vcpkg checkout exactly as documented in
`docs/BUILDING_WITH_WEBRTC_AEC3.md`, then run from **x64 Native Tools Command
Prompt for VS 2022**:

```cmd
set "VCPKG_ROOT=C:\path\to\vcpkg"
rmdir /S /Q out\build\x64-Release 2>nul

cmake -S . -B out/build/x64-Release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DVCPKG_MANIFEST_FEATURES=webrtc-aec3 ^
  -DSOUNDBOARD_ENABLE_WEBRTC_AEC3=ON ^
  -DSOUNDBOARD_BUILD_WEBRTC_AEC3_TESTS=ON ^
  -DSOUNDBOARD_WARNINGS_AS_ERRORS=ON

cmake --build out/build/x64-Release ^
  --target SoundBoardFasaFiso SoundBoardFasaFisoTests --parallel
ctest --test-dir out/build/x64-Release --output-on-failure
cmake --build out/build/x64-Release --target VerifyPortableRelease --parallel
```

`ctest` runs existing test executables but does not build them. Always build the
`SoundBoardFasaFisoTests` aggregate target before running the suite so no stale
test binary can pass accidentally.

Confirm that:

- the Release build completes with warnings treated as errors and every
  ordinary/AEC test passes;
- the executable version and portable filename contain the intended version;
- the portable ZIP opens from a newly extracted folder on Windows 10/11;
- no developer config, logs, build files or personal paths are present;
- audio playback, microphone routing, AEC, RNNoise, AGC, tray behavior,
  hotkeys, config rollback and device recovery work;
- Voice Effects presets, independent pitch/formant controls, Vocal Weight,
  dry/wet, bypass, user presets and preset hotkeys work without sustained
  deadline misses or dropped frames;
- the inline Voice Engine self-test reports `7/7` before and after the physical
  listening session;
- parametric EQ, de-esser, gate/expander and compressor work independently,
  the modular rack can be reordered live without a click/pop, and bypass returns
  to the clean aligned signal;
- `.sbffvoice` export/import survives an application restart, restores every
  parameter and rack order, and rejects a deliberately corrupted test copy;
- bypass, core-only, each built-in voice preset and the complete rack are
  recorded with the same microphone position and gain for physical comparison;
- AEC safely bypasses when the physical monitor reference is unavailable;
- the update check opens only the official GitHub Release page;
- the unsigned-build and Smart App Control limitation is present in both
  readmes;
- the package includes `LICENSE`, `THIRD_PARTY_NOTICES.txt` and generated
  `WEBRTC_THIRD_PARTY_NOTICES.txt`; and
- the generated checksum matches the exact portable ZIP attached to the
  release.

`VerifyPortableRelease` creates a fresh extraction under the build directory,
checks the package allowlist, safe config defaults, example sounds, bundled
media-tool hashes/licenses and build/runtime residue, then writes the matching
`.zip.sha256` file. Do not publish a package if this target fails.

The package name is versioned:

```text
SoundBoardFasaFiso-v<version>-windows-x64-portable.zip
```

## Merge the release

Open a pull request from the release branch to `main`. Wait for the Windows
workflow to pass, review the final diff and merge without changing the prepared
version.

After the merge:

```powershell
git switch main
git pull --ff-only
git status --short --branch
git log -3 --oneline --decorate
```

Build or inspect the workflow artifact from the exact merged commit before
tagging.

## Publish the release

Create an annotated tag from the verified `main` commit. Replace `<version>`
with the exact value from `SOUNDBOARD_VERSION`:

```powershell
git tag -a v<version> -m "SoundBoardFasaFiso v<version>"
git show --stat v<version>
git push origin v<version>
```

The **Windows Build and Release** workflow verifies that the tag matches the
project version, checks out the pinned vcpkg baseline, builds WebRTC AEC3 with
MSVC and warnings as errors, runs the complete test suite, validates the
portable package, creates a SHA-256 checksum, uploads both files as workflow
artifacts and attaches them to the tagged GitHub Release.

If `docs/releases/v<version>.md` exists, the workflow uses it as the release
description; otherwise it generates release notes. Tags containing a hyphen,
such as `v2.1.0-alpha.1` or `v2.1.0-rc.1`, are published as prereleases.

After publication, verify:

- stable tags are not marked as prereleases and prerelease tags are;
- the ZIP is named `SoundBoardFasaFiso-v<version>-windows-x64-portable.zip`;
- the matching `.sha256` file is attached and matches a fresh download;
- the release description contains the unsigned-build warning;
- `THIRD_PARTY_NOTICES.txt` and `WEBRTC_THIRD_PARTY_NOTICES.txt` are present;
  and
- the update checker treats only the latest non-prerelease release as stable.

If a tagged workflow fails because of a transient service error, rerun the same
workflow. Do not move the tag. If the tagged source itself requires a
correction, increment the version and publish a new tag instead of rewriting an
existing release.
