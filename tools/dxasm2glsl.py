#!/usr/bin/env python3
"""dxasm2glsl.py -- B0/S2.6: translate DX9 shader assembly text to GLSL 450.

Covers the op set encountered in CS:S content; unknown ops raise with the op
name so the coverage driver can report and extend. Fixed resource layout for
every shader (set 0: bindings 0-15 samplers, 16 VS UBO vec4 c[256],
17 PS UBO vec4 c[256]), so any VSxPS pair links without cross-shader
layout negotiation. VS and PS constant files are separate in D3D9, hence
separate UBO bindings.

Semantics:
- `def cN, ...` defines take precedence over the UBO for cN.
- VS oPos0 -> gl_Position; oPos1 (fog) -> vOut22; oPos2 (point size) ->
  gl_PointSize; oN (texcoords) -> vOutN; oD0/1 -> vOut20/21 (diffuse/
  specular, matching the PS v0/v1 mapping); oFog -> vOut22.
- VS inputs map by dcl usage (dcl_position v0): POSITION -> loc 20,
  NORMAL -> loc 4, TEXCOORDn -> loc n, COLOR0 -> loc 21. The mesh binds
  pos/color/uv/normal at exactly those locations.
- PS vN/tN inputs read varyings vInN (location = index), except v0/v1
  which are diffuse/specular (locations 20/21); oC0/oDepth mapped.
- Temp registers rN become vec4 locals, declared up front.
- Saturate (_sat) honored via clamp.

Usage: dxasm2glsl.py <in.asm> <out.glsl> <vs|ps>
"""
import re
import sys

N_SAMPLERS = 16
VS_UBO_BINDING = 16
PS_UBO_BINDING = 17
N_CONSTS = 256


def parse_operand(text):
    text = text.strip()
    neg = text.startswith('-')
    if neg:
        text = text[1:]
    # Source modifiers ride as a suffix: r0.yyyy_abs, r1.xxxx_absneg.
    mod = ''
    for suffix in ('_absneg', '_biasneg', '_bx2neg', '_bias', '_bx2',
                   '_comp', '_abs'):
        if text.endswith(suffix):
            mod = suffix
            text = text[:-len(suffix)]
            break
    # Relative addressing: c1024.xyzw[t0.x] (VS constant file indexing).
    addr = None
    mrel = re.match(r'^(.*)\[([a-zA-Z]+)(\d+)\.([xyzw])\]$', text)
    if mrel:
        text = mrel.group(1)
        addr = (mrel.group(2), int(mrel.group(3)), mrel.group(4))
    m = re.match(r'([a-zA-Z]+)(\d+)(?:\.([xyzw]{1,4}))?(_sat)?$', text)
    if not m:
        raise ValueError('bad operand %r' % text)
    return (m.group(1), int(m.group(2)), m.group(3) or 'xyzw',
            bool(m.group(4)), neg, mod, addr)


class Ctx:
    def __init__(self, is_vs):
        self.is_vs = is_vs
        self.defined_consts = {}  # idx -> (v0,v1,v2,v3) strings
        self.temps = set()
        self.booltemps = set()
        self.vary_out = set()     # VS output locations
        self.vary_in = set()      # PS/VS input locations
        self.samplers = set()
        self.vs_in_map = {}       # VS v-reg index -> Vulkan location

    @staticmethod
    def input_loc(reg, idx):
        # Pixel-shader interpolator semantics: v0 = diffuse, v1 =
        # specular (fixed function interpolators); tN carry texcoords.
        # SM3 reuses v2+ as generic interpolators (location = index).
        # (Vertex-shader inputs use the usage table instead; see
        # vs_input_loc. This stays for PS and for undeclared fallback.)
        if reg == 'v':
            return {0: 20, 1: 21}.get(idx, idx)
        return idx

    @staticmethod
    def vs_input_loc(usage, uidx):
        # D3DDECLUSAGE -> fixed mesh location (must match SeVkBindVertex
        # and the pipeline attribute descriptions). Unprovided slots are
        # legal Vulkan (undefined values) for combos the mesh cannot feed
        # yet (skinning extras, tangents: follow-up work).
        if usage in ('position', 'positiont'):
            return 20
        if usage == 'normal':
            return 4
        if usage == 'texcoord':
            return uidx if uidx <= 3 else 30 + uidx
        if usage == 'color':
            return 21 if uidx == 0 else 23
        return {  # unprovided staging slots
            'blendweight': 7, 'blendindices': 8, 'psize': 9,
            'tangent': 5, 'binormal': 6, 'tessfactor': 11, 'fog': 12,
            'depth': 13, 'sample': 14,
        }.get(usage, 15)

    def reg_read(self, reg, idx, swz, addr=None):
        if reg == 'r':
            self.temps.add(idx)
            base = 'rReg%d' % idx
        elif reg in ('b', 'p'):
            self.booltemps.add(idx)
            base = 'bReg%d' % idx
            if swz != 'xyzw':
                raise ValueError('swizzle on bool %s%d' % (reg, idx))
            return base
        elif reg == 'v':
            if self.is_vs:
                # Usage-declared mesh attribute; fall back to the old
                # position/texcoord assumption when undeclared (SM1.x has
                # no dcl usage to parse).
                loc = self.vs_in_map.get(idx, 20 if idx == 0 else idx - 1)
            else:
                # ps_2_x and below: v0 = diffuse, v1 = specular (fixed
                # function interpolators). SM3 reuses v2+ as generic
                # interpolators (location = index).
                loc = Ctx.input_loc(reg, idx)
            self.vary_in.add(loc)
            base = 'vIn%d' % loc
        elif reg == 't':
            loc = Ctx.input_loc(reg, idx)
            self.vary_in.add(loc)
            base = 'vIn%d' % loc
        elif reg == 'c':
            if addr is not None:
                # Relative constant indexing c[N + r.x]: D3D9 reads zero
                # past the 256-constant file; clamp in GLSL (OOB UBO
                # reads are undefined, unlike D3D9 zero-fill).
                areg, aidx, acomp = addr
                if areg == 'r':
                    self.temps.add(aidx)
                    abase = 'rReg%d' % aidx
                elif areg in ('v', 't'):
                    aloc = Ctx.input_loc(areg, aidx)
                    self.vary_in.add(aloc)
                    abase = 'vIn%d' % aloc
                else:
                    raise ValueError('relative addr reg %s' % areg)
                # The address source is a real use: declare it.
                if areg == 'r':
                    self.temps.add(aidx)
                else:
                    self.vary_in.add(aidx)
                base = ('((int(%s.%s) + %d) < 256 ? uConsts.c[int(%s.%s) + %d]'
                        ' : vec4(0.0))' % (abase, acomp, idx, abase, acomp, idx))
            elif idx in self.defined_consts:
                vals = self.defined_consts[idx]
                base = 'vec4(%s)' % ', '.join(vals)
            elif idx >= 256:
                # Out-of-range static index: D3D9 zero-fills.
                base = 'vec4(0.0)'
            else:
                base = 'uConsts.c[%d]' % idx
        elif reg == 's':
            base = 'sTex%d' % idx
        elif reg == 'aL':
            base = 'aLoop'
        else:
            raise ValueError('unhandled src reg %s%d' % (reg, idx))
        return self._sw(base, swz)

    def reg_write(self, reg, idx, swz):
        if reg == 'r':
            self.temps.add(idx)
            return 'rReg%d' % idx, swz, False
        if self.is_vs:
            # D3DVS_RASTOUT offsets: oPos0 = position, oPos1 = fog,
            # oPos2 = point size. oN are texcoord outputs (location N);
            # oD0/1 diffuse/specular (locations 20/21, matching PS v0/v1).
            if reg == 'oPos' and idx == 0:
                return 'gl_Position', swz, False
            if reg == 'oPos' and idx == 1:
                self.vary_out.add(22)
                return 'vOut22', swz, False
            if reg in ('oPos', 'oPts'):
                return 'gl_PointSize', swz, True
            if reg == 'oD':
                loc = 20 + idx
                self.vary_out.add(loc)
                return 'vOut%d' % loc, swz, False
            if reg == 'oFog':
                self.vary_out.add(22)
                return 'vOut22', swz, False
            if reg == 'o':
                self.vary_out.add(idx)
                return 'vOut%d' % idx, swz, False
            if reg == 'oPos':
                self.vary_out.add(idx)
                return 'vOut%d' % idx, swz, False
            if reg == 't':
                # Rare VS texcoord write (morph targets); same varying space.
                self.vary_out.add(idx)
                return 'vOut%d' % idx, swz, False
            raise ValueError('unhandled VS dst %s%d' % (reg, idx))
        else:
            if reg == 'oC':
                return 'oC0', swz, False
            if reg == 'oDepth':
                return 'gl_FragDepth', swz, True
            raise ValueError('unhandled PS dst %s%d' % (reg, idx))

    @staticmethod
    def _sw(base, swz):
        if swz == 'xyzw':
            return base
        return '%s.%s' % (base, swz)


BINARY = {
    'add': '(%s + %s)', 'sub': '(%s - %s)', 'mul': '(%s * %s)',
    'min': 'min(%s, %s)', 'max': 'max(%s, %s)',
    'dp3': 'vec4(dot(%s.xyz, %s.xyz))',
    'dp4': 'vec4(dot(%s, %s))',
}


def translate_inst(ctx, op, args):
    def S(i):
        reg, idx, swz, _, neg, mod, ad = parse_operand(args[i])
        e = ctx.reg_read(reg, idx, swz, ad if reg == 'c' else None)
        if ad is not None and reg != 'c':
            raise ValueError('relative addressing on %s%d' % (reg, idx))
        if mod in ('_abs', '_absneg'):
            e = 'abs(%s)' % e
            neg = neg or mod == '_absneg'
        elif mod == '_bias':
            e = '(%s - vec4(0.5))' % e
        elif mod == '_biasneg':
            e = '(-(%s - vec4(0.5)))' % e
            neg = False
        elif mod == '_bx2':
            e = '(%s * vec4(2.0) - vec4(1.0))' % e
        elif mod == '_bx2neg':
            e = '(-(%s * vec4(2.0) - vec4(1.0)))' % e
            neg = False
        elif mod == '_comp':
            e = '(vec4(1.0) - (%s))' % e
        elif mod:
            raise ValueError('unhandled src modifier %r' % mod)
        if neg:
            e = '(-%s)' % e
        return e

    def V2(i):
        reg, idx, swz, _, neg, mod, _ad = parse_operand(args[i])
        if mod:
            raise ValueError('modifier on texcoord %r' % mod)
        e = ctx.reg_read(reg, idx, 'xy')
        base = re.sub(r'\.[xyzw]+$', '', e)
        e = '%s.%s' % (base, swz[:2])
        if neg:
            e = '(-%s)' % e
        return e

    def D():
        reg, idx, swz, sat, neg, mod, _ad = parse_operand(args[0])
        e, s2, scalar_tgt = ctx.reg_write(reg, idx, swz)
        if scalar_tgt:
            return e, sat
        return '%s.%s' % (e, swz), sat

    def finish(dst, expr, sat, scalar=False):
        import re as _re
        if not _re.search(r'\.[xyzw]+$', dst):
            # Bare scalar target (gl_PointSize, gl_FragDepth): .x convention.
            if not scalar:
                expr = '(%s).x' % expr
            if sat:
                expr = 'clamp(%s, 0.0, 1.0)' % expr
            return '%s = %s;' % (dst, expr)
        m = _re.search(r'\.([xyzw]+)$', dst)
        mask = m.group(1) if m else 'xyzw'
        if scalar:
            if len(mask) > 1:
                expr = 'vec%d(%s)' % (len(mask), expr)
        elif len(mask) < 4:
            expr = '(%s).%s' % (expr, mask)
        if sat:
            n = 1 if (scalar and len(mask) == 1) else len(mask)
            t = 'float' if n == 1 else 'vec%d' % n
            expr = 'clamp(%s, %s(0.0), %s(1.0))' % (expr, t, t)
        return '%s = %s;' % (dst, expr)

    if op == 'mov':
        d, sat = D()
        return finish(d, S(1), sat)
    if op in BINARY:
        d, sat = D()
        a, b = S(1), S(2)
        if op in ('dp3', 'dp4'):
            sub = '.xyz' if op == 'dp3' else ''
            return finish(d, 'dot(%s%s, %s%s)' % (a, sub, b, sub), sat,
                          scalar=True)
        return finish(d, BINARY[op] % (a, b), sat)
    if op == 'mad':
        d, sat = D()
        return finish(d, '(%s * %s + %s)' % (S(1), S(2), S(3)), sat)
    if op == 'rcp':
        d, sat = D()
        return finish(d, '(vec4(1.0) / %s.xxxx)' % S(1), sat)
    if op == 'rsq':
        d, sat = D()
        return finish(d, 'inversesqrt(%s.xxxx)' % S(1), sat)
    if op == 'frc':
        d, sat = D()
        return finish(d, 'fract(%s)' % S(1), sat)
    if op == 'exp':
        d, sat = D()
        return finish(d, 'exp2(%s)' % S(1), sat)
    if op == 'log':
        d, sat = D()
        return finish(d, 'log2(%s)' % S(1), sat)
    if op == 'dp2add':
        d, sat = D()
        return finish(d, 'vec4(dot(%s.xy, %s.xy) + %s)' % (S(1), S(2), S(3)), sat)
    if op == 'lrp':
        d, sat = D()
        return finish(d, 'mix(%s, %s, %s)' % (S(2), S(1), S(3)), sat)
    if op == 'slt':
        d, sat = D()
        return finish(d, 'vec4(lessThan(%s, %s))' % (S(1), S(2)), sat)
    if op == 'sge':
        d, sat = D()
        return finish(d, 'vec4(greaterThanEqual(%s, %s))' % (S(1), S(2)), sat)
    if op == 'cmp':
        d, sat = D()
        return finish(d, 'mix(%s, %s, greaterThanEqual(%s, vec4(0.0)))' % (
            S(2), S(1), S(3)), sat)
    if op == 'texld':
        d, sat = D()
        if len(args) == 1:
            # ps_1_4 bare form: coord and sampler stage both come from dst.
            reg, idx, swz, _, neg, mod, _ad = parse_operand(args[0])
            ctx.samplers.add(idx)
            e = ctx.reg_read(reg, idx, swz)
            import re as _re2
            base = _re2.sub(r'\.[xyzw]+$', '', e)
            return finish(d, 'texture(sTex%d, %s.xy)' % (idx, base), sat)
        if len(args) == 2:
            # ps_1_4 form (no explicit sampler): stage follows the coord.
            reg1, idx1, _, _, neg1, mod1, _ad1 = parse_operand(args[1])
            ctx.samplers.add(idx1)
            return finish(d, 'texture(sTex%d, %s)' % (idx1, V2(1)), sat)
        reg2, idx2, _, _, neg2, mod2, _ad2 = parse_operand(args[2])
        ctx.samplers.add(idx2)
        return finish(d, 'texture(sTex%d, %s)' % (idx2, V2(1)), sat)
    if op == 'texkill':
        reg, idx, swz, _, neg, mod, _ad = parse_operand(args[0])
        e = ctx.reg_read(reg, idx, swz)
        return 'if (any(lessThan(%s.xyz, vec3(0.0)))) discard;' % e
    if op == 'nrm':
        d, sat = D()
        return finish(d, 'vec4(normalize(%s.xyz), 0.0)' % S(1), sat)
    if op == 'lit':
        d, sat = D()
        return finish(d, ('vec4(1.0, max(%s.x, 0.0), '
                           'pow(max(%s.y, 0.0), clamp(%s.w, -128.0, 128.0)), 1.0)'
                           % (S(1), S(1), S(1))), sat)
    if op == 'dst':
        d, sat = D()
        return finish(d, 'vec4(1.0, %s.y * %s.y, %s.z, %s.w)' % (
            S(1), S(2), S(1), S(2)), sat)
    if op == 'm4x4':
        d, sat = D()
        return finish(d, ('vec4(dot(%s, %s), dot(%s, %s), dot(%s, %s), '
                           'dot(%s, %s))' % (S(1), S(2), S(1), S(3), S(1),
                                             S(4), S(1), S(5))), sat)
    if op == 'abs':
        d, sat = D()
        return finish(d, 'abs(%s)' % S(1), sat)
    if op == 'pow':
        d, sat = D()
        return finish(d, 'pow(%s, %s)' % (S(1), S(2)), sat)
    if op == 'sincos':
        d, sat = D()
        # DX: dst.x = cos(src0.x), dst.y = sin(src0.x). Approximate; the
        # extra Taylor-series sources are a scalar fallback we skip.
        return finish(d, 'vec4(cos(%s.x), sin(%s.x), 0.0, 1.0)' % (
            S(1), S(1)), sat)
    if op == 'mova':
        d, sat = D()
        return finish(d, 'vec4(int(%s.x), int(%s.y), int(%s.z), int(%s.w))' % (
            S(1), S(1), S(1), S(1)), sat)
    if op == 'if':
        return 'if (%s) {' % S(0)
    if op == 'else':
        return '} else {'
    if op == 'endif':
        return '}'
    if op == 'nop':
        return '(void)0;'
    raise ValueError('unhandled op %r' % op)


def translate(asm_text, stage):
    is_vs = stage == 'vs'
    ctx = Ctx(is_vs)
    lines = [l.strip() for l in asm_text.splitlines() if l.strip()]
    assert lines[0].startswith('vs_') or lines[0].startswith('ps_')
    # Pass 1: collect defs (they precede uses in practice, but scan all).
    for line in lines[1:]:
        parts = line.split(None, 1)
        if parts[0] == 'def':
            args = [a.strip() for a in parts[1].split(',')]
            reg, idx, _, _, neg, mod, _ad = parse_operand(args[0])
            assert reg == 'c'
            ctx.defined_consts[idx] = args[1:5]
    # Pass 2: emit.
    body = []
    for line in lines[1:]:
        parts = line.split(None, 1)
        op = parts[0]
        args = [a.strip() for a in parts[1].split(',')] if len(parts) > 1 else []
        if op == 'dcl' or op.startswith('dcl_'):
            usage = op[4:] if op.startswith('dcl_') else None
            reg, idx, swz, _, neg, mod, _ad = parse_operand(args[0])
            if reg == 's':
                ctx.samplers.add(idx)
            elif reg in ('v', 't'):
                if usage is not None and ctx.is_vs and reg == 'v':
                    m = re.match(r'([a-z]+)(\d*)$', usage)
                    uidx = int(m.group(2)) if m.group(2) else 0
                    loc = Ctx.vs_input_loc(m.group(1), uidx)
                    ctx.vs_in_map[idx] = loc
                    ctx.vary_in.add(loc)
                elif usage is not None and ctx.is_vs and reg == 't':
                    m = re.match(r'([a-z]+)(\d*)$', usage)
                    uidx = int(m.group(2)) if m.group(2) else 0
                    loc = Ctx.vs_input_loc(m.group(1), uidx)
                    ctx.vary_in.add(loc)
                else:
                    ctx.vary_in.add(Ctx.input_loc(reg, idx))
            continue
        if op == 'def':
            continue  # folded into uses via defined_consts
        body.append('    ' + translate_inst(ctx, op, args))
    out = ['#version 450']
    if is_vs:
        for v in sorted(ctx.vary_in):
            out.append('layout(location = %d) in vec4 vIn%d;' % (v, v))
    else:
        for v in sorted(ctx.vary_in):
            out.append('layout(location = %d) in vec4 vIn%d;' % (v, v))
    if is_vs:
        for v in sorted(ctx.vary_out):
            out.append('layout(location = %d) out vec4 vOut%d;' % (v, v))
    else:
        out.append('layout(location = 0) out vec4 oC0;')
    for s in sorted(ctx.samplers):
        out.append('layout(set = 0, binding = %d) uniform sampler2D sTex%d;'
                   % (s, s))
    # VS and PS constant files are separate in D3D9 (vs_const vs ps_const),
    # so each stage gets its own UBO binding. Shared layout for all shaders.
    ubo = VS_UBO_BINDING if is_vs else PS_UBO_BINDING
    out.append('layout(set = 0, binding = %d) uniform DynConsts {' % ubo)
    out.append('    vec4 c[256];')
    out.append('} uConsts;')
    out.append('void main() {')
    for t in sorted(ctx.temps):
        out.append('    vec4 rReg%d;' % t)
    for t in sorted(ctx.booltemps):
        out.append('    bool bReg%d;' % t)
    out.extend(body)
    out.append('}')
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    out = translate(open(sys.argv[1]).read(), sys.argv[3])
    open(sys.argv[2], 'w').write(out)
