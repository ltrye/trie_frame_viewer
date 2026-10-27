# Playback optimization measurements — 2026-10-27

Compared v1.3.0 (`6d9edc7`) benchmark binaries, saved before the rebuild, with
the v1.4.0 implementation. Raw local results are under
`artifacts/performance/playback-optimization/`. Media, generated outputs, and
benchmark executables are intentionally excluded from Git.

## Conditions and limits

Same Windows machine, 512 MiB retained video payload budget, automatic decoding,
and sequential runs without concurrent builds, tests or encodes. Engine and scrub
benchmarks exclude rendering and audible output. Each engine run warms the cache
before serial/random seeking; its percentiles mix hits and misses. They are not
pure cold-seek measurements. OS file caches were not flushed; these are warm-file
tests, not controlled cold-storage trials. Two 4K pairs are too few for statistical
claims. No matched MPX runtime benchmark was performed.

- `long-4000.mp4`: generated 1280×720, 4,000 frames, software decoding.
- `E:\3D\Ref\film\venom1.mkv`: 3840×2160, VP9 8-bit, 24 fps, 1,034 frames,
  Opus stereo, 49,293,323 bytes; D3D11VA selected. This is the available user
  footage, not the earlier unavailable `zzzz2.mp4`.
- `reference-test.mp4`: generated 640×360, 24 fps, 96 frames, AAC; visible Qt
  Direct3D11 presentation trace, separate from the engine comparisons.

SHA-256 fingerprints (the files themselves are not distributed):

| Media | SHA-256 |
|---|---|
| `venom1.mkv` | `3a4c2d5a4c6a31e3aff71dac1e859bb5ef401628a5c6cb7babc8733f32e7db10` |
| `long-4000.mp4` | `6e94b1355e042938406e4abd09f534987a461b82e65238945ade16d11d85a1c9` |
| `reference-test.mp4` | `a003f4aed3d226318ed1fbc4480eef1adeda9c8b64f774b95139342758409280` |

## Engine comparisons

| Workload / run | Baseline p50 / p95 ms | Updated p50 / p95 ms | Baseline / updated peak working set MiB |
|---|---:|---:|---:|
| 720p 4,000 frames | 0.123 / 207.7 | 0.052 / 210.8 | 564.2 / 560.5 |
| 4K VP9 pair 1 | 0.178 / 489.4 | 0.011 / 622.3 | 650.3 / 639.0 |
| 4K VP9 pair 2 | 0.515 / 714.2 | 0.007 / 389.8 | 646.9 / 639.6 |

Cache-hit delivery improved; the blocked-worker regression test separately proves
that it no longer waits behind an in-flight cold foreground decode. Tail latency
is mixed, including a slower first 4K run. Do not infer a general cold-seek speedup.
4K CPU time was 4.03 → 4.36 seconds in pair 1 and 4.25 → 3.67 in pair 2; there is
no established broad CPU reduction. Video cache payload stayed near 510 MiB.

## Continuous sweep comparisons

Three-second forward/backward/random sweeps send approximately 193–195 inputs.
Every final released frame matched separately decoded source pixels in both builds.

| Workload / direction | Baseline / updated previews | Baseline / updated max gap ms | Baseline / updated release-to-exact ms |
|---|---:|---:|---:|
| 720p forward | 35 / 34 | 227.9 / 221.0 | 140.9 / 141.9 |
| 720p backward | 23 / 24 | 220.2 / 233.2 | 4.9 / 5.4 |
| 720p random | 27 / 27 | 215.2 / 232.2 | 0.742 / 0.011 |
| 4K forward | 11 / 9 | 881.0 / 784.9 | 531.8 / 741.7 |
| 4K backward | 10 / 9 | 450.4 / 574.0 | 29.0 / 58.4 |
| 4K random | 12 / 13 | 467.7 / 773.4 | 398.0 / 641.7 |

This pass does not demonstrate improved uncached 4K sweeping. The release targets
cache availability, prefetch starvation, and allocation reuse; it leaves expensive
keyframe preroll and GPU download/CPU repacking intact. Further work needs isolated
cold-seek traces and repetitions before selecting proxy or GPU-native changes.

## Visible cached presentation trace

After warming frames 0–10 in RAM and GPU, a visible native window received
30 cached inputs at 60 ms intervals, followed by an exact release, once muted and
once audible. Trace was complete: 1,631 events, zero dropped. Each measurement
window recorded 31 hits/acceptances, 30 presentation queue markers, zero decoded
pixel bytes, and zero texture upload bytes. Identical final-frame submissions
were acknowledged without requiring another render.

| Audio | Input → GUI p50 / p95 ms | Input → presentation queue p50 / p95 ms | Audio setup p95 / max ms | Longest presentation gap ms |
|---|---:|---:|---:|---:|
| Muted | 21.15 / 21.71 | 31.92 / 32.47 | 0.007 / 0.107 | 66.2 |
| Audible | 21.21 / 21.93 | 31.90 / 33.15 | 18.77 / 355.09 | 396.2 |

Presentation means Qt frameSwapped, not physical scanout. Per-request percentiles
alone conceal the audible stall: it occurred while a pending input had already
been presented and delayed subsequent input dispatch. Audio sink creation/teardown
on the GUI thread remains a priority for the next change. No claim of inaudible or
stall-free scrubbing is warranted.

## GPU allocation correctness

The native renderer test displayed 48 unique 320×180 YUV frames with a 1 MiB GPU
payload limit: 12 texture sets were created and 36 were recycled. Every rendered
color matched its expected image. The budget stayed within 1 MiB. This validates
reuse and pixels on this D3D11 system; it is not a measured fan-noise reduction.

## Reproduction

Release regression checks passed: optimization (8 QtTest results), core (28),
audio (5), export (9), renderer (4), and the full native UI smoke test. Counts
include QtTest suite setup/cleanup. Tests used workspace TEMP/TMP because the
sandbox default temporary path was unwritable. The isolated registry test and
MSBuild required normal Windows process/registry access outside the sandbox.

Use `scripts/benchmark.ps1` with `tfv_engine_benchmark.exe` and
`tfv_scrub_benchmark.exe`, the same media and `-CacheMiB 512 -DecodeMode auto`.
Preserve a baseline binary before rebuilding. Do not run performance tests while
building or encoding. For visible tracing:

```powershell
$env:TFV_TRACE_PATH = 'D:\CodeProject\trie_frameviewer\artifacts\trace.json'
$env:TFV_SMOKE_FOCUS = 'presentation-cache'
# Use the development bundle (smoke support is excluded from production).
& .\out\TrieFrameViewer\TrieFrameViewer.exe .\artifacts\reference-test.mp4 --smoke-test .\artifacts\presentation
python scripts\analyze_playback_trace.py artifacts\trace.json --output artifacts\trace-summary.json
```

Set trace paths in an existing writable directory and close the app normally to
flush. Clear both environment variables before normal use. Collection is opt-in,
bounded to 32,768 events, and does not read back video frames. Trace output includes
source-open generation identifiers and timing, not media names or pixel data.
