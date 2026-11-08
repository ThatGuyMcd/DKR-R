"""Run only the synthetic party handler regression and its failing control."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    if a.before.stem != 'DKRRaceReleaseBeforeChecks' or a.after.stem != 'DKRRaceReleaseChecks':
        raise ValueError('Only the explicitly synthetic fixture binaries may be run')
    a.output.mkdir(parents=True, exist_ok=True)
    records = {}
    for name in ('before', 'after'):
        path = getattr(a, name).resolve()
        result = subprocess.run([str(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=20, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        text = result.stdout.decode(errors='replace')
        (a.output / ('regression-' + name + '.log')).write_bytes(result.stdout)
        records[name] = {'exit_code': result.returncode, 'binary_sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        if name == 'before':
            if result.returncode != 1 or 'FIRST POST-LOAD RELEASE WAS DISCARDED' not in text:
                raise ValueError('Unchanged runtime did not reproduce the expected defect')
        else:
            passed = re.search(r'(\d+) party runtime regression checks passed', text)
            if result.returncode != 0 or not passed:
                raise ValueError('Fixed party runtime regression failed; inspect regression-after.log')
            records['passed_checks'] = int(passed[1])
    records.update({'synthetic_state_bytes': 16, 'production_party_handler': True,
                    'game_or_rom_or_rsp_executed': False})
    (a.output / 'REGRESSION-RECEIPT.json').write_text(json.dumps(records, indent=2) + '\n')
    print('Old runtime reproduces the defect; fixed runtime passes', records['passed_checks'], 'checks.')
