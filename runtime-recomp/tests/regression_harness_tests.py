#!/usr/bin/env python3
"""Exercise the real harness CLI with tiny synthetic subprocess workloads."""

from __future__ import annotations

import concurrent.futures
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts" / "regression.py"
sys.path.insert(0, str(ROOT / "scripts"))
import regression


class RegressionHarnessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="dkr harness tests ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.suite_path = self.root / "suite with spaces.json"
        self.next_output = 0

    def scenario(self, name="synthetic", code='print("OK")', **options):
        return {"id": name, "command": [sys.executable, "-c", code], **options}

    def suite(self, *scenarios):
        value = {"schema_version": 1, "name": "Synthetic CLI acceptance", "scenarios": list(scenarios)}
        self.suite_path.write_text(json.dumps(value), encoding="utf-8")
        return value

    def run_cli(self, *options, output=None):
        self.next_output += 1
        output = output or self.root / f"run with spaces {self.next_output}"
        command = [
            sys.executable,
            str(RUNNER),
            "--suite",
            str(self.suite_path),
            "--build-dir",
            str(self.root / "fake build"),
            "--repeat",
            "1",
            "--warmup",
            "0",
            "--output",
            str(output),
            *map(str, options),
        ]
        child = subprocess.run(command, cwd=self.root, capture_output=True, text=True, timeout=20)
        report_path = output / "report.json"
        report = json.loads(report_path.read_text()) if report_path.is_file() else None
        return child, report, output

    def assert_exit(self, child, code):
        self.assertEqual(child.returncode, code, child.stdout + "\n" + child.stderr)

    def test_warmup_is_checked_but_excluded_from_measurements(self):
        code = """from pathlib import Path
import json
p = Path('count')
n = int(p.read_text()) + 1 if p.exists() else 1
p.write_text(str(n))
print('DKR_HARNESS ' + json.dumps({'metrics': {'stage_ms': 999 if n == 1 else n}}))
print('STATE=stable')
"""
        self.suite(
            self.scenario(
                code=code, required_metrics=["stage_ms"], fingerprints=["STATE=\\w+"], budgets={"stage_ms": 3}
            )
        )
        child, report, output = self.run_cli("--warmup", "1", "--repeat", "2")
        self.assert_exit(child, 0)
        result = report["scenarios"][0]
        self.assertEqual(result["metrics"]["stage_ms"], 2.5)
        self.assertEqual([x["phase"] for x in result["attempts"]], ["warmup", "measured", "measured"])
        self.assertEqual(result["attempts"][0]["metrics"]["stage_ms"], 999)
        for attempt in result["attempts"]:
            self.assertTrue((output / attempt["stdout"]).is_file())
        self.assertTrue((output / "report.html").is_file())
        self.assertEqual(ET.parse(output / "junit.xml").getroot().attrib["failures"], "0")

    def test_child_crash_timeout_and_missing_input_are_not_passes(self):
        self.suite(
            self.scenario("crash", 'import os; print("before crash", flush=True); os._exit(9)'),
            self.scenario("timeout", 'import time; print("started", flush=True); time.sleep(.5)', timeout_seconds=0.05),
            self.scenario("missing", inputs=[str(self.root / "missing.bin")]),
            self.scenario("healthy"),
        )
        child, report, output = self.run_cli()
        self.assert_exit(child, 1)
        self.assertEqual(report["summary"], {"passed": 1, "failed": 2, "skipped": 1})
        results = {item["id"]: item for item in report["scenarios"]}
        self.assertEqual(results["crash"]["attempts"][0]["exit_code"], 9)
        self.assertIn("before crash", (output / results["crash"]["attempts"][0]["stdout"]).read_text())
        self.assertTrue(any("Timed out" in x for x in results["timeout"]["errors"]))
        self.assertEqual(results["missing"]["attempts"], [])
        xml = ET.parse(output / "junit.xml").getroot()
        self.assertEqual(xml.attrib["failures"], "2")
        self.assertEqual(xml.attrib["skipped"], "1")

    def test_missing_policy_is_explicit_and_all_skipped_still_fails(self):
        missing = self.scenario("missing", inputs=["${private_fixture}"])
        self.suite(missing, self.scenario("healthy"))
        child, report, _ = self.run_cli()
        self.assert_exit(child, 2)
        self.assertEqual(report["summary"]["skipped"], 1)
        child, _, _ = self.run_cli("--allow-missing")
        self.assert_exit(child, 0)
        self.suite(missing)
        child, report, _ = self.run_cli("--allow-missing")
        self.assert_exit(child, 2)
        self.assertEqual(report["summary"]["passed"], 0)

    def measurement_suite(self):
        # This external control file intentionally is not a fixture input: it
        # emulates varying runtime performance while keeping the workload fixed.
        control = self.root / "measurement control.json"
        code = (
            "import json; from pathlib import Path; x=json.loads(Path("
            + repr(str(control))
            + ").read_text()); print('DKR_HARNESS ' + json.dumps({'metrics': {'stage_ms': x['time']}})); print('STATE=' + x['state'])"
        )
        self.suite(self.scenario(code=code, required_metrics=["stage_ms"], fingerprints=["STATE=\\w+"]))
        control.write_text(json.dumps({"time": 10, "state": "stable"}))
        return control

    def test_same_machine_baseline_detects_regression_and_fingerprint_change(self):
        control = self.measurement_suite()
        child, _, baseline = self.run_cli()
        self.assert_exit(child, 0)
        # Process timing is noisy; a generous ms floor removes that source of
        # flakiness while the deterministic stage regression is much larger.
        options = ["--baseline", baseline / "report.json", "--absolute-tolerance-ms", "100"]
        child, report, _ = self.run_cli(*options)
        self.assert_exit(child, 0)
        self.assertTrue(report["scenarios"][0]["comparisons"])
        control.write_text(json.dumps({"time": 1000, "state": "stable"}))
        child, report, _ = self.run_cli(*options)
        self.assert_exit(child, 1)
        comparison = next(x for x in report["scenarios"][0]["comparisons"] if x["metric"] == "stage_ms")
        self.assertEqual(comparison["status"], "failed")
        control.write_text(json.dumps({"time": 10, "state": "changed"}))
        child, report, _ = self.run_cli(*options)
        self.assert_exit(child, 1)
        self.assertTrue(any("fingerprint" in error.lower() for error in report["scenarios"][0]["errors"]))

    def test_incompatible_baseline_is_rejected_before_execution(self):
        self.measurement_suite()
        child, _, baseline = self.run_cli()
        self.assert_exit(child, 0)
        child, report, output = self.run_cli("--baseline", baseline / "report.json", "--config", "Debug")
        self.assert_exit(child, 2)
        self.assertIsNone(report)
        self.assertFalse(output.exists())
        self.assertIn("Baseline", child.stderr)

    def test_list_does_not_execute_and_existing_output_is_preserved(self):
        sentinel = self.root / "executed"
        self.suite(self.scenario(code=f'from pathlib import Path; Path({str(sentinel)!r}).write_text("executed")'))
        child, report, output = self.run_cli("--list")
        self.assert_exit(child, 0)
        self.assertIn("synthetic", child.stdout)
        self.assertFalse(sentinel.exists())
        self.assertFalse(output.exists())
        self.assertIsNone(report)
        output.mkdir()
        preserved = output / "important.txt"
        preserved.write_text("keep")
        child, _, _ = self.run_cli(output=output)
        self.assert_exit(child, 2)
        self.assertFalse(sentinel.exists())
        self.assertEqual(preserved.read_text(), "keep")

    def test_command_arguments_are_literal_and_fixture_changes_invalidate_baseline(self):
        fixture = self.root / "private fixture with spaces.bin"
        fixture.write_bytes(b"first")
        marker = self.root / "should never execute"
        payload = f'$(touch "{marker}"); echo injected'
        item = self.scenario(
            code='import sys; print(sys.argv[1]); print("OK")', inputs=["${fixture}"], expect_stdout=[payload, "OK"]
        )
        item["command"].append(payload)
        self.suite(item)
        options = ["--var", f"fixture={fixture}"]
        child, report, baseline = self.run_cli(*options)
        self.assert_exit(child, 0)
        self.assertFalse(marker.exists())
        self.assertEqual(len(report["scenarios"][0]["input_sha256"]), 1)
        fixture.write_bytes(b"changed")
        child, report, _ = self.run_cli(*options, "--baseline", baseline / "report.json")
        self.assert_exit(child, 1)
        self.assertTrue(any("baseline" in error.lower() for error in report["scenarios"][0]["errors"]))

    def test_expanded_variables_are_part_of_the_baseline_contract(self):
        item = self.scenario(code="import sys; print(sys.argv[1])")
        item["command"].append("${workload}")
        self.suite(item)
        child, _, baseline = self.run_cli("--var", "workload=small")
        self.assert_exit(child, 0)
        child, report, _ = self.run_cli("--var", "workload=large", "--baseline", baseline / "report.json")
        self.assert_exit(child, 1)
        self.assertTrue(any("baseline" in error.lower() for error in report["scenarios"][0]["errors"]))

    def test_vars_file_resolves_paths_beside_file_and_cli_overrides_are_literal(self):
        directory = self.root / "private configuration"
        directory.mkdir()
        fixture = (directory / "fixture with spaces.bin").resolve()
        fixture.write_bytes(b"private fixture")
        variables = directory / "paths.json"
        variables.write_text(
            json.dumps(
                {
                    "fixture": fixture.name,
                    "absolute": str(fixture),
                    "workload": "unused",
                    "unused_for_selected_scenario": "missing.bin",
                }
            )
        )
        item = self.scenario(
            code="import sys; print(sys.argv[1]); print(sys.argv[2])",
            inputs=["${fixture}", "${absolute}"],
            expect_stdout=[str(fixture), "small"],
        )
        item["command"].extend(["${fixture}", "${workload}"])
        self.suite(item)
        # Overrides win regardless of argument order, and remain scalar strings.
        child, report, baseline = self.run_cli("--var", "workload=small", "--vars-file", variables)
        self.assert_exit(child, 0)
        self.assertEqual(report["scenarios"][0]["input_paths"], [str(fixture)] * 2)
        # Equivalent CLI inputs produce the same baseline contract.
        child, _, _ = self.run_cli(
            "--var",
            f"fixture={fixture}",
            "--var",
            f"absolute={fixture}",
            "--var",
            "workload=small",
            "--baseline",
            baseline / "report.json",
        )
        self.assert_exit(child, 0)

    def test_invalid_vars_files_fail_before_execution_or_output(self):
        self.suite(self.scenario())
        variables = self.root / "invalid paths.json"
        cases = [
            "[]",
            "null",
            "{",
            '{"fixture":"one", "fixture":"two"}',
            '{"bad-name":"value"}',
            '{"root":"value"}',
            '{"python":"value"}',
            '{"build":"value"}',
            '{"fixture":1}',
            '{"fixture":null}',
            '{"fixture":{}}',
            '{"fixture":""}',
            '{"fixture":"bad\\u0000path"}',
        ]
        for content in cases:
            with self.subTest(content=content):
                variables.write_text(content)
                child, report, output = self.run_cli("--vars-file", variables)
                self.assert_exit(child, 2)
                self.assertIsNone(report)
                self.assertFalse(output.exists())
                self.assertNotIn("Traceback", child.stderr)
        variables.unlink()
        child, _, output = self.run_cli("--vars-file", variables)
        self.assert_exit(child, 2)
        self.assertFalse(output.exists())
        self.assertNotIn("Traceback", child.stderr)

    def test_vars_file_does_not_permit_duplicate_or_reserved_cli_assignments(self):
        self.suite(self.scenario())
        variables = self.root / "paths.json"
        variables.write_text('{"fixture":"unused.bin"}')
        for assignments in [
            ("fixture=one", "fixture=two"),
            ("root=bad",),
            ("python=bad",),
            ("build=bad",),
            ("no_equals",),
            ("bad-name=value",),
            ("fixture=",),
        ]:
            with self.subTest(assignments=assignments):
                options = [argument for assignment in assignments for argument in ("--var", assignment)]
                child, report, output = self.run_cli("--vars-file", variables, *options)
                self.assert_exit(child, 2)
                self.assertIsNone(report)
                self.assertFalse(output.exists())
                self.assertNotIn("Traceback", child.stderr)

    def test_check_inputs_accepts_paths_with_spaces_without_running_commands(self):
        sentinel = self.root / "must not execute"
        fixture = self.root / "private input.bin"
        fixture.write_bytes(b"fixture")
        variables = self.root / "paths.json"
        variables.write_text(json.dumps({"fixture": fixture.name}))
        self.suite(
            self.scenario(code=f"from pathlib import Path; Path({str(sentinel)!r}).touch()", inputs=["${fixture}"])
        )
        child, report, output = self.run_cli("--vars-file", variables, "--check-inputs")
        self.assert_exit(child, 0)
        self.assertIn("Inputs ready for 1 selected scenarios", child.stdout)
        self.assertIsNone(report)
        self.assertFalse(output.exists())
        self.assertFalse(sentinel.exists())

    def test_check_inputs_aggregates_independent_failures_and_honors_selection(self):
        missing_file = self.root / "missing fixture.bin"
        not_executable = self.root / "nonexecutable file"
        not_executable.write_text("not a program")
        sentinel = self.root / "must not execute"
        self.suite(
            self.scenario(
                "missing-vars",
                inputs=["${missing_one}", "${missing_two}", str(missing_file)],
                env={"EXAMPLE": "${missing_env}"},
            ),
            {"id": "bad-command", "command": [str(not_executable)]},
            self.scenario("ready", code=f"from pathlib import Path; Path({str(sentinel)!r}).touch()"),
        )
        child, report, output = self.run_cli("--check-inputs", "--allow-missing")
        self.assert_exit(child, 2)
        # Inputs are canonicalized; a failed executable lookup preserves its argument.
        for expected in ("missing_one", "missing_two", "missing_env", str(missing_file.resolve()), str(not_executable)):
            self.assertIn(expected, child.stderr)
        self.assertIsNone(report)
        self.assertFalse(output.exists())
        self.assertFalse(sentinel.exists())
        child, _, output = self.run_cli("--check-inputs", "--scenario", "ready")
        self.assert_exit(child, 0)
        self.assertFalse(output.exists())
        self.assertFalse(sentinel.exists())

    def test_check_inputs_and_execution_resolve_relative_executable_from_invocation_directory(self):
        item = self.scenario(expect_stdout=["OK"])
        item["command"][0] = os.path.relpath(sys.executable, self.root.resolve())
        self.suite(item)
        child, _, output = self.run_cli("--check-inputs")
        self.assert_exit(child, 0)
        self.assertFalse(output.exists())
        child, report, _ = self.run_cli()
        self.assert_exit(child, 0)
        self.assertEqual(report["scenarios"][0]["command"][0], str(Path(sys.executable).resolve()))

    def test_check_inputs_validates_variables_and_needs_no_output_option(self):
        self.suite(self.scenario())
        child = subprocess.run(
            [sys.executable, str(RUNNER), "--suite", str(self.suite_path), "--check-inputs"],
            capture_output=True,
            text=True,
            timeout=20,
        )
        self.assert_exit(child, 0)
        child, _, output = self.run_cli("--check-inputs", "--var", "root=forbidden")
        self.assert_exit(child, 2)
        self.assertFalse(output.exists())

    @unittest.skipUnless(shutil.which("ctest"), "CTest is not installed")
    def test_check_inputs_inspects_ctest_registration_without_executing_tests(self):
        build = self.root / "fake build"
        build.mkdir()
        sentinel = self.root / "must not execute"
        child_script = self.root / "registered child.py"
        child_script.write_text(f"from pathlib import Path; Path({str(sentinel)!r}).touch()\n")
        (build / "CTestTestfile.cmake").write_text(
            f"add_test(Ready [==[{sys.executable}]==] [==[{child_script}]==])\n"
            f"add_test(Disabled [==[{sys.executable}]==] [==[{child_script}]==])\n"
            "set_tests_properties(Disabled PROPERTIES DISABLED TRUE)\n"
            "add_test(Unbuilt /missing/dkr/executable)\n"
        )
        self.suite(*({"id": name.lower(), "ctest": name} for name in ("Ready", "Disabled", "Unbuilt", "Missing")))
        child, _, output = self.run_cli("--check-inputs")
        self.assert_exit(child, 2)
        self.assertIn("CTest test is disabled: Disabled", child.stderr)
        self.assertIn("build Unbuilt first", child.stderr)
        self.assertIn("CTest test not configured: Missing", child.stderr)
        self.assertFalse(output.exists())
        self.assertFalse(sentinel.exists())
        child, _, output = self.run_cli("--check-inputs", "--scenario", "ready")
        self.assert_exit(child, 0)
        self.assertFalse(output.exists())
        self.assertFalse(sentinel.exists())

    def test_process_time_is_informational_unless_explicitly_compared(self):
        self.suite(self.scenario())
        child, baseline_report, baseline = self.run_cli()
        self.assert_exit(child, 0)
        # Force a deterministic process regression without introducing sleeps or
        # depending on scheduler variance between two real executions.
        for key in baseline_report["scenarios"][0]["metrics"]:
            if key.startswith("process."):
                baseline_report["scenarios"][0]["metrics"][key] = 0
        (baseline / "report.json").write_text(json.dumps(baseline_report))
        options = ["--baseline", baseline / "report.json", "--relative-tolerance", "0", "--absolute-tolerance-ms", "0"]
        child, report, _ = self.run_cli(*options)
        self.assert_exit(child, 0)
        self.assertFalse(any(x["metric"].startswith("process.") for x in report["scenarios"][0]["comparisons"]))
        child, report, _ = self.run_cli(*options, "--compare-process-time")
        self.assert_exit(child, 1)
        self.assertTrue(
            any(
                x["metric"].startswith("process.") and x["status"] == "failed"
                for x in report["scenarios"][0]["comparisons"]
            )
        )

    def test_instrumented_metrics_strip_ctest_prefix_and_exclude_unbounded_histogram(self):
        text = "\n".join(
            [
                "42: [rollback][timing] stage=tick count=8 mean-us=1250 p95-upper-us=2000 p99-upper-us=18446744073709551615 max-us=3000",
                '42: DKR_HARNESS {"metrics": {"custom_ms": 3}}',
                "42: [perf][present-interval] ms(p50/p95/p99/max)=16.0/18.0/20.0/25.0",
                "42: [perf][present-interval] ms(p50/p95/p99/max)=17.0/17.0/19.0/27.0",
            ]
        )
        metrics = regression.extract_metrics(text)
        self.assertEqual(metrics["rollback.tick.count"], 8)
        self.assertEqual(metrics["rollback.tick.mean_ms"], 1.25)
        self.assertEqual(metrics["rollback.tick.p95_upper_ms"], 2)
        self.assertNotIn("rollback.tick.p99_upper_ms", metrics)
        self.assertEqual(metrics["rollback.tick.max_ms"], 3)
        self.assertEqual(metrics["custom_ms"], 3)
        self.assertEqual(metrics["present.window_p50_max_ms"], 17)
        self.assertEqual(metrics["present.window_p95_max_ms"], 18)
        self.assertEqual(metrics["present.window_max_max_ms"], 27)

    @unittest.skipUnless(shutil.which("ctest"), "CTest is not installed")
    def test_ctest_skip_xml_is_not_a_passing_scenario(self):
        build = self.root / "fake build"
        build.mkdir()
        child_script = self.root / "skip child.py"
        child_script.write_text("raise SystemExit(77)\n")
        # A minimal CTest registration needs no compiler or configure step.
        (build / "CTestTestfile.cmake").write_text(
            f"add_test(SyntheticSkip [==[{sys.executable}]==] [==[{child_script}]==])\n"
            "set_tests_properties(SyntheticSkip PROPERTIES SKIP_RETURN_CODE 77)\n"
        )
        self.suite({"id": "ctest-skip", "ctest": "SyntheticSkip"})
        child, report, output = self.run_cli("--allow-missing")
        self.assert_exit(child, 2)
        self.assertEqual(report["summary"], {"passed": 0, "failed": 0, "skipped": 1})
        self.assertEqual(report["scenarios"][0]["attempts"][0]["status"], "skipped")
        xml = ET.parse(output / "junit.xml").getroot()
        self.assertEqual(xml.attrib["skipped"], "1")

    def test_timeout_is_classified_when_watchdog_has_not_been_scheduled(self):
        # Simulate a starved watchdog deterministically; the child exits normally
        # after the observed deadline, so its exit code alone cannot enforce it.
        for duration, expected in ((0.2, "Timed out after 0.15 seconds"), (0.1, None)):
            with (
                self.subTest(duration=duration),
                mock.patch.object(regression.threading, "Thread"),
                mock.patch.object(regression.time, "perf_counter", side_effect=[100.0, 100.0 + duration]),
            ):
                code, elapsed, error, interrupted = regression.execute(
                    [sys.executable, "-c", "pass"],
                    self.root,
                    os.environ.copy(),
                    0.15,
                    self.root / "stdout.log",
                    self.root / "stderr.log",
                )
            self.assertEqual(code, 0)
            self.assertAlmostEqual(elapsed, duration * 1000)
            self.assertEqual(error, expected)
            self.assertFalse(interrupted)

    @unittest.skipUnless(os.name == "posix", "POSIX process-group teardown")
    def test_timeout_retires_started_descendant(self):
        # A socket handshake proves the descendant is alive before advancing the
        # deadline. EOF after execute returns proves teardown, without racing a
        # delayed sentinel against a heavily loaded CI machine's scheduler.
        expired = threading.Event()
        real_counter = time.perf_counter

        def controlled_counter():
            return real_counter() + (60 if expired.is_set() else 0)

        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(10)
            descendant = (
                f'import socket; s=socket.create_connection({listener.getsockname()!r}); s.sendall(b"ready"); s.recv(1)'
            )
            code = f'import subprocess, sys; p=subprocess.Popen([sys.executable, "-c", {descendant!r}]); p.wait()'
            with (
                mock.patch.object(regression.time, "perf_counter", side_effect=controlled_counter),
                mock.patch.object(regression, "stop_process", wraps=regression.stop_process) as cleanup,
                concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool,
            ):
                future = pool.submit(
                    regression.execute,
                    [sys.executable, "-c", code],
                    self.root,
                    os.environ.copy(),
                    30,
                    self.root / "stdout.log",
                    self.root / "stderr.log",
                )
                try:
                    connection, _ = listener.accept()
                    with connection:
                        connection.settimeout(10)
                        self.assertEqual(connection.recv(5, socket.MSG_WAITALL), b"ready")
                        expired.set()
                        result = future.result(timeout=10)
                        self.assertEqual(cleanup.call_count, 1)
                        self.assertIn("Timed out", result[2] or "", repr(result))
                        self.assertNotEqual(result[0], 0, repr(result))
                        self.assertEqual(connection.recv(1), b"", "Descendant still holds its socket open")
                finally:
                    expired.set()

    def test_malformed_baseline_array_has_concise_error(self):
        self.suite(self.scenario())
        baseline = self.root / "bad baseline.json"
        baseline.write_text("[]")
        child, report, output = self.run_cli("--baseline", baseline)
        self.assert_exit(child, 2)
        self.assertNotIn("Traceback", child.stderr)
        self.assertIsNone(report)
        self.assertFalse(output.exists())

    def test_crlf_anchored_fingerprints_are_stable_across_attempts_and_baseline(self):
        # Write bytes so the fixture produces CRLF on every host without Python
        # text-mode newline translation changing the intended Windows input.
        self.suite(
            self.scenario(
                code='import sys; sys.stdout.buffer.write(b"STATE=stable\\r\\nDONE\\r\\n")',
                fingerprints=["(?m)^STATE=stable$"],
                expect_stdout=["STATE=stable"],
            )
        )
        options = ["--warmup", "1", "--repeat", "2"]
        child, report, baseline = self.run_cli(*options)
        self.assert_exit(child, 0)
        result = report["scenarios"][0]
        self.assertEqual(len(result["attempts"]), 3)
        self.assertEqual(result["fingerprints"], {"(?m)^STATE=stable$": ["STATE=stable"]})
        self.assertTrue(all(attempt["fingerprints"] == result["fingerprints"] for attempt in result["attempts"]))
        self.assertIn(b"\r\n", (baseline / result["attempts"][0]["stdout"]).read_bytes())
        child, report, _ = self.run_cli(*options, "--baseline", baseline / "report.json")
        self.assert_exit(child, 0)
        self.assertEqual(report["scenarios"][0]["fingerprints"], result["fingerprints"])

    def test_bad_schema_is_rejected_without_output(self):
        cases = [
            {"schema_version": 1, "name": "bad", "scenarios": [self.scenario("../escape")]},
            {"schema_version": 1, "name": "bad", "scenarios": [self.scenario("same"), self.scenario("same")]},
            {"schema_version": 1, "name": "bad", "scenarios": [{"id": "shell", "command": "echo unsafe"}]},
            {"schema_version": 1, "name": "bad", "scenarios": [self.scenario(timeout_seconds=float("nan"))]},
            {"schema_version": 1, "name": "bad", "scenarios": [self.scenario(unrecognized=True)]},
            {"schema_version": 1, "name": "bad", "scenarios": [self.scenario(fingerprints=["[invalid"])]},
        ]
        for suite in cases:
            with self.subTest(suite=suite):
                self.suite_path.write_text(json.dumps(suite))
                child, report, output = self.run_cli()
                self.assert_exit(child, 2)
                self.assertIsNone(report)
                self.assertFalse(output.exists())
                self.assertNotIn("Traceback", child.stderr)

    def test_bad_metrics_and_unstable_fingerprints_fail_as_reports(self):
        self.suite(
            self.scenario("reserved", 'print(\'DKR_HARNESS {"metrics": {"process.median_ms": 0}}\')'),
            self.scenario("nonfinite", 'print(\'DKR_HARNESS {"metrics": {"stage_ms": NaN}}\')'),
            self.scenario("missingmetric", required_metrics=["expected_ms"]),
            self.scenario(
                "unstable",
                "from pathlib import Path; p=Path('counter'); n=int(p.read_text())+1 if p.exists() else 1; p.write_text(str(n)); print('STATE='+str(n))",
                fingerprints=["STATE=\\d+"],
            ),
        )
        child, report, _ = self.run_cli("--repeat", "2")
        self.assert_exit(child, 1)
        self.assertEqual(report["summary"], {"passed": 0, "failed": 4, "skipped": 0})
        self.assertTrue(any("fingerprint" in error.lower() for error in report["scenarios"][-1]["errors"]))


if __name__ == "__main__":
    unittest.main()
