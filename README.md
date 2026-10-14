# Trie FrameViewer

A compact Windows desktop player for animation reference footage and playblasts.
Built with C++20, Qt Quick/QML, and FFmpeg. The current local release baseline is
**v1.0**, with application version **1.1.1**.

## Download and install

Get the Windows x64 installer from the [latest GitHub release](https://github.com/ltrye/trie_frame_viewer/releases/latest).
This repository is private, so invited users must sign in to GitHub to download it.
The installer includes the runtime dependencies. Save your project and close the
app before updating. The accompanying `.sha256` file verifies the download.

## Implemented

- Local video playback, forward/reverse frame stepping, and reverse playback.
- Numbered PNG/JPEG/BMP sequences (24 fps in this version; other image formats require matching Qt plugins).
- Compact YUV420 frame caching with bounded RAM use and timeline coverage.
- Direction-aware refill, reuse of visited frames, and sequential decoding of useful GOP frames.
- GPU color conversion and a separate bounded texture cache for recently displayed frames.
- Automatic D3D11 hardware decoding for supported 1080p+ footage, with CPU/GPU preferences.
- Timeline scrubbing and relative right-mouse scrubbing with audio snippets.
- Timeline clicks/drags preserve playing or paused state, including reverse playback.
- Cinema Mode (F11) hides the window chrome and viewer tools, with the timeline and playback controls always visible below the video.
- In/out ranges, looping, ping-pong, and playback speeds.
- One-row Current frame, Start, and End inputs using 1-based frame numbers.
- Frame bookmarks on the timeline, name tooltips, and right-click rename, frame change, or delete.
- Bookmark navigation, compact text menus, and themed Windows title bars.
- Versioned `.tfv` project save/load, relative media paths, identity checks, and relinking.
- Review settings: range, frame, speed, audio offset, pan, zoom, rotation, and flips.
- Autosave recovery files beside saved projects; save prompts require at least one bookmark and ignore speed changes.
- Save dialogs suggest the current media's base name with a `.tfv` extension.
- Per-user Windows video context-menu registration and removal in Preferences.
- Optional Debug Mode with live process RAM, dedicated VRAM, and shared GPU memory.
- Dark/light themes, configurable keyboard shortcuts, fullscreen, and always on top.

## Run the built app

After deployment, run `out/TrieFrameViewer/TrieFrameViewer.exe`, or use `scripts/run.ps1`.
The app accepts a video, image-sequence frame, or `.tfv` path as its first argument.

## Local development

Requires Windows x64, Python, CMake 3.24+, a Windows SDK, and MSVC with C++ tools.
The checked-in preset uses Visual Studio Build Tools 2026, which is installed on
the current workstation. For Visual Studio 2022, override the generator in a
`CMakeUserPresets.json` preset with a different build directory.

Dependencies are installed under `.tools/`; they are not added to the system PATH.

```powershell
python -m venv .tools\bootstrap
.\.tools\bootstrap\Scripts\python.exe -m pip install aqtinstall==3.3.0
.\.tools\bootstrap\Scripts\python.exe -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O .tools\Qt -m qtmultimedia qtshadertools
.\.tools\bootstrap\Scripts\python.exe scripts\download_ffmpeg.py
python scripts\dev.py configure
python scripts\dev.py build
python scripts\dev.py test
python scripts\dev.py deploy
.\scripts\run.ps1
```

`scripts/dev.py` normalizes the inherited environment because some launchers expose
both `Path` and `PATH`, which MSBuild rejects. It changes only child-process environments.

The FFmpeg download script verifies the release's SHA-256 checksum and records the
SDK URL and digest in `.tools/ffmpeg/sdk-lock.json`. It reuses an existing SDK.
For reproducible release builds, mirror and pin the recorded SDK rather than resolving
the moving development asset. Qt is pinned to 6.8.3.

## Where to make changes

| Change | Location |
|---|---|
| Colors, fonts, spacing, radii, layout sizes | `src/ui/Theme.qml` |
| Control icons | `src/ui/IconPaths.qml` |
| FV app logo and Windows icon | `assets/branding/`, `scripts/generate_app_icon.py` |
| Text menu layout | `src/ui/components/AppMenuBar.qml` |
| Default settings and action/shortcut definitions | `config/defaults.json` |
| Reusable controls | `src/ui/components/` |
| Screen layouts | `src/ui/views/` and `src/ui/Main.qml` |
| UI commands and playback state | `src/application/PlayerController.*` |
| Decoding and compact pixel storage | `src/media/Decoder.*`, `VideoFrame.*` |
| Background scheduling and cache policy | `src/media/MediaWorker.*`, `FrameCache.h` |
| Audio output and scrub snippets | `src/media/AudioEngine.*` |
| Bookmarks and project schema | `src/domain/` |
| GPU display integration | `src/presentation/VideoItem.*`, `YuvNode.*`, `shaders/` |
| Native Windows title-bar integration | `src/presentation/WindowChrome.*` |
| Windows context menu / Open with integration | `src/platform/WindowsIntegration.*`, `config/windows-integration.json` |
| Live process memory monitoring / overlay | `src/platform/ResourceMonitor.*`, `src/presentation/WindowDiagnostics.*`, `src/ui/components/DebugOverlay.qml` |
| Installer and production packaging | `packaging/windows/installer.nsi`, `scripts/build_installer.py` |

See `docs/architecture.md` for ownership and extension rules, and
`docs/project-format.md` for `.tfv` field meanings.
See `docs/performance.md` for benchmark clips, measurements, and cache limits.
See `docs/caching-and-scrubbing.md` for the cache layers, prefetch policy, seek
scheduling, audio scrubbing, and current limitations.

The FV app logo is an SVG with original vector letter outlines, without a font or
embedded bitmap. Run `python scripts/generate_app_icon.py` to regenerate its SVG,
transparent-background monogram, multi-size Windows ICO, and PNG preview. Pillow
is needed only for the ICO/PNG exports. Colors and geometry live in the generator.
The build embeds the ICO in the executable and the SVG as the Qt window icon.

Bookmarks appear on the timeline. Hover to see a name; right-click to rename,
change the frame, or delete. Older range bookmarks load at their first frame.
Title-bar colors follow the app theme on Windows 11; Windows 10 uses the available
system frame styling. The native frame keeps Windows resize, snap, and maximize behavior.

## Source control and releases

Git tracks source, configuration, documentation, tests, and branding assets.
Dependencies in `.tools/`, compiled output in `build/` and `out/`, and generated
test media/reports in `artifacts/` are ignored. The `v1.0` tag identifies the initial
source baseline on `main`.

Use commits and release tags to preserve versions. Update the version in the root
`CMakeLists.txt`; the app's About dialog, `--version`, and Windows executable metadata
derive their version from it. The `.tfv` schema version is independent.

Builds use `build/` and deploy into the same `out/TrieFrameViewer/` directory:

```powershell
python scripts\dev.py build
python scripts\dev.py test
# Close the running app before deployment.
python scripts\dev.py deploy
.\scripts\run.ps1
```

Deployment checks for a locked executable before changing bundle files. Use
`scripts/verify.ps1 -Native` to verify the deployed app. Existing older bundles
are excluded from Git; future deployments update the standard directory.

## Current limits and next work

- Video frame mapping currently targets constant-frame-rate footage. Variable-rate
  clips use a nominal-rate mapping and may not step accurately; the rate heuristic
  cannot detect every VFR file. Full presentation-timestamp indexing is next.
- Common 8-bit YUV420/NV12 video uses compact YUV storage and GPU conversion.
  Other pixel formats use an RGBA8 fallback. HDR, EXR, and OCIO workflows are not
  supported yet. Hardware decoding currently downloads frames into the RAM cache;
  direct decoder-to-renderer GPU sharing remains future work.
- Clips larger than the RAM budget still require decoding on cold seeks. A compressed
  or disk-backed full-clip cache has not been implemented; a 4,000-frame 720p clip
  needs about 5.15 GiB to retain every decoded YUV420 frame at full resolution.
- The audio cache contains the beginning of a clip up to the configured byte budget.
  Beyond that region video continues silently. A sliding audio cache is planned.
- Playback speed currently changes audio pitch. Pitch-preserving filtering is planned.
- Reverse playback is silent; backward scrubbing plays forward snippets at the
  selected position for intelligibility.
- Image-sequence numbering gaps are skipped and sequences currently use 24 fps.
  Exact source numbering and selectable sequence FPS are planned.
- Background prefetch follows scrub/playback direction and waits briefly during active
  requests. Active-range and loop-boundary prioritization still need profiling.
- Rotation is in 90-degree steps. Numeric arbitrary-angle rotation is planned.
- Undo/redo, remembered window layout, and automatic autosave discovery are pending.
  Recovery files can currently be opened through Open Project.
- Performance targets have not been certified across GPUs or resolutions.

## Validation

Core tests cover bounded eviction, contiguous cache coverage, transactional bookmark
validation, project round-trips, relative paths, schema rejection, image sequences,
corrupt inputs, cancelled decoding, and cache refill after random seeking. With the
generated fixtures, they also verify exact frames after 120 rapid random 1080p seeks
and playback of single-frame loops. Native audio tests cover 20 stop/restart cycles,
scrubbing, and replay after reaching the end of cached audio.
Renderer tests compare GPU YUV conversion with CPU conversion across color matrices
and ranges. Decoder tests compare D3D11 and software frames when hardware is available.

```powershell
.\scripts\verify.ps1
.\scripts\verify.ps1 -Native
```

The verification script generates a local 4-second H.264/AAC test clip and a
12-second 1080p long-GOP seek fixture, runs tests, and exercises `.tfv` save/reopen
in the actual QML app. Screenshots and a JSON report
are written to `artifacts/`. It also checks button centering, timeline bookmark
editing/tooltips, media-based save names, compact sizing, native title-bar themes, and audio
remaining active through repeated playback loops. Audio tests explicitly skip
when no native audio output device is available.
Headless screenshots validate the software scene graph;
Windows GPU rendering is tested separately when running the desktop app.

## Distribution

Run `python scripts/dev.py installer` to build the per-user Windows installer at
`out/installer/TrieFrameViewer-Setup-1.0.0-x64.exe` (the filename follows the CMake version).
Its production build disables development tests and includes Qt/QML plugins, FFmpeg,
Visual C++ runtime DLLs, dependency notices, a Start menu option, optional desktop
shortcut, optional video context menu, and an uninstaller. NSIS 3.13 is downloaded
into `.tools/` and verified against a pinned SHA-256; no compiler is installed globally.

The normal app output stays in `out/TrieFrameViewer`. Installer staging is generated
under `build/production/payload` and refreshed on each package build. Package checksums
are beside the installer. See `docs/windows-distribution.md` for installation, tests,
and release considerations. No project source-code license has been chosen.
