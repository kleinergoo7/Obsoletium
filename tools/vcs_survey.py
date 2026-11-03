#!/usr/bin/env python3
"""vcs_survey.py -- B0 spike: census of shader models across .vcs files.

Decodes v4 (raw dictionary combos) and v6 (LZMA blocks) containers,
reports the SM version of every combo found and the smallest SM3 combo
(for the hand-translation proof).
"""
import glob
import lzma
import struct
import sys

VER_TOKENS = {
    0xFFFE0101: 'vs_1_1', 0xFFFF0101: 'ps_1_1',
    0xFFFF0102: 'ps_1_2', 0xFFFF0103: 'ps_1_3', 0xFFFF0104: 'ps_1_4',
    0xFFFE0104: 'vs_1_4',
    0xFFFE0200: 'vs_2_0', 0xFFFF0200: 'ps_2_0',
    0xFFFF0201: 'ps_2_x',
    0xFFFE0300: 'vs_3_0', 0xFFFF0300: 'ps_3_0',
}

LZMA_F = [{'id': lzma.FILTER_LZMA1, 'dict_size': 0x40000,
           'lc': 3, 'lp': 0, 'pb': 2}]


def combos_v4(d):
    total, dyn = struct.unpack_from('<ii', d, 4)
    out = []
    for i in range(dyn):
        o, s = struct.unpack_from('<ii', d, 28 + 8 * i)
        if o >= 0 and s > 0 and o + s <= len(d):
            out.append(d[o:o + s])
    return out


def records_v6(d):
    """Static combo records of a v5/v6 container: [(staticComboID, payload)].

    The payload is the decompressed program block of that static combo; it
    holds one program per dynamic combo (see parse_entries in vcs_pack.py).
    The record table is at offset 28 (m_nStaticComboID, m_nFileOffset).
    """
    nstat = struct.unpack_from('<I', d, 20)[0]
    out = []
    for i in range(nstat):
        cid, foff = struct.unpack_from('<II', d, 28 + 8 * i)
        if cid == 0xFFFFFFFF or foff + 4 > len(d):
            continue
        w = struct.unpack_from('<I', d, foff)[0]
        kind, size = w & 0xC0000000, w & 0x3FFFFFFF
        payload = d[foff + 4:foff + 4 + size]
        if kind == 0x40000000 and len(payload) >= 17 and payload[:4] == b'LZMA':
            actual = struct.unpack('<I', payload[4:8])[0]
            try:
                dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=LZMA_F)
                out.append((cid, dec.decompress(payload[17:], max_length=actual)))
            except Exception:
                pass
        elif kind == 0x80000000:
            out.append((cid, payload))
    return out


def aliases_v6(d):
    """Duplicate static combo aliases: [(aliasID, sourceID)] (v6 only)."""
    nstat = struct.unpack_from('<I', d, 20)[0]
    p = 28 + 8 * nstat
    if p + 4 > len(d):
        return []
    n = struct.unpack_from('<i', d, p)[0]
    p += 4
    out = []
    for _ in range(max(0, n)):
        if p + 8 > len(d):
            break
        alias, src = struct.unpack_from('<II', d, p)
        p += 8
        out.append((alias, src))
    return out


def combos_v6(d):
    nstat = struct.unpack_from('<I', d, 20)[0]
    out = []
    for i in range(nstat):
        cid, foff = struct.unpack_from('<II', d, 28 + 8 * i)
        if cid == 0xFFFFFFFF or foff + 4 > len(d):
            continue
        w = struct.unpack_from('<I', d, foff)[0]
        kind, size = w & 0xc0000000, w & 0x3fffffff
        payload = d[foff + 4:foff + 4 + size]
        if kind == 0x40000000 and len(payload) >= 17 and payload[:4] == b'LZMA':
            actual = struct.unpack('<I', payload[4:8])[0]
            try:
                dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=LZMA_F)
                out.append(dec.decompress(payload[17:], max_length=actual))
            except Exception:
                pass
        elif kind == 0x80000000:
            out.append(payload)
    return out


def main():
    from collections import Counter
    vers = Counter()
    smallest = None
    for f in sorted(glob.glob('*.vcs')):
        d = open(f, 'rb').read()
        if len(d) < 28:
            continue
        v = struct.unpack_from('<i', d, 0)[0]
        combos = combos_v4(d) if v == 4 else combos_v6(d) if v in (5, 6) else []
        for c in combos:
            if len(c) < 4:
                continue
            t = struct.unpack_from('<I', c, 0)[0]
            name = VER_TOKENS.get(t)
            if name:
                vers[name] += 1
                if name in ('vs_3_0', 'ps_3_0') and (
                        smallest is None or len(c) < len(smallest[1])):
                    smallest = (f, c)
    print(dict(vers))
    if smallest:
        print('smallest SM3: %s %d bytes' % (smallest[0], len(smallest[1])))
        open('smallest_sm3.bin', 'wb').write(smallest[1])


if __name__ == '__main__':
    main()
