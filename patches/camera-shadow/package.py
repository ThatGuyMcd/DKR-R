"""Versioned beta-6 packaging composition. Never starts the game or a mod."""
from pathlib import Path
import argparse
import sys

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
source=ROOT/'patches/camera-plus/package_playtest.py'
text=source.read_text().replace('1.0.7+beta.5','1.0.7+beta.6')
text=text.replace("    subprocess.run([sys.executable,str(ROOT/'scripts/update_release.py')",
                  "    shutil.copyfile(ROOT/'patches/camera-shadow/RELEASE-NOTES.txt',stage/'RELEASE-NOTES.txt')\n"
                  "    subprocess.run([sys.executable,str(ROOT/'scripts/update_release.py')")
text=text.replace("    script=(ROOT/'scripts/Package-Linux-AppImage.sh').read_text()",
                  "    script=(ROOT/'scripts/Package-Linux-AppImage.sh').read_text().replace('1.0.7+beta.5','1.0.7+beta.6')\n"
                  "    script=script.replace('${PROJECT_ROOT}/docs/BETA5-1.0.7.txt','${PROJECT_ROOT}/patches/camera-shadow/RELEASE-NOTES.txt')")
scope={'__file__':str(source),'__name__':'camera_shadow_packaging'}
exec(compile(text,str(source),'exec'),scope)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=('windows','linux-script'))
    p.add_argument('--base',type=Path);p.add_argument('--binaries',type=Path)
    p.add_argument('--mod',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.mode=='windows':scope['windows'](a.base.resolve(strict=True),a.binaries.resolve(strict=True),a.mod.resolve(strict=True),a.output)
    else:scope['linux_script'](a.mod.resolve(strict=True),a.output)
