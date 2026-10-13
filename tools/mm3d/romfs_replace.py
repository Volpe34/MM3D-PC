"""Replace or add files in a raw RomFS (IVFC level-3) image copy.

New contents are appended at the end of the image. Replaced files get their
entries repointed; added files get a new entry in a file table copied to the
end of the image (linked into their directory's file list and the name hash
table). The game reads RomFS through FS without hash checks here.

Usage: romfs_replace.py <romfs.bin> <out.bin> <romfs path>=<file> ...
"""
import shutil
import struct
import sys

LEVEL3 = 0x1000


def name_hash(parent: int, name: str, buckets: int) -> int:
    value = parent ^ 123456789
    for char in name.encode("utf-16le"), :
        pass
    for index in range(0, len(name.encode("utf-16le")), 2):
        code = int.from_bytes(name.encode("utf-16le")[index:index + 2], "little")
        value = ((value >> 5) | (value << 27)) & 0xFFFFFFFF
        value ^= code
    return value % buckets


def main() -> int:
    source, output = sys.argv[1], sys.argv[2]
    replacements = dict(item.split("=", 1) for item in sys.argv[3:])
    shutil.copyfile(source, output)
    with open(output, "r+b") as image:
        image.seek(LEVEL3)
        header = list(struct.unpack("<10I", image.read(0x28)))
        dir_hash, dir_hash_size, dir_meta, dir_meta_size = header[1:5]
        file_hash, file_hash_size, file_meta, file_meta_size, data_offset = header[5:10]
        image.seek(LEVEL3 + dir_meta)
        dirs = bytearray(image.read(dir_meta_size))
        image.seek(LEVEL3 + file_meta)
        files = bytearray(image.read(file_meta_size))
        image.seek(LEVEL3 + file_hash)
        file_buckets = bytearray(image.read(file_hash_size))

        def dir_path(offset: int) -> str:
            parts = []
            while offset != 0:
                parent, _, _, _, _, length = struct.unpack_from("<6I", dirs, offset)
                parts.append(dirs[offset + 0x18:offset + 0x18 + length].decode("utf-16le"))
                offset = parent
            return "/".join(reversed(parts))

        def append(data: bytes) -> int:
            image.seek(0, 2)
            end = (image.tell() + 0xF) & ~0xF
            image.seek(end)
            image.write(data)
            return end - LEVEL3 - data_offset

        done = set()
        offset = 0
        while offset < len(files):
            parent, _, _, _, _, length = struct.unpack_from("<IIQQII", files, offset)
            name = files[offset + 0x20:offset + 0x20 + length].decode("utf-16le")
            path = (dir_path(parent) + "/" + name).lstrip("/")
            if path in replacements:
                data = open(replacements[path], "rb").read()
                struct.pack_into("<QQ", files, offset + 8, append(data), len(data))
                done.add(path)
                print(f"replaced {path}: {len(data)} bytes")
            offset += 0x20 + ((length + 3) & ~3)

        directories = {}
        offset = 0
        while offset < len(dirs):
            length = struct.unpack_from("<I", dirs, offset + 0x14)[0]
            directories[dir_path(offset)] = offset
            offset += 0x18 + ((length + 3) & ~3)
        added = False
        for path, host_file in replacements.items():
            if path in done:
                continue
            folder, _, name = path.rpartition("/")
            if folder not in directories:
                print("no directory for", path)
                return 1
            parent = directories[folder]
            data = open(host_file, "rb").read()
            entry_offset = len(files)
            encoded = name.encode("utf-16le")
            buckets = len(file_buckets) // 4
            bucket = name_hash(parent, name, buckets)
            head = struct.unpack_from("<I", file_buckets, bucket * 4)[0]
            entry = struct.pack("<IIQQII", parent, 0xFFFFFFFF, append(data), len(data), head,
                                len(encoded)) + encoded + bytes((-len(encoded)) & 3)
            files += entry
            struct.pack_into("<I", file_buckets, bucket * 4, entry_offset)
            # Last in the directory's file list.
            first = struct.unpack_from("<I", dirs, parent + 0x0C)[0]
            if first == 0xFFFFFFFF:
                struct.pack_into("<I", dirs, parent + 0x0C, entry_offset)
            else:
                current = first
                while True:
                    sibling = struct.unpack_from("<I", files, current + 4)[0]
                    if sibling == 0xFFFFFFFF:
                        break
                    current = sibling
                struct.pack_into("<I", files, current + 4, entry_offset)
            added = True
            done.add(path)
            print(f"added {path}: {len(data)} bytes")
        image.seek(LEVEL3 + dir_meta)
        image.write(dirs)
        image.seek(LEVEL3 + file_hash)
        image.write(file_buckets)
        if added:
            # The grown file table moves to the end of the image.
            image.seek(0, 2)
            table = (image.tell() + 0xF) & ~0xF
            image.seek(table)
            image.write(files)
            header[7], header[8] = table - LEVEL3, len(files)
            image.seek(LEVEL3)
            image.write(struct.pack("<10I", *header))
        else:
            image.seek(LEVEL3 + file_meta)
            image.write(files)
        image.seek(0, 2)
        image.write(bytes((-image.tell()) & 0xFFF))
    missing = set(replacements) - done
    if missing:
        print("not done:", sorted(missing))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
