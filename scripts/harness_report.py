#!/usr/bin/env python3
"""Portable, dependency-free reports for the regression and performance harness."""

from __future__ import annotations

import html
import json
from pathlib import Path, PurePosixPath
from urllib.parse import quote, urlsplit
import xml.etree.ElementTree as ET


def _escape(value: object) -> str:
    return html.escape(str(value), quote=True)


def _number(value: object) -> str:
    if isinstance(value, (float, int)) and not isinstance(value, bool):
        return f"{value:,.3f}".rstrip("0").rstrip(".")
    return _escape(value)


def _log_link(value: object, label: str) -> str:
    """Only link local artifacts; report data must never introduce active URLs."""
    path = str(value or "")
    try:
        parsed = urlsplit(path)
    except ValueError:
        return f"<span>{_escape(label)} unavailable</span>"
    parts = PurePosixPath(path).parts
    if (
        not path
        or parsed.scheme
        or parsed.netloc
        or parsed.query
        or parsed.fragment
        or path.startswith("/")
        or "\\" in path
        or ".." in parts
        or any(ord(char) < 32 for char in path)
    ):
        return f"<span>{_escape(label)} unavailable</span>"
    return f'<a href="{quote(path, safe="/")}">{_escape(label)}</a>'


def _status(value: object) -> str:
    status = str(value)
    css = status if status in {"passed", "failed", "skipped"} else "unknown"
    return f'<span class="status {css}">{_escape(status)}</span>'


def _errors(errors: list) -> str:
    if not errors:
        return ""
    return '<ul class="errors">' + "".join(f"<li>{_escape(error)}</li>" for error in errors) + "</ul>"


def _elapsed(scenario: dict) -> float:
    return sum(float(attempt.get("elapsed_ms", 0)) for attempt in scenario.get("attempts", [])) / 1000


def _junit(report: dict) -> bytes:
    scenarios = report.get("scenarios", [])
    suite = ET.Element(
        "testsuite",
        name=str(report.get("suite", "regression")),
        tests=str(len(scenarios)),
        failures=str(sum(item.get("status") == "failed" for item in scenarios)),
        skipped=str(sum(item.get("status") == "skipped" for item in scenarios)),
        errors="0",
        time=f"{sum(_elapsed(item) for item in scenarios):.6f}",
    )
    properties = ET.SubElement(suite, "properties")
    ET.SubElement(properties, "property", name="started_at", value=str(report.get("started_at", "")))
    for key, value in sorted(report.get("identity", {}).items()):
        ET.SubElement(properties, "property", name=str(key), value=str(value))
    for scenario in scenarios:
        case = ET.SubElement(
            suite,
            "testcase",
            classname=str(report.get("suite", "regression")),
            name=str(scenario["id"]),
            time=f"{_elapsed(scenario):.6f}",
        )
        messages = [str(error) for error in scenario.get("errors", [])]
        for attempt in scenario.get("attempts", []):
            messages.extend(f"Attempt {attempt.get('index')}: {error}" for error in attempt.get("errors", []))
        for comparison in scenario.get("comparisons", []):
            if comparison.get("status") == "failed":
                messages.append(
                    f"{comparison.get('metric')}: current={comparison.get('current')}, "
                    f"baseline={comparison.get('baseline')}, limit={comparison.get('limit')}"
                )
        status = scenario.get("status")
        if status == "failed":
            reason = "\n".join(messages) or "Scenario failed; inspect attempt logs."
            ET.SubElement(case, "failure", message=messages[0] if messages else "Scenario failed").text = reason
        elif status == "skipped":
            ET.SubElement(case, "skipped", message="\n".join(messages) or "Scenario was not executed.")
        ET.SubElement(case, "system-out").text = json.dumps(
            {"metrics": scenario.get("metrics", {}), "attempts": scenario.get("attempts", [])},
            indent=2,
            allow_nan=False,
        )
    # Child diagnostics can contain terminal controls, which XML 1.0 rejects.
    def xml_text(value: str) -> str:
        return "".join(
            char if (char in "\t\n\r" or 0x20 <= ord(char) <= 0xD7FF
                     or 0xE000 <= ord(char) <= 0xFFFD
                     or 0x10000 <= ord(char) <= 0x10FFFF) else "\ufffd"
            for char in value
        )

    for element in suite.iter():
        if element.text:
            element.text = xml_text(element.text)
        for key, value in element.attrib.items():
            element.set(key, xml_text(value))
    ET.indent(suite)
    return ET.tostring(suite, encoding="utf-8", xml_declaration=True)


def _html(report: dict) -> str:
    scenarios = report.get("scenarios", [])
    counts = {status: sum(item.get("status") == status for item in scenarios) for status in ("passed", "failed", "skipped")}
    cards = "".join(f'<div class="card {status}"><strong>{count}</strong><span>{status}</span></div>' for status, count in counts.items())
    identity = "".join(f"<dt>{_escape(key)}</dt><dd>{_escape(value)}</dd>" for key, value in sorted(report.get("identity", {}).items()))
    rows = []
    for scenario in scenarios:
        attempts = scenario.get("attempts", [])
        metrics = "".join(f"<tr><th>{_escape(key)}</th><td>{_number(value)}</td></tr>" for key, value in sorted(scenario.get("metrics", {}).items()))
        comparisons = "".join(
            f"<tr><th>{_escape(item.get('metric', ''))}</th><td>{_number(item.get('baseline'))}</td>"
            f"<td>{_number(item.get('current'))}</td><td>{_number(item.get('limit'))}</td><td>{_status(item.get('status'))}</td></tr>"
            for item in scenario.get("comparisons", [])
        )
        attempt_rows = "".join(
            f"<tr><th>{_escape(item.get('index'))}</th><td>{_escape(item.get('phase'))}</td>"
            f"<td>{_status(item.get('status'))}{_errors(item.get('errors', []))}</td><td>{_number(item.get('elapsed_ms', 0))}</td>"
            f"<td>{_escape(item.get('exit_code'))}</td><td>{_log_link(item.get('stdout'), 'stdout')} · {_log_link(item.get('stderr'), 'stderr')}</td></tr>"
            for item in attempts
        )
        details = []
        if metrics:
            details.append(f'<h3>Metrics</h3><div class="scroll"><table><thead><tr><th>Metric</th><th>Value</th></tr></thead><tbody>{metrics}</tbody></table></div>')
        if comparisons:
            details.append(f'<h3>Baseline comparisons</h3><div class="scroll"><table><thead><tr><th>Metric</th><th>Baseline</th><th>Current</th><th>Limit</th><th>Status</th></tr></thead><tbody>{comparisons}</tbody></table></div>')
        if attempt_rows:
            details.append(f'<h3>Attempts</h3><div class="scroll"><table><thead><tr><th>Attempt</th><th>Phase</th><th>Status</th><th>Wall time (ms)</th><th>Exit code</th><th>Logs</th></tr></thead><tbody>{attempt_rows}</tbody></table></div>')
        opened = " open" if scenario.get("status") == "failed" else ""
        rows.append(
            f'<section class="scenario"><details{opened}><summary>{_status(scenario.get("status"))} '
            f'<strong>{_escape(scenario["id"])}</strong><span class="duration">{_number(_elapsed(scenario))} s total</span></summary>'
            f'<p>{_escape(scenario.get("description", ""))}</p>{_errors(scenario.get("errors", []))}{"".join(details)}</details></section>'
        )
    title = _escape(report.get("suite", "Regression harness"))
    return f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title} — regression report</title>
<style>
:root{{color-scheme:light dark;--bg:#101722;--panel:#192331;--text:#eaf0f7;--muted:#aebed0;--border:#364558}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--bg);color:var(--text);font:16px/1.6 system-ui,sans-serif}}
main{{max-width:1180px;margin:auto;padding:40px 24px}}h1{{font-size:2.2rem;margin:0}}h2{{margin-top:2rem}}h3{{font-size:1rem;margin-bottom:.5rem}}
a{{color:#8fcbff}}.subtitle,.duration{{color:var(--muted)}}.scope{{border-left:3px solid #8fcbff;padding:12px 18px;background:var(--panel)}}
.cards{{display:flex;gap:16px;margin:24px 0;flex-wrap:wrap}}.card{{flex:1;min-width:120px;background:var(--panel);border:1px solid var(--border);border-radius:10px;padding:16px 24px}}.card strong{{font-size:2rem;display:block}}.card span{{text-transform:capitalize}}
.status{{display:inline-block;border-radius:5px;padding:2px 9px;font-size:.8rem;font-weight:700;text-transform:uppercase;background:#35455b}}.passed{{color:#86e2b0}}.failed{{color:#ffaba5}}.skipped{{color:#ebd18a}}
.scenario{{border:1px solid var(--border);border-radius:8px;margin:12px 0;padding:16px;background:var(--panel)}}summary{{cursor:pointer}}summary strong{{margin-left:8px}}.duration{{float:right;font-size:.9rem}}
.scroll{{overflow:auto}}table{{border-collapse:collapse;width:100%;font-size:.9rem}}th,td{{text-align:left;padding:10px;border-bottom:1px solid var(--border);vertical-align:top}}thead{{color:var(--muted)}}th{{font-weight:600}}td,dd{{overflow-wrap:anywhere}}.errors{{color:#ffb8b2;white-space:pre-wrap}}
dl{{display:grid;grid-template-columns:minmax(120px,1fr) 3fr;gap:8px 24px}}dt{{color:var(--muted)}}dd{{margin:0}}footer{{color:var(--muted);margin-top:32px;font-size:.9rem}}
@media(max-width:600px){{main{{padding:24px 14px}}.duration{{float:none;display:block}}dl{{display:block}}dd{{margin-bottom:12px}}}}
</style></head><body><main>
<p class="subtitle">DKR-R · Regression &amp; performance</p><h1>{title}</h1><p class="subtitle">Started {_escape(report.get('started_at', ''))} · {len(scenarios)} scenarios</p>
<div class="cards">{cards}</div>
<p class="scope">Process wall time includes startup and shutdown; it is not frame time. Skipped scenarios are not passes. This report covers the configured checks and does not certify real gameplay or hardware acceptance.</p>
<p><a href="report.json">Machine-readable JSON</a> · <a href="junit.xml">JUnit XML</a></p>
<details><summary>Run identity</summary><dl>{identity or '<dt>Identity</dt><dd>Not recorded</dd>'}</dl></details>
<h2>Scenarios</h2>{''.join(rows) or '<p>No scenarios were recorded.</p>'}
<footer>Warmup and measured attempts are shown separately. Scenario totals include all attempts. Metric names define their units.</footer>
</main></body></html>'''


def write_reports(output: Path, report: dict) -> None:
    """Write JSON, standalone HTML and JUnit reports into ``output``.

    Serialize everything before writing so invalid metrics do not leave a partial
    report. Log links are relative artifact paths; no log contents are embedded.
    """
    json_data = json.dumps(report, indent=2, allow_nan=False) + "\n"
    html_data = _html(report)
    junit_data = _junit(report)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / "report.json").write_text(json_data, encoding="utf-8")
    (output / "report.html").write_text(html_data, encoding="utf-8")
    (output / "junit.xml").write_bytes(junit_data)
