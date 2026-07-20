# Code signing policy

Free code signing provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

## Project roles

- Committer and reviewer: [cank4fun](https://github.com/cank4fun)
- Release signing approver: [cank4fun](https://github.com/cank4fun)

Changes submitted by contributors who do not have direct repository write access must be reviewed before they are merged.

Every release-signing request must be manually approved by the release signing approver.

## Privacy policy

SoundBoardFasaFiso does not transfer user audio, microphone content, configuration, logs, sound files or other personal information to external systems.

The application contacts GitHub only when the user enables the optional startup update check or manually requests an update check. The updater reads public release information and does not upload user data.

The application does not automatically download, replace or execute updates.

## Build and signing process

Official release binaries are built from this public source repository by GitHub Actions on GitHub-hosted Windows runners.

Release signing will be requested only for artifacts produced by the official repository workflow. Signed releases must correspond to a public source commit and release tag.

The signing certificate and private key are not stored in this repository or on maintainer computers.
