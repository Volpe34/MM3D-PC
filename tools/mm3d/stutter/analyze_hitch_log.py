#!/usr/bin/env python3
"""analyze_hitch_log.py <log> [<log> ...] [--markdown]: summarizes MM3D_HITCH_LOG files.

For each log: frame-time distribution (exact, from the FT lines), then per pass
(the F9 markers split the run), then the hitch frames' time split into
non-overlapping causes. Several logs give a side-by-side comparison.
"""
import math
import re
import sys
from collections import OrderedDict, defaultdict
from pathlib import Path

BOUNDS = (25, 50, 100, 250, 1000)

# Leaf causes: each hitch frame's time is split into these, subtracting nested
# events from their parents so nothing is counted twice (hitch_log.h has the tree).
CAUSES = OrderedDict([
    ("guest", "Guest game code (incl. its file reads)"),
    ("hd_wait", "Wait for HD textures (StartFrame)"),
    ("shader_compile", "Shader compile (shaderc, render thread)"),
    ("spirv_io", "SPIR-V cache read/lookup"),
    ("pipe_create", "Pipeline creation (driver, render thread)"),
    ("pipe_async_wait", "Wait for a worker-built pipeline"),
    ("pipe_lookup", "Pipeline lookup/identity (memo miss path)"),
    ("pipe_prewarm", "Pipeline prewarm pass"),
    ("tex_decode", "Native texture decode"),
    ("tex_hd_resolve", "HD texture hash/lookup"),
    ("tex_grass", "Grass texture observer"),
    ("tex_alloc", "Texture image + memory allocation"),
    ("tex_upload", "Texture upload recording"),
    ("tex_other", "Other new-texture work"),
    ("draw_texture", "Draw texture binding (no new texture)"),
    ("draw_command", "Draw command recording"),
    ("draw_upload", "Draw geometry/uniform upload"),
    ("draw_target", "Render target state/creation"),
    ("draw_variant", "Shader variant selection"),
    ("draw_shader", "Shader program lookup (excl. SPIR-V)"),
    ("draw_other", "Other per-draw work"),
    ("probe_draw", "Preparation probe (new draws)"),
    ("probe_wait", "Wait for shaders compiled in parallel"),
    ("grass", "Interactive grass"),
    ("shadow", "Directional shadow map"),
    ("fence_wait", "Frame fence / present wait (GPU)"),
    ("acquire", "Swapchain acquire"),
    ("idle_wait", "Device/queue idle wait"),
    ("tex_evict", "Texture eviction"),
    ("present", "Present / submit (excl. pacing)"),
    ("pacing", "Frame pacing wait"),
    ("other", "Unattributed"),
])


def parse_fields(line):
    fields = {}
    for token in line.split()[1:]:
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def event(fields, name):
    """(count, ms, mb) of one event field."""
    value = fields.get(name)
    if value is None:
        return 0, 0.0, 0.0
    parts = value.split("/")
    count = int(parts[0])
    ms = float(parts[1]) if len(parts) > 1 else 0.0
    mb = float(parts[2].rstrip("MB")) if len(parts) > 2 else 0.0
    return count, ms, mb


def ms(fields, name):
    return event(fields, name)[1]


def split_causes(fields):
    f = lambda key: float(fields.get(key, 0.0))
    m = lambda name: ms(fields, name)
    causes = {}
    causes["guest"] = f("guest_ms")
    causes["hd_wait"] = m("hd_wait")
    causes["shader_compile"] = m("shader_compile")
    causes["spirv_io"] = max(0.0, m("spirv") - m("shader_compile"))
    causes["pipe_create"] = m("pipe_create")
    causes["pipe_async_wait"] = m("pipe_async_wait")
    causes["pipe_lookup"] = max(0.0, m("pipe_uncached") - m("pipe_create") - m("pipe_async_wait"))
    causes["pipe_prewarm"] = m("pipe_prewarm")
    causes["tex_decode"] = m("tex_decode")
    causes["tex_hd_resolve"] = m("tex_hd_resolve")
    causes["tex_grass"] = m("tex_grass")
    causes["tex_alloc"] = m("tex_alloc")
    causes["tex_upload"] = max(0.0, m("tex_image") - m("tex_alloc"))
    # tex_create contains decode, HD resolve, grass and its own tex_image.
    created = m("tex_create")
    causes["tex_other"] = max(0.0, created - m("tex_decode") - m("tex_hd_resolve") - m("tex_grass")
                              - min(created, m("tex_image")))
    texture_work = created + max(0.0, m("tex_image") - min(created, m("tex_image")))
    causes["draw_texture"] = max(0.0, m("d_texture") - texture_work)
    causes["draw_command"] = m("d_command")
    causes["draw_upload"] = m("d_upload")
    causes["draw_target"] = m("d_target")
    causes["draw_variant"] = m("d_variant")
    causes["draw_shader"] = max(0.0, m("d_shader") - m("spirv"))
    staged = sum(m(name) for name in ("d_variant", "d_shader", "d_texture", "d_grass_surface", "d_target",
                                      "d_reflection", "d_temporal", "d_pipeline", "d_upload", "d_descriptor",
                                      "d_command"))
    # Probed draws run inside "draw" too (probe_draw); their shader-cache
    # reads are in spirv_io and their waits in probe_wait.
    probe = m("probe_draw")
    causes["probe_draw"] = max(0.0, probe - min(probe, m("spirv")))
    causes["probe_wait"] = m("probe_wait")
    causes["draw_other"] = max(0.0, m("draw") - staged - m("grass") - m("shadow") - probe - m("probe_wait"))
    causes["grass"] = m("grass")
    causes["shadow"] = m("shadow")
    causes["fence_wait"] = m("fence_wait")
    causes["acquire"] = m("acquire")
    causes["idle_wait"] = m("idle_wait")
    causes["tex_evict"] = m("tex_evict")
    causes["present"] = max(0.0, f("present_ms") - f("pacing_ms"))
    causes["pacing"] = f("pacing_ms")
    causes["other"] = f("other_ms")
    # Shader work found inside d_shader / pipelines inside d_pipeline are already above.
    return causes


def percentile(values, fraction):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, math.ceil(fraction * len(ordered)) - 1))
    return ordered[index]


class Segment:
    def __init__(self, name):
        self.name = name
        self.frames = []          # (host frame, ms)
        self.hitches = []         # field dicts

    def stats(self, threshold):
        values = [value for _, value in self.frames]
        result = OrderedDict()
        result["frames"] = len(values)
        result["seconds"] = sum(values) / 1000.0
        result["p50"] = percentile(values, 0.50)
        result["p95"] = percentile(values, 0.95)
        result["p99"] = percentile(values, 0.99)
        result["p99.9"] = percentile(values, 0.999)
        result["max"] = max(values) if values else 0.0
        for bound in BOUNDS:
            result[f">{bound}"] = sum(1 for value in values if value > bound)
        hitch_values = [value for value in values if value >= threshold]
        result["hitches"] = len(hitch_values)
        result["hitch_ms"] = sum(hitch_values)
        return result


def load(path):
    """Frames and hitches split into passes by the F9 markers. FT lines are
    written in batches (up to 0.5 s late), so frames are assigned to a pass by
    the host frame each MARK carries, not by line order."""
    threshold = 25.0
    frames, hitches, marks = [], [], []
    summary, events, waits = {}, {}, []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("# MM3D_HITCH_LOG"):
            match = re.search(r"threshold_ms=([0-9.]+)", line)
            if match:
                threshold = float(match.group(1))
        elif line.startswith("FT "):
            parts = line.split()
            first = int(parts[1])
            frames.extend((first + index, float(value)) for index, value in enumerate(parts[2:]))
        elif line.startswith("HITCH "):
            hitches.append(parse_fields(line))
        elif line.startswith("MARK "):
            marks.append(parse_fields(line))
        elif line.startswith("SUMMARY "):
            summary = parse_fields(line)
        elif line.startswith("EVENT "):
            parts = line.split()
            events[parts[1]] = parse_fields(" ".join(parts[1:]))
        elif line.startswith("WAIT "):
            waits.append(line[5:])
    segments = [Segment("before first F9")]
    segments += [Segment(f"pass {mark.get('n')} (F9 at {float(mark.get('t', 0)):.0f} s)") for mark in marks]
    mark_frames = [int(mark.get("host", 0)) for mark in marks]

    def index_of(host):
        index = 0
        while index < len(mark_frames) and host > mark_frames[index]:
            index += 1
        return index

    for frame in frames:
        segments[index_of(frame[0])].frames.append(frame)
    for hitch in hitches:
        segments[index_of(int(hitch.get("host", 0)))].hitches.append(hitch)
    return threshold, segments, summary, events, waits


def cause_totals(hitches):
    totals = defaultdict(float)
    dominant = defaultdict(int)
    for hitch in hitches:
        causes = split_causes(hitch)
        for key, value in causes.items():
            totals[key] += value
        # The frame's largest cause other than pacing.
        top = max((value, key) for key, value in causes.items() if key != "pacing")
        dominant[top[1]] += 1
    return totals, dominant


def format_table(headers, rows):
    widths = [max(len(str(header)), *(len(str(row[index])) for row in rows)) if rows else len(str(header))
              for index, header in enumerate(headers)]
    line = "| " + " | ".join(str(header).ljust(width) for header, width in zip(headers, widths)) + " |"
    rule = "|" + "|".join("-" * (width + 2) for width in widths) + "|"
    body = ["| " + " | ".join(str(cell).ljust(width) for cell, width in zip(row, widths)) + " |" for row in rows]
    return "\n".join([line, rule, *body])


def describe(path):
    threshold, segments, summary, events, waits = load(path)
    every = Segment("whole run")
    for segment in segments:
        every.frames.extend(segment.frames)
        every.hitches.extend(segment.hitches)
    print(f"== {Path(path).name}")
    if not every.frames:
        print("   no frames recorded")
        return None
    stat_rows = []
    for segment in [every] + (segments if len(segments) > 1 else []):
        if not segment.frames:
            continue
        stats = segment.stats(threshold)
        compiles = sum(event(h, "shader_compile")[0] for h in segment.hitches)
        pipelines = sum(event(h, "pipe_create")[0] for h in segment.hitches)
        stat_rows.append([segment.name, stats["frames"], f"{stats['seconds']:.0f}", f"{stats['p50']:.1f}",
                          f"{stats['p99']:.1f}", f"{stats['p99.9']:.1f}", f"{stats['max']:.0f}",
                          stats[">25"], stats[">50"], stats[">100"], stats[">250"],
                          f"{stats['hitch_ms']:.0f}", compiles, pipelines])
    print(format_table(["segment", "frames", "s", "p50", "p99", "p99.9", "max", ">25", ">50", ">100", ">250",
                        "hitch ms", "compiles*", "pipelines*"], stat_rows))
    print("   (* in hitch frames)")
    totals, dominant = cause_totals(every.hitches)
    hitch_ms = sum(float(h.get("ms", 0)) for h in every.hitches)
    rows = []
    for key, value in sorted(totals.items(), key=lambda item: -item[1]):
        if value < 0.5 and dominant.get(key, 0) == 0:
            continue
        rows.append([CAUSES[key], f"{value:.0f}", f"{100.0 * value / hitch_ms:.1f}%" if hitch_ms else "-",
                     dominant.get(key, 0)])
    print(f"   hitch frames (>= {threshold:.0f} ms): {len(every.hitches)}, {hitch_ms:.0f} ms in total")
    print(format_table(["cause", "ms in hitch frames", "share", "frames where largest"], rows))
    worst = sorted(every.hitches, key=lambda h: -float(h.get("ms", 0)))[:10]
    if worst:
        print("   worst frames:")
        for hitch in worst:
            causes = split_causes(hitch)
            top = sorted(((value, key) for key, value in causes.items() if value >= 1.0), reverse=True)[:4]
            detail = ", ".join(f"{key} {value:.0f}" for value, key in top)
            extras = []
            for name in ("shader_compile", "pipe_create", "tex_create", "hd_image", "file_read"):
                count, _, mb = event(hitch, name)
                if count:
                    extras.append(f"{name}={count}" + (f"/{mb:.0f}MB" if mb >= 1 else ""))
            print(f"     t={float(hitch.get('t', 0)):7.1f}s {float(hitch.get('ms', 0)):7.1f} ms: {detail}"
                  + (f"  [{' '.join(extras)}]" if extras else "") + (f"  wait {hitch['wait_site']}" if "wait_site" in hitch else ""))
    if waits:
        print("   idle waits: " + "; ".join(waits))
    return every.stats(threshold), totals, hitch_ms


def main():
    paths = [argument for argument in sys.argv[1:] if not argument.startswith("--")]
    if not paths:
        print(__doc__)
        return 2
    results = []
    for path in paths:
        if not Path(path).is_file():
            print(f"== {path}: no log (the game did not start, or MM3D_HITCH_LOG was not set)")
            continue
        result = describe(path)
        if result:
            results.append((Path(path).name, result))
        print()
    if len(results) > 1:
        print("== comparison")
        headers = ["run", "p50", "p99", "p99.9", "max", ">25", ">50", ">100", ">250", "hitch ms"]
        rows = [[name, f"{s['p50']:.1f}", f"{s['p99']:.1f}", f"{s['p99.9']:.1f}", f"{s['max']:.0f}", s[">25"],
                 s[">50"], s[">100"], s[">250"], f"{s['hitch_ms']:.0f}"] for name, (s, _, _) in results]
        print(format_table(headers, rows))
        keys = [key for key in CAUSES if any(totals.get(key, 0) >= 1.0 for _, (_, totals, _) in results)]
        rows = [[CAUSES[key]] + [f"{totals.get(key, 0):.0f}" for _, (_, totals, _) in results] for key in keys]
        rows.sort(key=lambda row: -max(float(cell) for cell in row[1:]))
        print(format_table(["cause (ms in hitch frames)"] + [name for name, _ in results], rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
