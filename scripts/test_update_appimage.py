#!/usr/bin/env python3
"""Run the copied updater against an isolated AppImage and retain its data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("package","manifest","signature","helper","parent"):
        parser.add_argument("--"+name)
    args=parser.parse_args()
    if args.parent:
        request=Path(args.parent);value=json.loads(request.read_text());job=request.parent
        value["parent"]=os.getpid();request.write_text(json.dumps(value))
        subprocess.Popen([str(job/"DKR-R-Updater"),"--apply",str(request)],cwd=job)
        end=time.monotonic()+10
        while time.monotonic()<end:
            if (job/"ready").is_file():return 0
            time.sleep(0.05)
        raise RuntimeError("No AppImage updater acknowledgement")
    manifest=json.loads(Path(args.manifest).read_text())
    with tempfile.TemporaryDirectory(prefix="dkrr-appimage-update-") as directory:
        root=Path(directory)/"Install café with spaces";root.mkdir()
        target=root/"DKR-R.AppImage";shutil.copy2(args.package,target);target.chmod(0o755)
        data=root/"dkr-runtime-data";(data/"saves").mkdir(parents=True)
        saved=data/"saves/offline.eep";saved.write_bytes(bytes(range(256))*2)
        old_save=hashlib.sha256(saved.read_bytes()).hexdigest()
        (root/"portable.txt").write_text("portable")
        identifier=uuid.uuid4().hex;job=data/"updates"/("handoff-"+identifier);job.mkdir(parents=True)
        for source,name in ((args.manifest,"manifest.json"),(args.signature,"manifest.sig"),(args.helper,"DKR-R-Updater")):
            shutil.copy2(source,job/name)
        (job/"DKR-R-Updater").chmod(0o755)
        stage=root/(".dkr-update-stage-"+identifier);shutil.copy2(args.package,stage);stage.chmod(0o755)
        request=job/"request.json";request.write_text(json.dumps({"schema":1,"id":identifier,"kind":"appimage","platform":"linux-x86_64","parent":0,
            "target":str(target),"stage":str(stage),"data":str(data),"executable":str(target),"arguments":["--self-test-pak",str(Path(directory)/"pak-tests")]}))
        environment=os.environ.copy();environment["APPIMAGE_EXTRACT_AND_RUN"]="1"
        subprocess.run([sys.executable,__file__,"--parent",str(request)],env=environment,check=True)
        end=time.monotonic()+90
        while time.monotonic()<end and not (job/"result.json").is_file():time.sleep(0.1)
        result=json.loads((job/"result.json").read_text())
        if result["state"]!="complete":raise RuntimeError(result)
        assert hashlib.sha256(saved.read_bytes()).hexdigest()==old_save
        assert (root/"portable.txt").read_text()=="portable"
        assert target.stat().st_mode&0o111
        assert (root/(".dkr-update-backup-"+identifier)).is_file()
        print("PASS: complete AppImage replacement, copied helper, parent retirement, actual runtime self-test, executable mode, Unicode paths and portable/offline-save preservation")
    return 0
if __name__=="__main__":sys.exit(main())
