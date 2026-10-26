"""Configure the additive online performance playtest through the Patch Pipeline."""
from pathlib import Path
import argparse,importlib.util,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','capsule','log'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cmake',default='cmake');a=p.parse_args()
    s=importlib.util.spec_from_file_location('performance_configure',ROOT/'patches/release-prep/v1.0.7/release.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
    m.AUTH=AUTH;m.VERSION='1.0.11-online-playtest.1';m.configure(a.build,a.log,a.cmake,a.capsule)
