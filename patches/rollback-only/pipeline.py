"""Rollback-only public policy in isolated first-party build copies."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
POLICY = 'dkr::runtime::netplay::rollback_policy::'
INCLUDE = re.compile(r'^#include "([^"\n]+)"', re.M)

def sha(data): return hashlib.sha256(data).hexdigest()
def once(text, old, new):
    if text.count(old) != 1: raise ValueError('Rollback policy anchor changed: ' + old[:130])
    return text.replace(old, new, 1)
def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != data: path.write_bytes(data)

def copy_labels(text):
    # Only literal prose changes here: engine enums, macros, commands, hashes
    # and serialized values keep their existing meaning.
    changes = [
        ('Auto / Experimental Rollback', 'Rollback'),
        ('Auto/Experimental Rollback', 'Rollback'),
        ('Auto or Experimental Rollback', 'Rollback'),
        ('Auto uses Experimental Rollback.', 'Online play uses Rollback.'),
        ('Experimental Rollback', 'Rollback'),
        ('Experimental rollback', 'Rollback'),
        ('Experimental online play stopped safely', 'Online play stopped safely'),
        ('Experimental scene preparation failed', 'Online scene preparation failed'),
        ('The experimental connection closed.', 'The online connection closed.'),
        ('The experimental packet consumer', 'The Rollback packet consumer'),
        ('ONLINE EXPERIMENTAL', 'ONLINE ROLLBACK'),
    ]
    def literal(match):
        value = match[0]
        for old, new in changes: value = value.replace(old, new)
        if value == '"EXPERIMENTAL"': value = '"ROLLBACK"'
        if 'Other online modes still support tracks and characters.' in value:
            value = '"Custom Magic Codes require Rollback."'
        if value == '"Custom cars and Magic Codes need Rollback."':
            value = '"Custom cars and Magic Codes require Rollback."'
        return value
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', literal, text)
    text = re.sub(r'(?<![\w:])(?:dkr::runtime::netplay::)?synchronization_name\(', POLICY + 'name(', text)
    text = text.replace('dkr::runtime::netplay::experimental::runtime_admission_error(', POLICY + 'admission_error(')
    text = text.replace('experimental::runtime_admission_error(', POLICY + 'admission_error(')
    return text

def ui(text):
    for old, new in [
        ('int g_online_maximum_players = 2;', 'int g_online_maximum_players = '+POLICY+'maximum_players;'),
        ('bool g_online_experimental_rollback = false;', 'bool g_online_experimental_rollback = true;'),
        ('bool g_online_automatic_backend = true;', 'bool g_online_automatic_backend = false;'),
        ('int g_online_rollback_window = 10;', 'int g_online_rollback_window = '+POLICY+'prediction_window;'),
        ('bool g_online_automatic_delay = true;', 'bool g_online_automatic_delay = '+POLICY+'automatic_delay;'),
        ('int g_online_manual_delay = 2;', 'int g_online_manual_delay = '+POLICY+'input_delay;'),
        ('bool g_online_record_replay = true;', 'bool g_online_record_replay = '+POLICY+'record_replay;'),
        ('bool g_online_development=false;', 'bool g_online_development='+POLICY+'development;'),
        ('bool g_online_allow_spectators=true,g_online_join_watching=false;', 'bool g_online_allow_spectators='+POLICY+'spectators,g_online_join_watching=false;'),
        ('    dkr::runtime::netplay::Rules rules;', '    dkr::runtime::netplay::Rules rules='+POLICY+'default_rules();'),
        ('''    return dkr::runtime::netplay::experimental::resolved_host_backend(
        g_online_synchronization, g_online_experimental_rollback, g_online_automatic_backend);''',
         '    return '+POLICY+'mode;'),
        ('''    g_online_automatic_backend = dkr::runtime::netplay::experimental::migrated_automatic_backend(
        has_automatic_backend_setting, g_online_automatic_backend, has_legacy_backend_setting);''',
         '''    // Canonicalize legacy backend flags after reading the complete file.
    // Retain valid delay/player/development preferences, not the removed modes.
    const bool migrated_rollback_backend = !has_legacy_backend_setting ||
        !has_automatic_backend_setting || !has_rollback_optin_setting || g_online_synchronization != 0 ||
        !g_online_experimental_rollback || g_online_automatic_backend;
    g_online_synchronization = 0;
    g_online_experimental_rollback = true;
    g_online_automatic_backend = false;
    if (migrated_rollback_backend) SaveSettings();'''),
    ]: text = once(text, old, new)
    text = once(text, '    bool has_automatic_backend_setting = false;',
        '    bool has_automatic_backend_setting = false;\n    bool has_rollback_optin_setting = false;')
    text = once(text, '''            if (key == "online_experimental_rollback") {
                g_online_experimental_rollback = number == 1;''',
        '''            if (key == "online_experimental_rollback") {
                has_rollback_optin_setting = true;
                g_online_experimental_rollback = number == 1;''')
    # The compatibility save keys stay readable by older versions, but are
    # always written as an explicit opt-in to engine 2, never old Auto.
    text = once(text, '''        output << "online_synchronization=" << g_online_synchronization << '\\n';''',
                     '''        output << "online_synchronization=" << 0 << '\\n';''')
    text = once(text, '''        output << "online_experimental_rollback=" << (g_online_experimental_rollback ? 1 : 0) << '\\n';''',
                     '''        output << "online_experimental_rollback=" << 1 << '\\n';''')
    text = once(text, '''        output << "online_automatic_backend=" << (g_online_automatic_backend ? 1 : 0) << '\\n';''',
                     '''        output << "online_automatic_backend=" << 0 << '\\n';''')
    # Missing/malformed values retain the initialized profile. Numeric values
    # outside supported ranges also use that profile, not a clamped extreme.
    text = once(text, 'g_online_host_control = std::clamp(number, 0, 2);',
        'g_online_host_control = number >= 0 && number <= 2 ? number : 0;')
    text = once(text, '''g_online_maximum_players = std::clamp(number, 2,
                    static_cast<int>(dkr::runtime::netplay::kSupportedOnlinePlayers));''',
        'g_online_maximum_players = number >= 2 && number <= 4 ? number : '+POLICY+'maximum_players;')
    text = once(text, 'g_online_rollback_window = std::clamp(number, 2, 20);',
        'g_online_rollback_window = number >= 2 && number <= 20 ? number : '+POLICY+'prediction_window;')
    text = once(text, 'g_online_manual_delay = std::clamp(number, 0, 9);',
        'g_online_manual_delay = number >= 0 && number <= 9 ? number : '+POLICY+'input_delay;')
    text = once(text, 'g_online_automatic_delay = number != 0;',
        'g_online_automatic_delay = number == 0 || number == 1 ? number != 0 : '+POLICY+'automatic_delay;')
    text = once(text, 'g_online_record_replay = number != 0;',
        'g_online_record_replay = number == 0 || number == 1 ? number != 0 : '+POLICY+'record_replay;')
    return text

def online(text):
    begin = text.index('void DrawOlHostSettings(float width) {')
    end = text.index('// ------------------------------------------------------------ online profile', begin)
    text = text[:begin] + (AUTH/'native/host_settings.inl').read_text() + '\n\n' + text[end:]
    old = '''    std::string line = rules.synchronization == SynchronizationMode::ExperimentalRollback ? "Experimental Rollback" :
        (rules.synchronization == SynchronizationMode::Rollback ? "Host prediction" : "Strict input sync");'''
    return once(text, old, '    std::string line = '+POLICY+'name(rules.synchronization);')

def types(text):
    text = once(text, '    if (descriptor.match_id == 0U ||',
        '    if (!'+POLICY+'supported(descriptor.synchronization) || descriptor.match_id == 0U ||')
    return once(text, 'bool valid_rules(const Rules& rules) {',
        'bool valid_rules(const Rules& rules) {\n    if (!'+POLICY+'supported(rules.synchronization)) return false;')

def protocol(text):
    text = once(text, '    if (!valid_rules(payload.rules)) {',
        '''    if (!'''+POLICY+'''supported(payload.rules.synchronization)) {
        error = '''+POLICY+'''admission_error(payload.rules.synchronization);
        return false;
    }
    if (!valid_rules(payload.rules)) {''')
    text = once(text, '''    payload.descriptor.synchronization =
        static_cast<SynchronizationMode>(bytes[cursor++]);''',
        '''    payload.descriptor.synchronization =
        static_cast<SynchronizationMode>(bytes[cursor++]);
    if (!'''+POLICY+'''supported(payload.descriptor.synchronization)) {
        error = '''+POLICY+'''admission_error(payload.descriptor.synchronization);
        return false;
    }''')
    return text

def direct(text):
    # Only an authenticated host's concrete unsupported-mode response ends
    # admission. Unrelated malformed or stale packets keep their existing policy.
    text = once(text, '        if (protocol::decode_lobby_state(packet.payload, payload, error)) apply_lobby_payload(payload);',
        '''        if (protocol::decode_lobby_state(packet.payload, payload, error)) apply_lobby_payload(payload);
        else if (error == '''+POLICY+'''unsupported_lobby) fail_locked(error);''')
    text = once(text, '''                    for(const auto& m:party->party.snapshot().members)if(m.port)party->ready[m.id]=room.players[*m.port].ready;
                }
            } else if(message.kind==watch::LinkKind::Launch) {''',
        '''                    for(const auto& m:party->party.snapshot().members)if(m.port)party->ready[m.id]=room.players[*m.port].ready;
                } else if(reason=='''+POLICY+'''unsupported_lobby) fail_locked(reason);
            } else if(message.kind==watch::LinkKind::Launch) {''')
    return text

def main(args):
    out = args.output.resolve()
    if out.is_relative_to(ROOT): raise ValueError('Rollback stage must stay outside the checkout')
    rows = json.loads(args.inputs.read_text(encoding='utf-8'))
    type_cpp = next(Path(r['path']) for r in rows if Path(r['path']).name == 'netplay_types.cpp')
    type_header = type_cpp.with_suffix('.hpp')
    policy = (AUTH/'native/rollback_policy.hpp').read_text()
    policy = once(policy, '#include "netplay_types.hpp"', '#include "'+type_header.as_posix()+'"')
    write(out/'rollback_policy.hpp', policy.encode())
    include_cache = {}; includes = []

    def resolve(name, origin, directories):
        path = Path(name)
        if path.is_absolute(): return path if path.is_file() else None
        for root in [origin.parent]+directories:
            path = root/name
            if path.is_file(): return path.resolve()
        return None

    def compose_includes(text, origin, directories):
        def replace(match):
            path = resolve(match[1], origin, directories)
            if path is None: return match[0]
            if path.suffix != '.inl': return match[0]
            cache_key = str(path)
            if cache_key in include_cache:
                cached = include_cache[cache_key]
                return '#include "'+cached.as_posix()+'"' if cached != path else match[0]
            raw = path.read_bytes(); content = raw.decode().replace('\r\n', '\n')
            if path.name == 'runtime_online_ui.inl': content = online(content)
            content = copy_labels(content)
            content = compose_includes(content, path, directories)
            if content == raw.decode().replace('\r\n', '\n'):
                include_cache[cache_key] = path
                return match[0]
            dest = out/'includes'/sha(path.name.encode()+raw)[:12]/path.name
            include_cache[cache_key] = dest
            def adjacent_include(child):
                # Preserve the compiler's transitive include ordering for
                # non-adjacent headers (including isolated renderer overlays).
                dependency = resolve(child[1], path, [])
                return '#include "'+dependency.as_posix()+'"' if dependency else child[0]
            content = INCLUDE.sub(adjacent_include, content)
            write(dest, content.encode())
            includes.append({'path':str(path), 'new':str(dest), 'input_sha256':sha(raw), 'output_sha256':sha(dest.read_bytes())})
            return '#include "'+dest.as_posix()+'"'
        return INCLUDE.sub(replace, text)

    swaps = []
    for row in rows:
        source = Path(row['path'])
        # No dependency code or generated recomp sources enter this policy.
        if any(x.lower() in ('extern','recompiledfuncs','recompiledpatches','recompiledrsp') for x in source.parts): continue
        raw = source.read_bytes(); original = raw.decode().replace('\r\n', '\n'); text = original
        directories = [Path(p) for p in row.get('includes', []) if '$<' not in p and Path(p).is_dir()]
        if source.name == 'runtime_ui.cpp': text = ui(text)
        elif source.name == 'netplay_types.cpp': text = types(text)
        elif source.name == 'netplay_protocol.cpp': text = protocol(text)
        elif source.name == 'direct_session.cpp': text = direct(text)
        text = copy_labels(text)
        # Only recurse through UI include trees; other files do not contain
        # presentation inls and keep all original include dependencies intact.
        if source.name == 'runtime_ui.cpp': text = compose_includes(text, source, directories)
        if text == original: continue
        text = '#include "'+(out/'rollback_policy.hpp').as_posix()+'"\n'+text
        def adjacent(match):
            path = resolve(match[1], source, [])
            return '#include "'+path.as_posix()+'"' if path else match[0]
        text = INCLUDE.sub(adjacent, text)
        dest = out/row['target']/source.name
        write(dest, text.encode())
        swaps.append({'target':row['target'],'path':str(source),'new':str(dest),
                      'input_sha256':sha(raw),'output_sha256':sha(dest.read_bytes())})
    expected = {'runtime_ui.cpp','netplay_types.cpp','netplay_protocol.cpp','direct_session.cpp'}
    if not expected.issubset({Path(r['new']).name for r in swaps}): raise ValueError('Rollback host/admission/UI sources are missing')
    write(out/'swaps.json', (json.dumps(swaps,indent=2)+'\n').encode())
    write(out/'includes.json', (json.dumps(includes,indent=2)+'\n').encode())
    print('Rollback-only host defaults, migration, UI and admission staged; no target execution.')

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    main(p.parse_args())
