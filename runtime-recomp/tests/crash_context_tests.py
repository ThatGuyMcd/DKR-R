"""Exercise the real fatal signal writer in an isolated process."""

import re
import subprocess
import sys

if len(sys.argv) != 2:
    # The dependency-free tooling suite cannot build the native probe. CTest
    # supplies its compiled path on both Windows and Linux.
    print("Native crash-context probe is exercised by CTest (DKRCrashContext).")
    sys.exit(0)

result = subprocess.run([sys.argv[1], "--fault"], capture_output=True, text=True, check=False)
if sys.platform == "win32":
    assert result.returncode == 0, result
    assert re.search(r"\[boot\]\[binary\] id=[a-fA-F0-9]{16,64} format=PE", result.stderr)
    frames = re.findall(r"frame=[12] pc=0x([A-F0-9]+) module=\S+ base=0x([A-F0-9]+) rva=0x([A-F0-9]+)", result.stderr)
    assert len(frames) == 2 and frames[0][1] != frames[1][1], result.stderr
    assert all(int(pc, 16) - int(base, 16) == int(rva, 16) for pc, base, rva in frames)
    print("EXE and external-DLL addresses use their own module base.")
    sys.exit(0)
assert result.returncode == 139, result
assert re.search(r"\[boot\]\[binary\] id=[a-f0-9]{8,64} format=ELF", result.stderr)
assert "[boot][crash] signal=11" in result.stderr
assert re.search(r"frame=0 pc=0x[0-9a-f]+ module-id=[a-f0-9]{8,64} base=0x[0-9a-f]+ rva=0x[0-9a-f]+", result.stderr)
assert re.search(r"frame=1 pc=0x[0-9a-f]+ module-id=[a-f0-9]{8,64}", result.stderr)
assert "sp=0x" in result.stderr
print("Native fault PC/module/build identity survived an actual SIGSEGV.")
