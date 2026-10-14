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

AppMenuBar groups commands and resolves labels/shortcuts from defaults.json via
Preferences. Main routes menu requests through the same dispatcher as keyboard
shortcuts. Timeline owns frame-bookmark markers, hover tooltips, and their context
menu. BookmarkEditor is a reusable dialog for name/frame edits. Edits address stable
bookmark IDs so removing another marker cannot redirect a pending edit. The controller
validates frame edits against the loaded media; names and changes remain in the domain
model and persist in `.tfv` projects. There is no bookmark sidebar or range-bookmark action.

## Threads and ownership

- The GUI thread owns PlayerController, BookmarkModel, Preferences, and AudioEngine.
- MediaWorker owns a joinable C++ worker. The FFmpeg decoder and FrameCache live
  exclusively on that worker. The destructor requests cancellation and joins it.
- PCM decoding runs in a separate joinable task so it does not block video requests.
  Changing the source cancels that task; worker destruction joins it.
- The worker accepts one latest requested seek rather than accumulating mouse moves.
- A source generation invalidates results from previous media. A transport epoch
  invalidates frames from earlier user seeks, pauses, or playback sessions. During
  playback, completed frames can advance monotonically toward the clock target;
  while paused, only the exact requested frame is accepted. The displayed frame
  and the requested clock target are separate controller state.
- The RAM cache uses LRU eviction biased toward a window around the playhead and
  scrub/playback direction. Moving the window does not immediately discard visited
  regions. Under byte pressure, old frames outside the window are evicted first.
  Clips that fit retain all frames. New requests cancel speculative prefetch; a new transport epoch also
  cancels foreground decoding. Cancellation forces the next decode to seek again
  because the demuxer may have advanced beyond the last returned frame.
- Both foreground decoding and prefetch retain useful intermediate GOP frames.
  Prefetch traverses each planned batch in ascending order, including the reverse
  lookbehind region, to avoid repeated backward FFmpeg seeks. Small forward jumps
  can continue decoding without flushing. Existing cached intermediates are skipped.
- Mouse seeks update the requested position immediately and coalesce decoder work
  over a configurable 16 ms interval. Mouse release requests the exact final frame.
  Prefetch waits for a configurable 80 ms quiet interval after foreground requests;
  cache-coverage notifications are throttled to avoid per-frame UI work.
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
  current frame even when the configured GPU cache is zero. Its budget counts plane
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
3. Add active-range and loop-boundary hints to the direction-aware cache scheduler.
4. Replace the beginning-only PCM cache with a sliding audio window.
5. Introduce a command history for editable project state; transient playback is not
   an undoable command.
6. Move project open/relink/recovery workflow into a document service if it grows.

## Formatting

C++ uses the checked-in .clang-format. QML uses Qt's qmlformat. Do not edit generated
build files or package dependencies. Never check .tools, build, out, or test media into Git.
