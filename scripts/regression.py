#!/usr/bin/env python3
"""Repeatable DKR regression checks and same-machine performance comparisons."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import fnmatch
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import statistics
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

from harness_report import write_reports

ROOT = Path(__file__).resolve().parents[1]
TOKEN = re.compile(r"\$\{([A-Za-z][A-Za-z0-9_]*)\}")
LOG_LIMIT = 32 * 1024 * 1024


def digest(path):
    with Path(path).open("rb") as source:
        value = hashlib.sha256()
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def canonical(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def number(value, label, minimum=0):
    if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value) or value < minimum:
        raise ValueError(f"{label} must be a finite number >= {minimum}")
    return value


def load_suite(path):
    suite = json.loads(path.read_text())
    if not isinstance(suite, dict) or suite.get("schema_version") != 1:
        raise ValueError("Suite requires schema_version 1")
    if set(suite) - {"schema_version", "name", "scenarios"}:
        raise ValueError("Unknown suite field")
    if not isinstance(suite.get("name"), str) or not suite["name"]:
        raise ValueError("Suite requires a name")
    scenarios = suite.get("scenarios")
    if not isinstance(scenarios, list) or not scenarios:
        raise ValueError("Suite requires nonempty scenarios")
    seen = set()
    allowed = {
        "id",
        "description",
        "command",
        "ctest",
        "env",
        "inputs",
        "expect_stdout",
        "timeout_seconds",
        "budgets",
        "required_metrics",
        "fingerprints",
    }
    for item in scenarios:
        if not isinstance(item, dict) or set(item) - allowed:
            raise ValueError("Unknown scenario field or invalid scenario")
        name = item.get("id", "")
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", name) or name in seen:
            raise ValueError(f"Invalid or duplicate scenario id: {name}")
        seen.add(name)
        if not isinstance(item.get("description", ""), str):
            raise ValueError(f"{name}: description must be a string")
        if ("command" in item) == ("ctest" in item):
            raise ValueError(f"{name}: provide exactly one of command or ctest")
        if "ctest" in item and (not isinstance(item["ctest"], str) or not item["ctest"]):
            raise ValueError(f"{name}: ctest must name exactly one test")
        for key in ("command", "inputs", "expect_stdout", "required_metrics", "fingerprints"):
            if key in item and (
                not isinstance(item[key], list)
                or not all(isinstance(x, str) and x and "\0" not in x for x in item[key])
            ):
                raise ValueError(f"{name}: {key} must be a string array")
        if "command" in item and not item["command"]:
            raise ValueError(f"{name}: empty command")
        env = item.get("env", {})
        if not isinstance(env, dict) or not all(
            isinstance(k, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", k) and isinstance(v, str) and "\0" not in v
            for k, v in env.items()
        ):
            raise ValueError(f"{name}: env must map strings to strings")
        for pattern in item.get("fingerprints", []):
            re.compile(pattern)
        number(item.get("timeout_seconds", 180), f"{name}: timeout_seconds", 0.01)
        if not isinstance(item.get("budgets", {}), dict):
            raise ValueError(f"{name}: budgets must map metric names to maxima")
        for key, value in item.get("budgets", {}).items():
            number(value, f"{name}: budget {key}")
    return suite


def expand(value, variables):
    return TOKEN.sub(lambda match: variables[match[1]], value)


def load_variables(build_dir, vars_file, assignments):
    variables = {"root": str(ROOT), "python": sys.executable, "build": str(build_dir)}
    reserved = set(variables)

    def validate(key, value, source):
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", key) or key in reserved:
            raise ValueError(f"{source}: invalid or reserved variable name: {key}")
        if not isinstance(value, str) or not value or "\0" in value:
            raise ValueError(f"{source}: variable {key} requires a nonempty string without NUL characters")

    if vars_file:

        def unique_object(pairs):
            values = {}
            for key, value in pairs:
                if key in values:
                    raise ValueError(f"{vars_file}: duplicate variable: {key}")
                values[key] = value
            return values

        values = json.loads(vars_file.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
        if not isinstance(values, dict):
            raise ValueError(f"{vars_file}: variables file must be a JSON object of path strings")
        for key, value in values.items():
            validate(key, value, str(vars_file))
            variables[key] = str((vars_file.resolve().parent / value).resolve())
    seen = set()
    for assignment in assignments:
        key, separator, value = assignment.partition("=")
        if not separator:
            raise ValueError(f"--var requires NAME=VALUE: {assignment}")
        validate(key, value, "--var")
        if key in seen:
            raise ValueError(f"--var: duplicate variable: {key}")
        seen.add(key)
        variables[key] = value
    return variables


def stop_process(process):
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    elif process.poll() is None:
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=10,
        )
        process.kill()
    process.wait()


def execute(command, cwd, env, timeout, stdout, stderr):
    started = time.perf_counter()
    error = None
    interrupted = False
    stopped = False
    with stdout.open("wb") as out, stderr.open("wb") as err:
        try:
            process = subprocess.Popen(
                command,
                cwd=cwd,
                env=env,
                stdout=out,
                stderr=err,
                stdin=subprocess.DEVNULL,
                start_new_session=os.name == "posix",
            )
        except OSError as failure:
            return None, 0.0, f"Could not launch: {failure}", False
        finished = threading.Event()

        def watchdog():
            nonlocal error, stopped
            while not finished.wait(0.02):
                if process.poll() is not None:
                    return
                if time.perf_counter() - started > timeout:
                    error = f"Timed out after {timeout:g} seconds"
                elif stdout.stat().st_size + stderr.stat().st_size > LOG_LIMIT:
                    error = "Output exceeded the 32 MiB per-attempt limit"
                if error:
                    stop_process(process)
                    stopped = True
                    return

        watcher = threading.Thread(target=watchdog, daemon=True)
        watcher.start()
        try:
            # Block on the actual exit rather than quantizing short benchmarks
            # to a polling interval. A separate watchdog bounds time/log size.
            process.wait()
        except KeyboardInterrupt:
            error, interrupted = "Interrupted by user", True
        finally:
            elapsed = (time.perf_counter() - started) * 1000
            finished.set()
            # Also retire lingering descendants after a normal parent exit on POSIX.
            # Join before final cleanup: the watchdog may already have killed
            # this group, and a second SIGKILL can hit macOS zombies.
            watcher.join()
            if not stopped:
                stop_process(process)
    # The watchdog may not be scheduled before a late child exit. Enforce the
    # observed deadline independently of its polling cadence, without adding
    # polling latency to successful process measurements.
    if error is None and elapsed > timeout * 1000:
        error = f"Timed out after {timeout:g} seconds"
    if stdout.stat().st_size + stderr.stat().st_size > LOG_LIMIT:
        error = "Output exceeded the 32 MiB per-attempt limit"
    return process.returncode, elapsed, error, interrupted


def read_log(path):
    with path.open("rb") as source:
        return source.read(LOG_LIMIT).decode("utf-8", errors="replace").replace("\r\n", "\n")


def extract_metrics(text):
    """Keep histogram bounds and window maxima distinct from exact percentiles."""
    metrics = {}
    for line in text.splitlines():
        line = re.sub(r"^\d+: ", "", line)  # CTest -V prefixes child output.
        if line.startswith("[rollback][timing] "):
            fields = dict(re.findall(r"([\w-]+)=([^\s]+)", line))
            stage = fields.get("stage")
            if not stage:
                raise ValueError("Timing record has no stage")
            for field in ("count", "mean-us", "p95-upper-us", "p99-upper-us", "max-us"):
                value = number(float(fields[field]), f"{stage}.{field}")
                # UINT64_MAX is the unbounded histogram bucket, not a duration.
                if field.endswith("upper-us") and value >= 2**64 - 1:
                    continue
                suffix = field.replace("-", "_")
                if field.endswith("-us"):
                    suffix, value = suffix[:-2] + "ms", value / 1000
                metrics[f"rollback.{stage}.{suffix}"] = value
        elif line.startswith("DKR_HARNESS "):
            event = json.loads(line[len("DKR_HARNESS ") :])
            if not isinstance(event, dict) or set(event) != {"metrics"} or not isinstance(event["metrics"], dict):
                raise ValueError("Invalid DKR_HARNESS metrics record")
            for key, value in event["metrics"].items():
                if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_.-]*", key) or key in metrics or key.startswith("process."):
                    raise ValueError(f"Duplicate, reserved or invalid metric: {key}")
                metrics[key] = number(value, key)
        elif "[perf][present-interval]" in line:
            match = re.search(r"ms\(p50/p95/p99/max\)=([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)", line)
            if not match:
                raise ValueError("Malformed present-interval record")
            for label, raw in zip(("p50", "p95", "p99", "max"), match.groups()):
                key = f"present.window_{label}_max_ms"
                metrics[key] = max(metrics.get(key, 0), number(float(raw), key))
    return metrics


def ctest_status(path):
    if not path.is_file():
        raise ValueError("CTest did not produce its result XML")
    cases = list(ET.parse(path).getroot().iter("testcase"))
    if len(cases) != 1:
        raise ValueError(f"Expected one CTest result, got {len(cases)}")
    case = cases[0]
    if case.find("skipped") is not None or case.get("status") in ("notrun", "disabled"):
        return "skipped"
    if case.find("failure") is not None or case.find("error") is not None:
        return "failed"
    return "passed"


def run_scenario(item, variables, output, args):
    result = {
        "id": item["id"],
        "description": item.get("description", ""),
        "status": "passed",
        "errors": [],
        "attempts": [],
        "metrics": {},
        "comparisons": [],
        "fingerprints": {},
    }
    try:
        command = [expand(x, variables) for x in item.get("command", [])]
        overrides = {k: expand(v, variables) for k, v in item.get("env", {}).items()}
        inputs = [Path(expand(x, variables)).resolve() for x in item.get("inputs", [])]
    except KeyError as missing:
        result.update(
            status="skipped",
            errors=[f"Missing variable {missing.args[0]} (set --var {missing.args[0]}=VALUE or --vars-file)"],
        )
        return result, False
    absent = [str(path) for path in inputs if not path.is_file()]
    if absent:
        result.update(status="skipped", errors=["Missing input: " + ", ".join(absent)])
        return result, False
    ctest_entry = args.ctest_tests.get(item.get("ctest")) if "ctest" in item else None
    if "ctest" in item and ctest_entry is None:
        result.update(status="skipped", errors=[f"CTest test not configured: {item['ctest']}"])
        return result, False
    if command:
        executable = shutil.which(command[0])
        if not executable:
            result.update(status="skipped", errors=[f"Missing executable: {command[0]}"])
            return result, False
        command[0] = str(Path(executable).resolve())
    result["input_sha256"] = [digest(path) for path in inputs]
    # Bind substituted workload parameters, while allowing a rebuilt executable
    # and identical private inputs to move between build directories.
    aliases = {str(path): "sha256:" + sha for path, sha in zip(inputs, result["input_sha256"])}

    def semantic(value):
        for path, alias in sorted(aliases.items(), key=lambda x: -len(x[0])):
            value = value.replace(path, alias).replace(path.replace("\\", "/"), alias)
        for path, alias in ((args.build_dir, "${build}"), (ROOT, "${root}")):
            value = value.replace(str(path), alias).replace(path.as_posix(), alias)
        return value

    def normalize(value):
        if isinstance(value, str):
            return semantic(value)
        if isinstance(value, list):
            return [normalize(x) for x in value]
        if isinstance(value, dict):
            return {k: normalize(v) for k, v in value.items()}
        return value

    contract = {
        "scenario": item,
        "inputs": result["input_sha256"],
        "arguments": [semantic(x) for x in command[1:]],
        "environment": {k: semantic(v) for k, v in overrides.items()},
    }
    if ctest_entry:
        contract["ctest"] = {k: v for k, v in ctest_entry.items() if k in ("command", "properties")}
        contract["ctest"] = normalize(contract["ctest"])
        result["ctest_definition"] = ctest_entry
    result["contract"] = canonical(contract)
    result["command"] = command or ["ctest", item["ctest"]]
    result["environment"] = overrides
    result["input_paths"] = [str(path) for path in inputs]
    executable = command[0] if command else (ctest_entry.get("command") or [""])[0]
    if Path(executable).is_file():
        result["executable_sha256"] = digest(executable)
    # An unrelated local debug switch must not silently change the workload or
    # make a probe dump private checkpoint bytes. Suites declare their switches.
    env = {k: v for k, v in os.environ.items() if not k.startswith("DKR_")}
    env.update(overrides)
    directory = output / item["id"]
    directory.mkdir()
    measured = []
    interrupted = False
    for index in range(args.warmup + args.repeat):
        phase = "warmup" if index < args.warmup else "measured"
        prefix = f"{index + 1:02d}-{phase}"
        stdout, stderr = directory / f"{prefix}.stdout.log", directory / f"{prefix}.stderr.log"
        xml = directory / f"{prefix}.ctest.xml"
        actual = command or [
            "ctest",
            "--test-dir",
            str(args.build_dir),
            "-C",
            args.config,
            "-R",
            "^" + re.escape(item["ctest"]) + "$",
            "--no-tests=error",
            "-V",
            "--output-junit",
            str(xml),
        ]
        code, elapsed, error, interrupted = execute(
            actual, directory, env, item.get("timeout_seconds", 180), stdout, stderr
        )
        attempt = {
            "index": index + 1,
            "phase": phase,
            "elapsed_ms": elapsed,
            "exit_code": code,
            "stdout": stdout.relative_to(output).as_posix(),
            "stderr": stderr.relative_to(output).as_posix(),
            "status": "passed",
            "errors": [],
            "command": actual,
        }
        result["attempts"].append(attempt)
        if error:
            attempt["errors"].append(error)
        if code != 0:
            attempt["errors"].append(f"Process exit: {code}")
        text = read_log(stdout) + "\n" + read_log(stderr)
        try:
            if not command and not attempt["errors"]:
                attempt["status"] = ctest_status(xml)
                if attempt["status"] == "failed":
                    attempt["errors"].append("CTest reported a failure")
                elif attempt["status"] == "skipped":
                    result.update(status="skipped", errors=["CTest skipped the scenario"])
                    break
            for expected in item.get("expect_stdout", []):
                if expected not in read_log(stdout):
                    attempt["errors"].append(f"Missing success marker: {expected}")
            attempt["metrics"] = extract_metrics(text)
            for metric in item.get("required_metrics", []):
                if metric not in attempt["metrics"]:
                    attempt["errors"].append(f"Missing required metric: {metric}")
            fingerprints = {}
            for pattern in item.get("fingerprints", []):
                matches = [match.group(0) for match in re.finditer(pattern, text, re.MULTILINE)]
                if not matches:
                    attempt["errors"].append(f"Missing fingerprint: {pattern}")
                fingerprints[pattern] = matches
            attempt["fingerprints"] = fingerprints
            if index and fingerprints != result["fingerprints"]:
                attempt["errors"].append("Deterministic fingerprints changed between attempts")
            result["fingerprints"] = fingerprints
        except (ValueError, KeyError, TypeError, ET.ParseError) as failure:
            attempt["errors"].append(f"Invalid measurement: {failure}")
        if attempt["errors"]:
            attempt["status"] = "failed"
        if attempt["status"] != "passed":
            result["status"] = attempt["status"]
            result["errors"].extend(attempt["errors"] or ["CTest skipped the scenario"])
            break
        if phase == "measured":
            measured.append(attempt)
    if measured and result["status"] == "passed":
        elapsed = sorted(x["elapsed_ms"] for x in measured)
        result["metrics"] = {
            "process.median_ms": statistics.median(elapsed),
            "process.p95_ms": elapsed[math.ceil(len(elapsed) * 0.95) - 1],
            "process.max_ms": max(elapsed),
        }
        keys = set().union(*(x["metrics"] for x in measured))
        for key in keys:
            if not all(key in x["metrics"] for x in measured):
                result["errors"].append(f"Metric missing in some measured attempts: {key}")
            else:
                result["metrics"][key] = statistics.median(x["metrics"][key] for x in measured)
        for key, limit in item.get("budgets", {}).items():
            value = result["metrics"].get(key)
            if value is None or value > limit:
                result["errors"].append(f"Budget exceeded or unavailable: {key}={value}, maximum {limit}")
        if result["errors"]:
            result["status"] = "failed"
    return result, interrupted


def compare(result, baseline, percent, absolute, compare_process=False):
    if result["status"] != "passed":
        return
    if not baseline or baseline.get("status") != "passed" or baseline.get("contract") != result["contract"]:
        result["errors"].append("No compatible passing baseline for this scenario")
    elif baseline.get("fingerprints", {}) != result["fingerprints"]:
        result["fingerprint_difference"] = {
            "expected": baseline.get("fingerprints", {}),
            "actual": result["fingerprints"],
        }
        result["errors"].append(
            "Deterministic fingerprint differs from baseline: "
            + json.dumps(result["fingerprint_difference"], sort_keys=True)
        )
    else:
        for metric, previous in baseline["metrics"].items():
            if metric.startswith("process.") and not compare_process:
                continue
            # Corrections/worker counts can vary with scheduling. Retain them as
            # provenance; they are not a lower-is-faster performance metric.
            if metric.endswith(".count"):
                continue
            current = result["metrics"].get(metric)
            number(previous, f"baseline {metric}")
            limit = previous + max(previous * percent / 100, absolute if metric.endswith("_ms") else 0)
            passed = current is not None and current <= limit
            result["comparisons"].append(
                {
                    "metric": metric,
                    "baseline": previous,
                    "current": current,
                    "limit": limit,
                    "status": "passed" if passed else "failed",
                }
            )
            if not passed:
                result["errors"].append(f"Performance regression: {metric}={current}; limit {limit:.3f}")
    if result["errors"]:
        result["status"] = "failed"


def build_identity(args, suite):
    cache = args.build_dir / "CMakeCache.txt"
    settings = {}
    if cache.is_file():
        for line in cache.read_text(errors="replace").splitlines():
            match = re.match(
                r"((?:CMAKE_(?:BUILD_TYPE|CXX_COMPILER|CXX_FLAGS[^:]*|C_FLAGS[^:]*|SYSTEM_PROCESSOR))|DKR_[A-Z0-9_]+):[^=]+=(.*)",
                line,
            )
            if match and not match[1].endswith(("_SOURCE", "_DIR", "_ROOT", "_GENERATED")):
                settings[match[1]] = match[2]
    compatibility = {
        "suite": canonical(suite),
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "machine_tag": args.machine_tag or canonical(platform.node())[:16],
        "config": args.config,
        "build_settings": settings,
        "warmup": args.warmup,
        "repeat": args.repeat,
    }
    git = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True)
    dirty = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain"], capture_output=True, text=True)
    return {
        "compatibility": compatibility,
        "commit": git.stdout.strip(),
        "dirty": bool(dirty.stdout.strip()),
        "python": platform.python_version(),
        "measurement": "Serial process wall time; stage values are medians across measured runs. No physical scanout or input-latency measurement.",
    }


def discover_ctest(args, items):
    if not any("ctest" in item for item in items):
        return {}
    result = subprocess.run(
        ["ctest", "--test-dir", str(args.build_dir), "-C", args.config, "--show-only=json-v1"],
        capture_output=True,
        text=True,
        timeout=30,
    )
    if result.returncode:
        raise ValueError("Could not discover CTest tests: " + result.stderr.strip())
    return {test["name"]: test for test in json.loads(result.stdout)["tests"]}


def check_inputs(items, variables, ctest_tests):
    errors = []
    for item in items:
        name = item["id"]
        values = item.get("command", []) + item.get("inputs", []) + list(item.get("env", {}).values())
        missing = sorted({key for value in values for key in TOKEN.findall(value) if key not in variables})
        errors.extend(f"{name}: missing variable {key} (set --var {key}=VALUE or --vars-file)" for key in missing)
        # Still inspect independent paths even when another substitution is missing.
        for value in item.get("inputs", []):
            if any(key not in variables for key in TOKEN.findall(value)):
                continue
            path = Path(expand(value, variables)).resolve()
            if not path.is_file():
                errors.append(f"{name}: missing input: {path}")
        if "command" in item:
            executable = item["command"][0]
            if not any(key not in variables for key in TOKEN.findall(executable)):
                executable = expand(executable, variables)
                if not shutil.which(executable):
                    errors.append(f"{name}: missing or nonexecutable command: {executable}")
        elif item["ctest"] not in ctest_tests:
            errors.append(f"{name}: CTest test not configured: {item['ctest']}")
        else:
            entry = ctest_tests[item["ctest"]]
            command = entry.get("command", [])
            if not command or not shutil.which(command[0]):
                errors.append(f"{name}: CTest executable unavailable; build {item['ctest']} first")
            if any(prop.get("name") == "DISABLED" and prop.get("value") for prop in entry.get("properties", [])):
                errors.append(f"{name}: CTest test is disabled: {item['ctest']}")
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", type=Path, default=ROOT / "regression/host.json")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/host")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--output", type=Path, help="new directory for reports and logs")
    parser.add_argument("--baseline", type=Path, help="passing report.json from the same machine/configuration")
    parser.add_argument("--scenario", action="append", default=[], help="scenario glob (repeatable)")
    parser.add_argument(
        "--vars-file", type=Path, metavar="PATH", help="JSON path-variable map; relative paths resolve beside this file"
    )
    parser.add_argument(
        "--var",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="literal variable value, overriding --vars-file (repeatable)",
    )
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--relative-tolerance", type=float, default=20, metavar="PERCENT")
    parser.add_argument("--absolute-tolerance-ms", type=float, default=2)
    parser.add_argument(
        "--compare-process-time",
        action="store_true",
        help="also gate whole-process timings (includes launch/test overhead)",
    )
    parser.add_argument("--machine-tag", help="explicit machine identity for controlled benchmark hosts")
    parser.add_argument("--allow-missing", action="store_true", help="allow partial runs; all-skipped still fails")
    inspection = parser.add_mutually_exclusive_group()
    inspection.add_argument("--list", action="store_true")
    inspection.add_argument(
        "--check-inputs",
        action="store_true",
        help="check selected variables, files, and executables without running scenarios",
    )
    args = parser.parse_args(argv)
    try:
        if not 1 <= args.repeat <= 100 or not 0 <= args.warmup <= 20:
            raise ValueError("repeat must be 1..100 and warmup 0..20")
        number(args.relative_tolerance, "relative tolerance")
        number(args.absolute_tolerance_ms, "absolute tolerance")
        args.build_dir = args.build_dir.resolve()
        suite = load_suite(args.suite)
        items = suite["scenarios"]
        if args.scenario:
            for pattern in args.scenario:
                if not any(fnmatch.fnmatchcase(x["id"], pattern) for x in items):
                    raise ValueError(f"No scenario matches {pattern}")
            items = [x for x in items if any(fnmatch.fnmatchcase(x["id"], p) for p in args.scenario)]
        if args.list:
            for item in items:
                print(f"{item['id']}: {item.get('description', '')}")
            return 0
        variables = load_variables(args.build_dir, args.vars_file, args.var)
        if args.check_inputs:
            errors = []
            try:
                ctest_tests = discover_ctest(args, items)
            except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
                ctest_tests = {}
                errors.append(str(error))
            errors.extend(check_inputs(items, variables, ctest_tests))
            if errors:
                for error in errors:
                    print(f"regression: {error}", file=sys.stderr)
                return 2
            print(f"Inputs ready for {len(items)} selected scenarios; no scenarios executed.")
            return 0
        identity = build_identity(args, suite)
        args.ctest_tests = discover_ctest(args, items)
        baseline = None
        if args.baseline:
            baseline = json.loads(args.baseline.read_text())
            if not isinstance(baseline, dict) or baseline.get("schema_version") != 1:
                raise ValueError("Baseline requires a schema_version 1 report object")
            if (
                not isinstance(baseline.get("identity"), dict)
                or baseline["identity"].get("compatibility") != identity["compatibility"]
            ):
                raise ValueError("Baseline machine, suite, build configuration, or sampling settings do not match")
            if (
                not isinstance(baseline.get("summary"), dict)
                or baseline["summary"].get("failed", 1)
                or not baseline["summary"].get("passed", 0)
            ):
                raise ValueError("Baseline must contain passing scenarios and no failures")
            if not isinstance(baseline.get("scenarios"), list) or not all(
                isinstance(x, dict) and isinstance(x.get("id"), str) and isinstance(x.get("metrics"), dict)
                for x in baseline["scenarios"]
            ):
                raise ValueError("Invalid baseline scenarios")
        if not args.output:
            raise ValueError("--output NEW_DIRECTORY is required")
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
        report = {
            "schema_version": 1,
            "suite": suite["name"],
            "started_at": datetime.now(timezone.utc).isoformat(),
            "identity": identity,
            "summary": {},
            "scenarios": [],
            "comparison_policy": {
                "relative_percent": args.relative_tolerance,
                "absolute_ms": args.absolute_tolerance_ms,
                "compare_process_time": args.compare_process_time,
            },
        }
        (output / "suite.json").write_text(json.dumps(suite, indent=2) + "\n")
        interrupted = False
        try:
            for item in items:
                print(f"Running {item['id']}…", flush=True)
                try:
                    result, interrupted = run_scenario(item, variables, output, args)
                    if baseline:
                        previous = next((x for x in baseline["scenarios"] if x["id"] == item["id"]), None)
                        compare(
                            result,
                            previous,
                            args.relative_tolerance,
                            args.absolute_tolerance_ms,
                            args.compare_process_time,
                        )
                except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
                    result = {
                        "id": item["id"],
                        "status": "failed",
                        "errors": [str(error)],
                        "attempts": [],
                        "metrics": {},
                        "comparisons": [],
                    }
                report["scenarios"].append(result)
                print(f"  {result['status'].upper()} " + "; ".join(result["errors"]), flush=True)
                report["summary"] = {
                    s: sum(x["status"] == s for x in report["scenarios"]) for s in ("passed", "failed", "skipped")
                }
                write_reports(output, report)
                if interrupted:
                    break
        finally:
            write_reports(output, report)
        print(f"Reports: {output / 'report.html'}")
        summary = report["summary"]
        if interrupted:
            return 130
        return (
            1
            if summary.get("failed")
            else (2 if not summary.get("passed") or (summary.get("skipped") and not args.allow_missing) else 0)
        )
    except KeyboardInterrupt:
        print("regression: interrupted", file=sys.stderr)
        return 130
    except (OSError, ValueError, KeyError, TypeError, re.error, subprocess.SubprocessError) as error:
        print(f"regression: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
