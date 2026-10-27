# MPX and FrameViewer: optimization comparison and proposed changes

Initial review: 2026-10-26. Revised: 2026-10-27. FrameViewer baseline: v1.3.0, commit
`6d9edc7e48cd86beebc51641dc337c022cf8d5af`.

This is the pre-implementation comparison against that baseline. Subsequent
v1.4.0 changes and measured results are recorded in
[the implementation plan](optimization-implementation-plan.md) and
[the measurement report](performance-playback-optimization-2026-10-27.md).

This compares the supplied [MPX optimization study](D:/CodeProject/rev_eng/KEYFRAME_MPX_OPTIMIZATION_STUDY.md),
the newer [eviction report](D:/CodeProject/rev_eng/investigation/reports/cache-eviction-policy.md),
and the newer [read-ahead report](D:/CodeProject/rev_eng/investigation/reports/read-ahead-policy.md)
with FrameViewer's current source. MPX findings below are attributed to those saved
analyses; selected exported pseudocode was cross-checked, without opening a new
REA session or independently reproducing the decompilation. No application code,
installed application, or media was changed for this review. No new performance
benchmark was run.

## Conclusion

FrameViewer already implements much of the same core design: bounded decoded-frame
caching, shared immutable pixels, selective frame materialization during seeking,
GPU YUV conversion, and hardware decoding with a CPU transfer path. These mechanisms
should be retained.

The newer evidence makes budget-driven cache filling and cache-boundary extension
the leading additional MPX lessons. Compatible texture reuse, timestamp-interval
selection, and coordination between requested time and available pixels remain
important. FrameViewer's most actionable findings are cache hits waiting behind
its decode worker, a quiet-period rule that can defer prefetch during continuous
playback or dragging, and allocation of textures for new frame identities.

There is no defensible overall performance winner from this evidence alone. The
studies did not benchmark MPX or establish which pixel/hardware paths were active
for a given video. Important scheduler, endpoint-eviction, and retarget rules are
now recovered, while full state coverage and runtime cadence remain incomplete.
FrameViewer's implementation
and historical measurements are known more precisely, which is not evidence that
its runtime performance is superior.

## Comparison

| Area | MPX, according to the supplied study | FrameViewer today | Assessment |
|---|---|---|---|
| Frame ownership | Reference-counted frame objects, including current/previous/next references | Immutable shared `VideoFrame::Data` and implicitly shared plane bytes | Same useful principle; no wholesale redesign needed |
| RAM lookup | Ordered time intervals, midpoint search, candidate coverage validation | `std::map<int, Entry>` lookup plus LRU age update | Both have logarithmic lookup in the ordinary case. MPX's interval model is more suitable for VFR; its lookup is not proven universally cheaper |
| Retention/eviction | Budget-gated endpoint removal using directional byte balance and a 0.5 parameter; one or zero removals per inspected call | LRU biased by an approximately 25% behind / 75% ahead frame-count window, reversed for backward motion; evicts until within its retained payload budget | MPX emphasizes a contiguous local range. FrameViewer can retain scattered visited regions. Different workload tradeoffs; no universal winner |
| Decode-ahead | Active producer admits updates below its byte budget, extends from retained boundaries, and continues event/maintenance service when full | Plans a playhead window but delays speculative work until 80 ms after the latest foreground request | Stronger evidence now favors adopting MPX's capacity-driven continuation for ordinary playback, with bounded foreground-priority work |
| Cache clear | A selected clear path retains references in a deferred queue, serviced at up to five entries per call | `FrameCache::clear` releases its entries immediately on the video worker | Deferred release may improve transition responsiveness, but can retain extra live memory; measure before adopting |
| Seek/preroll | Decoded frames can skip conversion/output construction below a timestamp threshold | Foreground seeks skip copies, downloads, and conversion of intermediate frames; reverse playback/idle prefetch can retain useful intermediates | Already substantially aligned since v1.1.2 |
| Common SDR pixels | Planar and packed storage paths observed, selected runtime format unknown | Tight 8-bit YUV420 planes for YUV420P/YUVJ420P/NV12 | FrameViewer's common path is demonstrably compact. No measured cross-app RAM winner |
| Hardware decoding | Inspected branch downloads hardware frames and then converts/copies/uploads | D3D11VA frames download into CPU storage and upload to Qt Quick textures | Neither inspected architecture is end-to-end zero-copy |
| GPU processing | OpenGL shaders perform YUV conversion and view/color operations | QRhi/D3D11 path performs YUV conversion; scene-graph transforms handle pan/zoom/rotation/flips | Both already use the GPU. Switching to OpenGL is not an optimization established by this study |
| GPU storage | Compatible texture storage is reused for different incoming images | GPU LRU reuses an existing frame's textures, but a new identity allocates a fresh three-plane texture set | MPX has the preferable allocation-reuse pattern for first visits; FrameViewer's retained textures benefit revisits |
| Redundant updates | A studied path suppresses same-interval/same-marker frame changes | Controller suppresses several duplicate requests, but `VideoItem::setFrame` always schedules an update | Extend suppression at presentation boundary; existing GPU hits already avoid another upload |
| Time correctness | Frame interval bounds are explicit | Decoder estimates an ordinal by rounding PTS × nominal FPS | MPX has the stronger demonstrated time-selection foundation; FrameViewer needs explicit PTS/duration indexing |
| Scrub scheduling | Identical pending targets are deduplicated; same-source retargeting updates a flag and the producer later drains work, clears active cache, and seeks forward | Latest pending request, shared drag epoch, exact release epoch, stale-result rejection, and visited-region retention | MPX now has a recovered retarget path. Keep FrameViewer's useful starvation/stale-result protections and compare target lag, cache survival, and preview cadence |
| HDR/high bit depth | HDR and Rec.2020 shader branches recovered; active paths unmeasured | Common GPU path is 8-bit; other formats fall back to RGBA, with no complete HDR tone-mapping pipeline | MPX has broader demonstrated color-processing code; FrameViewer needs a separate correctness/performance extension |

FrameViewer evidence:
[FrameCache](D:/CodeProject/trie_frameviewer/src/media/FrameCache.h:22),
[VideoFrame storage](D:/CodeProject/trie_frameviewer/src/media/VideoFrame.cpp:18),
[seek materialization gate](D:/CodeProject/trie_frameviewer/src/media/Decoder.cpp:251),
[GPU texture creation](D:/CodeProject/trie_frameviewer/src/presentation/YuvNode.cpp:33),
[GPU cache lookup](D:/CodeProject/trie_frameviewer/src/presentation/YuvNode.cpp:134),
[presentation updates](D:/CodeProject/trie_frameviewer/src/presentation/VideoItem.cpp:9).
The original MPX study's sections 1–6 and the newer policy reports provide the
corresponding recovered mechanisms and their evidence limitations.

## New evidence reviewed on 2026-10-27

The overview still says eviction/read-ahead policy is unresolved and has a file
write time of 2026-10-26 11:17 local time. The newer policy reports were written
at 17:22 and 17:25 that day. Their findings supersede those specific unknowns in
the overview; they do not supersede its lack of runtime measurements.

1. **Capacity-driven production:** the native-video producer checks
   `activeVideoBytes < maximumVideoBytes` before admitting a decoder update. At
   capacity it sleeps 10 ms, but still services events and balancing. This is a
   producer throttle, not a playback period. See
   [producer loop](D:/CodeProject/rev_eng/investigation/cache-policy/pseudocode/0x14001dd80.c:72)
   (Evidence `ev_94a9cde00313e3740bbff8d45ee7e1cac44457b31ca5485235087f5106ed9463`)
   and [capacity predicate](D:/CodeProject/rev_eng/investigation/cache-policy/pseudocode/0x140018570.c:7)
   (Evidence `ev_898acd32f1974eb2aeb7929b7dc09b179fa712501133c8aca1d2b6234aed92dd`).
2. **Directional endpoint eviction:** the wrapper supplies 0.5 to a byte-fraction
   decision. Under ordinary finite inputs at/above budget, forward mode can remove
   the earliest frame when completed-entry bytes exceed half; reverse mode can
   remove the latest frame when remaining/current-and-later bytes exceed half.
   A target beyond the upper cached bound selects the earliest entry. One call
   removes at most one entry, and can remove none. This is neither LRU nor a
   guaranteed 50/50 split of frames, time, or process RAM. See
   [decision](D:/CodeProject/rev_eng/investigation/cache-policy/pseudocode/0x140019f50.c:7)
   (Evidence `ev_44b806366fd070f6e80faf72bcd1f043b76ad0181582e8f70dd50bd6389194bc`)
   and [wrapper](D:/CodeProject/rev_eng/investigation/cache-policy/pseudocode/0x14002f990.c:7)
   (Evidence `ev_a434334485c2457ec96b5dc513bb78964ddca36bb633fe49813791f8e43a9f3e`).
   The fraction-helper argument recovery relies on the supplemental report's
   assembly analysis; the exported decision pseudocode alone omits those arguments.
3. **Cache-frontier extension:** producer starts can derive from cached end + 1
   or cached start - 1. A pending immediate target temporarily selects forward
   decoding; once covered, the track's direction bias selects continuation.
   Repeated targets do not need to reset a whole speculative window. See the
   [read-ahead report](D:/CodeProject/rev_eng/investigation/reports/read-ahead-policy.md).
4. **Deferred clearing:** a selected clear path moves frame references to a
   separate retained queue; servicing it releases at most five entries per call.
   Normal endpoint evictions are distinct and release their selected reference
   directly. Frame destruction cost may be spread across passes, but five entries
   is not a measured time budget and active-cache bytes can understate live bytes.
   See [queue service](D:/CodeProject/rev_eng/investigation/cache-policy/pseudocode/0x140018760.c:7)
   (Evidence `ev_31deb55a76d5a2763e549a457532a124662adebf6066261ebfe46646bfeb0651`).
5. **Retarget and reverse reconstruction:** the saved restart path updates a
   same-source retarget flag rather than proving per-input thread destruction.
   The producer waits for pending work, clears the relevant active cache, and
   reseeks forward. The reverse update path reconstructs intervals using forward
   compressed packets and retained decoded results. Exact queue order, every
   timebase adjustment, and runtime latency remain unresolved.

The reported constants must stay separate: **10 ms** is a full-cache/idle sleep;
**0.5** is the balancing parameter; the shipped **24-duration** value belongs to
the already-decoded-frame conversion gate. None establishes a fixed 24-frame
read-ahead distance. The reported `-100000` reverse-boundary adjustment is in
internal coordinates and must not be converted into a claimed GOP length.

Input snapshots, SHA-256:

- Overview: `31bf50c2deb1c2dfa35cf0fb468c3592d4868c6cde8630f7d376deaff77c51b1`.
- Eviction report: `7f8604ecb65e9ce84f66298e25734f6dcab4f4e4ea9693924eaf7607f1c02ce3`.
- Read-ahead report: `1f074823604ac096aa1a62a470681e8a958477fada8d9d61216a8d74eef7813d`.

## Why 512 MB does not mean a whole long video is decoded in memory

For tightly packed 8-bit YUV420, a frame needs approximately `width × height × 1.5`
bytes. With FrameViewer's 512 MiB video-payload budget:

| Resolution | Payload per frame | Frames fitting ideally | Time at 24 fps | Time at 30 fps |
|---|---:|---:|---:|---:|
| 1920 × 1080 | 2.97 MiB | 172 | 7.17 s | 5.73 s |
| 3840 × 2160 | 11.87 MiB | 43 | 1.79 s | 1.43 s |
| 960 × 540 preview | 0.74 MiB | 690 | 28.75 s | 23.00 s |

These are ideal payload calculations, not resident-memory measurements. Four
thousand 1080p frames alone require approximately 11.59 GiB; at 4K they require
46.35 GiB. Compressed file size does not describe this decoded memory requirement.

FrameViewer additionally budgets 64 MiB for audio and 64 MiB for GPU texture payloads.
Decoder pools, in-flight uploads, the displayed frame, UI resources, and driver
allocations are extra. MPX's documented cache preference covers audio and video
together; its study does not resolve the precise split. Also, the study's diagnostic
MB formatting uses decimal units. Equal-looking cache settings are therefore not
automatically equal total-memory budgets. Shared GPU/system RAM must not be blindly
added to process resident memory as though both were independent physical allocations.
The recovered MPX deferred queue and one-victim/no-victim balancing paths further
reinforce that its active byte counter is not a hard resident-memory ceiling.

The study does not establish a full-clip compressed/proxy cache, a hidden disk frame
cache, or direct GPU-only frame retention as the explanation for MPX's smoothness.
MPX's [official scrubbing guide](https://zurbrigg.com/keyframe-mpx-documentation/kmpx-scrubbing-and-frame-stepping)
specifically recommends cached ranges to avoid seeking delays. Its
[Preferences documentation](https://zurbrigg.com/keyframe-mpx-documentation/kmpx-preferences)
describes the combined audio/video memory budget.

The Full HD setting in FrameViewer's export dialog affects the saved MP4 only.
It does not change the resolution of cached review frames. Fitting a 4K video into
a smaller viewer also scales its display geometry, not its retained pixel payload.
A reduced-resolution review cache would be a new, separate feature.

## Most actionable findings in FrameViewer

### 1. A RAM hit can wait behind unrelated decoding

`FrameCache` belongs exclusively to the video worker. `MediaWorker::seek` posts a
request; the worker performs `cache.get(anchor)` when its loop next services that
request. During a drag, foreground decode deliberately shares the session epoch
and may finish an older target instead of cancelling on every mouse movement.
That prevents the starvation fixed in v1.1.2, but also means an already cached new
target can wait behind an in-flight foreground decode.

This is a source-supported possible delay, not a measured latency distribution.
MPX's complete thread topology is unknown, so the study does not establish whether
MPX avoids this same situation.

Proposed change: a short, synchronized cache-read path returning shared immutable
frame handles, or an equivalent snapshot-based frame repository, independent of
FFmpeg execution. No decode, conversion, upload, or long lock belongs on the GUI
thread. Cache-hit results must carry source/request identity so a slower older
worker result cannot overwrite a newer hit. LRU touches and memory accounting need
an explicit ownership protocol rather than unsynchronized access to today's cache.

Evidence: [request posting](D:/CodeProject/trie_frameviewer/src/media/MediaWorker.cpp:29),
[worker cache lookup](D:/CodeProject/trie_frameviewer/src/media/MediaWorker.cpp:207),
[accepted frame state](D:/CodeProject/trie_frameviewer/src/application/PlayerController.cpp:63).

### 2. The 80 ms quiet rule can suppress useful prefetch

Every foreground request restarts `quietClock`, and speculative decode waits until
that clock reaches `prefetchDelayMs`, default 80 ms. Requests every approximately
17–42 ms during ordinary 24–60 fps playback can therefore keep ahead-of-playhead
prefetch deferred. Continuous dragging can do the same. Sequential playback still
decodes requested frames and reverse playback has intermediate retention; this is
not a claim that all caching stops.

Proposed change: separate policies for playback, fine viewport scrubbing, rapid
timeline jumps, and idle warming. Playback should have a small ahead buffer;
fine scrubbing should warm adjacent frames; fast unpredictable jumps should keep
foreground priority and avoid speculative GOP copying. Active Start/End and loop
boundaries should influence the cache window. Use bounded, cooperative batches
and measured codec costs, with hysteresis against direction changes. Do not simply
remove the delay and recreate the CPU/cancellation problems fixed in v1.1.2.

The newer MPX reports strengthen this recommendation: continue useful production
from the existing cache boundary while capacity and foreground latency allow it,
instead of requiring every interaction to become quiet before any warming occurs.
Keep FrameViewer's condition-variable wakeups where no work is useful; MPX's
observed 10 ms sleep is not a reason to replace event-driven waiting with polling.
FrameViewer's window currently prefers roughly 75% of capacity in the motion
direction; MPX's 0.5 balancing decision is different. Test both and adaptive ratios
on forward motion, reversals, and scattered bookmark revisits. Do not replace the
LRU with a universal 50/50 rule just because the recovered wrapper passes 0.5.

Evidence: [request reset](D:/CodeProject/trie_frameviewer/src/media/MediaWorker.cpp:207),
[prefetch gate](D:/CodeProject/trie_frameviewer/src/media/MediaWorker.cpp:245),
[directional window](D:/CodeProject/trie_frameviewer/src/media/MediaWorker.cpp:72).

### 3. New frame identities allocate new GPU textures

Each new `VideoFrame` identity gets a new `Textures` object and three `PlaneTexture`
objects. On upload, each creates an R8 texture. The 64 MiB GPU LRU retains about
21 full-HD frames or five 4K frames by ideal payload accounting. Once traversal
exceeds that retained set, first visits keep allocating and evicting texture sets.

MPX's inspected helper instead reuses compatible texture storage and updates its
contents. FrameViewer should combine that idea with its useful revisit cache:
recycle compatible evicted texture sets, while retaining selected recent frames.
Pool and LRU storage must share one budget. Only reuse resources after outstanding
GPU work no longer needs their previous contents. Keep allocation and upload work
on the render thread, and keep R8 planes and disabled mipmaps.

This is a well-supported optimization candidate, not proof that texture creation
currently dominates decoder latency. Qt's upload batches can defer operations;
CPU enqueue time alone does not measure GPU transfer completion. See the
[Qt 6.8 resource-update documentation](https://doc.qt.io/qt-6.8/qrhiresourceupdatebatch.html).

### 4. Frame construction still copies and sometimes transforms substantial data

The common path already skips swscale and RGB conversion. It still allocates owned
Y/U/V plane buffers and copies rows. Hardware NV12 additionally splits interleaved
UV with a scalar per-sample loop. The hardware path downloads before these copies,
then the renderer uploads again.

After profiling, consider bounded CPU buffer reuse and a native NV12 representation
with Y plus interleaved UV textures. This can remove CPU UV splitting, but requires
shader and format changes plus hardware-support/fallback tests. Do not retain an
unbounded number of decoder `AVFrame` surfaces to avoid copies: that can exhaust
the decoder's surface pool and move the memory problem into VRAM.

Unsupported/10-bit inputs currently take the RGBA conversion path. At 4K, RGBA8
payloads alone fit only about 16 frames in 512 MiB. Native high-bit-depth planes
and correct transfer-function/tone-mapping handling are a separate significant
extension, especially for HDR footage. The existing Bt.2020 YUV matrix is not a
complete HDR pipeline.

Evidence: [hardware download and format branches](D:/CodeProject/trie_frameviewer/src/media/Decoder.cpp:263),
[UV split/copy](D:/CodeProject/trie_frameviewer/src/media/VideoFrame.cpp:30),
[current display shader](D:/CodeProject/trie_frameviewer/src/presentation/shaders/yuv.frag:1).

## Recommended implementation order

| Order | Change | Expected benefit | Effort / main risk |
|---|---|---|---|
| 0 | Per-stage tracing and matched hot/cold workloads | Establishes whether queueing, decoding, transfer, or rendering dominates | Small–medium; instrumentation must not add readbacks to normal playback |
| 1 | Independent cache-hit delivery and capacity-driven, interaction-aware prefetch | Cached targets need not wait for a miss; continue filling from cache boundaries during playback | Medium–large; races, stale-frame overwrite, cache thrash, and decoder starvation |
| 2 | Duplicate-presentation guards and budgeted texture recycling | Less redundant scene-graph work and less allocation churn on first visits | Small–medium; texture lifetime and GPU synchronization |
| 3 | Optional reduced-resolution RAM previews for difficult 4K sources | More cached seconds per byte and cheaper uploads; 4K-to-1080p payload reduction is 4× | Medium–large; decoding reference pictures may still cost the same; exact-source refinement is required |
| 4 | PTS/duration/keyframe index with stable source-frame ordinals | Correct VFR selection and better seek decisions than the fixed 256-frame threshold | Large; indexing cost and preservation of existing bookmark meaning |
| 5 | Native NV12/high-bit-depth handling, then evaluate D3D11 interop | Less copying/splitting; potential removal of GPU→CPU→GPU transfers | Large; color correctness, shared-device/fence handling, adapter changes, surface budgets |
| 6 | Optional bounded seek-friendly disk proxies, only if needed | Can help repeated distant seeks beyond RAM coverage | Large; encoding CPU cost, storage I/O, quality, invalidation, and quota enforcement |

Step 2's duplicate guard should compare frame identity without preventing resize,
zoom, color-state, or cache-budget updates. The controller already suppresses some
same-frame input and playback requests; this extends an existing design rather
than introducing suppression for the first time. Cache-coverage notifications
can also be emitted only when coverage/accounting actually changes.

For step 1, make interaction policy explicit. MPX's guide says viewport mouse
scrubbing attempts sequential frame display, while FrameViewer's fast drag path
coalesces targets and accepts intermediate previews. A smoother-looking sequence
can also be further behind the cursor. Compare both preview cadence and target
lag. Offer a precision stepping/scrubbing policy alongside fast timeline seeking,
without promising that every frame can be shown at arbitrary pointer speed.

Also profile large-cache clearing and source-switch first-frame latency. A bounded
retirement queue is worth considering if destruction dominates a transition, but
account active + retired + externally pinned payloads and enforce an overall
retention limit. Do not copy MPX's five-entry batch size as a timing guarantee or
allow repeated source switches to accumulate hidden memory. FrameViewer currently
clears on its video worker, so this concerns worker availability/open latency,
not proof of synchronous GUI-thread destruction.

For step 4, keep the current efficient ordinal-keyed RAM cache if appropriate;
add a `MediaTimeline` mapping rather than replacing it merely to imitate a binary
search. Keep bookmark source-frame identity stable. Build indexes asynchronously
and validate them against the source fingerprint.

For step 3, use a representation key including source, ordinal/PTS, preview size,
and color format. Keep preview and exact pixels under a shared memory budget.
Refine the released target from the source and use source frames for exports.
Downscaling a decoded picture saves storage/upload work, but does not by itself
eliminate compressed-video preroll decoding.

For step 6, respect the user's rejection of an approximately 11.9 GiB raw-frame
disk cache. Default to no disk proxy, or make it explicitly opt-in with a hard
user-configurable total limit, for example 256–512 MiB. Use seek-friendly reduced
representations/segments, evict by source fingerprint and age, and expose actual
disk use and Clear Cache. Do not claim that every full video will fit the quota.
Proxy creation should not block opening media or consume full CPU while scrubbing.
An ordinary long-GOP low-bitrate MP4 is not automatically an effective random-seek
proxy; GOP/decode distance matters along with file size.

## Further improvements after the primary video work

- Replace nominal 4 ms playback polling with scheduling around the next useful
  frame deadline/presentation opportunity if tracing shows meaningful wakeup cost.
  Keep the audio clock as master when active. MPX's observed 40 ms event-service
  threshold is neither a playback rate nor a value to copy into our frame timer.
- Keep reusable conversion contexts in fallback rendering where worthwhile.
  Decoder's fallback already uses `sws_getCachedContext`; `VideoFrame::toImage`
  creates a context per conversion, mostly relevant to software fallback/tests.
- Replace the beginning-only PCM cache with a sliding audio window for long clips.
  The 64 MiB prefix holds about 350 seconds at 48 kHz stereo S16. Preserve the
  tested Windows audio stop/restart workaround while changing cache scheduling.
- Prefer policy/service components such as `FrameRepository`, `MediaTimeline`,
  `PrefetchPlanner`, and `TexturePool` over more scheduling logic in QML or an
  enlarged `PlayerController`. Names here are proposals, not existing classes.

## Verification plan and evidence limits

Existing [v1.1.2 measurements](D:/CodeProject/trie_frameviewer/docs/performance-seek-1.1.2.md)
showed serial seek p95 improving from 254–264 ms to 68–78 ms, about half the process
CPU time, and a roughly unchanged 697–700 MiB peak working set on one 1080p source.
Those EngineBench/ScrubBench numbers measure decoded delivery, not screen latency,
and compare two FrameViewer builds, not MPX. The separate UI benchmark in
`PerformanceBench` forces native readbacks, which alter the workload.

Extend tracing from input through cache lookup/queue wait, seek/preroll, pixel
construction, hardware download, GUI delivery, upload submission, and the frame
queued for presentation. `QQuickWindow::frameSwapped` is a useful presentation-queue
marker, not a guarantee that the physical monitor has displayed those pixels;
correlate it with the actual rendered frame identity. Qt also warns that
`grabWindow` harms performance, so keep readback-based correctness tests separate.
See [Qt 6.8 QQuickWindow](https://doc.qt.io/qt-6.8/qquickwindow.html#frameSwapped).

For each build and MPX comparison, use identical media, window size, monitor/GPU,
audio state, hardware mode, and explicitly defined video/audio/GPU budgets. Record
MPX binary hash/version and distinguish cold OS file-cache runs from warm runs.
Run workloads sequentially without concurrent builds or encodes.

Measure warm-cache adjacent stepping, cached targets during a cold decode, cold
random GOP jumps, forward/backward drag, rapid direction reversal, loop boundaries,
and final release. Include 1080p/4K SDR, long-GOP H.264/VP9/HEVC, VFR, high bit depth,
and image sequences. Report p50/p95/p99, longest preview gap, delivered-frame count,
target lag, CPU time, allocation count, copied/uploaded bytes, and separately
defined RAM/VRAM/shared-memory readings.

Initial acceptance goals, not current measured results:

- Cache hits are independent of foreground miss duration; target warm-cache
  input-to-presentation-queue p95 within two refresh intervals on the test setup.
- After pool warmup at a stable resolution, recycled textures substantially reduce
  allocation count without increasing the configured retained GPU payload budget.
- Exact release still matches the source frame and rejects stale results.
- Cache limits, project/bookmark meaning, playback/pause preservation, reverse
  playback, loop behavior, and sound through seek/replay/mute remain correct.
- Speculative work demonstrably improves coverage without increasing cancellation
  churn or making CPU/fan behavior worse on the representative traces.

Revised next implementation scope: add tracing, then prioritize independent cache
hits and capacity-driven cache-boundary prefetch. Texture recycling and duplicate
presentation suppression follow as a separate optimization. Measure deferred
release only where cache-clear/source-switch costs justify it. Retain the compact
shared-YUV architecture and the useful retention of scattered visited frames.
Decide on GPU interop or bounded proxies from the remaining measured bottleneck,
rather than treating higher VRAM use or a long video's smoothness as proof of a
particular hidden MPX technique.
