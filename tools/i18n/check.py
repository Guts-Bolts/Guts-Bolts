#!/usr/bin/env python3
"""Checks the language files in website/lang/ and says what's missing.

Run:  python3 tools/i18n/check.py
It fails (exit 1) if a file isn't valid JSON, or if a translation has different
%d / %s bits than its English line (the Player and Studio would skip those).
It also counts how many of the lines in tools/i18n/strings.txt each language has.

Run with --sort to put every file's lines in A-Z order (do it after adding
translations). Lines in A-Z order land in different places, so two pull requests
adding translations don't both add theirs at the very end and conflict.
"""
import glob, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KINDS = re.compile(r'%(?!%)[-+#0-9.*hlLzjtq]*[a-zA-Z]')

def kinds(s):
    return KINDS.findall(s.replace('%%', ''))

strings = set()
path = os.path.join(ROOT, 'tools/i18n/strings.txt')
if os.path.exists(path):
    strings = {json.loads(l) for l in open(path, encoding='utf-8') if l.strip()}

SORT = '--sort' in sys.argv[1:]

bad = 0
for f in sorted(glob.glob(os.path.join(ROOT, 'website/lang/*.json'))):
    name = os.path.basename(f)
    try:
        data = json.load(open(f, encoding='utf-8'))
    except ValueError as e:
        print(f'{name}: not valid JSON ({e})'); bad += 1; continue
    table = data.get('strings', {})
    if not data.get('name') or not isinstance(table, dict):
        print(f'{name}: needs "name" and "strings"'); bad += 1; continue
    for en, tr in table.items():
        if kinds(en) != kinds(tr):
            print(f'{name}: {en!r} -> {tr!r}: the %-bits don\'t match'); bad += 1
    if list(table) != sorted(table):
        if SORT:
            data['strings'] = dict(sorted(table.items()))
            with open(f, 'w', encoding='utf-8') as out:
                out.write(json.dumps(data, indent=1, ensure_ascii=False) + '\n')
        else:
            print(f'{name}: lines aren\'t in A-Z order (run with --sort)'); bad += 1
    have = len(strings & table.keys())
    print(f'{name}: {data["name"]}, {len(table)} lines ({have} of {len(strings)} in strings.txt)')

sys.exit(1 if bad else 0)
