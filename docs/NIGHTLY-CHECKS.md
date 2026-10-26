# Sanitizers and seeded network stress

These ROM-free checks run locally and are defined in
[`nightly-host-checks.yml`](../.github/workflows/nightly-host-checks.yml).
The workflow runs daily at 08:17 UTC or through manual dispatch, without adding
work to pull-request checks. ASan/UBSan covers Linux and macOS; seeded stress
covers Linux, macOS, and Windows.

## Prepare once

Use Python 3.11+, Ninja, the platform's C++20 compiler, and the
[prepared host dependencies](DEVELOPMENT.md#first-contribution-tests-without-a-rom).
The stress runner requires CTest 4.1+ for explicit random seeds. If your current
CMake is older, install the same checksum-verified tools used by the workflow:

```sh
python3 -m venv build/nightly-tools
build/nightly-tools/bin/python -m pip install --require-hashes --only-binary=:all: \
  -r .github/nightly-tools.txt
export PATH="$PWD/build/nightly-tools/bin:$PATH"
```

On Windows, use PowerShell:

```powershell
python -m venv build/nightly-tools
& .\build\nightly-tools\Scripts\python.exe -m pip install --require-hashes --only-binary=:all: -r .github/nightly-tools.txt
$env:PATH = "$PWD\build\nightly-tools\Scripts;$env:PATH"
```

## ASan and UBSan

On native Linux or macOS with GCC or Clang:

```sh
cd runtime-recomp
cmake --preset host-sanitizers
cmake --build --preset host-sanitizers --parallel 4
ctest --preset host-sanitizers --parallel 2 --output-junit junit.xml
```

This creates `build/host-sanitizers` with debug information, assertions, frame
pointers, and fatal sanitizer diagnostics. It instruments native project code
and dependencies built in that tree. Generated retail payloads, the renderer,
cross-compilation, and MSVC are rejected by this configuration.

On Ubuntu 24.04, the nightly workflow installs `clang-18`, `clang-tools-18`,
`libclang-rt-18-dev`, and `ninja-build`, then configures with
`-DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18`.
`clang-tools-18` supplies the dependency scanner used by newer CMake builds.
Linux's default leak detection remains enabled; Apple's sanitizer runtime does
not provide LeakSanitizer. System libraries outside the build are not
instrumented.

The reviewed libjuice patch replaces incompatible picohash callback casts with
type-correct adapters and uses compiler byte-order constants under strict C99.
Known hash and HMAC vectors cover the adapters and byte-order detection; no hash
algorithm or dependency revision changes.

For shared prepared dependencies, add
`-DDKR_DEPENDENCIES_DIR=/absolute/path/to/extern` when configuring. Dependency
source trees stay unchanged. Results are in `build/host-sanitizers/junit.xml`
and `build/host-sanitizers/Testing/Temporary/`.

## Repeated, randomized session/network tests

Build the regular host tests, then run the exact ten-round nightly command:

```sh
cd runtime-recomp
cmake --preset host
cmake --build --preset host --parallel 4
cd ..
python3 scripts/stress_tests.py --build-dir build/host --rounds 10 --parallel 2 \
  --output build/stress/nightly-local
```

On Windows, use the `host-windows` preset and pass
`--build-dir build/host-windows --config Release` with `python`.
`--list` shows the selected tests without executing rounds. `--test-regex`
narrows the labeled suite for a focused investigation.

The default selection covers session/network policies and actual loopback
Friend, Quick Join, and DirectSession tests. The complete prepared host build
currently selects 11 tests. A missing required test, missing executable,
skipped test, incomplete JUnit report, or failed test makes the command fail.
The runner stops after the first failed round; it never retries to obtain a
passing result.

Each round changes the scheduling seed. The base seed is printed and recorded
in `manifest.json`; reproduce it using a new output directory:

```sh
python3 scripts/stress_tests.py --build-dir build/host --rounds 10 --parallel 2 \
  --seed 42611 --output build/stress/reproduce-42611
```

Seeds reproduce CTest's scheduling order, not operating-system thread
scheduling. Output directories must be new, preserving earlier evidence.
Each contains:

- `manifest.json` and `ctest-tests.json`: seeds, commands, selected tests, and
  test definitions.
- `junit.xml` and `summary.md`: combined results with round-prefixed test names.
- `round-NNN/`: that round's JUnit, CTest output, and copied diagnostic logs.

Manual workflow dispatch accepts `rounds` and an optional base `seed` using the
same command. Nightly job summaries and artifacts retain sanitizer diagnostics
and stress evidence for 14 days.
