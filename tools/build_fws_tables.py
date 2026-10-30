#!/usr/bin/env python3
"""Generate native/avionics/a380x_fws_tables.h from the pinned FlyByWire A380X ECAM message sources.

The FBW TypeScript is *parsed* (tokenizer + recursive-descent evaluator for the object/array
literal subset FBW uses), never hand-copied:

  fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/EcamMessages/
    index.ts                        EcamMemos, pfdMemoDisplay (+ AThrOffMemoKey), EcamInfos,
                                    EcamLimitations, EcamInopSys, ChecklistLineStyle,
                                    DeferredProcedureType (+ DEFERRED_PROCEDURE_TYPE_TO_STRING),
                                    EcamAbnormalSensedProcedures / EcamAbnormalProcedures,
                                    EcamAbNormalSensedSubMenuVector, EcamDeferredProcedures,
                                    FMS_PRED_UNRELIABLE_CHECKLIST_ITEM, WD_NUM_LINES, WD_LINE_CHARACTERS
    NormalProcedures.ts             EcamNormalProcedures, deferredProcedureIds
    AbnormalNonSensedProcedures.ts  AbnormalNonSensedProceduresOverview
    AbnormalSensed/*.ts             every abnormal (incl. secondary failures) and deferred dictionary

Supported TS: // and /* */ comments, BOM, trailing commas, object spreads (...X) with JS
last-wins semantics, single/double quoted strings and template literals (`${'\\xa0'.repeat(16)}x`),
all JS string escapes (\\x.., \\u...., \\u{..}, \\n, ...), identifier/quoted/numeric keys, string
concatenation with +, enum member references and const identifiers (also across imports).
Anything else raises FwsParseError ("fail loudly").

Usage:
  tools/build_fws_tables.py            regenerate header + manifest
  tools/build_fws_tables.py --check    exit 1 if the committed header/manifest are stale
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_UPSTREAM = ROOT.parent / 'upstream-aircraft'
ECAM_REL = 'fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/EcamMessages'
PUBLISHER_REL = 'fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/providers/FwsPublisher.ts'
TYPES_H = ROOT / 'native/avionics/a380x_fws_types.h'
OUT_H = ROOT / 'native/avionics/a380x_fws_tables.h'
OUT_JSON = ROOT / 'native/avionics/a380x_fws_tables.json'
ATTRIBUTION = ROOT / 'docs/ATTRIBUTION.md'

FWS_TEXT_MAX = 112          # a380x_fws_types.h; checked against the header below
MAX_LEVEL = 8               # conditionalActiveItems level map size used by a380x_fws_lines.c


class FwsParseError(Exception):
    pass


# --------------------------------------------------------------------------------------------
# Tokenizer
# --------------------------------------------------------------------------------------------
PUNCT = ['...', '===', '!==', '=>', '==', '!=', '&&', '||', '??', '?.',
         '{', '}', '[', ']', '(', ')', ':', ';', ',', '.', '=', '<', '>', '|', '?', '+', '-',
         '*', '/', '!', '&', '@', '%']
SIMPLE_ESCAPES = {'n': '\n', 'r': '\r', 't': '\t', 'b': '\b', 'f': '\f', 'v': '\v', '0': '\0'}


class Tok:
    __slots__ = ('kind', 'value', 'line', 'path')

    def __init__(self, kind, value, line, path):
        self.kind, self.value, self.line, self.path = kind, value, line, path

    def where(self):
        return f'{self.path}:{self.line}'

    def __repr__(self):
        return f'Tok({self.kind},{self.value!r},{self.where()})'


def _read_escape(src, i, line, path):
    """src[i] is the character after a backslash. Returns (cooked text, new index, new line)."""
    c = src[i]
    if c in SIMPLE_ESCAPES:
        if c == '0' and i + 1 < len(src) and src[i + 1].isdigit():
            raise FwsParseError(f'{path}:{line}: legacy octal escape not supported')
        return SIMPLE_ESCAPES[c], i + 1, line
    if c == 'x':
        h = src[i + 1:i + 3]
        if not re.fullmatch(r'[0-9A-Fa-f]{2}', h):
            raise FwsParseError(f'{path}:{line}: bad \\x escape')
        return chr(int(h, 16)), i + 3, line
    if c == 'u':
        if src[i + 1] == '{':
            j = src.index('}', i + 2)
            h = src[i + 2:j]
            if not re.fullmatch(r'[0-9A-Fa-f]{1,6}', h):
                raise FwsParseError(f'{path}:{line}: bad \\u{{}} escape')
            return chr(int(h, 16)), j + 1, line
        h = src[i + 1:i + 5]
        if not re.fullmatch(r'[0-9A-Fa-f]{4}', h):
            raise FwsParseError(f'{path}:{line}: bad \\u escape')
        return chr(int(h, 16)), i + 5, line
    if c == '\r':
        j = i + 1
        if j < len(src) and src[j] == '\n':
            j += 1
        return '', j, line + 1
    if c == '\n' or c in '  ':
        return '', i + 1, line + 1
    if c.isdigit():
        raise FwsParseError(f'{path}:{line}: octal escape not supported')
    return c, i + 1, line  # \\ \' \" \` and identity escapes


def tokenize(src: str, path: str, line: int = 1):
    toks = []
    i, n = 0, len(src)
    if src.startswith('﻿'):
        i = 1
    while i < n:
        c = src[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if c in ' \t\r﻿ ':
            i += 1
            continue
        if src.startswith('//', i):
            j = src.find('\n', i)
            i = n if j < 0 else j
            continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2)
            if j < 0:
                raise FwsParseError(f'{path}:{line}: unterminated comment')
            line += src.count('\n', i, j)
            i = j + 2
            continue
        if c in '\'"':
            j, out, start_line = i + 1, [], line
            while True:
                if j >= n or src[j] == '\n':
                    raise FwsParseError(f'{path}:{start_line}: unterminated string')
                ch = src[j]
                if ch == c:
                    j += 1
                    break
                if ch == '\\':
                    text, j, line = _read_escape(src, j + 1, line, path)
                    out.append(text)
                    continue
                out.append(ch)
                j += 1
            toks.append(Tok('str', ''.join(out), start_line, path))
            i = j
            continue
        if c == '`':
            j, parts, buf, start_line = i + 1, [], [], line
            while True:
                if j >= n:
                    raise FwsParseError(f'{path}:{start_line}: unterminated template literal')
                ch = src[j]
                if ch == '`':
                    j += 1
                    break
                if ch == '\\':
                    text, j, line = _read_escape(src, j + 1, line, path)
                    buf.append(text)
                    continue
                if src.startswith('${', j):
                    if buf:
                        parts.append(('text', ''.join(buf)))
                        buf = []
                    depth, k, expr_line = 1, j + 2, line
                    quote = None
                    while k < n and depth:
                        ch2 = src[k]
                        if quote:
                            if ch2 == '\\':
                                k += 2
                                continue
                            if ch2 == quote:
                                quote = None
                        elif ch2 in '\'"`':
                            quote = ch2
                        elif ch2 == '{':
                            depth += 1
                        elif ch2 == '}':
                            depth -= 1
                        if ch2 == '\n':
                            line += 1
                        k += 1
                    if depth:
                        raise FwsParseError(f'{path}:{start_line}: unterminated ${{}} in template')
                    parts.append(('expr', src[j + 2:k - 1], expr_line))
                    j = k
                    continue
                if ch == '\n':
                    line += 1
                buf.append(ch)
                j += 1
            if buf:
                parts.append(('text', ''.join(buf)))
            toks.append(Tok('tpl', parts, start_line, path))
            i = j
            continue
        if c.isdigit() or (c == '.' and i + 1 < n and src[i + 1].isdigit()):
            m = re.match(r'(0[xX][0-9A-Fa-f_]+|[0-9][0-9_]*(\.[0-9_]*)?([eE][+-]?[0-9]+)?|\.[0-9_]+)', src[i:])
            text = m.group(0).replace('_', '')
            if text.lower().startswith('0x'):
                value = int(text, 16)
            elif re.fullmatch(r'[0-9]+', text):
                value = int(text)
            else:
                value = float(text)
            toks.append(Tok('num', value, line, path))
            i += len(m.group(0))
            continue
        if c.isalpha() or c in '_$':
            m = re.match(r'[A-Za-z_$][A-Za-z0-9_$]*', src[i:])
            toks.append(Tok('id', m.group(0), line, path))
            i += len(m.group(0))
            continue
        for p in PUNCT:
            if src.startswith(p, i):
                toks.append(Tok('punc', p, line, path))
                i += len(p)
                break
        else:
            raise FwsParseError(f'{path}:{line}: unexpected character {c!r}')
    toks.append(Tok('eof', None, line, path))
    return toks


# --------------------------------------------------------------------------------------------
# Expression AST + evaluation
# --------------------------------------------------------------------------------------------
class Undefined:
    def __repr__(self):
        return 'undefined'


UNDEFINED = Undefined()


class EnumRef:
    __slots__ = ('enum', 'member')

    def __init__(self, enum, member):
        self.enum, self.member = enum, member

    def __eq__(self, other):
        return isinstance(other, EnumRef) and (self.enum, self.member) == (other.enum, other.member)

    def __hash__(self):
        return hash((self.enum, self.member))

    def __repr__(self):
        return f'{self.enum}.{self.member}'


class Node:
    def __init__(self, kind, tok, **kw):
        self.kind, self.tok = kind, tok
        self.__dict__.update(kw)


class Parser:
    def __init__(self, toks):
        self.toks, self.i = toks, 0

    @property
    def tok(self):
        return self.toks[self.i]

    def peek(self, k=1):
        return self.toks[min(self.i + k, len(self.toks) - 1)]

    def next(self):
        t = self.toks[self.i]
        self.i += 1
        return t

    def is_p(self, value, tok=None):
        t = tok or self.tok
        return t.kind == 'punc' and t.value == value

    def expect_p(self, value):
        t = self.next()
        if t.kind != 'punc' or t.value != value:
            raise FwsParseError(f'{t.where()}: expected {value!r}, got {t.value!r}')
        return t

    def parse_expr(self):
        left = self.parse_postfix()
        while self.is_p('+'):
            t = self.next()
            right = self.parse_postfix()
            left = Node('concat', t, left=left, right=right)
        if self.tok.kind == 'id' and self.tok.value in ('as', 'satisfies'):
            raise FwsParseError(f'{self.tok.where()}: TS "{self.tok.value}" expressions are not supported')
        if self.tok.kind == 'punc' and self.tok.value not in (',', '}', ']', ')', ';'):
            raise FwsParseError(f'{self.tok.where()}: unsupported operator {self.tok.value!r}')
        return left

    def parse_postfix(self):
        node = self.parse_primary()
        while True:
            if self.is_p('.') and self.peek().kind == 'id':
                t = self.next()
                name = self.next().value
                if self.is_p('('):
                    self.next()
                    args = []
                    while not self.is_p(')'):
                        args.append(self.parse_expr())
                        if not self.is_p(')'):
                            self.expect_p(',')
                    self.expect_p(')')
                    node = Node('call', t, obj=node, method=name, args=args)
                else:
                    node = Node('member', t, obj=node, name=name)
                continue
            if self.is_p('('):
                raise FwsParseError(f'{self.tok.where()}: function calls are not supported')
            if self.is_p('['):
                raise FwsParseError(f'{self.tok.where()}: computed member access is not supported')
            return node

    def parse_primary(self):
        t = self.next()
        if t.kind == 'str':
            return Node('str', t, value=t.value)
        if t.kind == 'num':
            return Node('num', t, value=t.value)
        if t.kind == 'tpl':
            parts = []
            for part in t.value:
                if part[0] == 'text':
                    parts.append(('text', part[1]))
                else:
                    sub = Parser(tokenize(part[1], t.path, part[2]))
                    expr = sub.parse_expr()
                    if sub.tok.kind != 'eof':
                        raise FwsParseError(f'{sub.tok.where()}: trailing tokens in template expression')
                    parts.append(('expr', expr))
            return Node('tpl', t, parts=parts)
        if t.kind == 'id':
            if t.value == 'true':
                return Node('const', t, value=True)
            if t.value == 'false':
                return Node('const', t, value=False)
            if t.value == 'null':
                return Node('const', t, value=None)
            if t.value == 'undefined':
                return Node('const', t, value=UNDEFINED)
            if t.value in ('new', 'function', 'class', 'this', 'typeof', 'void'):
                raise FwsParseError(f'{t.where()}: unsupported keyword {t.value!r}')
            return Node('ident', t, name=t.value)
        if t.kind == 'punc' and t.value == '-' and self.tok.kind == 'num':
            n = self.next()
            return Node('num', n, value=-n.value)
        if t.kind == 'punc' and t.value == '{':
            props = []
            while not self.is_p('}'):
                if self.is_p('...'):
                    st = self.next()
                    props.append(('spread', self.parse_expr(), st))
                else:
                    kt = self.next()
                    if kt.kind == 'id':
                        key = kt.value
                    elif kt.kind == 'str':
                        key = kt.value
                    elif kt.kind == 'num':
                        key = js_number_key(kt.value, kt)
                    else:
                        raise FwsParseError(f'{kt.where()}: unsupported object key {kt.value!r}')
                    if not self.is_p(':'):
                        raise FwsParseError(f'{self.tok.where()}: shorthand/method properties are not supported')
                    self.next()
                    props.append(('prop', key, self.parse_expr(), kt))
                if not self.is_p('}'):
                    self.expect_p(',')
            self.expect_p('}')
            return Node('object', t, props=props)
        if t.kind == 'punc' and t.value == '[':
            elems = []
            while not self.is_p(']'):
                if self.is_p('...'):
                    st = self.next()
                    elems.append(('spread', self.parse_expr(), st))
                else:
                    elems.append(('elem', self.parse_expr(), self.tok))
                if not self.is_p(']'):
                    self.expect_p(',')
            self.expect_p(']')
            return Node('array', t, elems=elems)
        if t.kind == 'punc' and t.value == '(':
            raise FwsParseError(f'{t.where()}: parenthesised/arrow expressions are not supported')
        raise FwsParseError(f'{t.where()}: unexpected token {t.value!r}')


def js_number_key(value, tok):
    if isinstance(value, float):
        if value != int(value):
            raise FwsParseError(f'{tok.where()}: non-integer numeric key')
        value = int(value)
    if value < 0:
        raise FwsParseError(f'{tok.where()}: negative numeric key')
    return str(value)


class Module:
    def __init__(self, path: Path, rel: str):
        self.path, self.rel = path, rel
        self.text = path.read_text(encoding='utf-8')
        self.consts = {}       # name -> (Node, type text)
        self.enums = {}        # name -> [(member, value)]
        self.imports = {}      # local name -> (module spec, exported name)
        self.types = {}        # exported type aliases -> token list
        self.exported_other = []
        self._parse()

    def _parse(self):
        toks = tokenize(self.text, self.rel)
        p = Parser(toks)
        while p.tok.kind != 'eof':
            t = p.tok
            if t.kind == 'id' and t.value == 'import' and p.is_p('{', p.peek()):
                p.next()
                p.next()
                names = []
                while not p.is_p('}'):
                    n = p.next()
                    if n.kind != 'id':
                        raise FwsParseError(f'{n.where()}: bad import')
                    local = n.value
                    if p.tok.kind == 'id' and p.tok.value == 'as':
                        p.next()
                        local = p.next().value
                    names.append((local, n.value))
                    if not p.is_p('}'):
                        p.expect_p(',')
                p.expect_p('}')
                frm = p.next()
                spec = p.next()
                if frm.value != 'from' or spec.kind != 'str':
                    raise FwsParseError(f'{frm.where()}: bad import clause')
                for local, exported in names:
                    self.imports[local] = (spec.value, exported)
                continue
            if t.kind == 'id' and t.value == 'export':
                p.next()
                kw = p.next()
                if kw.kind == 'id' and kw.value == 'const':
                    name = p.next()
                    type_toks = []
                    if p.is_p(':'):
                        p.next()
                        depth = 0
                        while not (depth == 0 and p.is_p('=')):
                            tt = p.next()
                            if tt.kind == 'eof':
                                raise FwsParseError(f'{name.where()}: unterminated type annotation')
                            if tt.kind == 'punc' and tt.value in '{[(<':
                                depth += 1
                            elif tt.kind == 'punc' and tt.value in '}])>':
                                depth -= 1
                            type_toks.append(tt.value)
                    p.expect_p('=')
                    expr = p.parse_expr()
                    if p.is_p(';'):
                        p.next()
                    if name.value in self.consts:
                        raise FwsParseError(f'{name.where()}: duplicate export {name.value}')
                    self.consts[name.value] = (expr, ' '.join(str(x) for x in type_toks))
                    continue
                if kw.kind == 'id' and kw.value == 'enum':
                    name = p.next().value
                    p.expect_p('{')
                    members = []
                    while not p.is_p('}'):
                        m = p.next()
                        if m.kind not in ('id', 'str'):
                            raise FwsParseError(f'{m.where()}: bad enum member')
                        value = UNDEFINED
                        if p.is_p('='):
                            p.next()
                            v = p.next()
                            if v.kind not in ('str', 'num'):
                                raise FwsParseError(f'{v.where()}: unsupported enum initialiser')
                            value = v.value
                        members.append((m.value, value))
                        if not p.is_p('}'):
                            p.expect_p(',')
                    p.expect_p('}')
                    self.enums[name] = members
                    continue
                if kw.kind == 'id' and kw.value == 'type':
                    name = p.next().value
                    p.expect_p('=')
                    body = []
                    while not p.is_p(';'):
                        body.append(p.next())
                    p.next()
                    self.types[name] = body
                    continue
                self.exported_other.append(kw.value)
                continue
            p.next()


class Program:
    """Resolves identifiers across the parsed modules and evaluates expressions."""

    def __init__(self, ecam_dir: Path, publisher: Path, upstream: Path):
        self.upstream = upstream
        self.modules = {}
        self.ecam_dir = ecam_dir
        self.publisher = publisher
        self.duplicates = []    # (dictionary, key, overridden from, by)
        self._cache = {}

    def module(self, path: Path) -> Module:
        path = path.resolve()
        if path not in self.modules:
            rel = path.relative_to(self.upstream.resolve()).as_posix()
            self.modules[path] = Module(path, rel)
        return self.modules[path]

    def resolve_spec(self, mod: Module, spec: str) -> Path:
        if not spec.startswith('.'):
            raise FwsParseError(f'{mod.rel}: cannot resolve non-relative import {spec!r}')
        base = (mod.path.parent / spec).resolve()
        for cand in (base, Path(str(base) + '.ts'), base / 'index.ts'):
            if cand.is_file():
                return cand
        raise FwsParseError(f'{mod.rel}: cannot resolve import {spec!r}')

    def lookup(self, mod: Module, name: str, tok):
        if name in mod.consts:
            return ('const', mod, name)
        if name in mod.enums:
            return ('enum', mod, name)
        if name in mod.imports:
            spec, exported = mod.imports[name]
            target = self.module(self.resolve_spec(mod, spec))
            return self.lookup(target, exported, tok)
        raise FwsParseError(f'{tok.where()}: unresolved identifier {name!r}')

    _IN_PROGRESS = object()

    def const(self, mod: Module, name: str):
        key = (mod.path, name)
        if key not in self._cache:
            self._cache[key] = self._IN_PROGRESS  # cycle guard
            expr, _ = mod.consts[name]
            self._cache[key] = self.evaluate(expr, mod, f'{mod.rel}:{name}')
        if self._cache[key] is self._IN_PROGRESS:
            raise FwsParseError(f'{mod.rel}: cyclic definition of {name}')
        return self._cache[key]

    def evaluate(self, node: Node, mod: Module, context: str):
        k = node.kind
        if k in ('str', 'num', 'const'):
            return node.value
        if k == 'tpl':
            out = []
            for part in node.parts:
                if part[0] == 'text':
                    out.append(part[1])
                else:
                    v = self.evaluate(part[1], mod, context)
                    if isinstance(v, bool) or not isinstance(v, (str, int)):
                        raise FwsParseError(f'{node.tok.where()}: unsupported template value {v!r}')
                    out.append(str(v))
            return ''.join(out)
        if k == 'concat':
            a = self.evaluate(node.left, mod, context)
            b = self.evaluate(node.right, mod, context)
            if not (isinstance(a, str) and isinstance(b, str)):
                raise FwsParseError(f'{node.tok.where()}: only string concatenation is supported')
            return a + b
        if k == 'call':
            obj = self.evaluate(node.obj, mod, context)
            if node.method == 'repeat' and isinstance(obj, str) and len(node.args) == 1:
                count = self.evaluate(node.args[0], mod, context)
                if isinstance(count, bool) or not isinstance(count, int) or count < 0:
                    raise FwsParseError(f'{node.tok.where()}: bad repeat count')
                return obj * count
            raise FwsParseError(f'{node.tok.where()}: unsupported call .{node.method}()')
        if k == 'ident':
            kind, target, name = self.lookup(mod, node.name, node.tok)
            if kind == 'const':
                return self.const(target, name)
            raise FwsParseError(f'{node.tok.where()}: enum {name} used as a value')
        if k == 'member':
            if node.obj.kind != 'ident':
                raise FwsParseError(f'{node.tok.where()}: unsupported member access')
            kind, target, name = self.lookup(mod, node.obj.name, node.obj.tok)
            if kind != 'enum':
                raise FwsParseError(f'{node.tok.where()}: member access on non-enum {name}')
            if node.name not in [m for m, _ in target.enums[name]]:
                raise FwsParseError(f'{node.tok.where()}: unknown enum member {name}.{node.name}')
            return EnumRef(name, node.name)
        if k == 'object':
            out = {}
            origin = {}
            for prop in node.props:
                if prop[0] == 'spread':
                    v = self.evaluate(prop[1], mod, context)
                    if not isinstance(v, dict):
                        raise FwsParseError(f'{prop[2].where()}: spread of non-object')
                    src = prop[1].name if prop[1].kind == 'ident' else '<spread>'
                    for key, value in v.items():
                        if key in out:
                            self.duplicates.append((context, key, origin[key], src))
                        out[key] = value
                        origin[key] = src
                else:
                    _, key, expr, kt = prop
                    if key in out:
                        self.duplicates.append((context, key, origin[key], kt.where()))
                    out[key] = self.evaluate(expr, mod, f'{context}.{key}')
                    origin[key] = kt.where()
            return out
        if k == 'array':
            out = []
            for kind, expr, t in node.elems:
                v = self.evaluate(expr, mod, context)
                if kind == 'spread':
                    if not isinstance(v, list):
                        raise FwsParseError(f'{t.where()}: spread of non-array')
                    out.extend(v)
                else:
                    out.append(v)
            return out
        raise FwsParseError(f'{node.tok.where()}: cannot evaluate {k}')


# --------------------------------------------------------------------------------------------
# Validation / conversion
# --------------------------------------------------------------------------------------------
def camel_to_upper(name: str) -> str:
    return re.sub(r'(?<=[a-z0-9])(?=[A-Z])', '_', name).upper()


def js_len(s: str) -> int:
    """JS string .length (UTF-16 code units)."""
    return sum(2 if ord(c) > 0xFFFF else 1 for c in s)


def require(cond, msg):
    if not cond:
        raise FwsParseError(msg)


ITEM_FIELDS = {'name', 'sensed', 'level', 'style', 'labelNotCompleted', 'labelCompleted',
               'colonIfCompleted', 'condition', 'time', 'appendTimeIfElapsed'}


def is_bool(v):
    return isinstance(v, bool)


def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def convert_item(raw, where, styles):
    require(isinstance(raw, dict), f'{where}: checklist item is not an object')
    unknown = set(raw) - ITEM_FIELDS
    require(not unknown, f'{where}: unrecognised item field(s) {sorted(unknown)}')
    require(isinstance(raw.get('name'), str), f'{where}: item name missing or not a string')
    require(is_bool(raw.get('sensed')), f'{where}: item sensed missing or not boolean')
    item = {'name': raw['name'], 'sensed': raw['sensed']}
    level = raw.get('level', UNDEFINED)
    require(level is UNDEFINED or (is_int(level) and 0 <= level <= MAX_LEVEL - 2),
            f'{where}: bad level {level!r}')
    item['level'] = 0 if level is UNDEFINED else level
    style = raw.get('style', UNDEFINED)
    if style is UNDEFINED:
        item['style'] = None
    else:
        require(isinstance(style, EnumRef) and style.enum == 'ChecklistLineStyle' and style.member in styles,
                f'{where}: bad style {style!r}')
        item['style'] = style.member
    for key in ('labelNotCompleted', 'labelCompleted'):
        v = raw.get(key, UNDEFINED)
        require(v is UNDEFINED or isinstance(v, str), f'{where}: {key} is not a string')
        item[key] = None if v is UNDEFINED else v
    for key in ('colonIfCompleted', 'condition', 'appendTimeIfElapsed'):
        v = raw.get(key, UNDEFINED)
        require(v is UNDEFINED or is_bool(v), f'{where}: {key} is not boolean')
        item[key] = None if v is UNDEFINED else v
    t = raw.get('time', UNDEFINED)
    require(t is UNDEFINED or (is_int(t) and 0 <= t <= 65535), f'{where}: bad time {t!r}')
    item['time'] = None if t is UNDEFINED else t
    return item


def convert_procedure(kind, key, raw, where, styles, deferred_types):
    require(isinstance(raw, dict), f'{where}: procedure is not an object')
    allowed = {'abnormal': {'title', 'sensed', 'items', 'recommendation'},
               'normal': {'title', 'items', 'onlyActivatedByRequest'},
               'deferred': {'fromAbnormalProcs', 'title', 'items', 'type'}}[kind]
    unknown = set(raw) - allowed
    require(not unknown, f'{where}: unrecognised {kind} procedure field(s) {sorted(unknown)}')
    proc = {'key': key, 'kind': kind}
    title = raw.get('title', UNDEFINED)
    if kind == 'deferred':
        require(title is UNDEFINED or isinstance(title, str), f'{where}: bad title')
    else:
        require(isinstance(title, str), f'{where}: title missing')
    proc['title'] = None if title is UNDEFINED else title
    if kind == 'abnormal':
        require(is_bool(raw.get('sensed')), f'{where}: sensed missing')
        proc['sensed'] = raw['sensed']
        reco = raw.get('recommendation', UNDEFINED)
        require(reco is UNDEFINED or reco in ('LAND ASAP', 'LAND ANSA'), f'{where}: bad recommendation {reco!r}')
        proc['recommendation'] = None if reco is UNDEFINED else reco
    else:
        proc['sensed'] = None
        proc['recommendation'] = None
    if kind == 'normal':
        v = raw.get('onlyActivatedByRequest', UNDEFINED)
        require(v is UNDEFINED or is_bool(v), f'{where}: bad onlyActivatedByRequest')
        proc['onlyActivatedByRequest'] = None if v is UNDEFINED else v
    else:
        proc['onlyActivatedByRequest'] = None
    if kind == 'deferred':
        frm = raw.get('fromAbnormalProcs')
        require(isinstance(frm, list) and all(isinstance(x, str) and re.fullmatch(r'\d{1,10}', x) for x in frm),
                f'{where}: bad fromAbnormalProcs')
        proc['fromAbnormalProcs'] = frm
        t = raw.get('type')
        require(isinstance(t, EnumRef) and t.enum == 'DeferredProcedureType' and t.member in deferred_types,
                f'{where}: bad deferred type {t!r}')
        proc['type'] = t.member
    else:
        proc['fromAbnormalProcs'] = []
        proc['type'] = None
    items = raw.get('items')
    require(isinstance(items, list), f'{where}: items missing')
    proc['items'] = [convert_item(it, f'{where}.items[{i}]', styles) for i, it in enumerate(items)]
    return proc


def numeric_id(key: str, where: str) -> int:
    require(re.fullmatch(r'\d{1,10}', key) is not None, f'{where}: non-numeric id {key!r}')
    value = int(key)
    require(value < 2 ** 32, f'{where}: id {key} does not fit an unsigned int')
    return value


# --------------------------------------------------------------------------------------------
# C emission
# --------------------------------------------------------------------------------------------
def c_string(s):
    if s is None:
        return 'NULL'
    for ch in s:
        if 0xD800 <= ord(ch) <= 0xDFFF or ord(ch) > 0xFFFF:
            raise FwsParseError(f'non-BMP character in {s!r}: JS length semantics would differ')
    data = s.encode('utf-8')
    out, prev_hex = ['"'], False
    for b in data:
        ch = chr(b)
        if prev_hex and ch in '0123456789abcdefABCDEF':
            out.append('" "')
        prev_hex = False
        if ch == '"':
            out.append('\\"')
        elif ch == '\\':
            out.append('\\\\')
        elif ch == '?':
            out.append('\\?')   # never form trigraphs (-std=c11 enables them)
        elif 32 <= b < 127:
            out.append(ch)
        else:
            out.append(f'\\x{b:02x}')
            prev_hex = True
    out.append('"')
    return ''.join(out)


def tri(v):
    return '-1' if v is None else ('1' if v else '0')


def parse_types_header(path: Path):
    text = path.read_text(encoding='utf-8')
    m = re.search(r'typedef enum \{(.*?)\} FwsLineStyle;', text, re.S)
    require(m is not None, f'{path}: FwsLineStyle enum not found')
    names = re.findall(r'\b(FWS_STYLE_[A-Z_]+)\b', m.group(1))
    names = [n for n in names if n != 'FWS_STYLE_COUNT']
    defs = {k: int(v) for k, v in re.findall(r'#define\s+(FWS_[A-Z_]+)\s+(\d+)', text)}
    return names, defs


def upstream_commit(upstream: Path):
    try:
        r = subprocess.run(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], capture_output=True, text=True,
                           timeout=30)
        if r.returncode == 0 and re.fullmatch(r'[0-9a-f]{40}', r.stdout.strip()):
            return r.stdout.strip(), 'git rev-parse HEAD'
    except (OSError, subprocess.SubprocessError):
        pass
    why = 'git rev-parse HEAD failed'
    dot_git = upstream / '.git'
    if dot_git.is_file():
        m = re.match(r'gitdir:\s*(.+)', dot_git.read_text(encoding='utf-8').strip())
        if m and not Path(m.group(1)).exists():
            why = f'the upstream checkout .git points to {m.group(1)}, which is not mounted'
    if ATTRIBUTION.is_file():
        m = re.search(r'`aircraft`:\s*`([0-9a-f]{40})`', ATTRIBUTION.read_text(encoding='utf-8'))
        if m:
            return m.group(1), f'pinned revision from docs/ATTRIBUTION.md ({why})'
    return 'unknown', why


class Tables:
    pass


def build(upstream: Path, types_h: Path = TYPES_H) -> Tables:
    ecam_dir = upstream / ECAM_REL
    publisher = upstream / PUBLISHER_REL
    prog = Program(ecam_dir, publisher, upstream)
    index = prog.module(ecam_dir / 'index.ts')
    normal_mod = prog.module(ecam_dir / 'NormalProcedures.ts')
    nonsensed_mod = prog.module(ecam_dir / 'AbnormalNonSensedProcedures.ts')
    pub_mod = prog.module(publisher)
    abn_files = sorted((ecam_dir / 'AbnormalSensed').glob('*.ts'))
    require(abn_files, 'no AbnormalSensed/*.ts found')
    abn_mods = [prog.module(p) for p in abn_files]

    known_index = {'WD_NUM_LINES', 'WD_LINE_CHARACTERS', 'EcamMemos', 'AThrOffMemoKey', 'pfdMemoDisplay',
                   'EcamInfos', 'EcamLimitations', 'EcamInopSys', 'DEFERRED_PROCEDURE_TYPE_TO_STRING',
                   'EcamAbnormalSensedProcedures', 'EcamAbnormalProcedures', 'EcamAbNormalSensedSubMenuVector',
                   'EcamDeferredProcedures', 'FMS_PRED_UNRELIABLE_CHECKLIST_ITEM'}
    unknown = set(index.consts) - known_index
    require(not unknown, f'index.ts: unrecognised export(s) {sorted(unknown)}; extend tools/build_fws_tables.py')
    missing = known_index - set(index.consts)
    require(not missing, f'index.ts: expected export(s) missing {sorted(missing)}')
    require(set(index.enums) == {'ChecklistLineStyle', 'DeferredProcedureType', 'WdSpecialLine'},
            f'index.ts: unexpected enums {sorted(index.enums)}')
    require(set(normal_mod.consts) == {'deferredProcedureIds', 'EcamNormalProcedures'},
            f'NormalProcedures.ts: unexpected exports {sorted(normal_mod.consts)}')
    require(set(nonsensed_mod.consts) == {'AbnormalNonSensedProceduresOverview'},
            f'AbnormalNonSensedProcedures.ts: unexpected exports {sorted(nonsensed_mod.consts)}')

    t = Tables()
    t.prog = prog
    t.upstream = upstream

    # Enums
    t.styles = [m for m, _ in index.enums['ChecklistLineStyle']]
    for m, v in index.enums['ChecklistLineStyle']:
        require(v == m, f'ChecklistLineStyle.{m} = {v!r}: expected string enum equal to the name')
    t.deferred_types = [m for m, _ in index.enums['DeferredProcedureType']]
    require(all(v is UNDEFINED for _, v in index.enums['DeferredProcedureType']),
            'DeferredProcedureType: explicit initialisers not supported')
    t.special_lines = [m for m, _ in index.enums['WdSpecialLine']]
    header_styles, header_defs = parse_types_header(types_h)
    expected = ['FWS_STYLE_' + camel_to_upper(s) for s in t.styles]
    require(header_styles == expected,
            f'a380x_fws_types.h FwsLineStyle order {header_styles} != ChecklistLineStyle {expected}')
    t.header_defs = header_defs
    t.special_lines_c = ['FWS_SPECIAL_' + camel_to_upper(s) for s in t.special_lines]
    types_text = types_h.read_text(encoding='utf-8')
    for name in t.special_lines_c:
        require(name in types_text, f'a380x_fws_types.h lacks {name} (WdSpecialLine)')

    # Constants
    t.wd_num_lines = prog.const(index, 'WD_NUM_LINES')
    t.wd_line_characters = prog.const(index, 'WD_LINE_CHARACTERS')
    require(is_int(t.wd_num_lines) and is_int(t.wd_line_characters), 'WD_* constants must be integers')
    require(header_defs.get('FWS_WD_NUM_LINES') == t.wd_num_lines, 'FWS_WD_NUM_LINES differs from WD_NUM_LINES')
    require(header_defs.get('FWS_WD_LINE_CHARACTERS') == t.wd_line_characters,
            'FWS_WD_LINE_CHARACTERS differs from WD_LINE_CHARACTERS')
    require(header_defs.get('FWS_TEXT_MAX') == FWS_TEXT_MAX, 'FWS_TEXT_MAX changed; update the generator')
    t.deferred_type_strings = prog.const(index, 'DEFERRED_PROCEDURE_TYPE_TO_STRING')
    require(isinstance(t.deferred_type_strings, list) and len(t.deferred_type_strings) == len(t.deferred_types)
            and all(isinstance(s, str) for s in t.deferred_type_strings), 'bad DEFERRED_PROCEDURE_TYPE_TO_STRING')
    t.athr_off_memo_key = prog.const(index, 'AThrOffMemoKey')
    require(isinstance(t.athr_off_memo_key, str), 'AThrOffMemoKey is not a string')
    t.fms_pred_item = convert_item(prog.const(index, 'FMS_PRED_UNRELIABLE_CHECKLIST_ITEM'),
                                   'index.ts:FMS_PRED_UNRELIABLE_CHECKLIST_ITEM', t.styles)

    # Message dictionaries
    t.messages = {}
    for name in ('EcamMemos', 'EcamInfos', 'EcamLimitations', 'EcamInopSys'):
        raw = prog.const(index, name)
        require(isinstance(raw, dict), f'{name} is not an object')
        rows = []
        for key, text in raw.items():
            require(isinstance(text, str), f'{name}[{key}] is not a string')
            rows.append((numeric_id(key, f'{name}[{key}]'), key, text))
        ids = [r[0] for r in rows]
        require(len(set(ids)) == len(ids), f'{name}: two keys map to the same numeric id')
        t.messages[name] = sorted(rows)
    pfd = prog.const(index, 'pfdMemoDisplay')
    require(isinstance(pfd, list) and all(isinstance(k, str) for k in pfd), 'pfdMemoDisplay is not a string[]')
    t.pfd_memo_display = [(numeric_id(k, 'pfdMemoDisplay'), k) for k in pfd]

    # Abnormal / deferred dictionaries per file
    t.abn_dicts = []
    abn_exports = {}
    for mod in abn_mods:
        require(not mod.exported_other, f'{mod.rel}: unexpected exported declarations {mod.exported_other}')
        for name, (expr, type_text) in mod.consts.items():
            if 'AbnormalProcedure' in type_text:
                kind = 'abnormal'
            elif 'DeferredProcedure' in type_text:
                kind = 'deferred'
            else:
                raise FwsParseError(f'{mod.rel}: export {name} has unrecognised type {type_text!r}')
            raw = prog.const(mod, name)
            require(isinstance(raw, dict), f'{mod.rel}:{name} is not an object')
            abn_exports[(mod.path.resolve(), name)] = kind
            t.abn_dicts.append((mod.rel, name, kind, len(raw)))

    # Merged dictionaries exactly as index.ts builds them (spread order, JS last-wins)
    def merged(name, kind):
        expr, _ = index.consts[name]
        require(expr.kind == 'object' and all(p[0] == 'spread' and p[1].kind == 'ident' for p in expr.props),
                f'index.ts:{name} is not a pure spread of dictionaries')
        used = []
        for p in expr.props:
            k2, target, n2 = prog.lookup(index, p[1].name, p[1].tok)
            require(k2 == 'const' and (target.path.resolve(), n2) in abn_exports,
                    f'index.ts:{name}: {p[1].name} is not an AbnormalSensed dictionary')
            require(abn_exports[(target.path.resolve(), n2)] == kind,
                    f'index.ts:{name}: {p[1].name} has the wrong procedure type')
            used.append((target.path.resolve(), n2))
        return prog.const(index, name), used

    abn_raw, used_a = merged('EcamAbnormalSensedProcedures', 'abnormal')
    def_raw, used_d = merged('EcamDeferredProcedures', 'deferred')
    alias = index.consts['EcamAbnormalProcedures'][0]
    require(alias.kind == 'ident' and alias.name == 'EcamAbnormalSensedProcedures',
            'EcamAbnormalProcedures is no longer an alias of EcamAbnormalSensedProcedures')
    unused = set(abn_exports) - set(used_a) - set(used_d)
    require(not unused, f'AbnormalSensed dictionaries not referenced by index.ts: {sorted(n for _, n in unused)}')

    def procs(raw, kind, label):
        out = []
        for key, value in raw.items():
            where = f'{label}[{key}]'
            p = convert_procedure(kind, key, value, where, t.styles, t.deferred_types)
            p['id'] = numeric_id(key, where)
            out.append(p)
        ids = [p['id'] for p in out]
        require(len(set(ids)) == len(ids), f'{label}: two keys map to the same numeric id')
        return sorted(out, key=lambda p: p['id'])

    t.abnormal = procs(abn_raw, 'abnormal', 'EcamAbnormalSensedProcedures')
    t.deferred = procs(def_raw, 'deferred', 'EcamDeferredProcedures')
    abn_ids = {p['id'] for p in t.abnormal}
    for d in t.deferred:
        for k in d['fromAbnormalProcs']:
            require(int(k) in abn_ids, f'deferred {d["key"]}: fromAbnormalProcs {k} is not an abnormal procedure')

    # Normal procedures
    t.normal = procs(prog.const(normal_mod, 'EcamNormalProcedures'), 'normal', 'EcamNormalProcedures')
    dids = prog.const(normal_mod, 'deferredProcedureIds')
    require(isinstance(dids, list) and all(is_int(x) for x in dids), 'deferredProcedureIds is not number[]')
    require(len(dids) == len(t.deferred_types), 'deferredProcedureIds must have one id per DeferredProcedureType')
    normal_ids = [p['id'] for p in t.normal]
    require(all(x in normal_ids for x in dids), 'deferredProcedureIds not all in EcamNormalProcedures')
    t.deferred_procedure_ids = dids

    # Abnormal non-sensed
    overview = prog.const(nonsensed_mod, 'AbnormalNonSensedProceduresOverview')
    cat_type = pub_mod.types.get('AbnormalNonSensedCategory')
    require(cat_type is not None, 'FwsPublisher.ts: AbnormalNonSensedCategory type missing')
    categories = [tk.value for tk in cat_type if tk.kind == 'str']
    t.categories = categories
    t.overview = []
    abn_by_id = {p['id']: p for p in t.abnormal}
    for i, e in enumerate(overview):
        require(isinstance(e, dict) and set(e) == {'id', 'category'}, f'AbnormalNonSensedProceduresOverview[{i}]')
        require(is_int(e['id']) and e['id'] in abn_by_id, f'AbnormalNonSensedProceduresOverview[{i}]: bad id')
        require(e['category'] is None or e['category'] in categories, f'Overview[{i}]: bad category')
        require(abn_by_id[e['id']]['sensed'] is False, f'Overview[{i}]: {e["id"]} is a sensed procedure')
        t.overview.append((e['id'], e['category']))
    sub = prog.const(index, 'EcamAbNormalSensedSubMenuVector')
    require(isinstance(sub, list) and all(isinstance(s, str) and s in categories for s in sub),
            'EcamAbNormalSensedSubMenuVector is not a category list')
    t.submenu = sub

    # Limits
    all_items = [it for group in (t.abnormal, t.deferred, t.normal) for p in group for it in p['items']]
    t.max_items = max(len(p['items']) for group in (t.abnormal, t.deferred, t.normal) for p in group)
    t.max_items_warning = None
    if t.max_items > header_defs['FWS_MAX_ITEMS']:
        longest = max((p for g in (t.abnormal, t.deferred, t.normal) for p in g), key=lambda p: len(p['items']))
        # Not a parse error: the header is still generated and its _Static_assert stops the C build.
        t.max_items_warning = (f'FWS_MAX_ITEMS={header_defs["FWS_MAX_ITEMS"]} in {types_h.name} < longest procedure '
                               f'{longest["key"]} ({t.max_items} items); the generated _Static_assert will fail')
    t.max_level = max(it['level'] for it in all_items)
    # Longest WD line texts the port builds without truncation (bytes, NUL excluded):
    nb = lambda s: len(s.encode('utf-8'))  # noqa: E731
    candidates = []
    for p in t.abnormal + t.normal:
        candidates.append((nb(p['title']), f'title {p["key"]}'))
    for p in t.deferred:
        candidates.append((6 + nb(p['title'] or 'UNDEFINED'), f'deferred title {p["key"]}'))
    for p in t.normal:
        candidates.append((5 + nb(p['title']) + 4, f'deferred page header {p["key"]}'))
    for p in t.abnormal + t.deferred:
        for it in p['items']:
            if it['condition'] is not None and not it['sensed']:
                candidates.append((2 * it['level'] + 8 + nb(it['name']), f'CONFIRM line in {p["key"]}'))
    for pid, _ in t.overview:
        candidates.append((8 + nb(abn_by_id[pid]['title']), f'ABN PROC menu line {pid}'))
    t.max_line_bytes, t.max_line_what = max(candidates)
    require(t.max_line_bytes < FWS_TEXT_MAX, f'{t.max_line_what} needs {t.max_line_bytes} bytes >= FWS_TEXT_MAX')
    t.commit, t.commit_source = upstream_commit(upstream)
    t.source_paths = sorted({m.path for m in prog.modules.values()})
    return t


def emit_items(lines, name, procs):
    flat = []
    for p in procs:
        p['first_item'] = len(flat)
        flat.extend(p['items'])
    lines.append(f'static const FwsChecklistItem {name}[{max(len(flat), 1)}] = {{')
    for it in flat:
        style = 'FWS_STYLE_' + camel_to_upper(it['style']) if it['style'] else '-1'
        lines.append(f'    {{{c_string(it["name"])}, {c_string(it["labelNotCompleted"])}, '
                     f'{c_string(it["labelCompleted"])}, {it["time"] or 0}, {int(it["sensed"])}, {it["level"]}, '
                     f'{style}, {tri(it["colonIfCompleted"])}, {tri(it["condition"])}, '
                     f'{tri(it["appendTimeIfElapsed"])}, {int(it["time"] is not None)}}},')
    if not flat:
        lines.append('    {0},')
    lines.append('};')
    return len(flat)


def emit_procs(lines, name, items_name, procs, from_name=None):
    lines.append(f'static const FwsProcedureDef {name}[{len(procs)}] = {{')
    from_index = 0
    for p in procs:
        reco = {None: 'FWS_RECOMMENDATION_NONE', 'LAND ASAP': 'FWS_RECOMMENDATION_LAND_ASAP',
                'LAND ANSA': 'FWS_RECOMMENDATION_LAND_ANSA'}[p['recommendation']]
        dtype = f'FWS_DEFERRED_{p["type"]}' if p['type'] else '-1'
        items = f'&{items_name}[{p["first_item"]}]' if p['items'] else 'NULL'
        frm = f'&{from_name}[{from_index}]' if p['fromAbnormalProcs'] else 'NULL'
        lines.append(f'    {{{p["id"]}u, {c_string(p["key"])}, {c_string(p["title"])}, {tri(p["sensed"])}, {reco}, '
                     f'{tri(p["onlyActivatedByRequest"])}, {dtype}, {len(p["items"])}, '
                     f'{len(p["fromAbnormalProcs"])}, {items}, {frm}}},')
        from_index += len(p['fromAbnormalProcs'])
    lines.append('};')


def render_header(t: Tables) -> str:
    L = []
    L += ['/* SPDX-License-Identifier: GPL-3.0-or-later',
          ' * GENERATED by tools/build_fws_tables.py - do not edit. Regenerate after changing the pinned FBW tree;',
          ' * `python3 tools/build_fws_tables.py --check` fails when this file is stale.',
          ' *',
          ' * Parsed (not hand-copied) from the pinned FlyByWire A380X sources, see a380x_fws_tables.json for the',
          ' * SHA-256 of every source file and the upstream revision:',
          f' *   {ECAM_REL}/',
          ' *     index.ts, NormalProcedures.ts, AbnormalNonSensedProcedures.ts, AbnormalSensed/<file>.ts',
          f' *   {PUBLISHER_REL} (AbnormalNonSensedCategory)',
          ' *',
          ' * IDs: FBW keys are strings ("000001001", "210200001", limitations "1"/"2"). They are stored as the',
          ' * unsigned value of the decimal key (so "000001001" -> 1001u, "1" -> 1u) plus the original key string',
          ' * in `key`; every table is checked to have no two keys with the same numeric value. Arrays are sorted',
          ' * by id for the binary-search lookups, except the order-carrying lists (pfd memo display, abnormal',
          ' * non-sensed overview, sub-menu vector, deferredProcedureIds) which keep FBW order.',
          ' *',
          ' * Strings are UTF-8 (U+00A0 NBSP -> "\\xc2\\xa0", U+00B0 -> "\\xc2\\xb0"); FBW escape codes (\\x1b<Nm ...)',
          ' * are kept. Every character is in the BMP, so one UTF-8 sequence == one JS UTF-16 code unit.',
          ' * Tri-state fields: -1 = property undefined in FBW, 0 = false, 1 = true.',
          ' * Header-only: each translation unit that references a table gets its own copy of it.',
          ' */',
          '#ifndef A380X_FWS_TABLES_H',
          '#define A380X_FWS_TABLES_H',
          '',
          '#include <stddef.h>',
          '#include "a380x_fws_types.h"',
          '',
          f'#define FWS_TABLES_WD_NUM_LINES {t.wd_num_lines}            /* index.ts WD_NUM_LINES */',
          f'#define FWS_TABLES_WD_LINE_CHARACTERS {t.wd_line_characters}      /* index.ts WD_LINE_CHARACTERS */',
          f'#define FWS_TABLES_MAX_ITEMS {t.max_items}               /* longest procedure / checklist */',
          f'#define FWS_TABLES_MAX_LEVEL {t.max_level}                /* deepest item level */',
          f'#define FWS_TABLES_MAX_LINE_BYTES {t.max_line_bytes}         /* longest untruncated WD line built by the port */',
          '_Static_assert(FWS_TABLES_WD_NUM_LINES == FWS_WD_NUM_LINES, "WD_NUM_LINES changed");',
          '_Static_assert(FWS_TABLES_WD_LINE_CHARACTERS == FWS_WD_LINE_CHARACTERS, "WD_LINE_CHARACTERS changed");',
          '_Static_assert(FWS_MAX_ITEMS >= FWS_TABLES_MAX_ITEMS, "FWS_MAX_ITEMS too small for the FBW procedures");',
          '_Static_assert(FWS_TEXT_MAX > FWS_TABLES_MAX_LINE_BYTES, "FWS_TEXT_MAX too small for the FBW texts");',
          '/* a380x_fws_types.h enums must keep the FBW order (ChecklistLineStyle, WdSpecialLine) */']
    for i, st in enumerate(t.styles):
        L.append(f'_Static_assert(FWS_STYLE_{camel_to_upper(st)} == {i}, "FwsLineStyle != ChecklistLineStyle.{st}");')
    L.append(f'_Static_assert(FWS_STYLE_COUNT == {len(t.styles)}, "FwsLineStyle count");')
    for i, name in enumerate(t.special_lines_c):
        L.append(f'_Static_assert({name} == {i}, "FwsSpecialLine != WdSpecialLine");')
    L += ['',
          '/* AbnormalProcedure.recommendation */',
          'typedef enum { FWS_RECOMMENDATION_NONE, FWS_RECOMMENDATION_LAND_ASAP, FWS_RECOMMENDATION_LAND_ANSA }'
          ' FwsRecommendation;',
          '',
          '/* DeferredProcedureType (index.ts), same order */',
          'typedef enum { ' + ', '.join(f'FWS_DEFERRED_{m}' for m in t.deferred_types) + ', FWS_DEFERRED_TYPE_COUNT }'
          ' FwsDeferredProcedureType;',
          '',
          '/* EcamMemos / EcamInfos / EcamLimitations / EcamInopSys entry */',
          'typedef struct { unsigned id; const char *key; const char *text; } FwsMessage;',
          '',
          '/* ChecklistAction | ChecklistCondition | ChecklistSpecialItem | Timed* (index.ts AbstractChecklistItem).',
          ' * label_* NULL = undefined (isChecklistAction() tests label_not_completed != NULL). style -1 = undefined.',
          ' * condition != -1 <=> isChecklistCondition(). has_time <=> `time` defined (timed action/condition). */',
          'typedef struct {',
          '    const char *name;',
          '    const char *label_not_completed;',
          '    const char *label_completed;',
          '    unsigned short time;',
          '    unsigned char sensed;',
          '    unsigned char level;              /* `level ?? 0` */',
          '    signed char style;                /* FwsLineStyle or -1 */',
          '    signed char colon_if_completed;   /* tri-state, FBW default true */',
          '    signed char condition;            /* tri-state */',
          '    signed char append_time_if_elapsed; /* tri-state */',
          '    unsigned char has_time;',
          '} FwsChecklistItem;',
          '',
          '/* AbnormalProcedure | NormalProcedure | DeferredProcedure. title NULL = undefined (deferred only);',
          ' * sensed -1 for normal/deferred; deferred_type -1 unless deferred. */',
          'typedef struct {',
          '    unsigned id;',
          '    const char *key;',
          '    const char *title;',
          '    signed char sensed;',
          '    signed char recommendation;       /* FwsRecommendation */',
          '    signed char only_activated_by_request;',
          '    signed char deferred_type;        /* FwsDeferredProcedureType or -1 */',
          '    unsigned short num_items;',
          '    unsigned short num_from_abnormal_procs;',
          '    const FwsChecklistItem *items;',
          '    const unsigned *from_abnormal_procs; /* DeferredProcedure.fromAbnormalProcs */',
          '} FwsProcedureDef;',
          '',
          '/* AbnormalNonSensedList: category NULL = null (shown on the ABNORMAL PROC top menu) */',
          'typedef struct { unsigned id; const char *category; } FwsAbnormalNonSensedEntry;',
          '']
    for name, cname, label in (('EcamMemos', 'fws_memos', 'MEMO'), ('EcamInfos', 'fws_infos', 'INFO'),
                               ('EcamLimitations', 'fws_limitations', 'LIMITATION'),
                               ('EcamInopSys', 'fws_inop_sys', 'INOP_SYS')):
        rows = t.messages[name]
        L.append(f'/* index.ts {name} */')
        L.append(f'#define FWS_{label}_COUNT {len(rows)}')
        L.append(f'static const FwsMessage {cname}[FWS_{label}_COUNT] = {{')
        for i, key, text in rows:
            L.append(f'    {{{i}u, {c_string(key)}, {c_string(text)}}},')
        L.append('};')
        L.append('')
    L.append('/* index.ts pfdMemoDisplay (FBW order; AThrOffMemoKey resolved) */')
    L.append(f'#define FWS_PFD_MEMO_DISPLAY_COUNT {len(t.pfd_memo_display)}')
    L.append(f'#define FWS_ATHR_OFF_MEMO_ID {int(t.athr_off_memo_key)}u /* AThrOffMemoKey {t.athr_off_memo_key!r} */')
    L.append('static const unsigned fws_pfd_memo_display[FWS_PFD_MEMO_DISPLAY_COUNT] = {'
             + ', '.join(f'{i}u' for i, _ in t.pfd_memo_display) + '};')
    L.append('')
    L.append('/* index.ts DEFERRED_PROCEDURE_TYPE_TO_STRING */')
    L.append('static const char *const fws_deferred_procedure_type_to_string[FWS_DEFERRED_TYPE_COUNT] = {'
             + ', '.join(c_string(s) for s in t.deferred_type_strings) + '};')
    L.append('')
    it = t.fms_pred_item
    L.append('/* index.ts FMS_PRED_UNRELIABLE_CHECKLIST_ITEM (spread into procedures as { ...X }) */')
    L.append(f'static const FwsChecklistItem fws_fms_pred_unreliable_checklist_item = {{{c_string(it["name"])}, '
             f'{c_string(it["labelNotCompleted"])}, {c_string(it["labelCompleted"])}, {it["time"] or 0}, '
             f'{int(it["sensed"])}, {it["level"]}, '
             f'{"FWS_STYLE_" + camel_to_upper(it["style"]) if it["style"] else "-1"}, '
             f'{tri(it["colonIfCompleted"])}, {tri(it["condition"])}, {tri(it["appendTimeIfElapsed"])}, '
             f'{int(it["time"] is not None)}}};')
    L.append('')
    L.append('/* index.ts EcamAbnormalSensedProcedures (== EcamAbnormalProcedures): all AbnormalSensed/<file>.ts')
    L.append(' * abnormal dictionaries merged in spread order, incl. secondary failures and non-sensed procedures */')
    n_abn_items = emit_items(L, 'fws_abnormal_items', t.abnormal)
    L.append(f'#define FWS_ABNORMAL_PROCEDURE_COUNT {len(t.abnormal)}')
    L.append(f'#define FWS_ABNORMAL_ITEM_COUNT {n_abn_items}')
    emit_procs(L, 'fws_abnormal_procedures', 'fws_abnormal_items', t.abnormal)
    L.append('')
    L.append('/* index.ts EcamDeferredProcedures */')
    n_def_items = emit_items(L, 'fws_deferred_items', t.deferred)
    flat_from = [int(k) for p in t.deferred for k in p['fromAbnormalProcs']]
    L.append(f'static const unsigned fws_deferred_from_abnormal_procs[{max(len(flat_from), 1)}] = {{'
             + (', '.join(f'{x}u' for x in flat_from) or '0') + '};')
    L.append(f'#define FWS_DEFERRED_PROCEDURE_COUNT {len(t.deferred)}')
    L.append(f'#define FWS_DEFERRED_ITEM_COUNT {n_def_items}')
    emit_procs(L, 'fws_deferred_procedures', 'fws_deferred_items', t.deferred, 'fws_deferred_from_abnormal_procs')
    L.append('')
    L.append('/* NormalProcedures.ts EcamNormalProcedures, ascending id (getNormalProceduresKeysSorted) */')
    n_norm_items = emit_items(L, 'fws_normal_items', t.normal)
    L.append(f'#define FWS_NORMAL_PROCEDURE_COUNT {len(t.normal)}')
    L.append(f'#define FWS_NORMAL_ITEM_COUNT {n_norm_items}')
    emit_procs(L, 'fws_normal_procedures', 'fws_normal_items', t.normal)
    L.append('/* NormalProcedures.ts deferredProcedureIds (index == FwsDeferredProcedureType) */')
    L.append('static const unsigned fws_deferred_procedure_ids[FWS_DEFERRED_TYPE_COUNT] = {'
             + ', '.join(f'{x}u' for x in t.deferred_procedure_ids) + '};')
    L.append('')
    L.append('/* AbnormalNonSensedProcedures.ts AbnormalNonSensedProceduresOverview (FBW order) */')
    L.append(f'#define FWS_ABN_NON_SENSED_OVERVIEW_COUNT {len(t.overview)}')
    L.append('static const FwsAbnormalNonSensedEntry fws_abn_non_sensed_overview[FWS_ABN_NON_SENSED_OVERVIEW_COUNT] = {')
    for pid, cat in t.overview:
        L.append(f'    {{{pid}u, {c_string(cat)}}},')
    L.append('};')
    L.append('/* index.ts EcamAbNormalSensedSubMenuVector (FBW order) */')
    L.append(f'#define FWS_ABN_SUB_MENU_COUNT {len(t.submenu)}')
    L.append('static const char *const fws_abn_sub_menu_vector[FWS_ABN_SUB_MENU_COUNT] = {'
             + ', '.join(c_string(s) for s in t.submenu) + '};')
    L.append('')
    L += ['/* Binary-search lookups (tables sorted by id). */',
          '#define FWS_TABLES_BSEARCH(T, table, count, id_value) do { \\',
          '        size_t lo_ = 0, hi_ = (count); \\',
          '        while (lo_ < hi_) { size_t mid_ = lo_ + (hi_ - lo_) / 2; \\',
          '            if ((table)[mid_].id < (id_value)) lo_ = mid_ + 1; else hi_ = mid_; } \\',
          '        return (lo_ < (count) && (table)[lo_].id == (id_value)) ? (T)&(table)[lo_] : (T)0; \\',
          '    } while (0)',
          'static inline const FwsMessage *fws_memo_find(unsigned id) '
          '{ FWS_TABLES_BSEARCH(const FwsMessage *, fws_memos, FWS_MEMO_COUNT, id); }',
          'static inline const FwsMessage *fws_info_find(unsigned id) '
          '{ FWS_TABLES_BSEARCH(const FwsMessage *, fws_infos, FWS_INFO_COUNT, id); }',
          'static inline const FwsMessage *fws_limitation_find(unsigned id) '
          '{ FWS_TABLES_BSEARCH(const FwsMessage *, fws_limitations, FWS_LIMITATION_COUNT, id); }',
          'static inline const FwsMessage *fws_inop_sys_find(unsigned id) '
          '{ FWS_TABLES_BSEARCH(const FwsMessage *, fws_inop_sys, FWS_INOP_SYS_COUNT, id); }',
          'static inline const char *fws_memo_text(unsigned id) '
          '{ const FwsMessage *m = fws_memo_find(id); return m ? m->text : NULL; }',
          'static inline const char *fws_info_text(unsigned id) '
          '{ const FwsMessage *m = fws_info_find(id); return m ? m->text : NULL; }',
          'static inline const char *fws_limitation_text(unsigned id) '
          '{ const FwsMessage *m = fws_limitation_find(id); return m ? m->text : NULL; }',
          'static inline const char *fws_inop_sys_text(unsigned id) '
          '{ const FwsMessage *m = fws_inop_sys_find(id); return m ? m->text : NULL; }',
          'static inline const FwsProcedureDef *fws_abnormal_procedure(unsigned id) '
          '{ FWS_TABLES_BSEARCH(const FwsProcedureDef *, fws_abnormal_procedures, FWS_ABNORMAL_PROCEDURE_COUNT, id); }',
          'static inline const FwsProcedureDef *fws_deferred_procedure(unsigned id) '
          '{ FWS_TABLES_BSEARCH(const FwsProcedureDef *, fws_deferred_procedures, FWS_DEFERRED_PROCEDURE_COUNT, id); }',
          'static inline const FwsProcedureDef *fws_normal_procedure(unsigned id) '
          '{ FWS_TABLES_BSEARCH(const FwsProcedureDef *, fws_normal_procedures, FWS_NORMAL_PROCEDURE_COUNT, id); }',
          '/* Index of a normal checklist in ascending-id order (FBW getNormalProceduresKeysSorted().indexOf), or -1. */',
          'static inline int fws_normal_procedure_index(unsigned id)',
          '{ const FwsProcedureDef *p = fws_normal_procedure(id); return p ? (int)(p - fws_normal_procedures) : -1; }',
          '/* deferredProcedureIds.indexOf(id) (== FwsDeferredProcedureType), or -1. */',
          'static inline int fws_deferred_procedure_id_index(unsigned id)',
          '{ int i; for (i = 0; i < FWS_DEFERRED_TYPE_COUNT; ++i) if (fws_deferred_procedure_ids[i] == id) return i;'
          ' return -1; }',
          '/* pfdMemoDisplay.includes(id) */',
          'static inline int fws_is_pfd_memo(unsigned id)',
          '{ int i; for (i = 0; i < FWS_PFD_MEMO_DISPLAY_COUNT; ++i) if (fws_pfd_memo_display[i] == id) return 1;'
          ' return 0; }',
          '',
          '#endif',
          '']
    return '\n'.join(L)


# How the revision was found (git, or the ATTRIBUTION.md pin and why) depends on the machine that
# ran the generator, e.g. an unmounted volume path; --check compares everything else.
MACHINE_FIELDS = ('upstream_commit_source',)


def same_output(committed: str, generated: str) -> bool:
    """True when a committed output matches the generated one, ignoring machine-specific fields."""
    if committed == generated:
        return True
    try:
        a, b = json.loads(committed), json.loads(generated)
    except ValueError:
        return False
    if not isinstance(a, dict) or not isinstance(b, dict):
        return False
    for field in MACHINE_FIELDS:
        a.pop(field, None)
        b.pop(field, None)
    return a == b


def render_manifest(t: Tables) -> str:
    files = {}
    for path in t.source_paths:
        rel = path.resolve().relative_to(t.upstream.resolve()).as_posix()
        files[rel] = hashlib.sha256(path.read_bytes()).hexdigest()
    doc = {
        'generator': 'tools/build_fws_tables.py',
        'output': 'native/avionics/a380x_fws_tables.h',
        'upstream_repository': 'flybywiresim/aircraft',
        'upstream_commit': t.commit,
        'upstream_commit_source': t.commit_source,
        'upstream_sources_sha256': files,
        'counts': census(t),
        'duplicate_keys_resolved_last_wins': [
            {'dictionary': d[0], 'key': d[1], 'overridden': str(d[2]), 'by': str(d[3])} for d in t.prog.duplicates],
        'not_parsed': {
            f'{ECAM_REL}/NormalProceduresBefore2020.ts': 'not imported by index.ts / ProcedureLinesGenerator.ts',
        },
    }
    return json.dumps(doc, indent=2, sort_keys=True) + '\n'


def census(t: Tables):
    return {
        'memos': len(t.messages['EcamMemos']),
        'infos': len(t.messages['EcamInfos']),
        'limitations': len(t.messages['EcamLimitations']),
        'inop_sys': len(t.messages['EcamInopSys']),
        'pfd_memo_display': len(t.pfd_memo_display),
        'abnormal_procedures': len(t.abnormal),
        'abnormal_items': sum(len(p['items']) for p in t.abnormal),
        'abnormal_non_sensed_procedures': sum(1 for p in t.abnormal if p['sensed'] is False),
        'deferred_procedures': len(t.deferred),
        'deferred_items': sum(len(p['items']) for p in t.deferred),
        'normal_procedures': len(t.normal),
        'normal_items': sum(len(p['items']) for p in t.normal),
        'abn_non_sensed_overview': len(t.overview),
        'abn_sub_menu': len(t.submenu),
        'max_items_per_procedure': t.max_items,
        'max_level': t.max_level,
        'max_line_bytes': t.max_line_bytes,
        'abnormal_sensed_dictionaries': {f'{rel}:{name}': n for rel, name, kind, n in t.abn_dicts},
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='fail if the committed outputs are stale')
    ap.add_argument('--upstream', type=Path, default=DEFAULT_UPSTREAM, help='FBW aircraft checkout')
    ap.add_argument('--header', type=Path, default=OUT_H)
    ap.add_argument('--manifest', type=Path, default=OUT_JSON)
    ap.add_argument('--types-header', type=Path, default=TYPES_H, help='a380x_fws_types.h to validate against')
    args = ap.parse_args(argv)
    try:
        tables = build(args.upstream, args.types_header)
    except FwsParseError as e:
        print(f'build_fws_tables: error: {e}', file=sys.stderr)
        return 2
    header, manifest = render_header(tables), render_manifest(tables)
    if tables.max_items_warning:
        print(f'build_fws_tables: WARNING: {tables.max_items_warning}', file=sys.stderr)
    if args.check:
        stale = [p for p, text in ((args.header, header), (args.manifest, manifest))
                 if not p.is_file() or not same_output(p.read_text(encoding='utf-8'), text)]
        if stale:
            print('build_fws_tables: stale: ' + ', '.join(str(p) for p in stale)
                  + ' (run tools/build_fws_tables.py)', file=sys.stderr)
            return 1
        print(f'build_fws_tables: up to date ({tables.commit}, {tables.commit_source})')
        return 0
    args.header.write_text(header, encoding='utf-8')
    args.manifest.write_text(manifest, encoding='utf-8')
    c = census(tables)
    print(f'{args.header.name}: {c["memos"]} memos, {c["infos"]} infos, {c["limitations"]} limitations, '
          f'{c["inop_sys"]} inop sys, {c["abnormal_procedures"]} abnormal ({c["abnormal_items"]} items), '
          f'{c["deferred_procedures"]} deferred, {c["normal_procedures"]} normal; '
          f'upstream {tables.commit} ({tables.commit_source})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
