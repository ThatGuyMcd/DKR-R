"""Read back delivered archives without executing the game or AppImage."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,zipfile
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--receipt',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--extract-dir',type=Path);a=p.parse_args()
    receipt=json.loads(a.receipt.read_text());artifact=a.receipt.parent/receipt['artifact']['name'];expected=receipt['files']
    if sha(artifact)!=receipt['artifact']['sha256']:raise ValueError('Archive hash changed')
    if artifact.suffix=='.zip':
        with zipfile.ZipFile(artifact) as z:
            if z.testzip():raise ValueError('ZIP CRC failed')
            prefix=artifact.stem+'/'
            actual={n[len(prefix):] for n in z.namelist() if n.startswith(prefix) and not n.endswith('/')}
            if actual!=set(expected) or any(not n.startswith(prefix) for n in z.namelist()):raise ValueError('ZIP file set differs')
            for name,record in expected.items():
                data=z.read(prefix+name)
                if len(data)!=record['bytes'] or hashlib.sha256(data).hexdigest()!=record['sha256']:raise ValueError('ZIP payload differs: '+name)
    else:
        if not a.extract_dir or a.extract_dir.exists():raise ValueError('Choose a fresh static extraction folder')
        with artifact.open('rb') as f:header=f.read(4*1024*1024)
        offsets=[m.start() for m in re.finditer(b'hsqs',header) if subprocess.run(['unsquashfs','-s','-o',str(m.start()),str(artifact)],capture_output=True).returncode==0]
        if len(offsets)!=1:raise ValueError('AppImage payload ambiguous')
        subprocess.run(['unsquashfs','-no-progress','-processors','2','-o',str(offsets[0]),'-d',str(a.extract_dir),str(artifact)],check=True)
        actual={p.relative_to(a.extract_dir).as_posix() for p in a.extract_dir.rglob('*') if p.is_file() or p.is_symlink()}
        if actual!=set(expected):raise ValueError('AppImage file set differs')
        for name,record in expected.items():
            path=a.extract_dir/name
            if 'link' in record:
                if not path.is_symlink() or path.readlink().as_posix()!=record['link']:raise ValueError('AppImage link differs: '+name)
            elif path.is_symlink() or path.stat().st_size!=record['bytes'] or sha(path)!=record['sha256']:raise ValueError('AppImage payload differs: '+name)
    a.output.write_text(json.dumps({'artifact_sha256':sha(artifact),'verified_files':len(expected),
        'exact_payload_match':True,'game_or_appimage_executed':False},indent=2)+'\n')
    print('Delivered archive read-back verified:',len(expected),'files; no game or AppImage execution.')
