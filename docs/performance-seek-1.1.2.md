# Seek and cancellation measurements: v1.1.2

Measured locally on 2026-10-14. This compares the v1.1.1 engine with the v1.1.2
seek changes on the user's actual `zzzz2.mp4`. It is not a comparison with MPX.
All workloads ran sequentially. RAM cache: 512 MiB; decoder threads: 2;
automatic decoding selected D3D11VA. No disk cache or reduced-resolution proxy.

## Source

- H.264 High, YUV420 8-bit, 1920 × 1080, 30 fps.
- 4,113 frames, 137.1 seconds, AAC stereo, keyframes every 90 frames.
- SHA-256: `088f74d00c117ff50fa63c532c9a174aee08ee2bf71338f9b9a74eeb2590d3e2`.
- The media remains outside the repository and installer.

## Existing engine trace

The unchanged EngineBench trace warms for three seconds, seeks to 24 distributed
positions and revisits them in reverse, then submits rapid random scrub inputs
for three seconds and finishes with an exact seek. Two runs per build:

| Metric | v1.1.1 | v1.1.2 |
|---|---:|---:|
| Serial seek p95, ms | 254–264 | 68–78 |
| Serial seek median, ms | 127–132 | 0.06–0.23 |
| Process CPU time, seconds | 9.56–9.69 | 4.39–4.75 |
| Peak working set, MiB | 700–703 | 697–700 |
| Rapid-phase previews delivered | 0–1 | 51–62 |

The new median is dominated by cache hits on revisits: foreground requests now
retain the requested frames without filling the cache with speculative GOP
copies. It is not a uniform cold-seek latency. Rapid deliveries are total frame
deliveries minus the initial, 48 serial, and final frames. Actual raw input counts
were 194–195 per run under the current Windows timer cadence.

Reports: `artifacts/performance/zzzz2-diagnosis/engine-baseline-matched-512*` and
`engine-updated-auto-512*`. Earlier diagnostic reports used a different effective
input timer cadence and are not used in the table above.

## Continuous input traces

A separate helper runs forward, backward, and random traces for three seconds
each, continually submitting inputs without waiting for the requested frame.
Each trace ends with an explicit release and a pixel comparison against a fresh
decoder at the final target. Two runs per build, with build order reversed in
the second round:

| Trace | Old previews during drag | New previews during drag | Old longest preview gap, ms | New longest preview gap, ms |
|---|---:|---:|---:|---:|
| Forward | 0–2 | 42–58 | 2496–3013 | 168–184 |
| Backward | 0–2 | 73–77 | 1724–3010 | 98–100 |
| Random | 0–2 | 80–82 | 1472–3001 | 90–94 |

All final selected frames matched the reference decoded pixels. Input cursors
remain at the newest target while completed previews are allowed to follow it.
Preview delivery is not required to match every mouse event.

The baseline helper links the unchanged v1.1.1 production core with its matching
public headers. The updated helper links the new core. Raw reports and diagnostic
source are in `artifacts/performance/zzzz2-diagnosis/scrub-{baseline,updated}-run*.json`
and `probe/`. These are development artifacts, not installer contents.

`tests/ScrubBench.cpp` now provides the updated trace as a reusable development
benchmark. It records input duration/count, preview gaps, input-to-decoded
latencies, exact release latency, and final pixel validation. Repeat after a
development build/deploy:

```powershell
.\scripts\benchmark.ps1 -AppPath out\TrieFrameViewer\tfv_scrub_benchmark.exe -MediaPaths 'E:\3D\Ref\film\zzzz2.mp4' -Label seek-scrub-check -CacheMiB 512 -DecodeMode auto
```

A follow-up run of this checked-in helper delivered 66 forward, 74 backward, and
78 random previews, with all final pixel checks passing. Its largest forward
gap was 191 ms. Report: `scrub-updated-reproducible/zzzz2.json` beside the raw
comparison artifacts.

## Scope and limits

- These helpers use the real controller, worker, cache, and decoder, with audio
  output muted. They measure decoded-frame delivery, not GPU presentation or
  request-to-screen latency. They perform no per-frame GPU screenshot readback.
- The trace deliberately traverses a 137-second clip in three seconds. Windows
  timer scheduling affects the precise submitted targets and input counts.
- Old successful-delivery percentiles during rapid input can look very low
  because almost no frames arrive. Delivery counts and gaps matter alongside
  latency; the helper reports both.
- Two runs are useful evidence, not a broad hardware study. The single largest
  forward gap still reached about 191 ms including the follow-up; this does not establish MPX parity or
  guarantee stutter-free scrubbing on every source.
- Core regression coverage checks continuous previews, pixel correctness,
  release accuracy, unchanged input cursor, cache limits, transport preservation,
  and rejection of obsolete exact requests. Native UI smoke also covers playback,
  reverse playback, audio restart/looping, bookmarks, and Cinema Mode.
