#!/usr/bin/env python3
"""add.py <pkg>...: download MSYS2 CLANG64 packages (and their dependencies) into ./clang64."""
import json, re, sys, os, urllib.request, subprocess
os.chdir(os.path.dirname(os.path.abspath(__file__)))
pkgs=json.load(open('index.json'))
prov={}
for n,p in pkgs.items():
    for pr in p['provides']: prov[re.split('[<>=]',pr)[0]]=n
skip={'mingw-w64-clang-x86_64-'+x for x in ['libc++','libunwind','cc-libs','compiler-rt','libwinpthread','winpthreads','crt','headers']}
installed=set(open('installed.txt').read().split()) if os.path.exists('installed.txt') else set()
todo=['mingw-w64-clang-x86_64-'+w for w in sys.argv[1:]]; seen=set()
while todo:
    n=re.split('[<>=]',todo.pop())[0]
    n=n if n in pkgs else prov.get(n,n)
    if n in seen or n in skip or n not in pkgs: continue
    seen.add(n); todo+=pkgs[n]['deps']
os.makedirs('pkgs',exist_ok=True)
for n in sorted(seen-installed):
    f=pkgs[n]['file']; path='pkgs/'+f
    if not os.path.exists(path): urllib.request.urlretrieve('https://repo.msys2.org/mingw/clang64/'+f, path)
    subprocess.run(['tar','--zstd','-xf',path,'-C','.'],check=True); print('added',n)
open('installed.txt','w').write('\n'.join(sorted(installed|seen)))
