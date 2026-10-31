#!/usr/bin/env python3
"""Extract files from a Crimson Desert 2.04+ archive shard (N/0.pamt + N/*.paz). Stdlib only.

Usage: cdextract.py <shard_dir> <out_dir> <substring> [<substring> ...]
Writes every file whose full path contains one of the substrings to <out_dir>/<basename>.

2.04 .pamt layout: magic, paz_count, 8 bytes, per-paz (hash, size[, separator]); folder and node
sections are u32 size + records (varint delta, u8 len, name) with parent = own offset - delta
(delta 0 = root); u32 folder_count, u32 hash, folder records (folder_ref, first_file, count, hash);
20-byte file records (node_ref, paz_offset, comp_size, orig_size, flags) where paz = flags & 0xFF,
compression = (flags >> 16) & 0xF (2 = LZ4 block), extension index = flags >> 24 into a trailing
table (u32 size + NUL-separated names).
"""
import struct
import sys
from pathlib import Path


def varint(buf, pos):
    value = shift = 0
    while True:
        byte = buf[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value, pos
        shift += 7


def parse_trie(section):
    """Return {offset: full name} for a (varint delta, u8 len, name) section."""
    names, pos = {}, 0
    while pos < len(section):
        start = pos
        delta, pos = varint(section, pos)
        length = section[pos]
        pos += 1
        name = section[pos:pos + length].decode('utf-8', 'replace')
        pos += length
        names[start] = (names[start - delta] if delta else '') + name
    return names


def lz4_block(src, size):
    out, pos = bytearray(), 0
    while pos < len(src):
        token = src[pos]
        pos += 1
        literal = token >> 4
        if literal == 15:
            while True:
                extra = src[pos]
                pos += 1
                literal += extra
                if extra != 255:
                    break
        out += src[pos:pos + literal]
        pos += literal
        if pos >= len(src):
            break
        offset = src[pos] | (src[pos + 1] << 8)
        pos += 2
        match = token & 15
        if match == 15:
            while True:
                extra = src[pos]
                pos += 1
                match += extra
                if extra != 255:
                    break
        match += 4
        start = len(out) - offset
        for index in range(match):
            out.append(out[start + index])
    if len(out) != size:
        raise ValueError(f'LZ4 size mismatch: {len(out)} != {size}')
    return bytes(out)


def parse_pamt(path):
    data = Path(path).read_bytes()
    pos = 4
    paz_count = struct.unpack_from('<I', data, pos)[0]
    pos += 4 + 8
    pos += paz_count * 8 + max(paz_count - 1, 0) * 4
    sections = []
    for _ in range(2):
        size = struct.unpack_from('<I', data, pos)[0]
        pos += 4
        sections.append(data[pos:pos + size])
        pos += size
    folders, nodes = (parse_trie(section) for section in sections)
    folder_count = struct.unpack_from('<I', data, pos)[0]
    pos += 8
    folder_records = [struct.unpack_from('<4I', data, pos + 16 * i) for i in range(folder_count)]
    pos += 16 * folder_count
    # The extension table follows the file records: find the 20-byte step whose u32 equals the rest.
    table_pos = pos
    while table_pos + 4 <= len(data) and struct.unpack_from('<I', data, table_pos)[0] != len(data) - table_pos - 4:
        table_pos += 20
    extensions = data[table_pos + 4:].split(b'\0')
    files = [struct.unpack_from('<5I', data, pos + 20 * i) for i in range((table_pos - pos) // 20)]
    entries = []
    for folder_ref, first, count, _ in folder_records:
        folder = folders.get(folder_ref, '')
        for node_ref, offset, comp, orig, flags in files[first:first + count]:
            ext = extensions[flags >> 24].decode() if (flags >> 24) < len(extensions) else ''
            stem = nodes.get(node_ref, f'?{node_ref}')
            # Either side may carry the separating dot.
            name = f"{stem.rstrip('.')}.{ext.lstrip('.')}" if ext else stem
            entries.append((f'{folder}/{name}'.strip('/'), flags & 0xFF, offset, comp, orig, (flags >> 16) & 0xF))
    return entries


def main():
    shard, out_dir, needles = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3:]
    out_dir.mkdir(parents=True, exist_ok=True)
    found = 0
    for path, paz, offset, comp, orig, compression in parse_pamt(shard / '0.pamt'):
        if not any(needle in path for needle in needles):
            continue
        with open(shard / f'{paz}.paz', 'rb') as handle:
            handle.seek(offset)
            blob = handle.read(comp)
        if compression == 2:
            blob = lz4_block(blob, orig)
        elif comp != orig:
            raise ValueError(f'{path}: unsupported compression {compression}')
        (out_dir / Path(path).name).write_bytes(blob)
        print(f'{len(blob):>10} {path}')
        found += 1
    if not found:
        sys.exit('no matching files')


if __name__ == '__main__':
    main()
