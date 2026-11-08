"""Compose the Android libc++ snapshot adapter in an external build only."""
from pathlib import Path
import argparse,hashlib,json

def compose(source,output):
    original=source.read_text(encoding='utf-8')
    text=original
    replacements={
        'std::atomic<std::shared_ptr<const View>> published{std::make_shared<const View>()};':
        'std::shared_ptr<const View> published{std::make_shared<const View>()};',
        'published.store(std::make_shared<const View>(ui));':
        'std::atomic_store(&published,std::make_shared<const View>(ui));',
        'return state().published.load();':
        'return std::atomic_load(&state().published);'}
    for before,after in replacements.items():
        if text.count(before)!=1:raise ValueError('Android snapshot anchor changed: '+before)
        text=text.replace(before,after,1)
    return text.encode('utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=compose(a.source,a.output);a.output.mkdir(parents=True,exist_ok=True)
    target=a.output/'online_directory.cpp'
    if not target.exists() or target.read_bytes()!=result:target.write_bytes(result)
    (a.output/'swaps.json').write_text(json.dumps([{'target':'DKRNetplayCore','path':a.source.as_posix(),'new':target.as_posix(),
        'input_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(result).hexdigest()}],indent=2)+'\n')
    print('Android directory uses the same sequentially consistent shared_ptr snapshot operations via portable libc++ free functions.')
