# Building, testing and releasing

## Build

qmake subdirs: `app` plus the first-party submodules `cpputils`, `cpp-template-utils`, `qtutils`, `thin_io` and
`github-releases-autoupdater` (static libs). `thin_io` supplies one thing: the filesystem entry identity behind
`sameDirectoryOnDisk`, which Qt does not expose. `github-releases-autoupdater` serves the update check
(`updates.md`) and is why the app links Qt Network. Compiler configuration lives in
`global.pri`, included by the app, the launcher, the tests and qtutils.

The `.sln`, `.vcxproj` and `Makefile*` files in the tree are qmake output (`qmake -tp vc -r` on Windows),
git-ignored and never edited.

`app/res` holds `theme.qrc` (the monochrome glyph SVGs the theme tints at load, and the window icon), the
platform icons and `Info.plist`.

## `gg` on PATH

- **Windows**: the `launcher` subproject builds a second `gg.exe` that starts the real one from the directory
  above it. Only the launcher's directory goes on PATH, so the Qt DLLs sitting beside the application are not
  offered to every process's DLL search. The installer adds that directory to the system PATH.
- **macOS**: File > Install 'gg' Command Line Tool (also offered on the welcome window) points `/usr/local/bin/gg` at
  the running bundle's executable (`commandlinetool_mac`). macOS asks for administrator credentials where that
  directory is not user-writable.
- **Linux**: nothing; the binary is `bin/<config>/gg`.

## Tests

`tests/tests.pro` is a Catch2 app built on its own, outside the subdirs project: it compiles the backend-
and UI-free sources (`unifieddiff`, `textdiff`, `movedblocks`) straight from `app/src` against QtCore, with
diffs captured from real changes as fixtures under `tests/fixtures/`.

`scripts/run_tests.bat` builds and runs it (`debug` as the argument for the debug configuration);
`scripts/debug_tests.bat` runs the built tests under cdb. Both resolve the Qt kit through `scripts/qt_kit.bat`:
`QT_ROOT_DIR` if set, else the git-ignored `scripts/local-env.bat`, else `C:\Qt\6.*`. On other platforms the
project is built by hand with qmake. CI runs the tests on Windows only.

## Version

`app/src/version.h` is the one place the version is written. `app.pro` parses it for the exe metadata and the
macOS plist; the installer reads it back from the built exe. A release tag must spell the same value: the update
check compares `GG_VERSION` against the tag names.

## Release

`.github/workflows/CI.yml` builds on Windows, macOS and Linux on every push.

- **Windows**: tests, msbuild, windeployqt into `dist/` (the TLS plugins stay, for the update check), then
  Inno Setup with `installer.iss` produces `GoodGit.exe`.
- **macOS**: `scripts/create_dmg.sh <Qt dir>` runs macdeployqt on the release bundle and packs it into
  `GoodGit.dmg`.
- **Linux**: build only, nothing packaged.

A tag push creates the GitHub release with both artifacts and a changelog of the commits since the previous
tag. Those releases are what the update check reads.

## Workspace scripts

`update_repository.bat`/`.sh` fast-forward the checkout to the remote's default branch and pull every
submodule on its own default branch. `push_repository.bat` pushes every submodule, then this repository.

**Submodule policy:** all submodules are first-party. When a helper almost fits, extend it in the library,
in library shape; ask before changing existing behavior, since other projects share them. Each such change
is a commit in the submodule plus a pointer bump here.
