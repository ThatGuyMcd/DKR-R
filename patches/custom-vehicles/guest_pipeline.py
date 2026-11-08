"""Counted, build-local hooks over final normal/private Patch Pipeline output."""
from pathlib import Path
import argparse
import hashlib
import json
import re

from pipeline import put, once

NAMES = ('render_3d_model', 'render_sprite_billboard', 'menu_track_select_init',
         'menu_track_select_unload', 'func_80092188', 'trackmenu_setup_render', 'trackmenu_assets', 'func_80053750')


def body(text, name):
    # Prefer the original body, preserving the existing code-mod dispatcher.
    pattern = r'(?:static void dkr_code_body_' + name + r'|RECOMP_FUNC void ' + name + r')\(uint8_t\* rdram, recomp_context\* ctx\) \{'
    matches = list(re.finditer(pattern, text))
    if not matches:
        return None
    if len(matches) > 1:
        matches = [m for m in matches if m.group().startswith('static')]
    if len(matches) != 1:
        raise ValueError('Ambiguous vehicle hook: ' + name)
    begin = matches[0].end()
    # Skip comments/strings while finding the matching function brace.
    tokens = re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\{|\}', text[begin:])
    depth = 1
    for token in tokens:
        if token.group() == '{': depth += 1
        if token.group() == '}': depth -= 1
        if depth == 0: return begin, begin + token.start()
    raise ValueError('Unbalanced guest function: ' + name)


def menu(event):
    return '{ recomp_context vc=*ctx; extern int dkr_legacy_track_menu(uint8_t*,recomp_context*,unsigned,const uint32_t*,unsigned); dkr_legacy_track_menu(rdram,&vc,' + str(event) + 'U,0,0U); }'


def render(event, obj, scope='0'):
    return '{ recomp_context vc=*ctx; vc.r8=' + scope + '; extern void dkr_legacy_character_event(uint8_t*,recomp_context*,unsigned,uint32_t); dkr_legacy_character_event(rdram,&vc,' + str(event) + 'U,' + obj + '); }'


def hook(text, name):
    span = body(text, name)
    if not span: return text, False
    start, end = span
    original = text[start:end]
    before, after = '', ''
    if name == 'render_3d_model':
        before = '''
    const uint32_t vehicle_object=(uint32_t)ctx->r4;
    uint32_t vehicle_scope=0;
    { recomp_context vc=*ctx; vc.r2=0; extern void dkr_legacy_character_event(uint8_t*,recomp_context*,unsigned,uint32_t);
      dkr_legacy_character_event(rdram,&vc,200U,vehicle_object); vehicle_scope=(uint32_t)vc.r2; }
'''
        after = render(201, 'vehicle_object', 'vehicle_scope')
    elif name == 'render_sprite_billboard':
        before = '\n    const uint32_t vehicle_wheel=(uint32_t)ctx->r7;\n' + render(202, 'vehicle_wheel') + '\n'
        after = render(203, 'vehicle_wheel')
    elif name == 'func_80053750': before = '\n' + render(204, '(uint32_t)ctx->r4') + '\n'
    elif name == 'menu_track_select_init': after = menu(100)
    elif name == 'menu_track_select_unload': before = '\n' + menu(104) + '\n'
    elif name == 'func_80092188':
        before = '\n    { recomp_context vc=*ctx; extern int dkr_legacy_track_menu(uint8_t*,recomp_context*,unsigned,const uint32_t*,unsigned); if(dkr_legacy_track_menu(rdram,&vc,101U,0,0U))return; }\n'
        after = menu(102)
    elif name == 'trackmenu_setup_render':
        before = '''
    int vehicle_picker=0;
    { recomp_context vc=*ctx; extern int dkr_legacy_track_menu(uint8_t*,recomp_context*,unsigned,const uint32_t*,unsigned);
      vehicle_picker=dkr_legacy_track_menu(rdram,&vc,103U,0,0U); }
'''
        # Keep the admitted native camera, title, timestamps and balloons.
        # Replace only its lower vehicle/TT/CPU widgets while the picker is
        # active. Original branch delay slots and epilogue remain intact.
        if '// 0x80090F30:' in original:
            pc,reg,lower,finish='800915AC','13','80092010','80092150'
        elif '// 0x800913E8:' in original:
            pc,reg,lower,finish='80091AA4','15','8009252C','8009266C'
        else: raise ValueError('Unreviewed native car-menu revision')
        branch=re.search(r'(// 0x'+pc+r': beq[^\n]*\n    if \()(ctx->r'+reg+r' == ctx->r1)(\) \{)',original)
        if not branch: raise ValueError('Native car-menu widget branch changed')
        original=original[:branch.start()]+branch.group(1)+'vehicle_picker || ('+branch.group(2)+')'+branch.group(3)+original[branch.end():]
        original=once(original,'L_'+lower+':','''L_'''+lower+''':
    if(vehicle_picker) {
        recomp_context vc=*ctx;
        extern void dkr_vehicle_menu_draw(uint8_t*,recomp_context*,void(*)(uint8_t*,recomp_context*),void(*)(uint8_t*,recomp_context*));
        dkr_vehicle_menu_draw(rdram,&vc,menu_element_render,get_text_width);
        goto L_'''+finish+''';
    }''')
    elif name == 'trackmenu_assets':
        before = '\n    const unsigned vehicle_asset_type=(unsigned)ctx->r4;\n'
        after = 'if(vehicle_asset_type==1U)' + menu(105)
    if after:
        # All generated returns are explicit. Do not touch comments/string data.
        original, count = re.subn(r'\breturn;', after + ' return;', original)
        if count < 1: raise ValueError('Guest hook has no return fence: ' + name)
    return text[:start] + before + original + text[end:], True


def main(args):
    out = args.output.resolve()
    sources = json.loads(args.sources.read_text(encoding='utf-8'))
    found = dict.fromkeys(NAMES, 0)
    swaps = []
    for name in sources:
        path = Path(name)
        if path.suffix != '.c': continue
        text = path.read_text(encoding='utf-8')
        changed = False
        for symbol in NAMES:
            text, yes = hook(text, symbol)
            found[symbol] += int(yes)
            changed |= yes
        if changed:
            target = out / path.name
            if target == path.resolve(): raise ValueError('Guest input/output must differ')
            put(target, text.encode())
            swaps.append({'old': str(path.resolve()), 'new': str(target),
                          'input_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                          'output_sha256': hashlib.sha256(text.encode()).hexdigest()})
    if any(n != 1 for n in found.values()):
        raise ValueError('Missing/duplicate final vehicle hooks: ' + repr(found))
    put(out / 'swaps.json', (json.dumps(swaps, indent=2) + '\n').encode())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    main(parser.parse_args())
