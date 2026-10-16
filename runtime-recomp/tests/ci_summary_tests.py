#!/usr/bin/env python3
"""Exercise CI summaries using CTest XML and the real harness report writer."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import ci_summary
from harness_report import write_reports


class CiSummaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def artifact(self, name, text):
        path = self.root / name
        path.write_text(text, encoding='utf-8')
        return path

    def report(self):
        return {'schema_version': 1, 'suite': 'Synthetic checkpoint regression',
                'summary': {'passed': 1, 'failed': 0, 'skipped': 0},
                'scenarios': [{'id': 'checkpoint-benchmark', 'status': 'passed', 'errors': [],
                    'attempts': [], 'comparisons': [],
                    'metrics': {'process.median_ms': 42, 'checkpoint.hash_median_ms': .25},
                    'fingerprints': {'Checkpoint fingerprint: [0-9a-f]+': ['Checkpoint fingerprint: deadbeef']}}]}

    def test_nested_suites_count_cases_once_and_keep_failures_skips_and_slowest(self):
        junit = self.artifact('junit.xml', '''<testsuites tests="999"><testsuite>
          <testcase name="fast" time=".1" status="run"/>
          <testsuite><testcase name="slow failure" time="9"><failure message="broken">stack</failure><system-out>details</system-out></testcase></testsuite>
          <testcase name="skip"><skipped/></testcase><testcase name="disabled" status="notrun"/>
          <testcase name="error" time="2"><error>crashed</error></testcase>
        </testsuite></testsuites>''')
        text, invalid = ci_summary.render(junit=junit)
        self.assertFalse(invalid)
        self.assertIn('1 passed, 2 failed, 2 skipped', text)
        self.assertIn('broken\nstack\ndetails', text.replace('\n\n', '\n'))
        self.assertIn('| slow failure | 9.000 | failed |', text)
        self.assertLess(text.index('| slow failure'), text.index('| error'))
        self.assertNotIn('999', text)

    def test_real_harness_writer_schema_and_checkpoint_evidence(self):
        write_reports(self.root, self.report())
        text, invalid = ci_summary.render(junit=self.root / 'junit.xml', regression=self.root / 'report.json')
        self.assertFalse(invalid)
        self.assertIn('1 passed, 0 failed, 0 skipped', text)
        self.assertIn('0.250 ms', text)
        self.assertIn('Checkpoint fingerprint: deadbeef', text)
        self.assertIn('42.000', text)

    def test_failed_and_skipped_scenarios_do_not_imply_passing_coverage(self):
        report = self.report()
        report['scenarios'][0].update(status='failed', errors=['baseline fingerprint changed'])
        report['scenarios'].append({'id': 'missing-rom', 'status': 'skipped', 'errors': ['Missing ROM'], 'metrics': {}})
        report['summary'] = {'passed': 0, 'failed': 1, 'skipped': 1}
        text, invalid = ci_summary.render(regression=self.artifact('report.json', json.dumps(report)))
        self.assertFalse(invalid)
        self.assertIn('0 passed, 1 failed, 1 skipped', text)
        self.assertIn('baseline fingerprint changed', text)
        self.assertIn('Missing ROM', text)

    def test_missing_and_empty_reports_are_explicit(self):
        text, invalid = ci_summary.render(junit=self.root / 'missing.xml')
        self.assertFalse(invalid)
        self.assertIn('earlier configure, build, or test step may have failed', text)
        self.assertNotIn('0 passed', text)
        text, invalid = ci_summary.render(junit=self.artifact('empty.xml', '<testsuite/>'))
        self.assertFalse(invalid)
        self.assertIn('not evidence of a passing test run', text)

    def test_malformed_artifacts_are_reported_and_exit_nonzero(self):
        bad_report = self.report()
        bad_report['summary']['passed'] = 99
        for text in ('{', '[]', json.dumps(bad_report)):
            with self.subTest(text=text):
                path = self.artifact('bad.json', text)
                output = self.root / 'summary.md'
                self.assertEqual(ci_summary.main(['--regression', str(path), '--output', str(output)]), 1)
                self.assertIn('Invalid artifact:', output.read_text())
        for xml in ('<broken', '<unrecognized/>', '<!DOCTYPE testsuite><testsuite/>', '<testsuite><testcase time="NaN"/></testsuite>'):
            text, invalid = ci_summary.render(junit=self.artifact('bad.xml', xml))
            self.assertTrue(invalid)
            self.assertIn('No successful result is inferred', text)

    def test_markdown_html_and_log_sizes_are_bounded(self):
        xml = '<testsuite><testcase name="&lt;img&gt;|[click](url)`\n# injected"><failure>&lt;/pre&gt;&lt;script&gt;' + 'x' * 4000 + '</failure></testcase></testsuite>'
        text, _ = ci_summary.render(junit=self.artifact('escape.xml', xml), label='<b>|bad')
        self.assertNotIn('<img>', text)
        self.assertNotIn('<script>', text)
        self.assertNotIn('[click](url)', text)
        self.assertIn('&lt;/pre&gt;&lt;script&gt;', text)
        self.assertIn('[truncated]', text)
        self.assertLess(len(text), 5000)

    def test_cache_formats_and_unknown_details_remain_literal(self):
        for stats in ('Cacheable calls: 100\n  Hits: 60 / 100\n  Misses: 40 / 100',
                      'Compile requests 100\nCache hits 60\nCache misses 40'):
            text, invalid = ci_summary.render(cache_stats=self.artifact('cache.txt', stats))
            self.assertFalse(invalid)
            self.assertIn('60', text)
            self.assertNotIn('60%', text)  # Never invent a hit ratio.
        text, _ = ci_summary.render(cache_stats=self.artifact('cache.txt', '<script>unknown</script>'))
        self.assertIn('Unrecognized cache statistics', text)
        self.assertIn('&lt;script&gt;', text)

    def test_output_appends_and_stdout_is_default(self):
        path = self.artifact('junit.xml', '<testsuite><testcase name="OK"/></testsuite>')
        output = self.artifact('summary.md', 'Earlier summary\n')
        self.assertEqual(ci_summary.main(['--junit', str(path), '--label', 'Windows host tests', '--output', str(output)]), 0)
        self.assertTrue(output.read_text().startswith('Earlier summary\n'))
        self.assertIn('## Windows host tests', output.read_text())
        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            self.assertEqual(ci_summary.main(['--junit', str(path)]), 0)
        self.assertIn('1 passed', stdout.getvalue())


if __name__ == '__main__':
    unittest.main()
