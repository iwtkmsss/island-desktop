"""Cache exact RSDS PDBs for the isolated taskbar prototype (Python 3 stdlib)."""
import argparse
import pathlib
import struct
import urllib.request
import uuid


def pdb_identity(path):
    data = path.read_bytes()

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError(f"Truncated PE: {path}")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("Not a PE image")
    pe, = unpack("<I", 60)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    machine, sections = unpack("<HH", pe + 4)
    optional_size, = unpack("<H", pe + 20)
    optional = pe + 24
    magic, = unpack("<H", optional)
    if machine != 0x8664 or magic != 0x20B or optional_size < 168:
        raise ValueError("Prototype requires an AMD64 PE32+ image")
    section_data = [unpack("<IIII", optional + optional_size + i * 40 + 8)
                    for i in range(sections)]

    def file_offset(rva, length):
        for _, base, raw_size, raw_offset in section_data:
            if base <= rva and rva + length <= base + raw_size:
                offset = raw_offset + rva - base
                if offset + length <= len(data):
                    return offset
        raise ValueError("Debug data outside PE file sections")

    debug_rva, debug_size = unpack("<II", optional + 112 + 6 * 8)
    if debug_size % 28 or debug_size > 1024 * 1024:
        raise ValueError("Invalid debug directory")
    debug = file_offset(debug_rva, debug_size)
    for entry in range(debug, debug + debug_size, 28):
        _, _, _, _, kind, length, _, raw = unpack("<IIHHIIII", entry)
        if kind != 2:
            continue
        if length < 25 or raw + length > len(data):
            raise ValueError("Invalid CodeView record")
        if data[raw:raw + 4] != b"RSDS":
            continue
        guid = uuid.UUID(bytes_le=data[raw + 4:raw + 20]).hex.upper()
        age, = unpack("<I", raw + 20)
        record = data[raw + 24:raw + length]
        if b"\0" not in record:
            raise ValueError("Unterminated PDB name")
        name = record.split(b"\0", 1)[0].decode("utf-8").replace("\\", "/").split("/")[-1]
        if not name.lower().endswith(".pdb") or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-" for c in name):
            raise ValueError("Unexpected PDB filename")
        return name, guid + format(age, "X")
    raise ValueError(f"No RSDS record: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", type=pathlib.Path)
    parser.add_argument("--cache", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1] / "artifacts/taskbar-symbols")
    args = parser.parse_args()
    for image in args.images:
        name, key = pdb_identity(image)
        target = args.cache / name / key / name
        url = f"https://msdl.microsoft.com/download/symbols/{name}/{key}/{name}"
        if not target.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_suffix(".download")
            try:
                print(f"Downloading {url}", flush=True)
                with urllib.request.urlopen(url, timeout=60) as response, temporary.open("wb") as output:
                    while chunk := response.read(1024 * 1024):
                        output.write(chunk)
                if temporary.stat().st_size < 32:
                    raise ValueError("Empty symbol response")
                temporary.replace(target)
            finally:
                temporary.unlink(missing_ok=True)
        print(f"{image}: {target.resolve()}", flush=True)
    print("PDB identity must also be checked by the prototype before installing hooks.")


if __name__ == "__main__":
    main()
