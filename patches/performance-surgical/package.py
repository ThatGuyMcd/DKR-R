"""Package the surgical qualification layer over accepted beta 10 assets."""
from pathlib import Path
import argparse
import importlib.util
import json
import shutil
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]


def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


def main():
    parser=argparse.ArgumentParser(add_help=False)
    parser.add_argument('--performance-stage',type=Path,required=True)
    args,remaining=parser.parse_known_args()
    pipeline=load('surgical_performance_pipeline',AUTH/'pipeline.py');pipeline.verify(args.performance_stage)
    presentation=load('surgical_presentation_package',ROOT/'patches/presentation-polish/package.py')
    original_load=presentation.load
    def presentation_load(name,path):
        module=original_load(name,path)
        if name=='dkr_title_package':
            title_load=module.load
            def nested(name,path):
                package=title_load(name,path)
                if name=='dkr_beta10_package':
                    prior_features=package.inspect_features
                    def features(data):
                        prior_features(data)
                        for marker in (b'[perf][surgical-features]',b'monotonic-pacer=',b'[perf][surgical-present]',b'p99-upper-us='):
                            if marker not in data:raise ValueError('Missing surgical performance feature: '+repr(marker))
                    package.inspect_features=features
                    prior_notes=package.notes
                    def notes(root,platform):
                        prior_notes(root,platform)
                        checks=(AUTH/'OWNER-CHECKS.txt').read_text()
                        (root/'SURGICAL-PERFORMANCE.txt').write_text(checks)
                        if platform.startswith('Windows'):
                            shutil.copy2(AUTH/'Record-Performance.cmd',root/'Record-Performance.cmd')
                        with (root/'RELEASE-NOTES.txt').open('a') as stream:
                            stream.write('\nSURGICAL PERFORMANCE QUALIFICATION\nSee SURGICAL-PERFORMANCE.txt for included changes, A/B switches and gated follow-up.\nNo gameplay tests or benchmarks were run. Version remains beta 10.\n')
                    package.notes=notes
                    # Validate build IDs BEFORE assembling an output tree.
                    package.build_ids(Path(remaining[remaining.index('--build-config')+1]))
                return package
            module.load=nested
        return module
    presentation.load=presentation_load
    sys.argv=[sys.argv[0]]+remaining;presentation.main()
    path=Path(remaining[remaining.index('--output')+1])/'BUILD-RECEIPT.json'
    receipt=json.loads(path.read_text())
    receipt['surgical_performance']=json.loads((args.performance_stage/'performance-receipt.json').read_text())
    receipt['performance_packager_sha256']=pipeline.sha(Path(__file__))
    receipt['performance_overlay_sha256']=pipeline.sha(AUTH/'NativeOverlay.cmake')
    audit=args.performance_stage.parent/'STATIC-SAFETY.json'
    if not audit.is_file():raise ValueError('Static source inspection report is required')
    receipt['performance_static_source_inspection']=json.loads(audit.read_text())
    receipt['performance_audit_sha256']=pipeline.sha(AUTH/'inspect_sources.py')
    receipt['acceptance']='Compilation/static package inspection only; owner gameplay and comparative performance captures pending.'
    path.write_text(json.dumps(receipt,indent=2)+'\n')


if __name__=='__main__':main()
