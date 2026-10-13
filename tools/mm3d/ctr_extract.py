#!/usr/bin/env python3
"""Inspect a decrypted .3ds/.cci and extract ExHeader, decompressed code.bin and raw RomFS."""
import hashlib, struct, sys, os

def lz_decompress(c):
    bt, extra = struct.unpack_from('<II', c, len(c) - 8)
    out = bytearray(c + bytes(extra))
    src = len(c) - (bt >> 24); stop = len(c) - (bt & 0xffffff); dst = len(out)
    while src > stop:
        src -= 1; ctl = out[src]
        for _ in range(8):
            if src <= stop: break
            if ctl & 0x80:
                src -= 2; v = out[src] | (out[src + 1] << 8)
                for _ in range((v >> 12) + 3):
                    b = out[dst + (v & 0xfff) + 2]; dst -= 1; out[dst] = b
            else:
                src -= 1; dst -= 1; out[dst] = out[src]
            ctl = (ctl << 1) & 0xff
    return bytes(out)

def main(path, outdir=None):
    f = open(path, 'rb'); h = f.read(0x200)
    assert h[0x100:0x104] == b'NCSD', 'not an NCSD image'
    base = struct.unpack_from('<I', h, 0x120)[0] * 0x200
    f.seek(base); n = f.read(0x200)
    flags = n[0x188:0x190]
    info = dict(title_id='%016x' % struct.unpack_from('<Q', n, 0x118)[0],
                product=n[0x150:0x160].rstrip(b'\0').decode(),
                remaster_version=None, encrypted=not (flags[7] & 4))
    f.seek(base + 0x200); exh = f.read(0x800)
    info['remaster_version'] = struct.unpack_from('<H', exh, 0xe)[0]
    info['name'] = exh[:8].rstrip(b'\0').decode()
    for k, v in info.items(): print(f'{k}: {v}')
    if info['encrypted'] or outdir is None: return
    os.makedirs(outdir, exist_ok=True)
    open(os.path.join(outdir, 'exheader.bin'), 'wb').write(exh)
    ea, es = struct.unpack_from('<II', n, 0x1a0)
    f.seek(base + ea * 0x200); exefs = f.read(es * 0x200)
    for i in range(10):
        name, off, size = struct.unpack_from('<8sII', exefs, i * 16)
        name = name.rstrip(b'\0').decode()
        if not name: continue
        data = exefs[0x200 + off:0x200 + off + size]
        if name == '.code':
            if exh[0xd] & 1: data = lz_decompress(data)
            name = 'code'
        open(os.path.join(outdir, name + '.bin'), 'wb').write(data)
        print(f'{name}.bin {len(data)} sha256={hashlib.sha256(data).hexdigest()}')
    ra, rs = struct.unpack_from('<II', n, 0x1b0)
    f.seek(base + ra * 0x200)
    with open(os.path.join(outdir, 'romfs.bin'), 'wb') as o:
        left = rs * 0x200; hs = hashlib.sha256()
        while left:
            chunk = f.read(min(left, 1 << 24)); o.write(chunk); hs.update(chunk); left -= len(chunk)
    print(f'romfs.bin {rs*0x200} sha256={hs.hexdigest()}')
    for nm, o in (('text', 0x10), ('ro', 0x20), ('data', 0x30)):
        a, p, s = struct.unpack_from('<III', exh, o); print(f'{nm}: addr={a:#x} size={s:#x}')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
