# Code signing and release integrity policy

## Current signing status

Official SoundBoardFasaFiso GitHub release artifacts are currently **unsigned**. The project does not currently use a SignPath Foundation certificate or another public code-signing certificate. No release should claim a publisher identity or certificate that was not actually applied to the distributed executable.

Unsigned Windows builds can trigger Microsoft Defender SmartScreen warnings, and Windows 11 Smart App Control can block them. A SHA-256 checksum verifies file identity but does not provide publisher authentication.

## Project roles

- Committer and reviewer: [cank4fun](https://github.com/cank4fun)
- Release approver: [cank4fun](https://github.com/cank4fun)

Changes submitted by contributors who do not have direct repository write access must be reviewed before they are merged.

## Official release provenance

Official release binaries are built from this public source repository by GitHub Actions on GitHub-hosted Windows runners. A release is considered official only when all of the following are true:

- the source commit is reachable from the public repository
- the Git tag exactly matches `SOUNDBOARD_VERSION` in `CMakeLists.txt`
- the portable ZIP and its `.sha256` file were produced by the official Windows workflow
- the assets are attached to the GitHub Release for that tag

Users should download artifacts only from the repository's GitHub Releases page and compare the ZIP with the attached checksum.

```powershell
Get-FileHash .\SoundBoardFasaFiso-v*-windows-x64-portable.zip -Algorithm SHA256
```

A matching hash confirms that the downloaded archive is identical to the published release asset. It does not replace Authenticode signing.

## Privacy policy

SoundBoardFasaFiso does not transfer user audio, microphone content, configuration, logs, sound files or other personal information to external systems.

The application contacts GitHub only when the user enables the optional startup update check or manually requests an update check. The updater reads public release information and does not upload user data.

The application does not automatically download, replace or execute updates.

## Future signed releases

Code signing may be added later when an appropriate certificate or signing service is available. Any future signed release must:

- be built from a public commit and immutable release tag
- be produced by, or traceable to, the official repository workflow
- require explicit release approval
- document the certificate subject and verification procedure
- keep private signing keys outside this repository and maintainer source trees

Until those conditions are met, releases remain explicitly unsigned.
