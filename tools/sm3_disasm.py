#!/usr/bin/env python3
"""sm3_disasm.py -- B0 spike: disassemble SM3.0 (vs_3_0/ps_3_0) token bytecode
to DX9 assembly text.

Implements the documented token layout (opcode = low 16 bits, instruction
length = bits[27:24]; dst/src operand encoding per the D3D9 spec). SM1.x has
different quirks (no length field, coissue) and is out of spike scope -- the
Phase 2 translator covers all versions with wined3d as reference.

Usage: sm3_disasm.py <blob.bin>   (blob runs version token .. end token)
"""
import struct
import sys

OPNAMES = {
    0: 'nop', 1: 'mov', 2: 'add', 3: 'sub', 4: 'mad', 5: 'mul',
    6: 'rcp', 7: 'rsq', 8: 'dp3', 9: 'dp4', 10: 'min', 11: 'max',
    12: 'slt', 13: 'sge', 14: 'exp', 15: 'log', 16: 'lit', 17: 'dst',
    18: 'lrp', 19: 'frc', 25: 'call', 26: 'callnz', 27: 'loop', 28: 'ret',
    29: 'endloop', 30: 'label', 31: 'dcl', 32: 'pow', 33: 'crs', 34: 'sgn',
    35: 'abs', 36: 'nrm', 37: 'sincos', 38: 'rep', 39: 'endrep', 40: 'if',
    41: 'ifc', 42: 'else', 43: 'endif', 44: 'break', 45: 'breakc',
    46: 'mova', 48: 'defi', 50: 'texkill', 51: 'texld', 65: 'texld',
    66: 'texld', 81: 'def', 88: 'cmp', 90: 'dp2add',
}
REGNAMES = {0: 'r', 1: 'v', 2: 'c', 3: 't', 4: 'oPos', 5: 'oD', 6: 'o',
            7: 'i', 8: 'oC', 9: 'oDepth', 10: 's', 14: 'b', 15: 'aL'}
# D3DDECLUSAGE for vertex-shader dcl (words[0] low 5 bits). Pixel-shader
# dcl words are not DECLUSAGE and must not use this table.
DCL_USAGE = {0: 'position', 1: 'blendweight', 2: 'blendindices',
             3: 'normal', 4: 'psize', 5: 'texcoord', 6: 'tangent',
             7: 'binormal', 8: 'tessfactor', 9: 'positiont', 10: 'color',
             11: 'fog', 12: 'depth', 13: 'sample'}
MODNAMES = {0: '', 1: '-', 2: '_bias', 3: '_biasneg', 4: '_bx2',
            5: '_bx2neg', 6: '_comp', 11: '_abs', 12: '_absneg'}
SWZ = 'xyzw'


def reg_kind(tok):
    return (((tok >> 28) & 0x7) | ((tok >> 8) & 0x18))


def fmt_dst(tok):
    kind = REGNAMES.get(reg_kind(tok), '?%d' % reg_kind(tok))
    idx = tok & 0x7ff
    mask = (tok >> 16) & 0xf
    s = '%s%d' % (kind, idx)
    if mask != 0xf:
        s += '.' + ''.join(SWZ[i] for i in range(4) if mask & (1 << i))
    if tok & 0x00100000:
        s += '_sat'
    return s


def fmt_src(tok):
    kind = REGNAMES.get(reg_kind(tok), '?%d' % reg_kind(tok))
    idx = tok & 0x7ff
    sw = (tok >> 16) & 0xff
    mod = MODNAMES.get((tok >> 24) & 0xf, '?mod')
    neg = '-' if mod == '-' else ''
    core = '%s%s%d.%s' % (neg, kind, idx,
                          ''.join(SWZ[(sw >> (2 * i)) & 3] for i in range(4)))
    if mod not in ('', '-'):
        core += mod
    return core


def disasm(blob):
    start = 0
    for cand in (0, 8):
        if cand + 4 <= len(blob):
            v = struct.unpack_from('<I', blob, cand)[0]
            if v in (0xFFFE0300, 0xFFFF0300, 0xFFFE0200, 0xFFFF0200,
                     0xFFFF0201, 0xFFFE0101, 0xFFFF0101):
                start = cand
                break
    ver = struct.unpack_from('<I', blob, start)[0]
    major = (ver >> 8) & 0xff
    minor = ver & 0xff
    pixel = (ver & 0xffff0000) == 0xffff0000
    out = ['%s_%d_%d' % ('ps' if pixel else 'vs', major, minor)]
    p = start + 4
    while p + 4 <= len(blob):
        tok = struct.unpack_from('<I', blob, p)[0]
        op = tok & 0xffff
        if op == 0xffff:
            break
        if op == 0xfffe:  # comment/CTAB
            p += 4 + ((tok >> 16) & 0x7fff) * 4
            continue
        ln = (tok >> 24) & 0xf
        words = list(struct.unpack_from('<%dI' % ln, blob, p + 4)) if ln else []
        p += 4 + ln * 4
        name = OPNAMES.get(op, 'op%d' % op)
        if op == 81 and len(words) >= 5:  # def
            import struct as st
            vals = [st.unpack('<f', st.pack('<I', w))[0] for w in words[1:5]]
            out.append('def %s, %s' % (fmt_dst(words[0]),
                                       ', '.join('%g' % v for v in vals)))
        elif op == 31:  # dcl
            if len(words) >= 2 and not pixel:
                # Vertex dcl carries D3DDECLUSAGE in words[0] (low 5 bits)
                # + usage index in bits[19:16]; the register is words[1].
                # Emit it (dcl_position v0) so the translator can map mesh
                # attributes to fixed Vulkan locations. Pixel-shader dcl
                # usage words are sampler/type flags, not DECLUSAGE: keep
                # the bare form (dcl t0) and let interpolator semantics map.
                regtext = (fmt_dst(words[1]) if len(words) > 1
                           else fmt_dst(words[0]) if words else '?')
                usage = words[0] & 0x1f
                uidx = (words[0] >> 16) & 0xf
                uname = DCL_USAGE.get(usage)
                if uname is not None and regtext.startswith(('v', 't')):
                    out.append('dcl_%s%s %s' % (
                        uname, uidx if uidx else '', regtext))
                else:
                    out.append('dcl %s' % regtext)
            else:
                out.append('dcl %s' % (fmt_dst(words[1]) if len(words) > 1 else fmt_dst(words[0]) if words else '?'))
        else:
            # A source token with the relative-address bit (13) consumes the
            # following token as its address register: c1024[t0.x].
            fmt = []
            i = 0
            is_dst = op not in (25, 26, 27, 28, 29, 38, 39, 40, 41, 42, 43, 44, 45, 30)
            if is_dst and words:
                fmt.append(('dst', fmt_dst(words[0])))
                i = 1
            while i < len(words):
                w = words[i]
                if (w & (1 << 13)) != 0 and i + 1 < len(words):
                    a = words[i + 1]
                    acomp = 'xyzw'[(a >> 16) & 0x3]
                    # address operand kind+index, x/y/z/w component
                    akind = REGNAMES.get(reg_kind(a), '?')
                    aidx = a & 0x7ff
                    fmt.append(('src', '%s[%s%d.%s]' % (
                        fmt_src(w), akind, aidx, acomp)))
                    i += 2
                else:
                    fmt.append(('src', fmt_src(w)))
                    i += 1
            parts = []
            dst = None
            src = []
            for kind, text in fmt:
                if kind == 'dst':
                    dst = text
                else:
                    src.append(text)
            if dst is not None:
                out.append('%s %s%s' % (name, dst, (', ' + ', '.join(src)) if src else ''))
            else:
                out.append('%s %s' % (name, ', '.join(src)) if src else name)
    return '\n'.join(out)


if __name__ == '__main__':
    print(disasm(open(sys.argv[1], 'rb').read()))
