#!/usr/bin/env python3
"""vkstubgen.py -- generate neutral stub method bodies for Source material
interfaces (Phase 2 shaderapivulkan bring-up).

The Vulkan backend implements IShaderAPI et al incrementally (S2.2:
instance/device first). C++ needs every pure virtual defined for the class
to instantiate, so this emits mechanical stubs that warn once per method and
return a neutral value. The warning is deliberate: a silently ignored call
looks like a renderer bug (wrong state applied, nothing drawn). Each stub is replaced by real Vulkan code as its feature
lands (S2.3+); nothing here is part of the D3D9->Vulkan mapping itself.

Usage:
  vkstubgen.py ClassName header1.h [header2.h ...] --out basepath
writes basepath.h (class with all overrides) and basepath.cpp (bodies).

Caveats: parses Valve-style declarations only
(`virtual Ret Name(params) [= 0];`, possibly multi-line). Function-pointer
params and exotic declarators may need hand fixes -- the compiler will say.
"""
import re
import sys


def split_top(s, sep=','):
    parts, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        if ch == sep and depth == 0:
            parts.append(cur)
            cur = ''
        else:
            cur += ch
    parts.append(cur)
    return parts


def strip_defaults(params):
    out = []
    for p in split_top(params):
        # drop `= default` at depth 0
        res, depth = '', 0
        for ch in p:
            if ch in '([':
                depth += 1
            elif ch in ')]':
                depth -= 1
            if ch == '=' and depth == 0:
                break
            res += ch
        res = res.strip()
        out.append(res if res else 'void /*empty*/')
    text = ', '.join(out)
    return 'void' if text in ('', 'void /*empty*/') else text


def default_for(ret):
    r = ret.strip()
    if r == 'void':
        return None
    if '&' in r:
        base = r.replace('const', '').replace('&', '').strip()
        return 's_%s_dummy' % re.sub(r'\W+', '_', base), base
    if '*' in r:
        return 'nullptr'
    # Value-initialization is universal for the rest: 0 for scalars/enums,
    # default ctor for classes. (Plain 0 fails on enums under -fpermissive.)
    return '%s{}' % r


def parse_header(path, only_class=None):
    text = open(path, encoding='utf-8', errors='replace').read()
    # strip // and /* */ comments (string-unsafe but fine for these headers)
    text = re.sub(r'//[^\n]*', '', text)
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    # Valve macro for pure-virtual config getters (non-Xbox branch):
    #   DEFCONFIGMETHOD( bool, HasFoo(), true )  ->  virtual bool HasFoo() const = 0;
    text = re.sub(
        r'DEFCONFIGMETHOD\s*\(\s*(.+?)\s*,\s*(\w+\([^)]*\))\s*,[^)]*\)',
        r'virtual \1 \2 const = 0;', text)
    if only_class:
        # scope to `class|abstract_class Name ... { ... };` at brace depth 0
        m = re.search(r'(?:class|abstract_class|struct)\s+%s\b[^{]*\{' % only_class, text)
        if not m:
            return []
        start = m.end()
        depth = 1
        i = start
        while i < len(text) and depth > 0:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
            i += 1
        text = text[start:i]
    # join continuations into statements
    methods = []
    buf = ''
    depth = 0
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        if not buf and not s.startswith('virtual'):
            continue
        buf += ' ' + s
        depth += s.count('(') - s.count(')')
        if '{' in buf:
            # inline definition (e.g. virtual ~IFoo() {}) -- not a stub target
            buf = ''
            depth = 0
        elif buf.rstrip().endswith(';') and depth <= 0:
            m = re.match(
                r'\s*virtual\s+(.+?)\s*(\w+)\s*\((.*)\)\s*(const)?\s*=\s*0\s*;',
                buf, flags=re.S)
            if m:
                ret, name, params, const = m.groups()
                methods.append((ret.strip(), name,
                                strip_defaults(params.strip()),
                                bool(const)))
            buf = ''
            depth = 0
    return methods


def main():
    args = sys.argv[1:]
    cls = args[0]
    headers = []
    out = None
    bases = []
    only = []
    skip = set()
    i = 1
    while i < len(args):
        if args[i] == '--out':
            out = args[i + 1]
            i += 2
        elif args[i] == '--skip':
            skip.update(a.strip() for a in args[i + 1].split(','))
            i += 2
        elif args[i] == '--class':
            only.append(args[i + 1])
            i += 2
        elif args[i] == '--base':
            bases.append(args[i + 1])
            i += 2
        else:
            headers.append(args[i])
            i += 1
    if not only:
        only = [None] * len(headers)
    elif len(only) == 1 and len(headers) > 1:
        only = only * len(headers)
    methods = []
    skipped = []
    seen = set()
    for h, oc in zip(headers, only):
        for m in parse_header(h, oc):
            key = (m[1], m[2], m[3])
            if key not in seen:
                seen.add(key)
                (skipped if m[1] in skip else methods).append(m)
    dummies = {}
    with open(out + '.h', 'w') as f:
        f.write('// Generated by tools/vkstubgen.py -- DO NOT EDIT.\n')
        f.write('// Neutral stubs for incremental Vulkan bring-up; replaced\n')
        f.write('// method by method as features land (S2.3+).\n')
        f.write('#pragma once\n')
        for h in headers:
            f.write('#include "%s"\n' % h.split('/')[-1])
        f.write('\nclass %s' % cls)
        if bases:
            f.write(' : %s' % (', '.join('public ' + b for b in bases)))
        f.write('\n{\npublic:\n')
        for ret, name, params, const in methods + skipped:
            f.write('\t%s %s(%s)%s;\n' % (
                ret, name, params, ' const' if const else ''))
        if skipped:
            f.write('\n\t// Hand-implemented (see %s.cpp); no neutral body emitted.\n' % cls.lower())
        f.write('};\n')
    with open(out + '.cpp', 'w') as f:
        f.write('// Generated by tools/vkstubgen.py -- DO NOT EDIT.\n')
        f.write('#include "%s.h"\n' % out.split('/')[-1])
        f.write('#include "tier0/dbg.h"\n\n')
        for ret, name, params, const in methods:
            d = default_for(ret)
            if isinstance(d, tuple):
                val, base = d
                dummies[base] = True
                body = 'static %s %s{};\n\treturn %s;' % (base, val, val)
            elif d is None:
                body = '/* neutral stub */'
            else:
                body = 'return %s;' % d
            f.write('%s %s::%s(%s)%s\n{\n\tstatic bool once = false;\n\tif ( !once ) { once = true; Warning( "[vulkan] %s: not implemented, call ignored\\n" ); }\n\t%s\n}\n\n' % (
                ret, cls, name, params, ' const' if const else '',
                name, body))
        if dummies:
            f.write('// dummy statics live in function scope above.\n')


if __name__ == '__main__':
    main()
