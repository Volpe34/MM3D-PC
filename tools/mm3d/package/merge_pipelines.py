#!/usr/bin/env python3
"""Merges TriAevum pipeline inventories (manifest format) into one manifest:
identical pipelines once, their evidence (ids, revisions, counts) combined.
Usage: merge_pipelines.py OUTPUT INPUT [INPUT ...]"""
import json, os, sys

EVIDENCE = ("canonical_pipeline_ids", "settings_revisions", "observation_count")

def main():
    output, inputs = sys.argv[1], sys.argv[2:]
    header, merged = None, {}
    for path in inputs:
        try:
            with open(path) as f:
                document = json.load(f)
        except (OSError, ValueError) as error:
            print(f"skipping {path}: {error}", file=sys.stderr)
            continue
        if header is None:
            header = {k: v for k, v in document.items() if k not in ("pipelines", "pipeline_count")}
        elif document.get("descriptor_schema_version") != header.get("descriptor_schema_version"):
            print(f"skipping {path}: other descriptor schema", file=sys.stderr)
            continue
        for entry in document.get("pipelines", []):
            key = json.dumps({k: v for k, v in entry.items() if k not in EVIDENCE}, sort_keys=True)
            if key not in merged:
                merged[key] = entry
                continue
            kept = merged[key]
            for field in ("canonical_pipeline_ids", "settings_revisions"):
                if field in entry or field in kept:
                    kept[field] = sorted(set(kept.get(field, [])) | set(entry.get(field, [])))
            if "observation_count" in entry:
                kept["observation_count"] = kept.get("observation_count", 0) + entry["observation_count"]
    if header is None:
        return 1
    document = dict(header)
    document["pipelines"] = [merged[key] for key in sorted(merged)]
    document["pipeline_count"] = len(merged)
    temporary = output + ".tmp"
    with open(temporary, "w") as f:
        json.dump(document, f, indent=1, sort_keys=True)
    os.replace(temporary, output)
    print(f"{len(merged)} pipelines in {output}")
    return 0

sys.exit(main())
