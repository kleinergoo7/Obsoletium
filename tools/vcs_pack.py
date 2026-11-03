#!/usr/bin/env python3
"""vcs_pack.py -- S2.6: AOT-convert .vcs shader combos to validated SPIR-V.

For each listed .vcs file: decode combos (v4 raw / v6 LZMA) -> DX9 asm text
-> GLSL -> glslangValidator -> SPIR-V -> spirv-val. Emits one .spv per combo
plus manifest.json mapping sha1(token_bytes) -> {file, shader, model}.
The runtime (se_vk_shaderpack) hashes incoming token bytecode and loads the
matching SPIR-V -- no JIT in the module.

Only SM2.0/2.x/3.0 combos convert for now (the disassembler's length-field
rule); SM1.x combos are reported and skipped (own walker is follow-up work).

Speed design (2026-10-09: the naive serial driver timed out past 500 s):
- incremental: combos already in the manifest with present outputs are
  skipped (manifest carries a generator version; bump MANIFEST_VERSION
  whenever the translator/disassembler changes semantics);
- parallel: translation + glslang run in a process pool (default: CPU
  count); the parent only parses containers (fast pure Python).

Usage: vcs_pack.py <vcs-dir> <out-dir> [--force] [--jobs N] [name-substring ...]
  Converts files whose basename contains any substring (default: all).
  Requires glslangValidator + spirv-val on PATH.
"""
import hashlib
import json
import multiprocessing
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vcs_survey import aliases_v6, combos_v4, records_v6  # noqa: E402
from sm3_disasm import disasm  # noqa: E402
from dxasm2glsl import translate  # noqa: E402

# Bump when translator/disassembler output semantics change -> full rebuild.
# v2: split DynConsts UBO binding 16 (VS) / 17 (PS); v1 used 256 for both.
# v3: usage-qualified VS dcl (dcl_position/dcl_texcoord) + o0->vOut0,
#     oPos1->fog varying; v1 is texcoord (loc 0), not specular.
MANIFEST_VERSION = 3

VER_TOKENS = {
    0xFFFE0200: ('vs', 'vs_2_0'), 0xFFFF0200: ('ps', 'ps_2_0'),
    0xFFFF0201: ('ps', 'ps_2_x'),
    0xFFFE0300: ('vs', 'vs_3_0'), 0xFFFF0300: ('ps', 'ps_3_0'),
}


def split_shaders(combo):
    """Yield (version-token, shader-bytes) for each shader in a combo blob."""
    out = []
    for start in (0, 8):
        if start + 4 > len(combo):
            continue
        if struct.unpack_from('<I', combo, start)[0] in VER_TOKENS:
            break
    else:
        return out
    p = start
    while p + 4 <= len(combo):
        tok = struct.unpack_from('<I', combo, p)[0]
        op = tok & 0xffff
        if op == 0xffff:
            break
        if op == 0xfffe:
            p += 4 + ((tok >> 16) & 0x7fff) * 4
            continue
        ln = (tok >> 24) & 0xf
        p += 4 + ln * 4
    return [(combo[start:p])]


def parse_entries(payload):
    """Yield (dynamicComboID, program bytes) for each program in a record.

    A static combo record holds one program per dynamic combo:
    (nCombo_ID, nShaderSize, bytes) repeated, see vertexshaderdx8.cpp.
    """
    p = 0
    while p + 8 <= len(payload):
        cid, size = struct.unpack_from('<II', payload, p)
        p += 8
        if size == 0 or p + size > len(payload):
            break
        yield cid, payload[p:p + size]
        p += size


def convert_one(task):
    """Worker: tokens -> asm -> GLSL -> SPIR-V -> validated. Returns a dict."""
    fn, ci, body, stage, model, name, out_dir = task
    res = {'key': hashlib.sha1(body).hexdigest(), 'name': name,
           'fn': fn, 'ci': ci, 'model': model, 'ok': False, 'fail': None,
           'spv': None}
    try:
        asm_text = disasm(body)
        glsl = translate(asm_text, stage)
    except Exception as ex:
        res['fail'] = 'translate %s: %s' % (type(ex).__name__, ex)
        return res
    ext = '.vert' if stage == 'vs' else '.frag'
    glsl_path = os.path.join(out_dir, name + '.glsl' + ext)
    spv_path = os.path.join(out_dir, name)
    try:
        with open(glsl_path, 'w') as f:
            f.write(glsl)
    except OSError as ex:
        res['fail'] = 'write %s' % ex
        return res
    r = subprocess.run(
        ['glslangValidator', '-V', glsl_path, '-o', spv_path],
        capture_output=True, text=True)
    if r.returncode != 0:
        out = (r.stdout.strip().splitlines() +
               r.stderr.strip().splitlines())
        res['fail'] = 'glslang %s' % out[:4]
        return res
    r = subprocess.run(['spirv-val', spv_path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        res['fail'] = 'spirv-val %s' % (r.stderr.strip().splitlines()[:1])
        return res
    res['ok'] = True
    res['spv'] = name
    return res


def main():
    args = sys.argv[1:]
    force = '--force' in args
    args = [a for a in args if a != '--force']
    jobs = multiprocessing.cpu_count()
    if '--jobs' in args:
        i = args.index('--jobs')
        jobs = int(args[i + 1])
        del args[i:i + 2]
    vcs_dir, out_dir = args[0], args[1]
    filters = args[2:]
    os.makedirs(out_dir, exist_ok=True)

    manifest_path = os.path.join(out_dir, 'manifest.json')
    manifest = {}
    if os.path.exists(manifest_path) and not force:
        try:
            manifest = json.load(open(manifest_path))
        except ValueError:
            manifest = {}
    if manifest.get('_meta', {}).get('version') != MANIFEST_VERSION:
        manifest = {}
    manifest.setdefault('_meta', {'version': MANIFEST_VERSION})

    # Phase A (parent, fast): enumerate + hash + incremental filter.
    # Dedupe by body hash: identical combos (common across files) must not
    # be compiled twice -- parallel workers sharing one output path race
    # (one's glslang reads the other's half-written files).
    # Input preserves VPK subdirs (shaders/vsh|psh|fxc): same basenames
    # exist in several dirs with different contents (e.g. vsh/eyes.vcs vs
    # psh/eyes.vcs); flattening would corrupt the pack. Index keys carry
    # the dir prefix so the runtime can try vsh/ then fxc/ (VS) or psh/
    # then fxc/ (PS), mirroring the DX9 loader.
    tasks = []
    seen = set()
    # Static index: shader basename -> [[flatIndex, key, model], ...] sorted
    # by flat index, so the runtime can resolve SetVertexShader(file, staticIdx)
    # without parsing .vcs itself. First shader per combo (matches pack).
    static_index = {}
    stats = {'files': 0, 'combos': 0, 'skipped': 0, 'skip_sm1': 0}
    vcs_files = []
    for root, _dirs, files in os.walk(vcs_dir):
        for fn in sorted(files):
            if fn.endswith('.vcs'):
                vcs_files.append(os.path.join(root, fn))
    vcs_files.sort()
    for path in vcs_files:
        fn = os.path.relpath(path, vcs_dir)
        if filters and not any(s in fn for s in filters):
            continue
        d = open(path, 'rb').read()
        if len(d) < 28:
            continue
        v = struct.unpack_from('<i', d, 0)[0]
        if v == 4:
            # v4: combos are positional, no static combo ids.
            records = [(i, c) for i, c in enumerate(combos_v4(d))]
            ndyn = 1
            alias_of = {}
        elif v in (5, 6):
            ndyn = struct.unpack_from('<i', d, 8)[0]
            records = records_v6(d)
            alias_of = dict(aliases_v6(d))
        else:
            continue
        stats['files'] += 1
        base = fn[:-4] if fn.endswith('.vcs') else fn
        flat = base.replace('/', '_')
        entries = static_index.setdefault(base, [])
        # A record's programs are the dynamic combos of that static combo.
        # The engine addresses them by one flat index: staticComboID * nDyn +
        # dynamicComboID (see vertexshaderdx8.cpp: the device computes
        # staticComboID = staticIndex / nDynamicCombos and adds the dynamic
        # combo the command buffer set). Only the dynamic combo 0 program is
        # converted: it is the one the runtime asks for (the dynamic index is
        # not applied yet), and converting all of them is ~680k programs.
        for cid, payload in records:
            for dyn, shader in parse_entries(payload):
                if dyn != 0:
                    continue
                stats['combos'] += 1
                start = 0
                for cand in (0, 8):
                    if cand + 4 <= len(shader) and struct.unpack_from(
                            '<I', shader, cand)[0] in VER_TOKENS:
                        start = cand
                        break
                else:
                    stats['skip_sm1'] += 1
                    continue
                stage, model = VER_TOKENS[struct.unpack_from('<I', shader, start)[0]]
                body = shader[start:]
                key = hashlib.sha1(body).hexdigest()
                flat_index = cid * ndyn + dyn
                entries.append([flat_index, key, model])
                name = '%s_%s_%s.spv' % (flat, model, key[:12])
                if (not force and key in manifest and
                        os.path.exists(os.path.join(out_dir, name))):
                    stats['skipped'] += 1
                    continue
                if key in seen:
                    stats['skipped'] += 1
                    continue
                seen.add(key)
                tasks.append((fn, flat_index, body, stage, model, name, out_dir))
        # Duplicate static combos alias another one; the DX9 loader remaps
        # the alias to its source before looking the record up.
        by_cid = {e[0] // ndyn: e for e in entries if e[0] % ndyn == 0}
        for alias, src in alias_of.items():
            if src in by_cid:
                e = by_cid[src]
                entries.append([alias * ndyn, e[1], e[2]])
        entries.sort()

    # Phase B (parallel): translate + compile + validate.
    ok = 0
    fail = []
    if tasks:
        with multiprocessing.Pool(jobs) as pool:
            for res in pool.imap_unordered(convert_one, tasks, chunksize=4):
                if res['ok']:
                    manifest[res['key']] = {
                        'file': res['name'], 'shader': res['fn'][:-4],
                        'model': res['model']}
                    ok += 1
                else:
                    fail.append('%s combo%d: %s' % (
                        res['fn'], res['ci'], res['fail']))
    open(manifest_path, 'w').write(
        json.dumps(manifest, indent=1, sort_keys=True))
    open(os.path.join(out_dir, 'static_index.json'), 'w').write(
        json.dumps(static_index, sort_keys=True))
    print('files=%d combos=%d skipped=%d skip_sm1=%d ok=%d fail=%d jobs=%d' % (
        stats['files'], stats['combos'], stats['skipped'],
        stats['skip_sm1'], ok, len(fail), jobs))
    for f in fail[:20]:
        print('  FAIL', f)
    with open(os.path.join(out_dir, 'pack-fail.log'), 'w') as f:
        for e in fail:
            f.write(e + '\n')


if __name__ == '__main__':
    main()
