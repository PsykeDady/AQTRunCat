# Third-Party Notices

AQTRunCat includes material from the following third-party projects.

## RunCat Neo runner frames

- Source: https://github.com/runcat-dev/RunCatNeo
- Revision: `b3b1543049ea0a051ecb78654a45f144724ea737` (2026-08-06)
- Original location: `LocalPackage/Sources/UserInterface/Resources/Media.xcassets/Runners/`
- Copyright: Copyright 2026 Kyome22 (Takuto Nakamura)
- License: Apache License 2.0. The full text is in `third_party/runcatneo/LICENSE`.
- Files: `third_party/runcatneo/runners/{cat,coffee,engine}/*.png`
- Modifications: none. The files are byte-for-byte copies of the upstream PNGs.
- Use:
  - `cat` and `coffee` are compiled into the `aqtruncat` executable as bundled runners (`AQTRUNCAT_BUNDLED_RUNNERS`).
    Since F4.2 the same unmodified files are also installed as
    `share/aqtruncat/runners/<id>/*.png`, where the backend lists them for
    the KDE Plasma widget (and other frontends) to animate.
  - `engine` is used only by the M1.0 tray spike.

The runner metadata files in `data/runners/<id>/metadata.json` are AQTRunCat
files. They only describe the frames above: name, author and monochrome
rendering, taken from RunCat Neo's English runner names and its built-in
`isTemplate = true`.

The RunCat and RunCat Neo names, application icon and branding are not part
of this grant (Apache-2.0 §6) and are not used as AQTRunCat branding.
