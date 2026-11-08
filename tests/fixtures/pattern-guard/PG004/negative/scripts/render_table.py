"""Writes a new table from data; no first-party text involved."""
from pathlib import Path
import sys

rows = Path(sys.argv[1]).read_text().splitlines()
Path(sys.argv[2]).write_text("\n".join(row.replace(",", " | ") for row in rows))
