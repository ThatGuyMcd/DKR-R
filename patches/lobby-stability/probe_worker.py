"""Check the packaged importer's online handshake without loading any ROM/mod."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import time

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    worker = args.worker.resolve()
    output = args.output.resolve()
    if not worker.is_file() or output.exists():
        raise ValueError('Need an existing packaged worker and a fresh probe folder')
    output.mkdir(parents=True)
    request = output / 'request.json'
    request.write_text(json.dumps({'schema': 1, 'operation': 'online-capabilities', 'source': output.as_posix(), 'roms': []}) + '\n')
    started = time.monotonic()
    result = subprocess.run([str(worker), str(request)], cwd=output, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=12, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
    (output / 'output.log').write_bytes(result.stdout[:16384])
    if result.returncode != 0 or json.loads((output / 'capabilities.json').read_text()) != {'schema': 1, 'online_prepare': 1, 'online_prepare_batch': 1}:
        raise ValueError('Packaged worker capability probe failed; inspect output.log')
    receipt = {'worker_sha256': hashlib.file_digest(worker.open('rb'), 'sha256').hexdigest(),
               'online_prepare': 1, 'online_prepare_batch': 1, 'elapsed_ms': round((time.monotonic() - started) * 1000),
               'roms_or_mods_loaded': False, 'game_executed': False}
    (output / 'PROBE-RECEIPT.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Packaged ModWorker online capability handshake passed; no ROM/mod loaded.')
