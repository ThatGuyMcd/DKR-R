"""Generate a NEW probe from recompiled guest code."""
from pathlib import Path
import re
import sys

source, output = Path(sys.argv[1]) / "RecompiledFuncs", Path(sys.argv[2])
for index, path in enumerate(sorted(source.glob("funcs_*.c"))):
    text = re.sub(r"\bosRecvMesg\b", "probe_recv_mesg", path.read_text())
    (output / f"guest_{index}.c").write_text(text)
