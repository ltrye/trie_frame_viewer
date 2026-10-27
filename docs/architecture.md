# Architecture and maintenance rules

## Boundaries

`domain` owns bookmarks and the project schema. It depends on Qt Core, with QColor
used for color validation, but has no QML, playback, or decoding dependency.

`media` owns file decoding, cache policy, scheduling, and PCM output. FFmpeg headers
are confined to Decoder.cpp and VideoFrame.cpp. Other components see MediaInfo,
immutable VideoFrame values, and PCM bytes.

`application` coordinates commands and exposes typed state to QML. PlayerController
is a presentation-facing facade; new persistence migrations belong in domain and
new decoder behavior belongs in media, not this class.

`platform/WindowsIntegration` owns per-user Windows registry integration. QML receives
it explicitly and only requests register/remove/refresh operations. Video extensions,
verb identifiers, and ProgIDs live in `config/windows-integration.json`; the installer
cleanup is generated from the same configuration. Registry removals check the quoted
command path before deleting entries, so uninstalling an older copy preserves a newer
copy's registration. The adapter never changes video default-player/UserChoice values.

`platform/ResourceMonitor` samples this process's resident/committed RAM and Windows
PDH GPU Process Memory counters on the GUI thread. It lazily opens a reusable query
and filters exact PID prefixes across all adapter/node instances. Sampling
runs only while the persisted Debug Mode preference is enabled; its interval lives
in `defaults.json`. The reusable DebugOverlay receives this service explicitly.
When performance counters omit this process or publish empty usage, `presentation/WindowDiagnostics`
services a throttled fallback request on Qt's render thread using its real D3D11
adapter. It returns plain byte counts through a queued callback; the monitor never
reads renderer objects. This fallback requests an update at the sampling interval,
creates no GPU device, and performs no frame readback. The overlay tooltip identifies
whether readings cover all adapters or only the rendering adapter.

`presentation` adapts VideoFrame values to Qt Quick textures. VideoItem selects the
YUV shader or an RGBA/software-rendering fallback. YuvNode owns a bounded texture
LRU and all plane uploads on the render thread. Its QRhi integration uses Qt's
private Gui API, isolated in this component and tied to the pinned Qt 6.8.3 build.
The scene graph does not assume OpenGL; the normal Windows backend is Direct3D 11.

WindowChrome is the separate native-frame adapter. It receives colors from QML,
coalesces theme updates, and reapplies them when Windows recreates the window
handle (for example after an always-on-top change). Platform APIs stay here;
the menu bar and bookmark layouts remain reusable QML components.

`ui` contains views, shared controls, theme tokens, and icon geometry. Views receive
their controller and preferences explicitly. There is no global QML context-property
dependency. The two singleton modules contain appearance and icon data only.

ReviewStage owns one Viewer, Timeline, and Transport in both normal and Cinema Mode.
The viewer ends at the playback panel in both modes, keeping the timeline and
transport controls always visible below the video. Cinema Mode hides the menu,
native title bar, and viewer tools without overlaying controls or using idle timers.
Main owns fullscreen transitions and restores the previous window visibility.
Cinema Mode is transient UI state and never changes project dirty/playback state.

AppMenuBar groups commands and resolves labels/shortcuts from defaults.json via
Preferences. Main routes menu requests through the same dispatcher as keyboard
shortcuts. Timeline owns frame-bookmark markers, hover tooltips, and their context
menu. BookmarkEditor is a reusable dialog for name/frame edits. Edits address stable
bookmark IDs so removing another marker cannot redirect a pending edit. The controller
validates frame edits against the loaded media; names and changes remain in the domain
model and persist in `.tfv` projects. There is no bookmark sidebar or range-bookmark action.

Preferences uses separate notification signals for volume, cache budgets, decoding,
theme, debug mode, and shortcuts. Dragging volume updates only audio gain and its UI
bindings; it does not rebuild command models or reconfigure the video worker. Volume
persists after 250 ms without another change, with a final flush on shutdown.

## Threads and ownership

ClipExporter owns a separate bundled FFmpeg process for MP4 exports. The controller
snapshots the media and inclusive range into ClipExportRequest; QML owns save/progress
dialogs. The process has bounded encoder thread counts and never uses playback frame
caches. Progress/error signals return through QProcess. Encoding targets a unique
temporary file beside the destination, committed with Windows replacement/rename only
on success. Cancellation, failure, and shutdown remove temporary files. Decoder and
export share image-sequence discovery so ordering and numbering gaps agree.

Full-video export uses the same exporter with a complete-media range and optional
output dimensions. Resolution presets and the Full HD default live in defaults.json;
ExportVideoDialog owns selection and passes the preset ID through the save dialog.
Scaling fits the source aspect ratio inside the target canvas, pads unused space,
and normalizes pixel aspect ratio. Range exports leave output dimensions unset.

WindowChrome applies Always on Top with SetWindowPos rather than changing QWindow
flags. This preserves implicit Windows decorations and the native window handle;
the desired topmost state is reapplied if the native surface changes.

- The GUI thread owns PlayerController, BookmarkModel, Preferences, and AudioEngine.
- MediaWorker owns a joinable C++ worker and its FFmpeg decoder. FrameRepository
  serializes shared FrameCache metadata so GUI requests can retrieve immutable
  cached pixels while decoding is busy. No codec/GPU work occurs under that lock.
  Source replacement and eviction destroy retired pixels outside the lock; source
  replacement frees the old cache on the worker. Destruction cancels and joins it.
- PCM decoding runs in a separate joinable task so it does not block video requests.
  Changing the source cancels that task; worker destruction joins it.
- The worker accepts one latest requested seek rather than accumulating mouse moves.
- A source generation invalidates results from previous media. A transport epoch
  invalidates frames from earlier user seeks, pauses, or playback sessions. During
  playback, completed frames can advance monotonically toward the clock target.
  A drag shares one preview epoch so useful in-flight results can be shown while
  the cursor follows the latest input. Release (or 100 ms of quiet) commits an
  exact seek in a new epoch, rejecting older previews. A monotonic request serial
  also rejects an older preview after a newer cache hit has already been accepted.
  Outside preview sessions,
  paused seeks accept only the exact target. The actual presented frame and the
  selected/clock target are separate controller state.
- The RAM cache uses LRU eviction biased toward a window around the playhead and
  scrub/playback direction. Moving the window does not immediately discard visited
  regions. Under byte pressure, old frames outside the window are evicted first.
  Clips that fit retain all frames. PrefetchPlanner adds active-range bounds and a
  budgeted opposite-end reserve near looping boundaries. Misses, reversals and
  distant hits cancel speculative work; nearby cache hits update its anchor without
  repeatedly aborting it. A new transport epoch cancels foreground decoding.
  Cancellation forces the next decode to seek again
  because the demuxer may have advanced beyond the last returned frame.
- Foreground seeks materialize only the requested frame, leaving speculative
  intermediate copies/downloads to idle prefetch. Reverse playback retains useful
  lookbehind intermediates to avoid repeated GOP decoding during normal playback.
  Prefetch traverses each planned batch in ascending order, including the reverse
  lookbehind region, to avoid repeated backward FFmpeg seeks. Small forward jumps
  can continue decoding without flushing. Existing cached intermediates are skipped.
- Mouse seeks update the requested position immediately and coalesce decoder work
  over a configurable 16 ms interval. Mouse release requests the exact final frame.
  Prefetch begins immediately during playback and fine preview motion; larger
  jumps and exact paused seeks retain a configurable 80 ms quiet interval.
  Cache-coverage notifications are revision-filtered and throttled to 100 ms.
- Queued VideoFrame signals share immutable, owned pixels. Common 8-bit YUV420 and
  NV12 sources are copied into tightly packed Y/U/V planes, without holding FFmpeg
  decoder buffers. QImage conversion happens only for fallback rendering and tests.
- D3D11VA decoding is optional and falls back to software on initial setup/decode
  failure. Hardware frames currently download into the same compact RAM representation;
  direct GPU sharing has not been implemented. Auto uses hardware for 1080p+ clips,
  based on the current benchmark tradeoffs. Software decoding defaults to two threads.
- GPU textures are created through updatePaintNode. A monotonic frame identity keeps
  texture reuse independent of allocator address reuse. The texture LRU retains recently
  displayed frames only, releases CPU upload bytes after submission, and keeps one
  current frame even when the configured GPU cache is zero. Compatible evicted
  texture sets are recycled in-place within the same budget, with QRhi ordering
  uploads after earlier draws. Identical frame submissions skip redundant item
  updates; geometry changes and budget changes still update normally.
  Its budget counts plane
  pixels, not driver alignment or all graphics allocations.
- The configured video budget covers retained cache pixels. An in-flight decoder
  frame, presentation frame, decoder internals, audio, and GPU storage are additional.
- Local file opening/FFmpeg reads are not currently interruptible inside each library
  call; cooperative cancellation happens between calls. Avoid network sources.
- AudioEngine recreates QAudioSink after a transport stop because the Windows Qt
  backend can retain queued buffers across stop/start. Scrub requests are coalesced
  over 20 ms and use the latest requested position.

## UI extension rules

1. Define visual values in Theme.qml. Defaults are allowed there; avoid repeated colors
   or spacing constants in screens. Geometric icon and timeline coordinates are not
   application configuration.
2. Keep components small and accept state through required properties and signals.
3. Add user actions in defaults.json and route their identifiers through Main.dispatch
   or PlayerController.execute. Native dialogs and window operations stay in Main.
4. Wrap user-facing strings in qsTr. Keep frame changes immediate; animation belongs
   to incidental controls, never to media position.
5. Update a specific notification group rather than sending every property notification
   for each frame. Position changes should not rebuild the project or preferences.
6. Keep frames and PCM out of QML JavaScript. Expose UI-sized metadata and models.
7. Treat the UI as replaceable. Media-worker logic must work without a QML engine.

## Persistence

ProjectDocument writes with QSaveFile and parses into a temporary object. BookmarkModel
validates all bookmarks before replacing its list. Relative paths are resolved against
the project location. A file-size and sampled-content fingerprint prevents silently
applying old bookmarks to changed video. It is not a full-file authenticity check.

The controller marks a project dirty for bookmark/review edits only while it has at
least one bookmark. Removing the last bookmark clears the dirty flag, so closing or
replacing bookmark-free media never prompts to save. Speed changes do not mark dirty,
even with saved bookmarks. Explicit saves still include current speed and review
settings. These rules apply to the title indicator, close/open/drop guards, and autosave.

Changing the schema requires a version bump plus an explicit migration or rejection.
The current schema supports one source per project. Introduce a media ID/reference
layer before adding playlists or A/B sources.

## Planned internal evolution

1. Replace nominal-FPS video indices with a presentation-timestamp index and exact
   decoded ordinal matching. Preserve bookmark ordinals during this change.
2. Profile user footage before choosing a compressed/disk-backed full-clip cache.
   A bounded decoded-frame cache cannot retain thousands of full-resolution frames
   within a small RAM budget. Direct D3D11 decoder/renderer interoperability can
   remove downloads/uploads, but does not by itself solve full-clip storage.
3. Extend the range-aware scheduler with measured-cost prediction only if traces
   demonstrate a benefit beyond its current bounded directional windows.
4. Replace the beginning-only PCM cache with a sliding audio window.
5. Introduce a command history for editable project state; transient playback is not
   an undoable command.
6. Move project open/relink/recovery workflow into a document service if it grows.

## Playback diagnostics

PlaybackTrace is disabled unless TFV_TRACE_PATH names an output JSON file. Enabled
traces buffer at most 32,768 events, count dropped events, and flush outside the
frame hot path at shutdown. Request serials connect input, cache lookup, decode,
GUI acceptance, render and Qt frameSwapped events. frameSwapped means queued for
presentation, not physical monitor display. Audio device setup/stop, compact pixel
construction, uploads and texture allocations have separate events. Use
scripts/analyze_playback_trace.py for a summary; never treat a truncated trace or
an engine-only seek measurement as end-to-end display latency.

## Formatting

C++ uses the checked-in .clang-format. QML uses Qt's qmlformat. Do not edit generated
build files or package dependencies. Never check .tools, build, out, or test media into Git.
