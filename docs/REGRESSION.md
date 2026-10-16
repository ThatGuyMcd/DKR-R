# Regression and performance harness

Run repeatable checks, retain their logs, and compare a candidate against a
baseline with `scripts/regression.py`. The default suite exercises the available
host build. The private replay suite exercises actual retail CPU code using
locally prepared fixtures. Reports distinguish test duration from instrumented
runtime stages; a headless result is not a rendered gameplay benchmark.

## First run

Build the host preset as described in [DEVELOPMENT.md](DEVELOPMENT.md), then
run from the repository root:

```sh
python3 scripts/regression.py --build-dir build/host --output build/regression/baseline
python3 scripts/regression.py --build-dir build/host \
  --baseline build/regression/baseline/report.json --output build/regression/candidate
```

Use a new output directory for each run. Keep the baseline from the known-good
build, rebuild your candidate, and repeat the same scenarios with the same inputs.
The report and retained process logs are the evidence for the comparison.

Inspect or narrow the work before running it:

```sh
python3 scripts/regression.py --build-dir build/host --list
python3 scripts/regression.py --build-dir build/host \
  --scenario rollback --scenario checkpoint-benchmark --repeat 3 --warmup 1 \
  --output build/regression/replay-host
```

Warmups are separate from measured repetitions. Repetition helps expose flaky
checks and variation; it does not make different machines comparable. Keep build
type, hardware, power mode, background load, and instrumentation consistent.
Process duration includes startup, allocation, and test assertions. It measures
how long that check took, not milliseconds per gameplay frame.

The checkpoint benchmark separately times the production hash, store, and restore
implementations against deterministic synthetic 8 MiB state. It checks exact
roundtrips, retained history, eviction and a branched timeline, and emits a fixed
fingerprint. Its internal four warmups and 32 samples are independent of the
harness's process repetitions. It is a storage benchmark, not a game-frame benchmark.

Each output directory contains a self-contained `report.html`, machine-readable
`report.json`, `junit.xml`, the suite definition and separate stdout/stderr logs
for every attempt. Reports are updated after each completed scenario. Open HTML
locally; it requires no server or network. Input files are hashed, never copied.
Reports and logs can include private paths, so inspect them before sharing.

Baseline comparisons require the same suite, machine/OS, build settings, warmup
and repetition counts, input hashes and scenario arguments. Binary hashes and git
revisions are recorded as provenance, but may change between baseline and candidate.
An explicit `--machine-tag` identifies a controlled benchmark host; it does not make
different hardware comparable. Stage metrics are medians across measured attempts;
process p95 uses nearest rank. A timing fails when its increase exceeds both 20%
and 2 ms by default. Set `--relative-tolerance` and `--absolute-tolerance-ms` to
adjust these noise allowances. Counter values are informational, not speed gates.
Whole-process timings are informational by default because startup and test-runner
overhead dominate tiny checks. `--compare-process-time` also gates those metrics.
No baseline is overwritten or automatically promoted.

Exit codes are 0 for success, 1 for a failed check or regression, 2 for invalid
configuration or incomplete coverage, and 130 for interruption. Missing private
inputs are skipped and make the run incomplete. `--allow-missing` explicitly
allows a partial run; a run with every scenario skipped still fails. Child crashes,
timeouts, missing success markers and malformed/required-but-missing measurements
fail the scenario. POSIX child process groups are retired on timeout or interruption;
Windows uses `taskkill /T /F` for timed-out processes.

## Actual retail replay suite

`regression/replay.json` contains both US revisions' owned-race, authored-driver,
menu, Adventure, scene-resume, and impaired scene-session checks, plus the
production registry's interleaved two-revision state/PCM proof. Every scenario
requires a successful process exit and specific existing success markers.

These checks require separately generated and built private qualification
executables; the host build alone cannot run them. `DKRGuestReplayProbe` is
built from `runtime-recomp/tests/replay_probe/CMakeLists.txt` with
`DKR_PROBE_GENERATED` pointing at the corresponding Patch Pipeline output.
Generate the audited audio, input, authored CPU, full-scene and symbol-isolation
closures needed by the selected modes. The registry target is
`DKROwnedRevisionRegistryTests` and must include both revisions. Do not substitute
an ordinary release executable for either target.

### Prepare actual-ROM fixtures

Start with your own canonical US `.z64` ROMs and prepared CPU/RSP outputs from
the [runtime preparation workflow](BUILDING.md). Normal generated code and
dependencies remain read-only. Capture and probe builds must use the same
checkout's native participant layout. Keep every output below ignored `build/`
or another private directory; snapshots contain ROM-derived data and save state.

A diagnostic runtime can capture CPU fixtures without RT64, a window or a GPU.
For example, from the repository root, with absolute input paths substituted:

```sh
cmake -S runtime-recomp -B build/retail/capture -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DDKR_RUNTIME_BUILD_GENERATED=ON \
  -DDKR_DEPENDENCIES_DIR=/absolute/prepared/extern \
  -DDKR_GENERATED_SOURCE_V77=/absolute/generated/v77/RecompiledFuncs \
  -DDKR_GENERATED_SOURCE_V80=/absolute/generated/v80/RecompiledFuncs \
  -DDKR_GENERATED_RSP_SOURCE=/absolute/prepared/RecompiledRSP \
  -DDKR_RUNTIME_BUILD_RT64=OFF -DDKR_RUNTIME_BUILD_SDL3_INPUT_HOST=OFF \
  -DDKR_REPLAY_QUALIFICATION=ON
cmake --build build/retail/capture --target DKRPortGame --parallel 4

python3 scripts/capture_replay_fixture.py \
  --runtime build/retail/capture/bin/Release/DKR-R \
  --rom /absolute/roms/us77.z64 --output build/retail/captured-v77
python3 scripts/capture_replay_fixture.py \
  --runtime build/retail/capture/bin/Release/DKR-R \
  --rom /absolute/roms/us80.z64 --output build/retail/captured-v80
```

On Windows use the corresponding `DKR-R.exe` path. The helper defaults to two
players, retail map 3, and a 60-second runtime timeout. `--players`, `--map`, and
`--timeout` expose bounded alternatives. It verifies the canonical ROM SHA-1
before creating anything, requires a new output directory, removes inherited
`DKR_*` switches, and starts the game with a separate profile. The runtime boots
the actual retail race, parks after 120 active-race ticks, and joins native
workers before writing `race.dkr-component`.

Success requires exit zero, exactly one successful quiescence record for the ROM
revision in `profile/logs/runtime.log`, and a matching nonzero fixture size.
`fixture.json` records the ROM identity, runtime and fixture hashes, map, player
count, size and capture duration. Failed captures retain private logs but do not
produce this success manifest. Capturing a fixture does not establish replay
correctness; run the replay suite afterward. Diagnostic capture also supplies
no GPU, rendered-image, or presentation-latency evidence.

Generate fresh isolated probes for each revision using the same normal CPU
outputs and the pinned runtime header. For v77:

```sh
python3 scripts/generate_replay_probe.py \
  /absolute/generated/v77/RecompiledFuncs \
  /absolute/prepared/extern/n64-modern-runtime/N64Recomp/include/recomp.h \
  build/retail/reviewed-v77 --revision 77 \
  --dependencies-dir /absolute/prepared/extern \
  --rsp-source /absolute/prepared/RecompiledRSP \
  --scene-cuts --audio-services --input-services --authored-cpu \
  --isolate-symbols --full-scenes --mod-services --inline-memory
cmake -S runtime-recomp/tests/replay_probe -B build/retail/probe-v77 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDKR_DEPENDENCIES_DIR=/absolute/prepared/extern \
  -DDKR_PROBE_GENERATED="$PWD/build/retail/reviewed-v77"
cmake --build build/retail/probe-v77 --target DKRGuestReplayProbe --parallel 4
```

Repeat with v80 inputs, output/build directory names and `--revision 80`.

After generating both private closures, add them to the existing capture build
and explicitly build the registry test (it is excluded from the default build):

```sh
cmake -S runtime-recomp -B build/retail/capture \
  -DDKR_RUNTIME_BUILD_GENERATED=ON \
  -DDKR_EXPERIMENTAL_OWNED_GENERATED="$PWD/build/retail/reviewed-v77" \
  -DDKR_EXPERIMENTAL_OWNED_GENERATED_V80="$PWD/build/retail/reviewed-v80"
cmake --build build/retail/capture --target DKROwnedRevisionRegistryTests \
  --config Release --parallel 4
```

This reuses the capture build's normal CPU/RSP inputs and shared dependency
settings. Both private revision directories are required for the interleaved
proof. For Ninja, set `registry` to the absolute path of
`build/retail/capture/DKROwnedRevisionRegistryTests`; multi-configuration Windows
builds place `DKROwnedRevisionRegistryTests.exe` under `Release/`.

Generation verifies audited inputs and refuses existing output directories;
never edit generated code or bypass a failed audit. `--self-test` on the resulting
probe checks actual retail CPU closure behavior without a race snapshot; the
suite below exercises captured state. Use absolute paths for suite variables.
For the default captures, `race77` and `menu77` both point to
`build/retail/captured-v77/race.dkr-component`; likewise for v80. The `menu*`
variables deliberately name two-player race fixtures, not menu-screen snapshots.

```sh
python3 scripts/regression.py --suite regression/replay.json \
  --build-dir build/retail/capture \
  --var probe77=/private/probe77/DKRGuestReplayProbe \
  --var probe80=/private/probe80/DKRGuestReplayProbe \
  --var registry=/private/build/DKROwnedRevisionRegistryTests \
  --var race77=/private/fixtures/race77.dkr-component \
  --var race80=/private/fixtures/race80.dkr-component \
  --var menu77=/private/fixtures/tracks-two-player77.dkr-component \
  --var menu80=/private/fixtures/tracks-two-player80.dkr-component \
  --var rom77=/private/roms/us77.z64 --var rom80=/private/roms/us80.z64 \
  --repeat 3 --warmup 1 --output build/regression/retail-baseline
```

For a focused run, add `--scenario 'v77-owned-race'`. For candidate comparison,
repeat the same command with a new output directory and
`--baseline build/regression/retail-baseline/report.json`. `--list` shows the
selected scenarios without executing them. Missing inputs or variables are errors;
use `--allow-missing` only when deliberately accepting incomplete coverage, then
check the skipped scenarios in the report.

Set `--build-dir` to the qualification build used for the registry so reports
record its CMake settings rather than the default host build. The standalone
probes have separate build directories: keep their configuration consistent
between baseline and candidate. Each executed binary is hashed independently;
the report's CMake settings describe only the selected build directory.

### Fixture meanings

- `race77` and `race80` are quiesced bootstrap snapshots of the matching revision's
  active retail race. They must match the generated probe's native-participant
  layout and scene capabilities. The probe validates format/revision; a ROM is
  checked against the canonical retail fingerprint.
- Despite their short names, `menu77` and `menu80` are **two-player retail Tracks
  race snapshots**, not snapshots already sitting at Title or a menu. The
  authored controller route pauses the race, quits to Title, selects characters,
  and then enters another track or Adventure. Adventure requires exactly two
  owners and follows the real new-game introduction. One suitable race snapshot
  may serve both variables; arbitrary race fixtures are not interchangeable.
- Scene-session checks exercise two, three, and four logical peers with loss and
  delay. That does not establish that separately initialized 2P, 3P, and 4P
  renderer configurations were tested.

The suite sets `DKR_PROBE_TEST_FRAMES=120` for modes that honor this setting.
Menu and Adventure routes have their own bounded progression loops; their timing
must not be divided by 120. The registry fingerprint proof uses its existing six
interleaved ticks per revision. It prints separate state and effects-with-PCM
fingerprints for each revision; matching these supplements each executable's
internal byte-exact restore/replay assertions.

Fixtures contain ROM-derived assets and may contain save state. Keep them local.
The harness identifies inputs without copying their bytes into the report.
The runner removes inherited `DKR_*` switches and applies only those declared by
the scenario. In particular, it does not inherit `DKR_PROBE_REFERENCE_OUTPUT`,
which would emit private state/effect dumps. Child working directories are unique
to each scenario; commands should use absolute input paths.

## Reading performance evidence

The retail manifest enables `DKR_ROLLBACK_PROFILE=1`. Private probe entrypoints
flush existing opt-in telemetry on ordinary exit. Where the exercised code records
a stage, logs contain `[rollback][timing]` rows for guest ticks, audio DSP,
checkpoints, journals, publication, and other operations. Missing stage samples
mean unavailable data, not zero cost. Failure or timeout logs can be useful for
diagnosis but are not a successful performance sample.
Owned-race, authored-driver and registry scenarios require guest-tick telemetry;
rebuild old private probes before using them with this suite.

`p95-upper-us` and `p99-upper-us` are upper bounds from fixed histogram buckets.
They are not exact quantiles and should not be compared as though they were exact
presentation intervals. Aggregate CPU telemetry is approximate and includes the
extra work intentionally performed by qualification, such as rollback and replay.

Rendered performance remains a separate acceptance layer. The existing
`scripts/Test-IntegratedRollback.ps1` launches real isolated peers, checks normal
launch/handoff and confirmed race progress, and can evaluate warmed presentation
intervals and genuine interpolation. A CPU suite pass says nothing about GPU
readback, physical scanout, controller latency, audible output, or WAN behavior.
Use actual hardware for those measurements.

## Adding a scenario

A suite is JSON with `schema_version: 1`, a `name`, and a `scenarios` list. Each
scenario has a unique `id`, `description`, tokenized `command`, `env`, `inputs`,
`expect_stdout`, and positive `timeout_seconds`. `${name}` substitutions are
provided with `--var name=value`. Commands are argument lists, not shell strings.
List every fixture/configuration input so a changed input cannot silently stand
in for the same test. Use deterministic success markers and a bounded timeout.

Alternatively, use `"ctest": "ExactRegisteredTestName"` instead of `command`.
The runner preserves CTest's environment, working-directory and other registered
properties, discovers the exact test and checks its JUnit outcome; an empty test
selection or skipped CTest test is never a pass. `${root}`, `${build}`, and
`${python}` are built-in variables. Only use trusted suite files: command arrays
execute programs, even though no shell is involved.

Optional `fingerprints` are multiline regular expressions whose complete matches
must remain identical across attempts and against a baseline. `required_metrics`
names measurements that must exist; `budgets` maps metric names to absolute maxima.
Custom executables can emit one line such as:

```text
DKR_HARNESS {"metrics":{"scene.load_ms":12.5,"tick.p95_ms":3.2}}
```

Metrics must be finite, nonnegative and uniquely named; `process.*` is reserved.
Timing names should end in `_ms`; comparisons assume lower is better. The runner
also reads existing rollback timing records and presentation-interval summaries.
Presentation metrics are the worst reported window percentile within a process,
then the median across repetitions, not pooled frame percentiles or scanout times.

Prefer extending real checks that compare complete simulation state and confirmed
effects. A process that launches successfully is not a gameplay regression test.
The current retail routes are authored C++ qualification sequences; this harness
does not yet play arbitrary `.dkrreplay` recordings or provide screenshot matching.

## Reusing local replay paths

Save the replay suite's nine path variables once in an ignored local file such as
`build/retail/replay-paths.json`:

```json
{
  "probe77": "probe-v77/DKRGuestReplayProbe",
  "probe80": "probe-v80/DKRGuestReplayProbe",
  "registry": "capture/DKROwnedRevisionRegistryTests",
  "race77": "captured-v77/race.dkr-component",
  "race80": "captured-v80/race.dkr-component",
  "menu77": "captured-v77/race.dkr-component",
  "menu80": "captured-v80/race.dkr-component",
  "rom77": "/private/roms/us77.z64",
  "rom80": "/private/roms/us80.z64"
}
```

Adjust the executable locations for your build generator and configuration. Then
run a focused check or omit `--scenario` to select the complete suite:

```sh
python3 scripts/regression.py --suite regression/replay.json \
  --build-dir build/retail/capture \
  --vars-file build/retail/replay-paths.json \
  --scenario v77-owned-race --repeat 1 --warmup 0 \
  --output build/regression/retail-focused
```

Every value in `--vars-file` is a path. Relative paths resolve against the JSON
file's directory, regardless of the invocation directory; absolute paths stay
absolute. Paths need not exist until a selected scenario uses them. Extra valid
variable names are allowed, so one file can serve multiple scenario selections.
No shell, environment-variable, or `${name}` expansion occurs inside this file.

`--var NAME=VALUE` overrides an entry from the file, regardless of option order,
and retains its literal value. Use `--var` for scalar workload parameters as well
as one-off path overrides. Duplicate JSON keys, repeated `--var` names, invalid
identifiers, empty values, non-string values, and the reserved names `root`,
`build`, and `python` are rejected before execution. Keep this file and private
fixtures under ignored `build/`; the file references ROMs without copying them.

Before an expensive replay run, check all selected inputs at once:

```sh
python3 scripts/regression.py --suite regression/replay.json \
  --build-dir build/retail/capture \
  --vars-file build/retail/replay-paths.json --check-inputs
```

`--check-inputs` reports every missing variable, missing input file, and missing
or nonexecutable command across the selected scenarios. It exits `0` when ready,
`2` otherwise, even with `--allow-missing`. It honors `--scenario` filters and
validates variable files and overrides. No output directory is required or
created, and no scenario command runs. Direct-command checks only inspect paths;
CTest scenarios use CTest's show-only discovery, which can update CTest's own
`Testing/Temporary/LastTest.log`, and report unregistered, unbuilt, or disabled
tests. Preflight establishes availability, not ROM validity or gameplay success;
those assertions still require an actual run. `--list` remains a lightweight
scenario listing and does not validate local variables or inputs.

Direct-command executable paths resolve against the invocation directory (or
`PATH` for bare command names) before the runner enters the scenario's output
directory. Preflight uses the same lookup. `--vars-file` provides absolute paths
after resolution, so its executable paths do not depend on the invocation directory.
