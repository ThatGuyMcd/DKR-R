"""Compose Mod Store into a fresh Patch Pipeline payload; never edit inputs."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import shutil
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
AUTH = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Mod Store input anchor changed: ' + old[:120])
    return text.replace(old, new)


def transform_ui(text):
    text = once(text, '#include "runtime_ui.hpp"', '#include "runtime_ui.hpp"\n#include "mod_store.hpp"\n#include "mod_store_install.hpp"')
    text = once(text, '#include "runtime_code_mods_ui.inl"', '#include "runtime_code_mods_ui.inl"\n#include "mod_store_ui.inl"')
    text = once(text, 'constexpr std::array<const char*, 4> labels{{\n            "My mods##section", "Magic Codes##section", "Track Lab##section",\n            "Help & imports##section"}};',
        'constexpr std::array<const char*, 5> labels{{\n            "My mods##section", "Magic Codes##section", "Track Lab##section",\n            "Help & imports##section", "Mod Store##section"}};')
    text = once(text, 'for (int index = 0; index < 4; ++index) {\n            const float tab = PaddockSectionTabWidth(labels[index]);',
        'for (int index = 0; index < 5; ++index) {\n            const float tab = PaddockSectionTabWidth(labels[index]);')
    text = once(text, '        if (g_mods_page.section == kModsSectionLibrary) {\n            DrawModsLibrarySection(width, mods, mods_locked, dkr_tracks);',
        '        if (game_running && g_mods_page.section == 4) g_mods_page.section = kModsSectionLibrary;\n        if (g_mods_page.section == 4) {\n            DrawModStore(width, mods_locked);\n        } else if (g_mods_page.section == kModsSectionLibrary) {\n            DrawModsLibrarySection(width, mods, mods_locked, dkr_tracks);')
    text = once(text, '    LoadSettings();', '    LoadSettings();\n    dkr::store::installations().configure(g_config_directory, DKR_RELEASE_VERSION, mod_worker);\n    dkr::store::service().configure(g_config_directory, DKR_RELEASE_VERSION);')
    text = once(text, '    SDL_DestroyRenderer(renderer);', '    dkr::store::service().gameplay(true);\n    dkr::store::installations().safe_to_write(false);\n    ReleaseModStoreTextures();\n    SDL_DestroyRenderer(renderer);')
    text = once(text, '    StartupResult result{};\n    dkr::runtime::startup::prepare_launch',
        '    StartupResult result{};\n    dkr::runtime::startup::prepare_launch')
    # The store never renders through RT64: its textures belong to the launcher
    # SDL renderer, are released before handoff, and are recreated on return.
    text = once(text, '    ImGui_ImplSDLRenderer2_Init(renderer);', '    ImGui_ImplSDLRenderer2_Init(renderer);\n    dkr::store::service().gameplay(false);\n    g_mod_store_renderer = renderer;')
    text = once(text, '        PumpDialogJob();\n        DrawTexturePackImportModal();\n        DrawPaddockLegacyModImportModal();\n        DrawModLaunchModal();',
        '        PumpDialogJob();\n        PumpModStoreLauncher();\n        DrawTexturePackImportModal();\n        DrawPaddockLegacyModImportModal();\n        DrawModLaunchModal();')
    text = once(text, '            const float tab = PaddockSectionTabWidth(labels[index]);',
        '            if(index==4&&game_running)continue;\n            const float tab = PaddockSectionTabWidth(labels[index]);')
    text = once(text, '               !g_legacy_imports.snapshot().busy && !g_code_library.view()->busy && !g_mod_launch.snapshot().modal) {',
        '               !dkr::store::installations().view()->busy && dkr::store::installations().view()->ready &&\n               !g_legacy_imports.snapshot().busy && !g_code_library.view()->busy && !g_mod_launch.snapshot().modal) {')
    text = once(text, '    if (g_texture_import_state.running) return false;',
        '    if (g_texture_import_state.running || dkr::store::installations().view()->busy) return false;')
    text = once(text, '    if (g_legacy_imports.snapshot().busy) return;',
        '    if (g_legacy_imports.snapshot().busy || dkr::store::installations().view()->busy) return;')
    text = once(text, 'void DrawSharedTexturePackModal() {',
        'void DrawSharedTexturePackModal() {\n    if (dkr::store::installations().view()->busy) return;')
    text = once(text, '        if (g_online_compatibility_sync_requested && !compatibility_scan.valid()) {',
        '        if (!dkr::store::installations().view()->busy && dkr::store::installations().view()->ready && g_online_compatibility_sync_requested && !compatibility_scan.valid()) {')
    text = once(text, '        if (compatibility_scan.valid() &&',
        '        if (!dkr::store::installations().view()->busy && dkr::store::installations().view()->ready && compatibility_scan.valid() &&')
    # Repair labels only in copied UI cards. Never change an artifact name,
    # review hash, selected mod identity or online compatibility contract.
    text = once(text, 'void RefreshModBrowserCards(const dkr::mods::ModLibraryView& mods) {',
        'void RefreshModBrowserCards(const dkr::mods::ModLibraryView& mods) {\n    static std::uint64_t names_generation = 0;\n    const auto current_names = dkr::store::library_names_generation();\n    const bool names_changed = names_generation != current_names;\n    names_generation = current_names;')
    text = once(text, '        if (browser.snapshot != snapshot) {',
        '        if (browser.snapshot != snapshot || names_changed) {')
    text = once(text, '                : std::vector<dkr::mods::browser::Card>{};\n            browser.filter_key.clear();',
        '                : std::vector<dkr::mods::browser::Card>{};\n            for (auto& card : browser.all) {\n                const auto category = characters ? "characters" : "tracks";\n                card.item.name = dkr::store::library_display_name(card.item.patch, category, card.item.name);\n                card.item.source_name = dkr::store::library_display_name(card.item.patch, category, card.item.source_name);\n                std::set<std::string> sources;\n                for (const auto& source : card.sources) sources.insert(dkr::store::library_display_name(card.item.patch, category, source));\n                card.sources = std::move(sources);\n            }\n            browser.filter_key.clear();')
    return text


def transform_track_archives(text):
    start = text.index('bool read_zip_entry(')
    end = text.index('// The textureDigest stamped', start)
    text = text[:start] + '#include "store_track_zip.inl"\n\nbool read_zip_entry(const std::filesystem::path& path, const char* name,\n                    std::vector<std::uint8_t>& bytes) {\n    return StoreReadTrackZipEntry(path, name, bytes);\n}\n\n' + text[end:]
    start = text.index('bool extract_zip(')
    end = text.index('// In an unpacked zip under', start)
    text = text[:start] + 'bool extract_zip(const std::filesystem::path& path, const std::filesystem::path& destination,\n                 std::string& error, std::stop_token stop = {}) {\n    return StoreExtractTrackZip(path, destination, error, stop);\n}\n\n' + text[end:]
    start = text.index('// In an unpacked zip under')
    end = text.index('// The level header a track ships', start)
    text = text[:start] + '#include "store_track_locator.inl"\n\n' + text[end:]
    text = once(text, 'if (!locate_unpacked_track(temp_root, track_dir, wrapped_hd_pack)) {',
        'if (!locate_unpacked_track(temp_root, track_dir, wrapped_hd_pack, &extract_error)) {')
    text = once(text, 'error = "That .zip does not contain a .dkrmap track.";',
        'error = extract_error.empty() ? "That .zip does not contain a .dkrmap track." : extract_error;')
    return text


def authors():
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in AUTH.rglob('*')
            if p.is_file() and '__pycache__' not in p.parts}


def polished_store_ui(text):
    # Replace presentation sections only. Keep the accepted service, installer
    # entry points and both existing library-browser implementations.
    for start, end, include in (
        ('void StoreReviewForm(', 'void StoreBrowser(', 'store_player_details.inl'),
        ('void StoreDownloads(', 'void StoreLoadDraft(', 'store_player_activity.inl'),
        ('void StoreUploads(', 'void StoreAccount(', 'store_staff_pages.inl'),
        ('void StoreAccount(', 'void DrawModStore(', 'store_account_page.inl'),
    ):
        a = text.index(start)
        b = text.index(end, a)
        text = text[:a] + '#include "' + include + '"\n' + text[b:]
    a = text.index('void DrawModStore(')
    b = text.rindex('} // namespace')
    return text[:a] + '#include "store_page_shell.inl"\n' + text[b:]


def stage(baseline, worker, output, frontend=None, backfill=None):
    baseline = baseline.resolve(strict=True)
    worker = worker.resolve(strict=True)
    receipt = json.loads((baseline / 'sdk2-receipt.json').read_text())
    for relative, expected in receipt['files'].items():
        path = baseline / relative
        if path.is_symlink() or digest(path) != expected:
            raise ValueError('Accepted SDK baseline changed: ' + relative)
    if output.exists():
        raise ValueError('Use a NEW stage directory')
    if not output.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Stage must remain inside build/')
    output.mkdir(parents=True)
    game = output / 'native/src/game'
    game.mkdir(parents=True)
    original = baseline / 'native/src/game/runtime_ui.cpp'
    (game / 'runtime_ui.cpp').write_text(transform_ui(original.read_text()), newline='\n')
    for source in (AUTH / 'native').iterdir():
        if source.is_file():
            if source.name == 'mod_store_ui.inl':
                (game / source.name).write_text(polished_store_ui(source.read_text()), newline='\n')
            elif source.name == 'mod_store_management_ui.inl':
                text = source.read_text()
                text = text[:text.index('void StoreInstalledPage(')] + '#include "store_player_library.inl"\n'
                (game / source.name).write_text(text, newline='\n')
            else:
                shutil.copy2(source, game / source.name)
    spec = importlib.util.spec_from_file_location('dkr_store_texture_names', AUTH / 'texture_names_pipeline.py')
    names_pipeline = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(names_pipeline)
    for name, transform in (
        ('runtime_texture_packs.cpp', names_pipeline.transform_texture),
        ('runtime_rice_texture_import.cpp', names_pipeline.transform_rice),
    ):
        result = transform((baseline / 'native/src/game' / name).read_text(), once)
        if name == 'runtime_texture_packs.cpp':
            result = '#include "mod_store_install.hpp"\n' + result + '\n#include "store_texture_adapter.inl"\n'
        (game / name).write_text(result, newline='\n')
    track = transform_track_archives((baseline / 'native/src/game/custom_tracks.cpp').read_text())
    (game / 'custom_tracks.cpp').write_text('#include "mod_store_install.hpp"\n' + track + '\n#include "store_track_adapter.inl"\n', newline='\n')
    online = (baseline / 'native/src/game/runtime_online_mods_ui.inl').read_text()
    online = once(online, '    return g_online_mod_host_request.has_value();',
        '    return g_online_mod_host_request.has_value() || dkr::store::installations().view()->busy || !dkr::store::installations().view()->ready;')
    (game / 'runtime_online_mods_ui.inl').write_text(online, newline='\n')
    online_page = (baseline / 'native/src/game/runtime_online_ui.inl').read_text()
    online_page = once(online_page, 'void DrawOnlinePage(float available_width, bool launcher, bool rom_ready) {',
        'void DrawOnlinePage(float available_width, bool launcher, bool rom_ready) {\n    if (!dkr::runtime::netplay::session().active() && (dkr::store::installations().view()->busy || !dkr::store::installations().view()->ready)) {\n        ImGui::TextWrapped("Finish the Mod Store operation before starting or joining an online session.");\n        return;\n    }')
    (game / 'runtime_online_ui.inl').write_text(online_page, newline='\n')
    details = (baseline / 'native/src/game/runtime_details_ui.inl').read_text()
    details = once(details, '        std::uint64_t generation = 0U;',
        '        std::uint64_t generation = 0U, names_generation = 0U;')
    details = once(details, '    if (cache.generation != generation) {',
        '    const auto names_generation = dkr::store::library_names_generation();\n    if (cache.generation != generation || cache.names_generation != names_generation) {\n        cache.names_generation = names_generation;')
    details = once(details, '    const bool busy = TexturePackImportRunning() || DialogJobRunning();',
        '    const bool busy = TexturePackImportRunning() || DialogJobRunning() || dkr::store::installations().view()->busy;')
    details = once(details, '                                  pack.hidden || !pack.compatible)) {',
        '                                  busy || pack.hidden || !pack.compatible)) {')
    details = once(details, '                if (DetailRaceButton("MANAGE...", column - 32.0F)) {',
        '                if (DetailRaceButton("MANAGE...", column - 32.0F, 0x0A6EA1, busy)) {')
    (game / 'runtime_details_ui.inl').write_text(details, newline='\n')
    codes = (baseline / 'native/src/game/runtime_code_mods_ui.inl').read_text()
    if codes.count('item.name') != 3:
        raise ValueError('Code-mod presentation label anchors changed')
    codes = codes.replace('item.name', 'dkr::store::library_display_name(item.digest, "magic", item.name)')
    (game / 'runtime_code_mods_ui.inl').write_text(codes, newline='\n')
    backend = output / 'backend'
    backend.mkdir()
    shutil.copy2(worker, backend / 'worker-baseline.mjs')
    # The supplied deployed bundle is authoritative. No extracted FTPS module,
    # secret, existing account or earlier backend release is overwritten.
    source = worker.read_text(encoding='utf-8-sig')
    source = once(source, '      releases: versions, version: current.version || \'\', fileName: current.fileName || \'\', fileUrl: current.fileUrl || \'\',',
        '      currentReleaseId: row.current_release_id || null, releases: versions, version: current.version || \'\', fileName: current.fileName || \'\', fileUrl: current.fileUrl || \'\',')
    source = once(source, 'function completeAtFrontend(frontend, clientState, values) {',
        'async function completeAtFrontend(frontend, clientState, values, env) {\n  if (clientState.startsWith("dkrnative_")) return launcherComplete(frontend, clientState, values, env);')
    source = source.replace('completeAtFrontend(config.frontend, flow.client_state, { error })',
                            'completeAtFrontend(config.frontend, flow.client_state, { error }, env)')
    source = once(source, 'completeAtFrontend(config.frontend, flow.client_state, { grant })',
                            'completeAtFrontend(config.frontend, flow.client_state, { grant }, env)')
    auth_extension = (AUTH / 'backend/launcher-auth.inl').read_text()
    source = once(source, 'async function startDiscord(request, env) {', auth_extension + '\nasync function startDiscord(request, env) {')
    source = once(source, "    '/auth/discord/start': ['GET', startDiscord],", "    '/auth/launcher/start': ['POST', launcherStart],\n    '/auth/launcher/authorize': ['GET', launcherAuthorize],\n    '/auth/launcher/refresh': ['POST', launcherRefresh],\n    '/auth/launcher/logout': ['POST', launcherLogout],\n    '/auth/launcher/poll': ['POST', launcherPoll],\n    '/auth/launcher/cancel': ['POST', launcherCancel],\n    '/auth/discord/start': ['GET', startDiscord],")
    # No cross-origin privilege bypass: native requests use the same configured
    # origin plus real opaque bearer sessions and the existing role checks.
    extension = (AUTH / 'backend/launcher-service.inl').read_text()
    backfill_input = None
    if backfill:
        records = json.loads(backfill.read_text(encoding='utf-8'))
        if records.get('schema') != 1 or len(records.get('records', [])) > 80:
            raise ValueError('Invalid backfill records')
        import re
        for row in records['records']:
            if not re.fullmatch(r'release_[A-Za-z0-9_-]{1,100}', row['id']) or not re.fullmatch(r'[a-f0-9]{64}', row['sha256']) or not 0 < row['bytes'] <= 2 * 1024**3 or not row['url'].startswith('https://positron-portal.com/dkr-r/downloads/'):
                raise ValueError('Unsafe backfill record')
        extension = 'const launcherPinnedBackfill=' + json.dumps(records['records'], separators=(',', ':')) + ';\n' + extension
        backfill_input = {str(backfill.resolve(strict=True)): digest(backfill)}
        shutil.copy2(backfill, backend / 'BACKFILL-REPORT.json')
        report = ['Existing catalogue download preparation', '',
            str(len(records['records'])) + ' valid packages have pinned checksums in this Worker.',
            str(len(records.get('failures', []))) + ' addresses could not be prepared.',
            'Unavailable entries are not marked verified or silently bypassed.', '']
        for failure in records.get('failures', []):
            report.extend([failure['title'], failure['url'], failure['error'], ''])
        (backend / 'DOWNLOAD-HOSTING-REPORT.txt').write_text('\n'.join(report), encoding='utf-8')
    else:
        extension = 'const launcherPinnedBackfill=[];\n' + extension
    source = once(source, 'export default {\n  async fetch(request, env, ctx)', extension + '\nexport default {\n  async fetch(request, env, ctx)')
    source = once(source, 'response = await handleAuth(request, env, ctx) ||',
                            'await launcherStorage(env);\n        response = await handleLauncher(request, env) || await __dkrUploads.launcherUploadIntegrity(request, env) || await handleAuth(request, env, ctx) ||')
    source = once(source, 'async function handleUploads(request, env, ctx, options = {}) {',
        (AUTH / 'backend/upload-integrity.inl').read_text() + '\nasync function handleUploads(request, env, ctx, options = {}) {')
    source = once(source, 'Object.freeze({UPLOADS_REVISION,UPLOAD_CHUNK_BYTES,handleUploads,listUploadedAssets,cleanupUploads})',
        'Object.freeze({UPLOADS_REVISION,UPLOAD_CHUNK_BYTES,handleUploads,listUploadedAssets,cleanupUploads,launcherUploadIntegrity,requirePreparedUpload})')
    source = once(source, '  const draft = cleanDraft(input.draft || parse(source.content_json, {}), env, true);',
        '  const draft = cleanDraft(input.draft || parse(source.content_json, {}), env, true);\n  await __dkrUploads.requirePreparedUpload(db,env,draft.fileUrl);')
    publication = "  queries.push(statement(db, `UPDATE library_items SET current_release_id=? WHERE id=? AND ${claimGate}`, [releaseId, listingId, claim]));"
    source = once(source, publication, "  queries.push(statement(db, `INSERT INTO launcher_release_integrity(release_id,sha256,bytes,verified_at) SELECT ?,a.sha256,a.bytes,? FROM launcher_asset_integrity a WHERE a.file_url=? AND ${claimGate} ON CONFLICT(release_id) DO NOTHING`, [releaseId,stamp,draft.fileUrl,claim]));\n" + publication)
    source = once(source,'    healthRevision:',"    launcherRevision: '20261018-beta8',\n    healthRevision:")
    source = once(source, "  if (name.startsWith('admin.') || name.startsWith('catalogue.')) requireRole(user, 'admin');",
        "  if (name.startsWith('admin.') || name === 'catalogue.archive') requireRole(user, 'admin');\n  if (name.startsWith('catalogue.') && name !== 'catalogue.archive') requireRole(user, 'moderator');")
    # Publish full-file checksums to both clients. Older rows remain explicitly
    # unverified until the owner registers their real file digest.
    source = once(source, "  const releases = rowsOf(batch[0]), ratings = rowsOf(batch[1]);",
        "  const releases = rowsOf(batch[0]), ratings = rowsOf(batch[1]);\n  const integrity = await all(db, `SELECT v.* FROM launcher_release_integrity v JOIN library_releases r ON r.id=v.release_id WHERE r.item_id IN (${slots})`, ids);")
    source = once(source, 'requirements: release.requirements, install: release.install, createdAt: iso(release.created_at) }));',
        'requirements: release.requirements, install: release.install, createdAt: iso(release.created_at),\n      ...((verified) => verified ? {sha256:verified.sha256,size:verified.bytes} : {})(integrity.find(value => value.release_id === release.id)) }));')
    (backend / 'worker-single-file.mjs').write_text(source, encoding='utf-8', newline='\n')
    for name in ('launcher-schema.sql', 'web.config', 'OWNER-SETUP.txt', 'release_manifest.py', 'backfill_library.py'):
        shutil.copy2(AUTH / 'backend' / name, backend / name)
    frontend_inputs={}
    if frontend:
        spec=importlib.util.spec_from_file_location('dkr_mod_store_frontend',AUTH/'frontend_pipeline.py')
        transforms=importlib.util.module_from_spec(spec);spec.loader.exec_module(transforms)
        for name in ('app.js','api-store.js','uploads-client.js'):
            path=frontend/'assets'/name
            frontend_inputs[str(path.resolve(strict=True))]=digest(path)
            destination=output/'FTP-PATCH/assets'/name;destination.parent.mkdir(parents=True,exist_ok=True)
            destination.write_text(transforms.transform(name,path.read_text(encoding='utf-8')),encoding='utf-8',newline='\n')
    files = {p.relative_to(output).as_posix(): digest(p) for p in output.rglob('*') if p.is_file()}
    (output / 'mod-store-receipt.json').write_text(json.dumps({
        'schema': 1, 'baseline': str(baseline), 'baseline_receipt': digest(baseline / 'sdk2-receipt.json'),
        'worker_sha256': digest(worker), 'frontend_inputs':frontend_inputs,'backfill_input':backfill_input,'authored': authors(), 'files': files
    }, indent=2) + '\n')
    verify(output)


def verify(output):
    receipt = json.loads((output / 'mod-store-receipt.json').read_text())
    if receipt['schema'] != 1 or receipt['authored'] != authors():
        raise ValueError('Mod Store authoring changed; compose a fresh stage')
    baseline = Path(receipt['baseline'])
    # Windows-composed receipts remain verifiable from WSL without modifying
    # the immutable stage or the receipt's recorded provenance.
    if not baseline.exists() and len(receipt['baseline']) > 3 and receipt['baseline'][1:3] in (':/', ':\\'):
        baseline = Path('/mnt') / receipt['baseline'][0].lower() / receipt['baseline'][3:].replace('\\', '/')
    if digest(baseline / 'sdk2-receipt.json') != receipt['baseline_receipt']:
        raise ValueError('SDK baseline receipt changed')
    actual = {p.relative_to(output).as_posix(): digest(p) for p in output.rglob('*')
              if p.is_file() and p.name != 'mod-store-receipt.json'}
    if actual != receipt['files'] or any(p.is_symlink() for p in output.rglob('*')):
        raise ValueError('Mod Store payload changed after composition')
    print('Mod Store authored payload and immutable baseline inspected; no application execution.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('stage', 'verify'))
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--worker', type=Path)
    parser.add_argument('--frontend',type=Path)
    parser.add_argument('--backfill',type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.mode == 'stage':
        stage(args.baseline, args.worker, args.output,args.frontend,args.backfill)
    else:
        verify(args.output.resolve(strict=True))
