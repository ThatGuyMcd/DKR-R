"""Read composed patch layers back to their baseline, entirely in memory."""
from pathlib import Path

def read_before(stage,path,patches,text=None):
    # An authored composer may supply an in-memory overlay while a build still
    # owns an earlier staged revision. Never write into that active build tree.
    if text is None:text=(Path(stage)/path).read_text()
    if path=='src/render/rt64_water_renderer.cpp' and 'WaterRenderer::prepareRays' in text:
        text=text.replace('        if(!rayShader) {','        if(!rayShader || !rayLayout) {\n            // Do not keep a shader if its companion layout creation failed.\n            rayShader.reset();rayLayout.reset();')
    for patch in patches:
        patch=Path(patch)
        if not patch.exists():continue
        hunks=[];active=False;old=[];new=[]
        def finish():
            if active and (old or new):hunks.append((''.join(new),''.join(old)))
        for line in patch.read_text().splitlines(True):
            if line.startswith('diff --git '):
                finish();old=[];new=[];active=line.split()[2][2:]==path
            elif line.startswith('@@ '):finish();old=[];new=[]
            elif line.startswith(('--- ','+++ ')):continue
            elif line.startswith(' '):old.append(line[1:]);new.append(line[1:])
            elif line.startswith('-'):old.append(line[1:])
            elif line.startswith('+'):new.append(line[1:])
        finish()
        for new,old in reversed(hunks):
            if text.count(new)!=1:raise ValueError(f'Cannot reverse {patch.name} in {path}')
            text=text.replace(new,old,1)
    return text
