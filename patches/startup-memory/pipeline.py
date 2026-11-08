"""Publish the startup-memory CMake hook through the Patch Pipeline."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
path = ROOT / 'runtime-recomp/cmake/PerformancePatches.cmake'
text = path.read_text(encoding='utf-8')
anchor = '    "${DKRPORT_ROOT}/patches/custom-magic-codes/runtime-code-heap.patch" librecomp/src/recomp.cpp)'
hook = '\ninclude("${DKRPORT_ROOT}/patches/startup-memory/RuntimeMemory.cmake")'
if hook not in text:
    if text.count(anchor) != 1:
        raise ValueError('Runtime heap pipeline anchor changed')
    path.write_text(text.replace(anchor, anchor + hook), encoding='utf-8', newline='\n')
print('Startup-memory staging hook published; protected source checkouts unchanged.')
