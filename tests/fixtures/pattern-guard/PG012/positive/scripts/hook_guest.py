"""Rewrite recompiled guest code in place."""
from pathlib import Path
import re
import sys

funcs = Path(sys.argv[1]) / "RecompiledFuncs"
for path in funcs.glob("funcs_*.c"):
    text = path.read_text()
    path.write_text(re.sub(r"\bosRecvMesg\b", "dkr_recv_mesg", text))
