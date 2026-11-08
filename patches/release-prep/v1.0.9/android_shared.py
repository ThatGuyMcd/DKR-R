"""Equivalent shared snapshot operations for Android vehicles/achievements."""
from pathlib import Path
import argparse,hashlib,json

def compose(source):
    text=source.read_text(encoding='utf-8')
    if source.name=='vehicle_binding.cpp':
        replacements={
            'std::atomic<std::shared_ptr<const Resources>> renderer;':'std::shared_ptr<const Resources> renderer;',
            'renderer.store(std::move(resources),std::memory_order_release);':'std::atomic_store_explicit(&renderer,std::move(resources),std::memory_order_release);',
            'return renderer.load(std::memory_order_acquire);':'return std::atomic_load_explicit(&renderer,std::memory_order_acquire);'}
    elif source.name=='achievements.cpp':
        replacements={
            'std::atomic<std::shared_ptr<const View>> published_{std::make_shared<View>()};':'std::shared_ptr<const View> published_{std::make_shared<View>()};',
            'published_.store(std::make_shared<const View>(state_));':'std::atomic_store(&published_,std::make_shared<const View>(state_));',
            'return published_.load();':'return std::atomic_load(&published_);'}
    else:raise ValueError('Unexpected Android snapshot source')
    for before,after in replacements.items():
        if text.count(before)!=1:raise ValueError('Android shared snapshot anchor changed: '+before)
        text=text.replace(before,after,1)
    return text.encode('utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('vehicles','achievements','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();ledger=a.output/'swaps.json';rows=json.loads(ledger.read_text())
    if len(rows)!=2:raise ValueError('Expected directory/HTTP adapters before other snapshots')
    for target,source in (('DKRCustomVehicles',a.vehicles),('DKRPortGame',a.achievements)):
        data=compose(source);destination=a.output/source.name
        if not destination.exists() or destination.read_bytes()!=data:destination.write_bytes(data)
        rows.append({'target':target,'path':source.as_posix(),'new':destination.as_posix(),
            'input_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(data).hexdigest()})
    ledger.write_text(json.dumps(rows,indent=2)+'\n')
    print('Android vehicle/achievement snapshots retain their original acquire/release or sequentially consistent synchronization.')
