"""Inventory release documentation and check the changed guides' local links."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from release import ROOT,AUTH,files
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
    changes=json.loads((a.evidence/'METADATA-CHANGES.json').read_text());inventory=[];broken=[];links=0
    for name in files():
        path=ROOT/name
        if path.suffix.lower() not in {'.md','.txt','.rst'} or path.name=='CMakeLists.txt':continue
        if name.startswith('patches/release-prep/v1.0.10/'):kind='release-pipeline-authoring'
        elif name in changes:kind='updated-current-document'
        elif re.search('LICENSE|COPYING|NOTICE|THIRD_PARTY',name,re.I):kind='licence-attribution-preserved'
        elif 'source-capsule' in name:kind='frozen-source-input'
        elif name.startswith('patches/') or '/history/' in name or re.search('/(?:PUBLIC-|BETA|PLAYTEST|DOCUMENTATION-1.0.9)',name):kind='historical-record-preserved'
        else:kind='reviewed-reference-no-release-change'
        data=path.read_bytes();text=data.decode('utf-8',errors='replace')
        inventory.append({'path':name,'disposition':kind,'sha256':hashlib.sha256(data).hexdigest()})
        if name not in changes or path.suffix!='.md' or '/history/' in name:continue
        for link in re.findall(r'(?<!!)\[[^\]\n]+\]\(([^)\s]+)(?:\s+"[^"]*")?\)',text):
            if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:',link) or link.startswith('#'):continue
            target=(path.parent/link.split('#')[0]).resolve();links+=1
            if not target.is_relative_to(ROOT) or not target.exists():broken.append({'path':name,'link':link})
    if broken:raise ValueError('Broken links: '+json.dumps(broken))
    counts={}
    for r in inventory:counts[r['disposition']]=counts.get(r['disposition'],0)+1
    (a.evidence/'DOCUMENTATION-INVENTORY.json').write_text(json.dumps(inventory,indent=2)+'\n')
    report={'version':'1.0.10','documents':len(inventory),'dispositions':counts,'local_links_checked':links,'broken_links':broken}
    (a.evidence/'DOCUMENTATION-CHECKS.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
