"""Execute only synthetic retention and party-runtime regression fixtures."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
AUTH=Path(__file__).resolve().parent
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('retention','race-before','race-after','output'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args()
 if a.retention.stem!='DKROfflineRetentionChecks' or a.output.exists():raise ValueError('Need the synthetic retention binary and a fresh evidence folder')
 a.output.mkdir(parents=True);binary=a.retention.resolve()
 r=subprocess.run([str(binary),str(a.output.resolve()/'fixture')],cwd=a.output,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
  timeout=30,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
 (a.output/'retention.log').write_bytes(r.stdout)
 passed=re.search(rb'(\d+) offline retention checks passed',r.stdout)
 if r.returncode!=0 or not passed:raise ValueError('Retention regression failed; inspect retention.log')
 receipt={'exit_code':r.returncode,'passed_checks':int(passed[1]),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
  'test_source_sha256':hashlib.sha256((AUTH/'tests/retention_tests.cpp').read_bytes()).hexdigest(),
  'synthetic_files_only':True,'game_or_rom_or_rsp_executed':False}
 (a.output/'RETENTION-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
 subprocess.run([sys.executable,'-B',str(AUTH.parent/'race-release-repair/run_checks.py'),
  '--before',str(a.race_before),'--after',str(a.race_after),'--output',str(a.output/'race')],check=True)
 print('Offline retention:',receipt['passed_checks'],'checks passed; race-start regressions retained.')
