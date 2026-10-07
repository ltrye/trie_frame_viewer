# Performance measurements

Measured on 2026-10-07 on this Windows 11 workstation (Intel Core i7-14700F,
NVIDIA GeForce RTX 5060 Ti), using Release builds, Qt 6.8.3, and FFmpeg 8.1.
These are development measurements on generated test patterns. They are not a
comparison with Keyframe MPX or a guarantee for arbitrary footage.

## Videos used

All four clips live in `artifacts/` and use H.264, 8-bit YUV420, and constant FPS.
The picture is FFmpeg's moving `testsrc2` pattern; audio, where present, is a sine
wave. No user footage was used. The 271-frame clip matches the resolution and
frame count shown in the user's screenshot, not the actual encoding or content.

| File | Resolution | FPS | Frames | Duration | GOP interval | Audio |
|---|---:|---:|---:|---:|---:|---|
| reference-test.mp4 | 640 × 360 | 24 | 96 | 4 s | 48 | AAC |
| reference-720.mp4 | 1280 × 720 | 30 | 271 | 9.03 s | 240 | None |
| seek-stress.mp4 | 1920 × 1080 | 30 | 360 | 12 s | 240 | AAC |
| long-4000.mp4 | 1280 × 720 | 30 | 4,000 | 133.33 s | 240 | None |

`scripts/generate_performance_fixtures.ps1` generates missing fixtures. The
verification script supplies the small and 1080p clips with its own encoder
settings. Do not regenerate media between before/after measurements: encoder
preset, GOP structure, and picture content affect results. The SHA-256 manifest
in `artifacts/performance/fixtures.json` identifies the files used for this run.

## What changed

- Common YUV420/NV12 sources retain packed Y/U/V planes, rather than RGBA copies.
  This uses 62.5% fewer pixel bytes for even-sized 8-bit 4:2:0 frames.
- GPU shaders convert planes during display. Recently displayed textures have a
  separate LRU budget, defaulting to 64 MiB. Shader tests cover BT.601, BT.709,
  BT.2020, full/limited range, odd dimensions, reuse, and budget eviction.
- The RAM LRU keeps visited regions until byte pressure requires eviction. It
  biases eviction toward a direction-aware playhead window, retains useful frames
  decoded while seeking, and fills batches in ascending order to reuse GOP work.
- Scrub requests coalesce over 16 ms, with an exact request on mouse release.
  Speculative prefetch waits for 80 ms of quiet after foreground requests.
  Software decoding defaults to two threads rather than automatic thread counts.
- D3D11VA decoding is available with software fallback. Auto selects hardware for
  supported 1080p+ clips; smaller clips use CPU decoding. Forced GPU decoding
  reduced CPU time on the long 720p prototype but increased cold seek latency
  and process/GPU allocations. The preference allows either choice and takes
  effect when opening footage again. This policy needs broader GPU testing.

Hardware decoding currently downloads decoded surfaces into the compact RAM
cache, and presentation uploads planes. It is not a zero-copy path. Direct GPU
interoperability remains future work.

## Engine benchmark

The helper uses the actual controller, decoder, cache, and scrub scheduler without
QML rendering. It opens a source, waits three seconds for background warming,
requests 24 deterministic positions (`n * 7919 % frameCount`) and returns through
them in reverse order, then issues rapid scrub requests for three seconds. The
final exact seek targets the middle of the clip. No audio output is enabled.

This serial trace deliberately revisits frames and can have very high cache-hit
rates. In particular, the 360-frame trace concentrates near the end of the clip.
Its percentile is not a uniform cold-seek benchmark. The rapid trace and final
seek can miss the cache. These limits matter when interpreting the table.

Latest reports: `artifacts/performance/final-engine-auto/`.

| Clip | Decoder | Retained pixels, MiB | Peak working set, MiB | CPU seconds | Serial p95, ms | Final seek, ms |
|---|---|---:|---:|---:|---:|---:|
| 96 frames, 360p | CPU | 31.6 | 62.5 | 0.28 | 0.30 | 0.48 |
| 271 frames, 720p | CPU | 357.3 | 399.6 | 0.64 | 0.36 | 0.09 |
| 360 frames, 1080p | D3D11VA | 510.2 | 676.4 | 3.89 | 0.30 | 360.08 |
| 4,000 frames, 720p | CPU | 511.5 | 572.7 | 13.50 | 262.08 | 94.52 |

The full 271-frame clip fits into the default 512 MiB RAM cache. Its old RGBA pixel
representation would need about 952.7 MiB; the new representation needs 357.3 MiB.
The cache budgets cover retained pixels, not total process memory. Decoder buffers,
in-flight/presented frames, audio, QML, allocator overhead, and driver allocations
are additional. Working set and private commit are different Windows measures.

## Native application benchmark

The same deterministic trace also runs in the QML app in a hidden native window.
Each delivered frame forces `grabWindow()` so the native graphics path executes.
This introduces substantial synchronous GPU readback overhead and can recreate
scene-graph resources when the window is not exposed. Its seek latency and texture
retention are not representative of ordinary visible-window playback. The native
rapid trace uses immediate seeks; the engine trace exercises mouse coalescing.

Use these results as a repeatable instrumentation workload only. CPU seconds count
all process threads, so they can exceed wall seconds. Background activity and
readback dominate the small fully cached fixture, which showed no CPU improvement.
Fan speed or noise was not measured.

| Clip | Before CPU s | After CPU s | Before peak working set, MiB | After peak working set, MiB |
|---|---:|---:|---:|---:|
| 96 frames, 360p | 15.16 | 16.86 | 242.0 | 153.8 |
| 271 frames, 720p | 100.81 | 17.41 | 703.2 | 508.3 |
| 360 frames, 1080p | 80.23 | 12.45 | 793.9 | 779.2 |
| 4,000 frames, 720p | 101.11 | 39.22 | 716.6 | 684.3 |

Before reports are in `baseline-native-basic/`, `baseline-native-720/`, and
`baseline/` (1080p and 4,000 frames). The old basic/debug reports in `baseline/`
timed out before native readback was added; exclude those reports. After reports
are in `final-native-auto/`. All runs use the same respective media files and a
512 MiB frame budget. There is one run per table entry, not a statistical study.

The after 1080p run peaked at about 85.6 MiB of local GPU allocations and 85.4 MiB
of non-local GPU allocations. Hardware decoding increased these allocations and
private commit; it does not automatically reduce every memory measure. Renderer
tests separately verify texture retention in an exposed window.

## Repeating the measurement

```powershell
.\scripts\generate_performance_fixtures.ps1
python scripts\dev.py build
python scripts\dev.py deploy
.\scripts\benchmark.ps1 -Label native-auto -DecodeMode auto
.\scripts\benchmark.ps1 -AppPath out\TrieFrameViewer\tfv_engine_benchmark.exe -Label engine-auto -DecodeMode auto
```

The engine helper and app benchmark implementation require `BUILD_TESTING=ON`.
Use `-DecodeMode software` or `hardware` to compare backends. Use `-MediaPaths`
with local video paths to test actual footage. Labels create separate report
directories; repeating a label overwrites its reports. Run workloads sequentially.

## Remaining performance work

4,000 full-resolution 720p YUV frames require about 5.15 GiB of decoded pixels.
A 512 MiB cache retains about 388 of them. Hardware decoding and texture reuse
help computation and display, but cannot make that whole clip fit into this budget.
The long test still has noticeable cold-seek delays. A compressed or disk-backed
full-clip tier is not implemented in this pass.

Next, measure the user's actual comparison videos, including codec, bit depth,
resolution, frame count, GOP spacing, warm-up time, and the same seek trace in
each player. That evidence should guide full-clip caching, proxy resolution, or
direct GPU interoperability. MPX's higher VRAM usage and installed DLLs alone do
not establish its internal caching strategy.
