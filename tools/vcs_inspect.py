#!/usr/bin/env python3
"""vcs_inspect.py -- B0 spike: parse Valve .vcs shader containers.

Usage: vcs_inspect.py <file.vcs> [--dump-combo N --out file.bin]

Prints header, static combo records, and identifies the per-combo payload
encoding (LZMA eiher with 13-byte Valve header or raw tokens).
"""
import struct
import sys


def main():
    path = sys.argv[1]
    data = open(path, 'rb').read()
    ver, total, dyn, flags, centroid, nstatic, crc = struct.unpack_from('<iiiIIII', data, 0)
    print('version=%d total=%d dynamic=%d flags=0x%x centroid=0x%x nstatic=%d crc=0x%08x' % (
        ver, total, dyn, flags, centroid, nstatic, crc))
    off = 28
    recs = []
    for i in range(nstatic):
        cid, foff = struct.unpack_from('<II', data, off)
        off += 8
        recs.append((cid, foff))
    print('static records: %d (first 5: %s)' % (len(recs), recs[:5]))
    # combo 0 payload probe
    if recs:
        cid, foff = recs[0]
        blob = data[foff:foff + 64]
        print('combo0 @0x%x head: %s' % (foff, blob[:48].hex(' ')))
        # Valve LZMA: 5-byte props + 8-byte size? or 4-byte uncompressed len?
        # Heuristic scan for known shader token version markers nearby
        for marker in (b'vs_2_0', b'ps_2_0', b'vs_1_1', b'ps_1_1', b'ps_1_4'):
            j = data.find(marker)
            if j >= 0:
                print('ascii marker %r at file offset 0x%x' % (marker, j))
                break
    if '--dump-combo' in sys.argv:
        n = int(sys.argv[sys.argv.index('--dump-combo') + 1])
        out = sys.argv[sys.argv.index('--out') + 1]
        cid, foff = recs[n]
        # next record offset bounds the size; else to EOF
        nxt = min([r[1] for r in recs if r[1] > foff] or [len(data)])
        open(out, 'wb').write(data[foff:nxt])
        print('combo %d: %d bytes -> %s' % (n, nxt - foff, out))


if __name__ == '__main__':
    main()
