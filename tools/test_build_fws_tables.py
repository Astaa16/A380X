#!/usr/bin/env python3
"""Tests of tools/build_fws_tables.py: a regex census of the FBW TypeScript against the generated
header, the TS subset parser (escapes, spreads, templates, keys) and the --check mode."""
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import build_fws_tables as gen  # noqa: E402

ECAM = gen.DEFAULT_UPSTREAM / gen.ECAM_REL
HEADER = gen.OUT_H
MANIFEST = gen.OUT_JSON


def block(text, start_pattern):
    """Text of the `export const X ... = {` / `[` literal starting at start_pattern (brace matched)."""
    m = re.search(start_pattern, text)
    if not m:
        raise AssertionError(f'{start_pattern} not found')
    i = text.index('=', m.end()) + 1
    while text[i] in ' \t\n':
        i += 1
    opener = text[i]
    closer = {'{': '}', '[': ']'}[opener]
    depth, j = 0, i
    while True:
        if text[j] == opener:
            depth += 1
        elif text[j] == closer:
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
        j += 1


def strip_comments(text):
    """Census helper: drop // and /* */ comments (FBW keeps commented-out items, e.g. ata21-22-23.ts 213800021)."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def header_define(name):
    m = re.search(rf'#define {name} (\d+)', HEADER.read_text(encoding='utf-8'))
    return int(m.group(1))


class Census(unittest.TestCase):
    """Independent regex counts of the sources vs the generated tables."""

    @classmethod
    def setUpClass(cls):
        cls.index = strip_comments((ECAM / 'index.ts').read_text(encoding='utf-8'))
        cls.abn_files = sorted((ECAM / 'AbnormalSensed').glob('*.ts'))

    def test_messages(self):
        for name, define, key_re in (('EcamMemos', 'FWS_MEMO_COUNT', r"^\s+'\d+':\s*'"),
                                     ('EcamInfos', 'FWS_INFO_COUNT', r"^\s+\d+:\s*'"),
                                     ('EcamLimitations', 'FWS_LIMITATION_COUNT', r"^\s+\d+:\s*'"),
                                     ('EcamInopSys', 'FWS_INOP_SYS_COUNT', r"^\s+\d+:\s*'")):
            body = block(self.index, rf'export const {name}\b')
            count = len(re.findall(key_re, body, re.M))
            self.assertEqual(count, header_define(define), name)
        self.assertEqual((132, 51, 34, 337), tuple(header_define(d) for d in (
            'FWS_MEMO_COUNT', 'FWS_INFO_COUNT', 'FWS_LIMITATION_COUNT', 'FWS_INOP_SYS_COUNT')))

    def _procedure_census(self, type_name):
        procs = items = 0
        for path in self.abn_files:
            text = strip_comments(path.read_text(encoding='utf-8-sig'))
            for m in re.finditer(rf'export const (\w+): \{{ \[n: number\]: {type_name} \}}', text):
                body = block(text, rf'export const {m.group(1)}\b')
                procs += len(re.findall(r"^  '?\d{9}'?:\s*\{", body, re.M))
                items += len(re.findall(r'\bname\s*:', body))
                items += len(re.findall(r'\.\.\.FMS_PRED_UNRELIABLE_CHECKLIST_ITEM', body))
        return procs, items

    def test_abnormal_and_deferred(self):
        procs, items = self._procedure_census('AbnormalProcedure')
        self.assertEqual(procs, header_define('FWS_ABNORMAL_PROCEDURE_COUNT'))
        self.assertEqual(items, header_define('FWS_ABNORMAL_ITEM_COUNT'))
        procs, items = self._procedure_census('DeferredProcedure')
        self.assertEqual(procs, header_define('FWS_DEFERRED_PROCEDURE_COUNT'))
        self.assertEqual(items, header_define('FWS_DEFERRED_ITEM_COUNT'))
        # every procedure title + every 'items:' of the files
        total_items_keys = sum(len(re.findall(r'\bitems\s*:', strip_comments(p.read_text(encoding='utf-8-sig'))))
                               for p in self.abn_files)
        self.assertEqual(total_items_keys, header_define('FWS_ABNORMAL_PROCEDURE_COUNT')
                         + header_define('FWS_DEFERRED_PROCEDURE_COUNT'))

    def test_normal_and_non_sensed(self):
        text = strip_comments((ECAM / 'NormalProcedures.ts').read_text(encoding='utf-8'))
        body = block(text, r'export const EcamNormalProcedures\b')
        self.assertEqual(len(re.findall(r'^  \d+:\s*\{', body, re.M)), header_define('FWS_NORMAL_PROCEDURE_COUNT'))
        self.assertEqual(len(re.findall(r'\bname\s*:', body)), header_define('FWS_NORMAL_ITEM_COUNT'))
        text = (ECAM / 'AbnormalNonSensedProcedures.ts').read_text(encoding='utf-8')
        self.assertEqual(len(re.findall(r'\{ id: \d+, category:', text)),
                         header_define('FWS_ABN_NON_SENSED_OVERVIEW_COUNT'))
        body = block(self.index, r'export const EcamAbNormalSensedSubMenuVector\b')
        self.assertEqual(len(re.findall(r"'[^']+'", body)), header_define('FWS_ABN_SUB_MENU_COUNT'))

    def test_manifest(self):
        doc = json.loads(MANIFEST.read_text(encoding='utf-8'))
        self.assertRegex(doc['upstream_commit'], r'^[0-9a-f]{40}$')
        for rel, digest in doc['upstream_sources_sha256'].items():
            self.assertRegex(digest, r'^[0-9a-f]{64}$')
            self.assertTrue((gen.DEFAULT_UPSTREAM / rel).is_file(), rel)
        self.assertIn(f'{gen.ECAM_REL}/AbnormalSensed/secondary-failures.ts', doc['upstream_sources_sha256'])
        self.assertEqual(doc['counts']['abnormal_procedures'], header_define('FWS_ABNORMAL_PROCEDURE_COUNT'))
        self.assertEqual(doc['duplicate_keys_resolved_last_wins'], [])

    def test_exact_entries(self):
        h = HEADER.read_text(encoding='utf-8')
        # index.ts:37 and ata26.ts:21-43 / NormalProcedures.ts:37-57
        self.assertIn('{1001u, "000001001", " \\x1b<7m\\x1b" "4mT.O\\x1bm"},', h)
        self.assertIn('{260800001u, "260800001", "\\x1b<2m\\x1b" "4mAPU FIRE", 1, FWS_RECOMMENDATION_LAND_ASAP, '
                      '-1, -1, 3, 0, &fws_abnormal_items[', h)
        self.assertIn('{"APU AGENT", "DISCH", NULL, 30, 1, 0, -1, -1, -1, -1, 1},', h)
        self.assertIn('{"BEACON", "ON", NULL, 0, 1, 0, -1, 0, -1, -1, 0},', h)
        self.assertIn('{"VLV STILL FULL OPEN", NULL, NULL, 60, 1, 0, -1, -1, 1, 0, 1},', h)
        self.assertIn('{2u, "2", "\\x1b<4mLAND ANSA"},', h)


class Parser(unittest.TestCase):
    def parse(self, source, name='X'):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        (tmp / 'mod.ts').write_text(source, encoding='utf-8')
        prog = gen.Program(tmp, tmp / 'mod.ts', tmp)
        return prog.const(prog.module(tmp / 'mod.ts'), name), prog

    def test_escapes(self):
        v, _ = self.parse("export const X = ['\\x1b4m', \"\\x1b'm\", '\\u00b0\\xa0', '\\u{41}', 'a\\'b', `t\\n`];")
        self.assertEqual(v, ['\x1b4m', "\x1b'm", '\u00b0\u00a0', 'A', "a'b", 't\n'])
        self.assertEqual(gen.c_string('\x1b4m'), '"\\x1b" "4m"')
        self.assertEqual(gen.c_string('\x1b<4m'), '"\\x1b<4m"')
        self.assertEqual(gen.c_string('\xa0A'), '"\\xc2\\xa0" "A"')
        self.assertEqual(gen.c_string('??='), '"\\?\\?="')
        self.assertEqual(gen.c_string('a"b\\c'), '"a\\"b\\\\c"')
        self.assertEqual(gen.js_len('\xa0\x1b'), 2)

    def test_literals(self):
        src = """// comment
        /* block */ const Y = 1;
        export const B: { [n: string]: string } = { a: 'x', };
        export const X: { [n: number]: Foo } = {
          ...B,
          'q': 'quoted', 12: 'num', 'a': 'override',
          t: `${'\\xa0'.repeat(3)}T${'!'}`,
          c: 'a' + "b",
          arr: [1, 2, ],
          e: E.Two,
        };
        export enum E { One, Two }
        """
        v, prog = self.parse(src)
        self.assertEqual(v['q'], 'quoted')
        self.assertEqual(v['12'], 'num')
        self.assertEqual(v['a'], 'override')
        self.assertEqual(v['t'], '\xa0\xa0\xa0T!')
        self.assertEqual(v['c'], 'ab')
        self.assertEqual(v['arr'], [1, 2])
        self.assertEqual(v['e'], gen.EnumRef('E', 'Two'))
        self.assertEqual([d[1] for d in prog.duplicates], ['a'])

    def test_fails_loudly(self):
        for bad in ("export const X = foo();", "export const X = [] as number[];", "export const X = { a };",
                    "export const X = Y;", "export const X = 'unterminated;", "export const X = 1 * 2;",
                    "export const X = { [k]: 1 };", "export const X = '\\077';"):
            with self.subTest(bad=bad), self.assertRaises(gen.FwsParseError):
                self.parse(bad)

    def test_item_validation(self):
        styles = ['Standard', 'Cyan']
        with self.assertRaises(gen.FwsParseError):
            gen.convert_item({'name': 'A', 'sensed': True, 'bogus': 1}, 'w', styles)
        with self.assertRaises(gen.FwsParseError):
            gen.convert_item({'name': 'A'}, 'w', styles)
        item = gen.convert_item({'name': 'A', 'sensed': False, 'style': gen.EnumRef('ChecklistLineStyle', 'Cyan'),
                                 'labelNotCompleted': ''}, 'w', styles)
        self.assertEqual((item['labelNotCompleted'], item['style'], item['time']), ('', 'Cyan', None))


class CheckMode(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(HERE / 'build_fws_tables.py'), *args], capture_output=True,
                              text=True)

    def test_committed_outputs_are_current(self):
        r = self.run_tool('--check')
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_stale_outputs_fail(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        header, manifest = tmp / 'h.h', tmp / 'm.json'
        header.write_text(HEADER.read_text(encoding='utf-8').replace('APU FIRE', 'APU FIRE!'), encoding='utf-8')
        shutil.copy(MANIFEST, manifest)
        r = self.run_tool('--check', '--header', str(header), '--manifest', str(manifest))
        self.assertEqual(r.returncode, 1)
        self.assertIn('stale', r.stderr)
        r = self.run_tool('--header', str(header), '--manifest', str(manifest))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(header.read_text(encoding='utf-8'), HEADER.read_text(encoding='utf-8'))
        self.assertEqual(manifest.read_text(encoding='utf-8'), MANIFEST.read_text(encoding='utf-8'))

    def test_deterministic(self):
        t = gen.build(gen.DEFAULT_UPSTREAM)
        self.assertEqual(gen.render_header(t), gen.render_header(gen.build(gen.DEFAULT_UPSTREAM)))

    @unittest.skipUnless(shutil.which('clang') or shutil.which('cc'), 'no C compiler')
    def test_header_compiles(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        src = tmp / 't.c'
        src.write_text('#include "a380x_fws_tables.h"\nint main(void){return fws_abnormal_procedure(260800001u)'
                       '->num_items == 3 && fws_memo_text(1001u) ? 0 : 1;}\n', encoding='utf-8')
        cc = shutil.which('clang') or shutil.which('cc')
        r = subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(HEADER.parent), str(src), '-o',
                            str(tmp / 't')], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(subprocess.run([str(tmp / 't')]).returncode, 0)


if __name__ == '__main__':
    unittest.main()
