#!/usr/bin/env python3
"""prepare_shaders.py <install>: merge the shaders and pipelines recorded by
earlier sessions (data/shader_corpus/runs) into the pack and pipeline list the
game builds at startup (the same as play.sh). Failures only cost stutter."""
import glob, os, subprocess, sys
from pathlib import Path

install = Path(sys.argv[1]); corpus = install / "data/shader_corpus"
(corpus / "runs").mkdir(parents=True, exist_ok=True)
shaders = sorted(glob.glob(str(corpus / "runs/*_shaders.json")))
pipelines = sorted(glob.glob(str(corpus / "runs/*_pipelines.json")))
log = open(corpus / "last_build.log", "w")
compiler = install / ("oot3d_native_pica_aot_compiler.exe" if os.name == "nt" else "oot3d_native_pica_aot_compiler")
if shaders and compiler.exists():
    print("Preparing shaders recorded in earlier sessions...", flush=True)
    arguments = [str(compiler)]
    if (corpus / "all_shaders.json").exists():
        arguments += ["--inventory", str(corpus / "all_shaders.json")]
    for path in shaders:
        arguments += ["--inventory", path]
    arguments += ["--pack", str(corpus / "mm3d.o3ps.tmp"), "--manifest", str(corpus / "pack_receipt.json"),
                  "--merged-inventory", str(corpus / "all_shaders.json.tmp")]
    if subprocess.run(arguments, stdout=log, stderr=log).returncode == 0:
        os.replace(corpus / "mm3d.o3ps.tmp", corpus / "mm3d.o3ps")
        os.replace(corpus / "all_shaders.json.tmp", corpus / "all_shaders.json")
        for path in shaders:
            os.remove(path)
if pipelines:
    previous = [str(corpus / "all_pipelines.json")] if (corpus / "all_pipelines.json").exists() else []
    merge = Path(__file__).with_name("merge_pipelines.py")
    if subprocess.run([sys.executable, str(merge), str(corpus / "all_pipelines.json"), *previous, *pipelines],
                      stdout=log, stderr=log).returncode == 0:
        for path in pipelines:
            os.remove(path)
