#!/usr/bin/env python3
"""Render bounded GitHub Markdown from existing CI artifacts (never run tests)."""
from __future__ import annotations

import argparse
from collections import Counter
import html
import json
import math
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

READ_LIMIT = 16 * 1024 * 1024
STATUSES = ('passed', 'failed', 'skipped')


def clipped(value, limit=180):
    text = ''.join(c for c in str(value) if c in '\n\t' or (ord(c) >= 32 and ord(c) != 127))
    return text if len(text) <= limit else text[:limit] + '… [truncated]'


def escape(value, limit=180):
    text = html.escape(clipped(value, limit).replace('\n', ' ').replace('\t', ' '))
    # Numeric entities keep artifact names literal in tables and inline Markdown.
    return re.sub(r'[\\`*_{}\[\]()#+.!|~-]', lambda match: f'&#{ord(match[0])};', text)


def block(value):
    return '<pre>' + html.escape(clipped(value, 1200)) + '</pre>'


def duration(value):
    if isinstance(value, bool):
        raise ValueError('Boolean duration/metric')
    number = float(value)
    if not math.isfinite(number) or number < 0:
        raise ValueError('Nonfinite or negative duration/metric')
    return number


def totals(counts):
    return ', '.join(f'{counts.get(status, 0)} {status}' for status in STATUSES)


def junit_summary(text):
    if '<!DOCTYPE' in text.upper() or '<!ENTITY' in text.upper():
        raise ValueError('DTD/entity declarations are unsupported')
    root = ET.fromstring(text)
    if root.tag not in ('testsuite', 'testsuites'):
        raise ValueError('Expected JUnit testsuite or testsuites')
    cases = []
    for case in root.iter('testcase'):
        failed = case.find('failure') is not None or case.find('error') is not None or case.get('status') in ('failed', 'fail')
        skipped = case.find('skipped') is not None or case.get('status') in ('notrun', 'disabled', 'skipped')
        status = 'failed' if failed else 'skipped' if skipped else 'passed'
        cases.append((case, status, duration(case.get('time', '0'))))
    if not cases:
        return ['No test cases recorded; this is not evidence of a passing test run.']
    lines = [totals(Counter(status for _, status, _ in cases)) + '.', '']
    failures = [case for case, status, _ in cases if status == 'failed']
    for case in failures[:12]:
        lines.append('**Failed: ' + escape(case.get('name', '(unnamed)')) + '**')
        messages = []
        for tag in ('failure', 'error', 'system-out', 'system-err'):
            for node in case.findall(tag):
                messages.append(clipped(node.get('message', ''), 600) + '\n' + clipped(''.join(node.itertext()), 1200))
        lines.extend([block('\n'.join(messages) or 'No failure output recorded.'), ''])
    if len(failures) > 12:
        lines.append(f'{len(failures) - 12} additional failures omitted; inspect the JUnit artifact.')
    lines += ['', 'Slowest tests:', '', '| Test | Seconds | Result |', '| --- | ---: | --- |']
    for case, status, seconds in sorted(cases, key=lambda row: row[2], reverse=True)[:5]:
        lines.append(f'| {escape(case.get("name", "(unnamed)"))} | {seconds:.3f} | {status} |')
    return lines


def regression_summary(text):
    report = json.loads(text)
    if not isinstance(report, dict) or report.get('schema_version') != 1 or not isinstance(report.get('scenarios'), list):
        raise ValueError('Expected harness schema_version 1 with scenarios')
    scenarios = report['scenarios']
    counts = Counter()
    seen = set()
    for item in scenarios:
        if not isinstance(item, dict) or not isinstance(item.get('id'), str) or not item['id'] or item['id'] in seen or item.get('status') not in STATUSES:
            raise ValueError('Invalid or duplicate harness scenario')
        seen.add(item['id'])
        counts[item['status']] += 1
        if not isinstance(item.get('errors', []), list) or not all(isinstance(value, str) for value in item.get('errors', [])):
            raise ValueError('Invalid scenario errors')
        if not isinstance(item.get('metrics', {}), dict):
            raise ValueError('Invalid scenario metrics')
        for value in item.get('metrics', {}).values():
            duration(value)
        fingerprints = item.get('fingerprints', {})
        if not isinstance(fingerprints, dict) or not all(isinstance(values, list) and all(isinstance(value, str) for value in values) for values in fingerprints.values()):
            raise ValueError('Invalid scenario fingerprints')
    declared = report.get('summary')
    if not isinstance(declared, dict) or any(type(declared.get(status)) is not int or declared[status] != counts[status] for status in STATUSES):
        raise ValueError('Harness summary does not match scenario outcomes')
    lines = [escape(report.get('suite', 'Regression/replay')) + ': ' + totals(counts) + '.', '']
    if not scenarios:
        return lines + ['No scenarios recorded; no passing regression evidence.']
    lines += ['| Scenario | Result | Process median (ms) |', '| --- | --- | ---: |']
    for item in scenarios[:50]:
        value = item.get('metrics', {}).get('process.median_ms')
        measured = f'{duration(value):.3f}' if value is not None else '—'
        lines.append(f'| {escape(item["id"])} | {item["status"]} | {measured} |')
    if len(scenarios) > 50:
        lines.append(f'{len(scenarios) - 50} additional scenarios omitted; inspect report.json.')
    for item in scenarios[:50]:
        errors = item.get('errors', [])
        metrics = {key: value for key, value in item.get('metrics', {}).items()
                   if (key.startswith('checkpoint.') and key.endswith('_ms')) or key == 'rollback.tick.mean_ms'}
        prints = [value for values in item.get('fingerprints', {}).values() for value in values]
        if not errors and not metrics and not prints:
            continue
        lines += ['', '**' + escape(item['id']) + '**']
        if errors:
            lines.append(block('\n'.join(clipped(error, 300) for error in errors[:4])))
        if metrics:
            lines.append('; '.join(f'{escape(key)}: {duration(value):.3f} ms' for key, value in sorted(metrics.items())[:8]))
        if prints:
            lines.append(block('\n'.join(clipped(value, 240) for value in prints[:4])))
    lines += ['', 'Process timing includes launch overhead. Checkpoint benchmarks use synthetic data; replay results reflect only the scenarios shown.']
    return lines


def cache_summary(text):
    lines = [line for line in text.splitlines() if re.search(r'compile requests|cacheable|hits|misses|hit rate|uncacheable|cache size', line, re.IGNORECASE)]
    return [block('\n'.join(lines[:14]))] if lines else ['Unrecognized cache statistics format; supplied details:', block(text)]


def render(junit=None, regression=None, cache_stats=None, label='CI results'):
    lines, invalid = ['## ' + escape(label), ''], False
    for title, path, renderer in (('Host tests', junit, junit_summary), ('Regression and replay', regression, regression_summary), ('Compiler cache', cache_stats, cache_summary)):
        if path is None:
            continue
        lines += ['### ' + title, '']
        try:
            with Path(path).open('rb') as source:
                payload = source.read(READ_LIMIT + 1)
            if len(payload) > READ_LIMIT:
                raise ValueError('Artifact exceeds 16 MiB summary input limit')
            lines += renderer(payload.decode('utf-8'))
        except FileNotFoundError:
            lines += ['Missing artifact: ' + escape(path) + '. An earlier configure, build, or test step may have failed; no result is inferred.']
        except (OSError, ValueError, TypeError, KeyError, ET.ParseError) as error:
            invalid = True
            lines += ['Invalid artifact: ' + escape(path) + '. ' + escape(error, 300) + '. No successful result is inferred.']
        lines.append('')
    bounded, size = [], 0
    for line in lines:
        if size + len(line) > 60000:
            bounded.append('\nSummary truncated; inspect full artifacts.')
            break
        bounded.append(line)
        size += len(line) + 1
    return '\n'.join(bounded), invalid


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--junit', type=Path)
    parser.add_argument('--regression', type=Path)
    parser.add_argument('--cache-stats', type=Path)
    parser.add_argument('--label', default='CI results', help='summary heading')
    parser.add_argument('--output', type=Path, help='append Markdown to this file instead of stdout')
    args = parser.parse_args(argv)
    if not any((args.junit, args.regression, args.cache_stats)):
        parser.error('provide at least one report path')
    text, invalid = render(args.junit, args.regression, args.cache_stats, args.label)
    try:
        if args.output:
            with args.output.open('a', encoding='utf-8') as output:
                output.write('\n' + text + '\n')
        else:
            print(text)
    except OSError as error:
        print(f'ci-summary: {error}', file=sys.stderr)
        return 1
    return int(invalid)


if __name__ == '__main__':
    raise SystemExit(main())
