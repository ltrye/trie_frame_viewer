# Windows installer and shell integration

## User behavior

- Save Project and Save Project As suggest `<media base name>.tfv` beside the media.
  For example, `walk.v002.mp4` suggests `walk.v002.tfv`. The filename remains editable.
  Saving an existing project with Save keeps its current project path.
- Preferences → Appearance & playback → Windows integration has a context-menu
  registration/removal button. It affects the current Windows account, uses the
  current executable path, and does not replace the default video player.
- Supported video extensions are declared in `config/windows-integration.json`.
  Registration adds “Open with Trie FrameViewer” plus Open with entries. Windows 11
  may place the direct verb under Show more options.
- `.tfv` is listed as a supported Open with type. Default-app selection remains under
  Windows/user control.

## Installer

Build with `python scripts/dev.py installer`. The output name follows the version
in the root CMakeLists.txt. The default install folder is
`%LOCALAPPDATA%\Programs\TrieFrameViewer`, for the current user without elevation.
Windows 10/11 x64 is targeted. The wizard offers Start menu shortcuts, an optional
desktop shortcut, and video context-menu registration. An entry is added to Windows
Installed apps. Settings and media/projects are preserved on uninstall.

The production preset uses `build/production`, with BUILD_TESTING=OFF. Its generated
payload includes Qt/QML, FFmpeg, and app-local Visual C++ runtime DLLs. It does not
include QtTest, benchmarks, generated footage, Python, or build tools. Only the
known generated `build/production/payload` staging tree is refreshed. Deployment of
the development app continues to use `out/TrieFrameViewer`.

An installation manifest contains SHA-256 hashes for payload files. The uninstaller
uses a generated list of exact files and nonrecursive directory removal, preserving
unknown files placed in the install folder. Registry and shortcut removal checks
which installation owns the registration. A running app must be saved/closed before
updating or uninstalling; setup never force-terminates it.

Silent setup supports `/S`, `/NOCONTEXTMENU`, `/NOSHORTCUTS`, and NSIS's final `/D=...`
argument. Silent uninstall supports `/S`. These switches are useful for deployment
verification and do not change the default interactive wizard.

## Dependencies and release artifacts

NSIS is portable, pinned to 3.13 with a SHA-256 in `scripts/bootstrap_nsis.py`.
The compiler is a local build dependency, not an end-user requirement. Dependency
notices, license texts, Qt's upstream SBOMs, and the FFmpeg SDK lock are included in
the installed app. `packaging/licenses/sources.json` records the license-text origin
and checksum. Qt and FFmpeg remain shared, replaceable DLLs.

Publish the installer and its `.sha256` file together. For a public release, retain
the exact dependency binaries/build recipes and arrange corresponding-source access
for the LGPL components and their bundled dependencies; upstream links alone should
not be treated as a complete release compliance audit. Qt's complete upstream SBOM
contains tools and optional components beyond this runtime payload. The installer
has no signing certificate configured. Code-sign it and validate it on a clean
Windows machine before claiming a signed/clean-machine-certified release.

## Validation

Run `python scripts/verify_installer.py` after creating the installer. It refuses to
replace an existing registered installation. It creates and removes its own test
installation and shell registrations; the retained project and report are under
`artifacts/installer-smoke`.

- Core tests use an isolated HKCU test subtree for register/remove, quoted paths,
  preserving defaults/other programs, and ownership changes between app copies.
- The native app check verifies the default save URL/folder and Settings control,
  along with existing playback, audio, bookmarks, project restore, and theme checks.
- Installer validation installs into a workspace path with spaces, verifies payload
  hashes/runtime dependencies, launches with SDK paths removed, verifies registration,
  and uninstalls while retaining an unknown user project file. This is local Windows
  validation; it is not a replacement for a separate clean machine or VM.

Reference implementations follow the Qt FileDialog API and Microsoft's per-user
application registration guidance:
https://doc.qt.io/qt-6.8/qml-qtquick-dialogs-filedialog.html
https://learn.microsoft.com/en-us/windows/win32/shell/app-registration
