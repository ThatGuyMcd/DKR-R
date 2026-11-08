"""Derive release-only tooling; never rewrite the accepted native repair layers."""
from pathlib import Path
AUTH=Path(__file__).resolve().parent
PREVIOUS=AUTH.parent/'v1.0.10'
if __name__=='__main__':
    for name in ('release.py','check_docs.py','update_assets.py'):
        target=AUTH/name
        if target.exists():raise ValueError('Refusing to overwrite authored release tooling: '+name)
        target.write_text((PREVIOUS/name).read_text().replace('1.0.10','1.0.11').replace('release110','release111'))
    for name in ('PLATFORMS.md','UPDATES.md'):
        folder=AUTH/'pages';folder.mkdir(exist_ok=True)
        (folder/name).write_text((PREVIOUS/'pages'/name).read_text().replace('1.0.10','1.0.11'))
    (AUTH/'UPDATER.txt').write_text((PREVIOUS/'UPDATER.txt').read_text().replace('1.0.10','1.0.11'))
    print('Derived release document/signing wrappers; accepted native layers untouched.')
