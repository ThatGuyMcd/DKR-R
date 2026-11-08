"""Give every inventoried document a disposition; never rewrite source docs."""
from pathlib import Path
import argparse
import json
import re
import sys
from urllib.parse import unquote
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[3]
CURRENT={
 'README.md':'Rewrite the 1.0.6-to-1.0.7 feature overview, current setup, save migration, Store/SDK/Camera++/Online/presentation and honest limits.',
 'CHANGELOG.md':'Consolidate the complete release ledger; keep dated beta/contributor history rather than copying it into packages.',
 'RELEASE-VALIDATION.md':'Record final source/asset identities and distinguish static/compiler, historical contributor and owner/device results.',
 'docs/README.md':'Update guide navigation for Store, shared saves, SDK 2/Bible and Camera++; retain a clearly historical index.',
 'docs/SAVES.md':'Replace obsolete mod-set personal-save swapping with shared personal, explicit recovery, online/development isolation and custom records.',
 'docs/GRAPHICS.md':'Document accepted flow/HD-water/credits fixes and cached glow; reconcile Camera++ and disputed online model-detail scope.',
 'docs/HUD-WORKSHOP.md':'Keep only actual supported HUD layout/online view guidance; do not advertise the disabled workshop.',
 'docs/LAUNCHER.md':'Use current Store, Save Manager, support, update and controller/keyboard page labels.',
 'docs/ONLINE_MULTIPLAYER.md':'Current admission/resource proofs, code mods, per-device view, host continuation and exact platform/backend limits.',
 'docs/MAGIC_CODES.md':'Distinguish stock codes, Custom Magic Codes API 1/2, approvals, settings, samples and development-lobby authority.',
 'docs/LEGACY-MODS-BETA.md':'Current manual/Store import support, track toggles, portraits, package layouts and adapter limits.',
 'docs/CUSTOM_TRACKS.md':'Current native track/music/HD package layout, records and shared cup dependency rules.',
 'docs/TEXTURE_PACKS.md':'Managed names/status/updates, priority, original/enhanced optics and complete/incomplete HD liquid families.',
 'docs/UPDATES.md':'Stable version ordering, manual +beta transition, signatures/ownership/recovery and original APK signer requirements.',
 'docs/TROUBLESHOOTING.md':'Real log history/export, save/development scopes, Store repair and useful failure details; no profile-delete advice.',
 'docs/BUILDING.md':'A complete clean-clone release Pipeline recipe with explicit inputs, both revisions and platform packaging.',
 'docs/DEVELOPMENT.md':'Current contributor workflow, owner-run execution policy and reproducible stage/receipt contract.',
 'docs/ARCHITECTURE.md':'Current runtime/store/save/SDK and renderer ownership without a second source of contradictory setup advice.',
 'docs/F3DDKR.md':'Actual native graphics HLE boundaries and staged source overrides, not guest simulation claims.',
 'docs/RENDER_SNAPSHOT_ARCHITECTURE.md':'Retain frozen ownership/lifetimes and describe current isolated presentation composition.',
 'docs/PLATFORMS.md':'Actual released artifacts, binary-derived requirements, signer/version codes and device acceptance limits.',
 'docs/PUBLIC-1.0.7-NOTES.txt':'Full grouped release notes from the same source-backed feature ledger as README/CHANGELOG.',
 'packaging/RELEASE-README.md':'One concise platform-specific player README; no beta-5, mod-save swapping or stale camera statements.',
 'runtime-recomp/README.md':'Current integration/SDK/save ownership and top-level clean release recipe; preserve protected boundaries.',
 'scripts/README.md':'Single release entrypoint, explicit input/output contract, SDK/Bible generation and allowlisted packaging.',
 'modding/USER-GUIDE.txt':'Current import, permissions, cups, AI/Dev Menu, Store, online and sandbox behaviour.',
 'modding/SDK.txt':'Publish exact SDK 2 headers/modules/authoring tools while preserving API 1 compatibility.',
 'modding/MODDERS-GUIDE.txt':'Replace the API-1-only public book with the reviewed SDK 2 Bible and generate matching TXT/HTML/PDF.',
 'modding/IMPLEMENTATION-STATUS.txt':'Separate implemented supported APIs from gated/unsupported work; no unrestricted execution/VR claims.',
 'patches/sdk2/SDK-README.txt':'Current SDK 2 release setup, exact app baseline, API 1 compatibility and safe example choices.'}
TECH={'docs/ANDROID-CI.md','docs/IOS-CI.md','docs/NIGHTLY-CHECKS.md','docs/REGRESSION.md','docs/LEVEL_MODEL_FORMAT.md','docs/LEVEL_OBJECT_MAP_FORMAT.md','docs/CONTROLLERS.md','docs/ROM_SETUP.md','docs/SOUND.md','tools/blender/README.md','tools/blender/CHANGELOG.md','runtime-recomp/tests/legacy_mods/README.md','packaging/MACOS-BUILD-README.md'}
LEGAL=re.compile(r'license|licence|copying|copyright|notice|third.party',re.I)
def disposition(doc):
    path=doc['path'];name=Path(path).name
    if name=='CMakeLists.txt':return 'KEEP_BUILD_CONTRACT','Build/fixture source, not player documentation. Verify references; do not rewrite prose or archive as clutter.'
    if path=='assets/controllers/gamecontrollerdb.txt' or path=='.github/nightly-tools.txt' or path.startswith('tools/blender/tests/fixtures/'):
        return 'KEEP_DATA','Versioned runtime/tool/test data, not an obsolete instruction sheet.'
    if LEGAL.search(name) or path in {'assets/filters/README.md','assets/controllers/README.md','docs/ASSET_POLICY.md'}:
        return 'KEEP_PROVENANCE','Preserve legal/asset provenance and attribution. Verify hashes/paths; no owner-voice rewrite of third-party terms.'
    if path=='RELATORIO_IA_BOTS.md':return 'KEEP_CONTRIBUTOR','Preserve the contributor report in its original language and attribution; exclude from the game ZIP.'
    if path in CURRENT:return 'UPDATE_CURRENT',CURRENT[path]
    if path in TECH:return 'VERIFY_TECHNICAL','Check commands, platform/version scope and final recipe against the frozen release. Update affected instructions only; retain contributor evidence.'
    if path=='docs/MODDING_ARCHITECTURE.md':return 'MARK_HISTORICAL','Keep the original proposal; point to the implemented SDK 2 contract instead of presenting this as the current SDK.'
    if path.startswith('patches/'):
        if name in {'PLAYTEST-README.txt','STARTUP-FIX-README.txt'}:return 'KEEP_INTERNAL_HISTORY','Keep as dated implementation/startup evidence; stop shipping as another current player README.'
        return 'VERIFY_PIPELINE','Verify authored stage inputs/order/limits and connect the final clean release recipe. Keep technical evidence out of runtime packages.'
    if path.startswith('docs/'):
        return 'KEEP_HISTORY','Retain dated/proposal/previous-release evidence and its original results. Label historical in the index; exclude from current player packages.'
    return 'VERIFY_SOURCE_REFERENCE','Retain supported SDK/example/developer documentation; update only stale commands/labels and preserve attribution.'
def links(path):
    text=path.read_text(encoding='utf-8-sig',errors='replace');text=re.sub(r'```.*?```','',text,flags=re.S)
    bad=[]
    for value in re.findall(r'\]\(([^)]+)\)',text):
        target=value.strip().split(' "',1)[0].strip('<>')
        if not target or target.startswith(('#','http:','https:','mailto:','data:')):continue
        filename=unquote(target.split('#',1)[0])
        if filename and not (path.parent/filename).exists():bad.append(target)
    return sorted(set(bad))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inventory',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--windows',type=Path)
    a=p.parse_args()
    if a.output.exists() or not a.output.resolve().is_relative_to(ROOT/'build'):raise ValueError('Choose a NEW generated documentation report under build')
    a.output.mkdir(parents=True)
    rows=[];lines=['DOCUMENT-BY-DOCUMENT RELEASE PLAN','20 October 2026','No source document was changed by this inventory action.','']
    for doc in sorted(json.loads(a.inventory.read_text()),key=lambda x:x['path']):
        action,reason=disposition(doc);row=dict(doc,action=action,reason=reason)
        if action=='UPDATE_CURRENT' and Path(doc['path']).suffix=='.md':row['missing_relative_targets']=links(ROOT/doc['path'])
        rows.append(row);lines += [doc['path'],'  '+action+': '+reason]
        if row.get('missing_relative_targets'):lines+=['  Missing relative targets: '+', '.join(row['missing_relative_targets'])]
    (a.output/'DOCUMENTATION-PLAN.json').write_text(json.dumps(rows,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    (a.output/'DOCUMENTATION-PLAN.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    package=[]
    if a.windows:
        for path in sorted(a.windows.rglob('*.md')):
            missing=links(path)
            if missing:package.append({'path':path.relative_to(a.windows).as_posix(),'missing_relative_targets':missing})
    (a.output/'PACKAGE-LINK-FINDINGS.json').write_text(json.dumps(package,indent=2)+'\n')
    print(json.dumps({'documents':len(rows),'current_updates':sum(r['action']=='UPDATE_CURRENT' for r in rows),'readmes':sum(r['readme'] for r in rows),'package_markdown_with_missing_targets':len(package)},indent=2))
if __name__=='__main__':main()
