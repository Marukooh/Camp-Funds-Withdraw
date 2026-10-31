#!/usr/bin/env python3
"""Decrypt (and LZ4-decompress) Crimson Desert PAZ entries that are ChaCha20 encrypted. Stdlib only.

Usage: cddecrypt.py <shard_dir> <out_dir> <substring> [...]
Key/nonce come from Lookup3(hashlittle) of the lower-case basename, initval 0x000C5EDE
(format documented by fitzforge/UltimateCameraMod wiki "Dev PAZ and PAMT Archives").
"""
import importlib.util
import struct
import sys
from pathlib import Path

_spec = importlib.util.spec_from_file_location('cdextract', Path(__file__).with_name('cdextract.py'))
cdextract = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cdextract)

M = 0xFFFFFFFF


def rot(x, k):
    return ((x << k) | (x >> (32 - k))) & M


def hashlittle(data, initval):
    length = len(data)
    a = b = c = (0xDEADBEEF + length + initval) & M
    off = 0
    while length > 12:
        a = (a + struct.unpack_from('<I', data, off)[0]) & M
        b = (b + struct.unpack_from('<I', data, off + 4)[0]) & M
        c = (c + struct.unpack_from('<I', data, off + 8)[0]) & M
        a = (a - c) & M; a ^= rot(c, 4); c = (c + b) & M
        b = (b - a) & M; b ^= rot(a, 6); a = (a + c) & M
        c = (c - b) & M; c ^= rot(b, 8); b = (b + a) & M
        a = (a - c) & M; a ^= rot(c, 16); c = (c + b) & M
        b = (b - a) & M; b ^= rot(a, 19); a = (a + c) & M
        c = (c - b) & M; c ^= rot(b, 4); b = (b + a) & M
        off += 12
        length -= 12
    if length == 0:
        return c
    tail = data[off:] + b'\0' * (12 - length)
    a = (a + struct.unpack_from('<I', tail, 0)[0]) & M
    b = (b + struct.unpack_from('<I', tail, 4)[0]) & M
    c = (c + struct.unpack_from('<I', tail, 8)[0]) & M
    c ^= b; c = (c - rot(b, 14)) & M
    a ^= c; a = (a - rot(c, 11)) & M
    b ^= a; b = (b - rot(a, 25)) & M
    c ^= b; c = (c - rot(b, 16)) & M
    a ^= c; a = (a - rot(c, 4)) & M
    b ^= a; b = (b - rot(a, 14)) & M
    c ^= b; c = (c - rot(b, 24)) & M
    return c


def chacha20(key, nonce, data):
    const = (0x61707865, 0x3320646E, 0x79622D32, 0x6B206574)
    k = struct.unpack('<8I', key)
    n = struct.unpack('<4I', nonce)
    counter = n[0]
    out = bytearray(len(data))

    def qr(s, a, b, c, d):
        s[a] = (s[a] + s[b]) & M; s[d] = rot(s[d] ^ s[a], 16)
        s[c] = (s[c] + s[d]) & M; s[b] = rot(s[b] ^ s[c], 12)
        s[a] = (s[a] + s[b]) & M; s[d] = rot(s[d] ^ s[a], 8)
        s[c] = (s[c] + s[d]) & M; s[b] = rot(s[b] ^ s[c], 7)

    for block in range(0, len(data), 64):
        state = list(const) + list(k) + [counter & M, n[1], n[2], n[3]]
        w = state[:]
        for _ in range(10):
            qr(w, 0, 4, 8, 12); qr(w, 1, 5, 9, 13); qr(w, 2, 6, 10, 14); qr(w, 3, 7, 11, 15)
            qr(w, 0, 5, 10, 15); qr(w, 1, 6, 11, 12); qr(w, 2, 7, 8, 13); qr(w, 3, 4, 9, 14)
        stream = struct.pack('<16I', *((w[i] + state[i]) & M for i in range(16)))
        chunk = data[block:block + 64]
        out[block:block + len(chunk)] = bytes(x ^ y for x, y in zip(chunk, stream))
        counter += 1
    return bytes(out)


def params(name):
    seed = hashlittle(Path(name).name.lower().encode(), 0x000C5EDE)
    nonce = struct.pack('<I', seed) * 4
    base = seed ^ 0x60616263
    deltas = (0, 0x0A0A0A0A, 0x0C0C0C0C, 0x06060606, 0x0E0E0E0E, 0x0A0A0A0A, 0x06060606, 0x02020202)
    key = b''.join(struct.pack('<I', base ^ d) for d in deltas)
    return key, nonce


def decode(blob, name, orig, compression):
    plain = chacha20(*params(name), blob)
    if compression == 2:
        plain = cdextract.lz4_block(plain, orig)
    return plain


def main():
    shard, out_dir, needles = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3:]
    out_dir.mkdir(parents=True, exist_ok=True)
    for path, paz, offset, comp, orig, compression in cdextract.parse_pamt(shard / '0.pamt'):
        if not any(needle in path for needle in needles):
            continue
        with open(shard / f'{paz}.paz', 'rb') as handle:
            handle.seek(offset)
            blob = handle.read(comp)
        try:
            data = decode(blob, path, orig, compression)
        except Exception:
            data = cdextract.lz4_block(blob, orig) if compression == 2 else blob
        (out_dir / Path(path).name).write_bytes(data)
        print(f'{len(data):>10} {path}')


if __name__ == '__main__':
    main()
