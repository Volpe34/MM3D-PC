# MM3D stutter investigation (branch `fix/stutter`)

Date: 2026-11-08. Base: v0.1.9 (`fc4d8f6`). Machine: Windows 11, i7-10700KF
(8C/16T), 32 GB, RTX 3070 Ti (driver 617.42), game on NVMe. Settings: 2560x1440
borderless, TAA, CACAO AO, single-cascade shadows, grass High, uncapped + VSync,
Henriko's 4K pack (1,624 PNGs, 1.7 GB).

Goal: find what actually causes the hitches and freezes in normal play (new
areas, actors, effects) and remove most of them permanently, with identical
rendering and no title-module ABI change (only `TriAevum.exe` is replaced).

## 1. Method

Measure first, then change one thing at a time and measure again.

- **`MM3D_HITCH_LOG`** (env-gated, `fast/renderer/hitch_log.{h,cpp}`): fixed
  atomic counters per event (count, ns, bytes); RAII scopes with outermost-only
  frame accounting; all idle waits labelled by call site. Each host frame the
  host takes the deltas; every frame time and one line per frame over 25 ms are
  written by a background thread (no per-frame allocation or I/O on the render
  thread), plus an exit summary. F9 writes a pass marker. Events cover: HD wait,
  SPIR-V resolve and compiles (render thread / workers), AOT and artifact
  misses, skipped draws, pipeline memo misses, driver pipeline creation (render
  thread / workers), waits for worker pipelines, prewarm, new-texture decode,
  hashes, HD lookup, grass observer, allocation, upload, HD images, staging
  buffers, samplers, eviction, raw copies, fences, acquire, guest file reads,
  grass, shadows, the renderer's per-draw CPU stages, and the preparation probe.
- **`tools/mm3d/stutter/analyze_hitch_log.py`**: exact percentiles per F9 pass;
  splits hitch frames into non-overlapping causes (nested events subtracted from
  their parents); worst frames; side-by-side comparison of runs.
- **`StutterTest.bat <config> [tag]`**: Play.bat with the log on, on a copy of the
  settings (`asis`, `nowait`, `nohd`, `preload`, `asyncall`, `guestthread`,
  combinable with `+`).
- **`tools/mm3d/stutter/autotour.py`**: unattended, repeatable run. Boots on an
  empty temporary save, warps through 15 main areas (field, Clock Town, swamp,
  mountain, coast, canyon, woods) with the shader tour's walk/attack/roll inputs,
  about 190 s. `--cold-cache` uses an empty SPIR-V/pipeline cache directory;
  `--no-shader-pack` makes every material new (the worst case of a first visit).
  Driver caches were not cleared (stated per result).
- **Visual identity**: `TRIAEVUM_WARP_FRAMES` makes warps frame-exact under
  `--fixed-delta-seconds`, so framebuffer captures of two builds can be compared
  byte for byte.

## 2. Evidence before any change

The player's own logs (v0.1.9, shader tour done):

| Session | Runtime shader compiles | Driver pipelines |
| --- | --- | --- |
| 2 h | 1,102, **104.7 s** in total (95 ms each) | 3,431, 98.6 s in total (incl. 1,576 prewarmed) |
| 3 min (later, corpus merged) | 35, **3.9 s** (112 ms each); 20 of them in one burst (about 3 s) | - |

A 20 s "not advancing" freeze report coincided with a burst of runtime compiles.
New materials keep appearing because each MM3D material (TEV combiner setup,
lighting LUT use, alpha test) generates its own fragment program: the recorded
shader corpus only converges slowly.

## 3. Root causes, ranked by measured cost

1. **Runtime shader compilation on the render thread.** Model draws (Link, NPCs)
   build missing shaders on the spot (they cannot be skipped). Each compile cost
   ~107 ms, and a new area brings 10-20 at once: 1.0-1.4 s frames in the
   worst-case tour. About 80% of each compile was glslang re-initialization:
   every compile created and destroyed its own `shaderc::Compiler`, so glslang
   finalized and rebuilt its built-in symbol tables each time.
2. **Driver pipeline creation**, serial, for the same new materials: ~90 ms each
   when the driver's own cache is cold (seen when the SPIR-V changed), a few ms
   when warm.
3. **Waiting for HD textures** ("Wait for custom textures" on): every frame waits
   until all demanded PNGs are decoded (with mip chains); area entries cost
   85-133 ms frames.
4. **The game's own work on area loads** (guest code incl. file reads): 50-200 ms.
   Not renderer work.
5. Smaller: new native textures (decode about 14 ms per area entry), dedicated
   memory per texture (NRI committed textures = one VMA dedicated allocation
   each), eviction of textures unused for 120 s (up to 105 ms when an area's
   worth is retired at once), upload recording.

Measured and not significant here: device/queue idle waits (about 1 ms per run,
startup/settings only), raw-copy submits (none), staging buffer creation (5-9 ms
per run), sampler creation (34 ms per run, spread), pipeline memo misses (about
4 us each).

## 4. Changes

| Commit | Change | Rendering |
| --- | --- | --- |
| `b498b48` | `MM3D_HITCH_LOG` instrumentation | none (env-gated) |
| `80fa13e`, `b3fb12a` | StutterTest.bat, analyzer | - |
| `42e227b` | Keep glslang initialized between compiles | byte-identical SPIR-V |
| `a14fad0` | `--pica-parametric-tev` usable on MM3D (opt-in only) | default unchanged |
| `c72db71` | autotour.py | - |
| `e9dd974` | Preparation probe: build a frame's new shaders and pipelines in parallel before drawing it | unchanged |
| `af8765e` | `TRIAEVUM_WARP_FRAMES` (test-only) | none |
| `569aba9` | At most 16 texture evictions per frame | unchanged |
| `1eba73b` | Shader compiler warm-up on a worker at start | unchanged |
| `a338210` | Stutter tools: `guestthread`, combinable configs | - |

**Glslang.** A static `shaderc::Compiler` keeps glslang initialized; each
compile still uses its own instance (thread-safe). On 24 real MM3D fragment
shaders: 106.6 -> 22.4 ms per compile, SPIR-V byte-identical (24/24), so the
SPIR-V disk cache and the driver's cache keep matching. (Unoptimized compiles
would be 3 ms but produce different SPIR-V; not used.)

**Preparation probe.** Before a visual frame is replayed, the presentation
scheduler takes the draws whose pipeline / shader / attachment identity it has
not seen. For those, `SubmitPicaDraw` runs in probe mode: it records nothing,
consumes no composition-schedule entry, makes no HUD copies, and only queues
the draw's missing SPIR-V compiles (and, once they exist, its main pipeline) on
the background workers (shader workers cores-2, max 8; pipeline workers up to
10). Up to three rounds; only draws that queued work are probed again. The frame
then waits once for the parallel compiles; the normal pass finds everything
built, and a synchronous compile of a shader still building on a worker waits
for that result instead of compiling it twice. Known draws cost one hash each.

**Warm-up and eviction.** Two tiny shaders compiled on a worker at renderer
start build glslang's symbol tables before the first frame needs a shader.
Texture eviction destroys at most 16 textures per frame and continues next
frame (eviction time in hitch frames 105 -> 4 ms).

**Parametric TEV (not adopted).** The existing developer path turns TEV,
lighting, fog and alpha test into uniform data with 4 fragment programs in
total. On MM3D it cut runtime compiles 172 -> 3, but its uber-shader pipelines
cost the driver ~90 ms each (one 3.7 s frame), the median frame went 5.8 ->
8.8 ms, and the effect instrumentation (AO/TAA/shadows) does not attach to
parametric programs, so it would change the image. Left opt-in.

## 5. Results

Unattended 15-area tour, HD pack, empty app caches, driver cache warm unless
stated. "No pack" = every material new.

| Run | >25 ms | >50 | >100 | >250 | max (ms) | p99.9 | time in hitch frames |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| v0.1.9, no pack, Wait on | 106 | 52 | 40 | 21 | 1431 | 146 | 20.8 s |
| + glslang, no pack, Wait on | 113 | 40 | 20 | 5 | 384 | 64 | 7.8 s |
| + probe, no pack, Wait on | 114 | 43 | 16 | 1 | 358 | 70 | 6.8 s |
| + probe, no pack, Wait off | 107 | 27 | 8 | 1 | 350 | 45 | 5.3 s |
| final, no pack, Wait off | 102 | 32 | 9 | 1 | 363 | 53 | 5.4 s |
| v0.1.9, pack, Wait on | 102 | 35 | 10 | 0 | 181 | 57 | 5.5 s |
| v0.1.9, pack, Wait off | 81 | 12 | 1 | 0 | 178 | 40 | 3.3 s |
| final, pack, Wait off | 82 | 11 | 1 | 0 | 174 | 39 | 3.3 s |
| final, pack, Wait off, game logic thread | 72 | 11 | 1 | 0 | 192 | 39 | 2.9 s |

The single frame over 250 ms in the no-pack runs is the title screen's first
two vertex shaders (about 48 KB of GLSL each, ~160 ms per compile); in normal
play they come from the collected pack. Worst frame after the first 3 s:

| Run | worst gameplay frame |
| --- | ---: |
| v0.1.9, no pack (every material new) | 1431 ms |
| final, no pack, Wait off | 146 ms |
| v0.1.9 / final, pack, Wait off | 82 ms / 82 ms |
| final, pack, Wait off, game logic thread | 65 ms |

Render-thread work for the no-pack tour: shaderc 15.8 s -> 0 (frames wait
0.9 s in total for the parallel compiles); driver pipelines 0.34 s -> 0 (all
built by workers).

With the collected pack (normal play, materials already recorded) the tour
needs no compiles in either build; the final build differs there by the
eviction cap (eviction time in hitch frames 105 -> 4 ms).

**Game logic thread (F1, `Graphics.Presentation.GuestThread`, off by default).**
Same tour with the pack and pop-in: p99 14.9 -> 12.7 ms, frames > 25 ms
82 -> 72, time in hitch frames 3.25 -> 2.94 s, guest time inside hitch frames
1967 -> 1533 ms, worst gameplay frame 82 -> 65 ms. It moves part of the game's
own load work off the presented frame. It stays a player option (off by
default); this branch does not change it.

**Visual identity.** Frame-exact captures (fixed 1/60 s step, HD off so pop-in
timing cannot differ, no shader pack, empty caches so the probe and workers are
exercised): v0.1.9 vs. this branch (final commit), 8/8 title/menu frames and 21/21 frames over
a warp tour of South and North Clock Town, Termina Field, Southern Swamp and the
mountain are byte-identical. Two runs of the same build are also identical
(deterministic baseline).

**Tests.** 434 of 441 runtime unit tests pass, including all 14 Azahar texture
pack tests. The 7 others fail identically on unmodified `fc4d8f6` (two test
targets do not link, the cross-title token check, a grass placement test, a GPU
LOD test, an SDL segfault). Host-side tests for the changed code: draw plan,
draw bridge and visual savestate tests pass; the PICA submission test and the
CTR host test fail identically on `fc4d8f6`.

**Linux.** `TriAevum` builds on Ubuntu 24.04 with clang 18 (the configure
arguments of `scripts/build-linux-runtime.sh`, upscaler SDKs off): 599/599
steps, no errors, at `5b8bd9a`. The draw plan, draw bridge and visual savestate
tests pass there too. Reproduce with `tools/mm3d/stutter/linux/` (Docker).

## 6. Remaining hitches and next steps

- **The game's own area loads** (guest code, 50-200 ms) are now the largest
  cause. The F1 game logic thread reduces them (section 5).
- **First visits still pay for new materials**, now in parallel: the frame waits
  for the slowest compile plus the slowest cold driver pipeline. Removing that
  needs effect instrumentation on the parametric programs, or graphics pipeline
  libraries with the fragment parts prepared ahead (see
  `TRIAEVUM_STUTTERING_RESOLUTION_STRATEGY.md`).
- **HD textures with "Wait for custom textures" on** still cost 85-133 ms per
  area entry. With it off (pop-in), uploads stay within the 32 MB per-frame
  budget. Recommendation: keep it off.
- New native textures: decode (~14 ms per area entry) and one dedicated
  allocation per texture could be parallelized / sub-allocated.
- The SPIR-V cache writes compiled shaders on the render thread (~1.5 ms each).

## 7. Building on Windows

The public repo does not compile as-is: `.gitignore` (`[Dd]ebug/`, `[Ll]og/`)
dropped `include/ship/debug/{Console,CrashHandler}.h`,
`include/fast/debug/GfxDebugger.h`, their `.cpp` files and the legacy
`log/luslog.{h,cpp}`. They were recovered from libultraship (port-maintenance)
into the working tree (not committed). Native Windows build used for this work:
llvm-mingw 20261006 (same as Setup), MSYS2 CLANG64 packages with zlib pinned to
1.3.2 (so the exe imports `zlib1.dll` like the release), CMake/Ninja, DXC
1.9.2609; the configure arguments are those of `scripts/build-windows-mingw.sh`.
