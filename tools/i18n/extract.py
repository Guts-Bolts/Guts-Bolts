#!/usr/bin/env python3
"""Finds the English text shown in the Player, Studio and website, so it can be
translated. Writes tools/i18n/strings.txt (one string per line, JSON-quoted).

Run:  python3 tools/i18n/extract.py
Then add translations for any new lines to website/lang/<code>.json, and run
tools/i18n/check.py to check them.
"""
import html, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# C++ folders with UI. (scene/scripting/renderer/net mostly print logs, not UI.)
CPP_DIRS = ['src/player', 'src/editor', 'src/game', 'src/core', 'src/online']
CPP_FILES = ['src/scene/MaterialGraph.cpp']   # (its box names and help show in Studio)
CPP_SKIP = re.compile(r'(Log::|LOG_|printf|fprintf|std::cerr|std::cout|logLine|logf\(|"\s*\]|\[\s*"|\.value\(\s*"|\.contains\(\s*"|call\(\s*"|getenv|#include|\.find\(\s*"|== *"|!= *"|starts_with|ends_with|fopen|PushID|BeginChild\(\s*"|"Content-Type"|R"\()')
JS_FILES = ['website/app/app.js', 'website/app/index.html', 'website/index.html', 'website/lang/i18n.js']

CPP_STR = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
JS_STR = re.compile(r"'((?:[^'\\\n]|\\.)*)'|\"((?:[^\"\\\n]|\\.)*)\"")

def looks_like_text(s):
    s = s.split('##')[0]
    if len(s) < 2 or not re.search(r'[A-Za-z]{2}', s):
        return False
    if re.fullmatch(r'[a-z0-9_.\-/:]+', s):          # ids, keys, file names
        return False
    if re.fullmatch(r'[A-Za-z][A-Za-z0-9]*[A-Z][A-Za-z0-9]*', s) and ' ' not in s:   # camelCase names
        return False
    if re.search(r'https?:|\.(png|jpg|json|gbscene|lua|ttf|obj|wav|ogg|mp3|js|css|html|cpp|h|txt|exe|ttf|ttc|otf)\b|^[./]|::|[{};]|\bgl_|#version', s):
        return False
    if re.fullmatch(r'[A-Z_0-9]+', s):                 # CONSTANTS
        return False
    if re.fullmatch(r'%[-0-9.]*[a-z]+', s.strip()):
        return False
    if re.search(r'/dev/|%\d+\[|^-W|\*\.|^--|^#\w|\b\w+\.[A-Z]\w*\s*[=(,]|\w:\w+\(|^\w+\.\w+$|^\w+\+\w+$|^[^A-Za-z]*$', s):
        return False                                    # shell, scanf, code, CSS selectors, Ctrl+X
    if s[0] in ",.;:)]'\"&|" or re.search(r"\]\s*,|:\s*\[|raw\(|\? *bolts", s):
        return False                                    # bits of JS, not text
    return True

def unescape_c(s):
    try:
        return bytes(s, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')
    except Exception:
        return s

def js_strings(text):
    """The '...' and \"...\" strings in JavaScript (not comments, not `templates`)."""
    i, n, out = 0, len(text), []
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            i = text.find('\n', i); i = n if i < 0 else i
        elif text.startswith('/*', i):
            i = text.find('*/', i); i = n if i < 0 else i + 2
        elif c in '\'"`':
            j, buf = i + 1, ''
            while j < n and text[j] != c:
                if text[j] == '\\': buf += text[j:j+2]; j += 2; continue
                if c != '`' and text[j] == '\n': break
                buf += text[j]; j += 1
            if c != '`':
                out.append(buf.replace("\\'", "'").replace('\\"', '"'))
            i = j + 1
        else:
            i += 1
    return out

found = {}
def add(s, where):
    s = s.split('##')[0]
    if s.strip() != s:          # leading/trailing spaces are layout; translate the words only
        return
    if looks_like_text(s):
        found.setdefault(s, where)

def cpp_files():
    for d in CPP_DIRS:
        for dirpath, _, files in os.walk(os.path.join(ROOT, d)):
            for f in sorted(files):
                if f.endswith('.cpp') or f.endswith('.h'):
                    yield dirpath, f
    for p in CPP_FILES:
        yield os.path.dirname(os.path.join(ROOT, p)), os.path.basename(p)

for dirpath, f in cpp_files():
    path = os.path.join(dirpath, f)
    rel = os.path.relpath(path, ROOT)
    if 'StudioMcp' in f or 'AiTools' in f or f.startswith('I18n') or f.startswith('Lua') or f.startswith('PropertiesPanel'):   # (Properties keep script names)
        continue
    for n, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
        st = line.strip()
        if st.startswith('//') or CPP_SKIP.search(line):
            continue
        code = line.split('//')[0] if '"' not in line.split('//')[0][-1:] else line
        for m in CPP_STR.finditer(code):
            add(unescape_c(m.group(1)), f'{rel}:{n}')

for f in JS_FILES:
    path = os.path.join(ROOT, f)
    text = open(path, encoding='utf-8').read()
    # Text between tags in html`...` templates and HTML pages.
    for m in re.finditer(r'>([^<>`$]+)<', text):
        for part in re.split(r'\$\{[^}]*\}', m.group(1)):
            add(' '.join(html.unescape(part).split()), f)   # (the page shows &amp; as &)
    for m in re.finditer(r'(?:placeholder|title|aria-label)="([^"$]+)"', text):
        add(html.unescape(m.group(1)).strip(), f)
    if f.endswith('.js'):
        for s in js_strings(text):
            if ' ' in s and not re.search(r'[<>=]', s):
                add(s, f)

out = os.path.join(ROOT, 'tools/i18n/strings.txt')
with open(out, 'w', encoding='utf-8') as fp:
    for s in sorted(found):
        fp.write(json.dumps(s, ensure_ascii=False) + '\n')
print(len(found), 'strings ->', os.path.relpath(out, ROOT))
