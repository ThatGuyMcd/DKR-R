#!/usr/bin/env python3
"""Render the authored SDK book and exact headers as a self-contained HTML guide.

Document rendering/coverage only: never loads a mod or launches the game.
The source remains the SDK's plain-text book; no online assets are required.
"""
from __future__ import annotations
import argparse
import html
import json
from pathlib import Path
import re


def inline(value: str) -> str:
    escaped = html.escape(value)
    return re.sub(r'`([^`]+)`', r'<code>\1</code>', escaped)


def render(sdk: Path, output: Path) -> dict:
    if output.exists():
        raise ValueError('Choose a new guide output file')
    source = (sdk / 'MODDERS-GUIDE.txt').read_text(encoding='utf-8')
    headers = [(name, (sdk / 'include/dkr' / name).read_text(encoding='utf-8'))
               for name in ('magic_codes.h', 'game.h')]
    public = sorted(set(re.findall(r'(?:DKR_IMPORT|static inline)[^;{]*?\b(dkr_\w+)\s*\(',
                                   '\n'.join(text for _, text in headers))))
    missing = [name for name in public if name not in source]
    if missing:
        raise ValueError('Undocumented public functions: ' + ', '.join(missing))
    sections, body, paragraph, code = [], [], [], None
    list_open = False

    def flush():
        nonlocal paragraph, list_open
        if paragraph:
            body.append('<p>' + inline(' '.join(paragraph)) + '</p>')
            paragraph = []
        if list_open:
            body.append('</ul>')
            list_open = False

    for line in source.splitlines():
        if line.startswith('```'):
            if code is not None:
                body.append('<pre><code>' + html.escape('\n'.join(code)) + '</code></pre>')
                code = None
            else:
                flush()
                code = []
            continue
        if code is not None:
            code.append(line)
            continue
        heading = re.match(r'^(#{1,3}) (.+)$', line)
        if heading:
            flush()
            level, title = len(heading[1]), heading[2]
            anchor = 'section-' + str(len(sections) + 1)
            sections.append((level, title, anchor))
            body.append(f'<h{level} id="{anchor}">{inline(title)}</h{level}>')
        elif not line.strip():
            flush()
        elif line.startswith('- '):
            if paragraph:
                flush()
            if not list_open:
                body.append('<ul>')
                list_open = True
            body.append('<li>' + inline(line[2:]) + '</li>')
        elif list_open and line.startswith('  '):
            # Continuations belong to the preceding list item.
            body[-1] = body[-1][:-5] + ' ' + inline(line.strip()) + '</li>'
        else:
            if list_open:
                flush()
            paragraph.append(line.strip())
    flush()
    if code is not None:
        raise ValueError('Unclosed code fence in guide')
    body.append('<h2 id="declarations">Appendix Full SDK headers</h2>'
                '<p>These are the exact shipped declarations, comments, fields and constants. '
                'Weak import bodies are metadata, not runtime fallbacks.</p>')
    for name, text in headers:
        body.append('<h3>' + html.escape(name) + '</h3><pre><code>' + html.escape(text) + '</code></pre>')
    toc = ''.join('<li><a href="#' + anchor + '">' + html.escape(title) + '</a></li>'
                  for level, title, anchor in sections if level == 2)
    page = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Modders Guide to Custom Magic Code Creation</title><style>
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:#f5f6f8;color:#172b3a;
font:17px/1.65 system-ui,Segoe UI,sans-serif}a{color:#075a92;overflow-wrap:anywhere}
nav{position:fixed;inset:0 auto 0 0;width:300px;overflow:auto;padding:24px;background:#0b2636;color:#fff0c2}
nav a{color:#e1f1f5;text-decoration:none}nav a:hover,nav a:focus{text-decoration:underline;color:#ffca62}
nav ol{padding-left:20px;font-size:14px;line-height:1.5}nav li{margin:0 0 10px}
nav strong{display:block;font-size:20px}nav small{color:#b8cbd2}
main{max-width:1320px;margin-left:300px;padding:44px 6vw 100px;min-width:0}
h1,h2,h3{line-height:1.25;color:#073d59;scroll-margin-top:24px}h1{font-size:38px}
h2{font-size:29px;border-top:2px solid #c5d4df;padding-top:32px;margin-top:64px}h3{font-size:22px;margin-top:30px}
p,li{overflow-wrap:anywhere}li{margin-bottom:8px}code{font:0.88em/1.6 Consolas,ui-monospace,monospace}
p code,li code{background:#e4ebf0;padding:2px 4px;border-radius:4px}
pre{max-width:100%;overflow-x:auto;background:#0b2636;color:#eef4ed;border-radius:10px;padding:22px;line-height:1.6}
pre code{white-space:pre;overflow-wrap:normal}footer{margin-top:60px;color:#40586a;font-size:14px}
@media(max-width:950px){nav{position:static;width:auto;max-height:none}nav ol{columns:2;column-gap:32px}
nav li{break-inside:avoid}main{margin:0;padding:28px 6vw 70px}}
@media(max-width:580px){body{font-size:16px}nav ol{columns:1}h1{font-size:29px}h2{font-size:25px}pre{padding:14px}}
@media print{nav{display:none}main{margin:0;max-width:none;padding:0}body{background:white;font-size:10pt}
pre{background:#f1f3f5;color:#111;white-space:pre-wrap;overflow:visible}pre code{white-space:pre-wrap}
h2{break-before:page}h1,h2,h3{break-after:avoid}a{color:inherit;text-decoration:none}}
</style></head><body><nav aria-label="Chapters"><strong>Custom Magic Codes</strong>
<small>DKR-R v1.0.7 beta 2 · API 1</small><ol>''' + toc + '''<li><a href="#declarations">Full SDK headers</a></li>
</ol><a href="#section-1">Back to top</a></nav><main>''' + '\n'.join(body) + '''
<footer>DKR-R Custom Magic Codes API 1. Keep this guide with the matching headers and examples.
This document is self-contained and makes no network requests.</footer></main></body></html>'''
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(page, encoding='utf-8')
    return {'chapters': sum(level == 2 for level, _, _ in sections),
            'public_functions': len(public), 'documented_functions': public,
            'source_words': len(source.split()), 'output': str(output),
            'executed_mod_code': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=Path(__file__).resolve().parents[1] / 'modding')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(render(args.sdk, args.output), indent=2))


if __name__ == '__main__':
    main()
