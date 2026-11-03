#!/usr/bin/env python3
"""vpk_extract.py -- minimal VPK lister/extractor for the B0 shader spike.

Reads Valve VPK v1 (tree at end of file) and v2 (tree in the _dir.vpk
companion, data in numbered archives). No dependency on the engine build;
used to pull shader blobs (.dx90/.sw/.dx80) out of the CS:S content for
format identification before any renderer work.

Usage:
  vpk_extract.py list <file.vpk> [substring]     list entries, optional filter
  vpk_extract.py extract <file.vpk> <entry> <out>  extract one entry to <out>
  vpk_extract.py scan <dir> <suffix>             count entries per VPK archive

Entry paths are reported as ext/path/name (e.g. dx90/shaders/sky_hdr).
"""
import os
import struct
import sys

SIG = 0x55AA1234
NO_ARCHIVE = 0x7FFF


def read_cstr(buf, off):
    end = buf.index(b'\0', off)
    return buf[off:end].decode('utf-8', 'replace'), end + 1


def parse_tree(buf, base_off):
    """Parse a VPK directory tree at base_off. Yields (path, meta) dicts."""
    off = base_off
    n = len(buf)
    while off < n:
        ext, off = read_cstr(buf, off)
        if not ext:
            break
        while True:
            path, off = read_cstr(buf, off)
            if not path:
                break
            while True:
                name, off = read_cstr(buf, off)
                if not name:
                    break
                crc, preload, arch, entry_off, entry_len, term = struct.unpack_from(
                    '<IHHIIH', buf, off)
                off += 18
                assert term == 0xFFFF, hex(term)
                blob = buf[off:off + preload]
                off += preload
                full = '%s/%s/%s.%s' % (path, name, '', ext) if False else None
                yield {
                    'ext': ext, 'path': path, 'name': name,
                    'full': '%s/%s.%s' % (path, name, ext),
                    'crc': crc, 'arch': arch,
                    'entry_off': entry_off, 'entry_len': entry_len,
                    'preload': blob,
                }


def open_vpk(path):
    """Return (entries, datafiles dict arch->open file)."""
    data = open(path, 'rb').read()
    sig, ver, tree_size = struct.unpack_from('<III', data, 0)
    assert sig == SIG, 'bad signature in %s' % path
    if ver == 1:
        tree_off = len(data) - tree_size
        archives = {NO_ARCHIVE: path}
    elif ver == 2:
        tree_off = 7 * 4  # sig/ver/tree + 4 section sizes; tree follows header
        stem = path[:-len('_dir.vpk')] if path.endswith('_dir.vpk') else path[:-4]
        archives = {}
        # discover numbered archives lazily by index map below
        archives['stem'] = stem
        archives[NO_ARCHIVE] = path
    else:
        raise ValueError('unknown vpk version %d' % ver)
    entries = list(parse_tree(data, tree_off))
    return ver, entries, archives, data


def archive_path(archives, arch, data):
    if arch == NO_ARCHIVE or archives.get('stem') is None:
        return archives[arch]
    stem = archives['stem']
    for cand in ('%s_%03d.vpk' % (stem, arch), '%s.vpk' % stem):
        if os.path.exists(cand):
            return cand
    # single-file v1 style fallback
    return archives.get(arch, archives[NO_ARCHIVE])


def extract(path, entry_name, out):
    ver, entries, archives, data = open_vpk(path)
    for e in entries:
        if e['full'] == entry_name or e['name'] == entry_name:
            with open(archive_path(archives, e['arch'], data), 'rb') as f:
                f.seek(e['entry_off'])
                payload = f.read(e['entry_len'])
            with open(out, 'wb') as f:
                f.write(e['preload'] + payload)
            print('wrote %d bytes to %s' % (len(e['preload']) + len(payload), out))
            return
    sys.exit('entry not found: %s' % entry_name)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    cmd = argv[1]
    if cmd == 'list':
        ver, entries, _, _ = open_vpk(argv[2])
        filt = argv[3] if len(argv) > 3 else ''
        for e in entries:
            if filt in e['full']:
                print('%8d %s' % (len(e['preload']) + e['entry_len'], e['full']))
        print('v%d, %d entries' % (ver, len(entries)), file=sys.stderr)
    elif cmd == 'extract':
        extract(argv[2], argv[3], argv[4])
    elif cmd == 'scan':
        d, suffix = argv[2], argv[3]
        for fn in sorted(os.listdir(d)):
            if not fn.endswith('.vpk'):
                continue
            if '_dir' not in fn and any(
                    fn.startswith(o.replace('_dir.vpk', '_')) for o in os.listdir(d)
                    if o.endswith('_dir.vpk')):
                continue  # numbered archive, covered by its _dir
            try:
                ver, entries, _, _ = open_vpk(os.path.join(d, fn))
            except Exception as ex:
                print('%-28s ERROR %s' % (fn, ex))
                continue
            n = sum(1 for e in entries if e['ext'] == suffix.strip('.'))
            print('%-28s v%d %6d entries, %5d .%s' % (fn, ver, len(entries), n, suffix.strip('.')))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv)
