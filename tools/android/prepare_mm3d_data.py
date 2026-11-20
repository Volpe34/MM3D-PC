#!/usr/bin/env python3
"""Stage the private MM3D game data for the Android app (never an APK payload).

usage: prepare_mm3d_data.py --installation <PC MM3D folder> --output <staging dir>

Copies only what the Android launch needs from a finished PC Setup (the process
manifest, the ROM-derived code/exheader/romfs files and resources) and writes
TriAevum.android.launch.json, mirroring the arguments of tools/mm3d/package/linux/Play.sh.
The title module itself is packaged in the APK, so --title-plugin is not written here.
"""
import argparse
import json
from pathlib import Path
import shutil


def contained(root: Path, path: Path) -> Path:
    resolved = path.resolve()
    if not resolved.is_relative_to(root.resolve()):
        raise ValueError(f"Input escapes the prepared installation: {path}")
    return resolved


def prepare(installation: Path, output: Path, max_seconds: int | None) -> None:
    root, output = installation.resolve(), output.resolve()
    repository = Path(__file__).resolve().parents[2]
    if output == repository or output.is_relative_to(repository):
        raise ValueError("Private game data must remain outside the source repository")
    if output == root or output.is_relative_to(root) or root.is_relative_to(output):
        raise ValueError("Use a separate staging directory outside the installation")
    manifest_path = root / "process-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    inputs = [manifest_path, root / "resources"]
    for key in ("code_bin_path", "exheader_path", "romfs_image_path"):
        inputs.append(contained(root, manifest_path.parent / manifest["source"][key]))
    if any(not path.exists() for path in inputs):
        raise FileNotFoundError("Prepared installation is missing a required input")
    output.mkdir(parents=True, exist_ok=False)
    for source in inputs:
        destination = output / source.relative_to(root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            shutil.copytree(source, destination)
        else:
            shutil.copy2(source, destination)
    target = {
        "--a32-process-manifest": "${profile_dir}/process-manifest.json",
        "--resource-root": "${profile_dir}/resources",
        "--renderer": "nri",
        "--ui-profile": "oot3d",  # what the PC Play.sh uses for MM3D
        "--config": "${profile_dir}/data/config/TriAevum.android.json",
        "--save-data": "${profile_dir}/data/savedata",
        "--renderer-cache-directory": "${profile_dir}/data/cache/renderer",
        "--output": "${profile_dir}/data/runtime-state.json",
        "--gameplay-timing": "native30_no_interpolation",
        "--presentation-rate": "30",
        "--width": "640", "--height": "360",
    }
    if max_seconds:
        target["--max-seconds"] = str(max_seconds)
    config = output / "data/config/TriAevum.android.json"
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text("{}\n", encoding="utf-8")
    (output / "TriAevum.android.launch.json").write_text(json.dumps({
        "format": "oot3d_native_game_launch_profile_v1",
        "arguments": [item for pair in target.items() for item in pair],
        "path_scopes": {key: "relative" for key, value in target.items()
                        if value.startswith("${profile_dir}/")},
    }, indent=2) + "\n", encoding="utf-8")
    print(f"Private MM3D inputs staged at {output}; do not publish or put in APK assets.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installation", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-seconds", type=int, default=None,
                        help="optional bound for benchmark runs; omit for normal play")
    args = parser.parse_args()
    prepare(args.installation, args.output, args.max_seconds)


if __name__ == "__main__":
    main()
