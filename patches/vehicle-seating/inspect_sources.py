"""Inspect immutable archives, staged source receipts and linked bytes only."""
from pathlib import Path
import argparse
import base64
import hashlib
import importlib.util
import io
import json
import math
import struct
import zipfile
import zlib

AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
DRIVERS=('KRUNCH','BUMPER','TIPTUP','CONKER','TIMBER','BANJO','DRUMSTICK','PIPSY','TT','DIDDY')
ROLES=('front_left','front_right','rear_left','rear_right')

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def require(condition,message):
    if not condition:raise ValueError(message)

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

def png(blob):
    require(blob[:8]==b'\x89PNG\r\n\x1a\n','Not PNG')
    at=8;compressed=b'';header=None;ended=False
    while at<len(blob):
        size=struct.unpack_from('>I',blob,at)[0];kind=blob[at+4:at+8];data=blob[at+8:at+8+size]
        crc=struct.unpack_from('>I',blob,at+8+size)[0];at+=12+size
        require(zlib.crc32(kind+data)&0xffffffff==crc,'PNG CRC')
        if kind==b'IHDR':header=struct.unpack('>IIBBBBB',data)
        if kind==b'IDAT':compressed+=data
        if kind==b'IEND':ended=True;break
    require(ended and at==len(blob) and header is not None,'Incomplete PNG')
    w,h,depth,kind,compression,filtering,interlace=header
    require(w>0 and h>0 and depth==8 and kind==6 and compression==filtering==interlace==0,'Unexpected PNG layout')
    decoded=zlib.decompress(compressed)
    require(len(decoded)==h*(4*w+1),'PNG scanline length')
    return w,h

def bundle(path):
    rows=[]
    with zipfile.ZipFile(path) as outer:
        require(outer.testzip() is None,'Outer ZIP checksum')
        require(1<=len(outer.namelist())<=16,'Vehicle bundle count exceeds the supported catalogue bound')
        for name in outer.namelist():
            require('/' not in name and '\\' not in name and name.endswith('.dkrkart'),'Unexpected pack member')
            blob=outer.read(name)
            with zipfile.ZipFile(io.BytesIO(blob)) as archive:
                require(archive.testzip() is None,'Inner ZIP checksum')
                expected={'kart.json','geometry.json','portrait.png','README.txt'}|{f'lods/lod{i}.json' for i in range(6)}
                require(set(archive.namelist())==expected and len(archive.namelist())==10,'Car archive layout')
                manifest=json.loads(archive.read('kart.json'));settings=manifest['settings'];transform=manifest['kart_transform']
                require(manifest['driver_position_mode']=='per-base-driver' and manifest['driver_anchor_kind']=='kart-fixed-base-reference-v1','Seating convention')
                require(set(manifest['driver_anchors'])==set(manifest['driver_seat_positions'])==set(DRIVERS),'Driver table coverage')
                require(settings['driver_seat_positions']==manifest['driver_seat_positions'],'Duplicate seats')
                require('seat' not in settings and settings['driver']==[0,0,0],'Legacy placement mixed with new seats')
                require(transform['driver_positions_inherit_transform'] is False,'Seat transform inheritance')
                require(transform['order']=='subtract origin, scale, rotate around +Y, translate','Transform order')
                require(transform['units']=='Y-up Studio units; forward -Z; yaw in degrees','Transform units')
                require(transform['scale']==settings['scale'] and transform['yaw_degrees']==settings['yaw'] and transform['translation']==settings['kart_offset'],'Duplicate transforms')
                require(transform['origin']==manifest['geometry_origin'],'Duplicate origins')
                require(0.001<=settings['scale']<=4096 and abs(settings['yaw'])<=3600,'Model scale/yaw')
                require(set(settings['wheels'])==set(ROLES),'Wheel roles')
                wheel_ids=[settings['wheels'][r] for r in ROLES if settings['wheels'][r]]
                require(len(set(wheel_ids))==len(wheel_ids),'Duplicate wheel IDs')
                source=json.loads(archive.read('geometry.json'));materials=source['textures'];textures=[]
                for texture in materials:
                    w,h=png(base64.b64decode(texture['image'].split(',',1)[1],validate=True))
                    require([w,h]==[texture['width'],texture['height']],'Texture dimensions')
                    require(w*h*4<=4096 and w&(w-1)==0 and h&(h-1)==0,'Native-safe texture footprint')
                    textures.append([w,h])
                require(manifest['texture_profile']=='native-4kib-rgba32-safe-v1','Texture profile')
                lods=[];max_st=0
                for filename in ['geometry.json']+[f'lods/lod{i}.json' for i in range(6)]:
                    geometry=json.loads(archive.read(filename));meshes=geometry['meshes'];triangles=vertices=0
                    require(geometry['schema']==1 and geometry['origin']==transform['origin'],'Geometry origin/schema')
                    require(geometry.get('textures',materials)==materials,'LOD materials')
                    require(set(wheel_ids)<={mesh['id'] for mesh in meshes},'Missing assigned wheel')
                    require(0<len(meshes)<=64,'Mesh count')
                    for mesh in meshes:
                        vs,uv,faces=mesh['vertices'],mesh['uv'],mesh['faces'];vertices+=len(vs);triangles+=len(faces)
                        require(vs and faces and len(vs)==len(uv) and mesh.get('primitive',0)==0,'Mesh layout')
                        require(all(len(v)==3 and all(math.isfinite(c) and abs(c)*settings['scale']<=32700 for c in v) for v in vs),'Vertex bounds')
                        require(all(len(u)==2 and all(math.isfinite(c) and abs(c)<=128 for c in u) for u in uv),'UV bounds')
                        require(all(len(f)==3 and len(set(f))==3 and min(f)>=0 and max(f)<len(vs) for f in faces),'Face references')
                        t=mesh.get('texture',-1);require(-1<=t<len(materials),'Material reference')
                        if t>=0:
                            w,h=textures[t]
                            max_st=max(max_st,max(max(abs(u[0])*w,abs(u[1])*h)*32 for u in uv))
                    require(triangles<=4096 and vertices<=8192 and max_st<=32700,'Native geometry bounds')
                    if filename!='geometry.json':
                        level=int(filename[8]);require(manifest['lods'][level]['triangles']==triangles,'LOD metadata');lods.append(triangles)
                for table in (manifest['driver_anchors'],manifest['driver_seat_positions']):
                    require(all(len(v)==3 and all(math.isfinite(c) and abs(c)<=1024 for c in v) for v in table.values()),'Driver coordinate bounds')
                seats={d:manifest['driver_seat_positions'][d] for d in DRIVERS}
                # Reference-space offsets are metadata arithmetic, not native execution.
                offsets={d:[round(seats[d][a]-manifest['driver_anchors'][d][a],6) for a in range(3)] for d in DRIVERS}
                portrait=png(archive.read('portrait.png'));require(max(portrait)<=512,'Portrait bounds')
                require('No shared seat or global calibration' in archive.read('README.txt').decode(),'Export convention notes')
                rows.append({'name':name,'sha256':hashlib.sha256(blob).hexdigest(),'texture_sizes':textures,'portrait':portrait,
                    'lod_triangles':lods,'absent_wheel_roles':[r for r in ROLES if not settings['wheels'][r]],
                    'unique_seats':len({tuple(v) for v in seats.values()}),'reference_offsets':offsets})
    return {'sha256':sha(path),'packages':rows,'lod_files':len(rows)*6,'driver_seat_pairs':len(rows)*10}

def inspect(stage,repair_stage,cars,binary=None):
    detail_stage=stage.parent/'vehicle-seating'
    contracts=json.loads((detail_stage/'driver-contracts.json').read_text())
    repair=load('fixed_seats_repair_inspector',ROOT/'patches/mod-runtime-repair/inspect_sources.py').inspect(repair_stage,binary,linked_contracts=contracts)
    ledger='driver-detail-and-postrace-lens-v2\n'
    for name in ('driver_detail_pipeline.py','ReleaseOverlay.cmake'):ledger+=name+':'+sha(AUTH/name)+'\n'
    require(contracts=={revision:hashlib.sha256((value+'\n'+ledger).encode()).hexdigest()
        for revision,value in repair['rollback_build_ids'].items()},'Final driver detail compatibility chain differs')
    driver_checks={
        'local_model_rt64.cpp':['source_triangle=(source-packet.model.triangles)/16+tri',
            'driver_mask=cars->detail_profile','if(driver_mask&&driver_mask->hide[step.source_triangle])continue;',
            'independent-driver-detail-v1'],
        'visual_model_kernel.c':['ARENA=0xC10000','ARENA_END=0xD00000','VERTICES=0xC20000','STACK=0xCF0000'],
        'f3ddkr_rt64.cpp':['kind==PresentationMarkerKind::TrackSelectLensFlare&&presentation_token==1U','postrace-lens-window-v1'],
        'runtime_stubs.cpp':['const bool postrace_lens=PostraceFullViewScope',
            'postrace_lens&&!g_postrace_frame_gate.contracted_frame_submitted?1U:0U'],
        'runtime_enhancements.cpp':['native-lod-selector-v2','context->r8 = context->r4;',
            'detail_reports.fetch_add(1,std::memory_order_relaxed)<32U'],
        'probe_native.c':['lens_scope=postrace||dkr_probe_lens_scope',
            'DKR_OWNED_LENS_BEGIN,postrace&&!state.postrace_contracted?1U:0U'],
    }
    primary_hooks={}
    detail_swaps=json.loads((detail_stage/'swaps.json').read_text())
    for row in detail_swaps:
        path=Path(row['path']);target=Path(row['new'])
        require(sha(path)==row['input_sha256'] and sha(target)==row['output_sha256'],'Driver detail source/receipt changed: '+str(target))
        text=target.read_text()
        if row.get('guest'):
            require(text.count('primary-driver-lod-residency-v2')==1,'Wrong primary driver residency count')
            require('if((uint32_t)ctx->r30<=2U)ctx->r5&=~(gpr)12U;' in text,'Actual vehicle register, boss boundary or LOD flags changed')
            require('if((uint32_t)ctx->r7<=2U)' not in text,'Settings row pointer is not a vehicle type')
            require('// 0x8000D644: sll         $t8, $fp, 2\n    ctx->r24 = S32(ctx->r30 << 2);' in text,'Vehicle table index lineage missing')
            require('// 0x8000D658: addu        $a3, $s7, $t6\n    ctx->r7 = ADD32(ctx->r23, ctx->r14);' in text,'Settings row register lineage missing')
            require('maximum_detail' not in text,'Local preference entered deterministic residency')
            primary_hooks[row['target']]=1
        else:
            for marker in driver_checks[target.name]:require(marker in text,'Missing driver/tint invariant: '+marker)
    require(len(primary_hooks)==4,'Not all normal/owned revisions retain high models')
    receipt=json.loads((stage/'vehicle-receipt.json').read_text())
    for path,value in receipt['inputs'].items():require(sha(Path(path))==value,'Read-only vehicle input changed: '+path)
    for path,value in receipt['outputs'].items():require(sha(stage/path)==value,'Staged vehicle source changed: '+path)
    for path in (ROOT/'patches/custom-vehicles/native').iterdir():
        require(sha(stage/'native'/path.name)==sha(path),'Unstaged authored vehicle source: '+path.name)
    native=stage/'native'
    invariants={
        'vehicle_package.cpp':['PerBaseDriverV1','kart-fixed-base-reference-v1','driver_positions_inherit_transform','if(p->wheels[w].empty())continue;','RGBA32-safe 4 KiB profile'],
        'vehicle_seat_alignment.hpp':['if(car.per_driver())return {};','value.x-car.origin.x','car.driver_seats.at(base)'],
        'vehicle_rt64.cpp':['seat_target(car,driver_->base)','seating::source_point(car,mesh.vertices[vi])','offset.x/scale_','car.per_driver()?0.F:DriverSeatAdvance','geometry=origin-scale-yaw-translate-v1',
            'maximum_detail_=enhancements::maximum_detail_enabled();','vehicle_lod_=maximum_detail_?0U:std::min(original_lod_,5U);','car.lods[vehicle_lod_]','(vehicle_lod_<<19)','detail-policy=character-effective-v1',
            'rdp.setOtherMode(G_CYC_2CYCLE|G_TF_POINT|G_TP_PERSP',
            'lighting::normal_matrix(basis)','lighting::shade(light,normal,mesh.colour)',
            'rdp.setEnvColor(light.environment)','rdp.setCombine(lighting::combine(textured))',
            'vertex.color.a=255','!lighting_.ready()',
            '.rgba32?G_IM_SIZ_32b:G_IM_SIZ_16b',
            'rdp.setTile(0,G_IM_FMT_RGBA,format,(width*2+7)/8',
            'texture-policy=source-colour-point-v1','driver_->lods[0]','RenderPass::detail_profile','thread_local const RenderPass* current_pass'],
        'vehicle_library.cpp':['matches!=batch.conflicts','ImportPolicy::RejectDuplicates','replacement_enabled','dkr-vehicles-v2:fixed-reference-seats:cars'],
        'vehicle_ui.inl':['DrawVehicleImportConfirm(locked)','Replace older copies','Keep both versions','RejectDuplicates'],
        'vehicle_protocol.hpp':['CarAdapterVersion=2'],
        'online_mod_manifest.cpp':['c.adapter!=CarAdapterVersion'],
        'online_mod_profile.cpp':['car->digest,CarAdapterVersion'],
        'vehicle_resources.cpp':['dkr-car-resources-v5-source-colour-fixed-seats','seating::source_point(*p,source)',
            'const bool rgba32=bank_row*t.image.height<=2048;',
            'if(rgba32)pixels.push_back({t.image.rgba,true});',
            'Bytes(encoded.begin()+32,encoded.begin()+32+size),false',
            'bytes+=pixels.back().bytes.size();'],
        'vehicle_world.cpp':['if(player>=8&&g.get(racer,2)==0xffffU&&g.get(racer+0x1d8,1)==1)',
            'owner<4&&(!race_racers_||owner<race_humans_)&&ports&&g.get(ports+owner*4)==object',
            'owner=g.get(s.current_player),ports=g.get(s.racers_by_port)',
            '[custom-vehicles][finish-wheels]',
            'wheel_phases_[player][w]+=std::uint64_t(-(speed*time)/(10LL*frames))',
            'CheckpointWriter out;out.u32(5);'],
        'vehicle_world.hpp':['current_player,racers_by_port,light_intensity,inverse_view,viewport_layout;'],
    }
    for filename,markers in invariants.items():
        text=(native/filename).read_text()
        for marker in markers:require(marker in text,'Missing staged invariant: '+marker)
    render=(native/'vehicle_rt64.cpp').read_text()
    require('car.lods[std::min(original_lod_,5U)]' not in render,'Car detail still ignores the effective preference')
    require('profile_=&driver_->lods[lod]' in render,'Driver ownership must retain the submitted native LOD')
    require('NativeDrawScope restore(s);' in render,'Car texture/filter changes must restore native drawing state')
    require('rdp.setOtherMode(G_TF_BILERP|' not in render,'Car material path still forces smoothing')
    require('maximum_detail' not in (native/'vehicle_world.cpp').read_text(),'Graphics preference leaked into guest/checkpoint code')
    # Inspect all normal/private revision hook receipts without executing the
    # generated guest code. Preserve exactly one existing wheel tick per call.
    wheel_hooks={}
    for target in ('DKRRecompiledFuncsV77','DKRRecompiledFuncsV80','DKRExperimentalOwnedGameV77','DKRExperimentalOwnedGameV80'):
        swaps=json.loads((stage/target/'swaps.json').read_text());ticks=0
        for swap in swaps:
            path=Path(swap['new']);require(sha(path)==swap['output_sha256'],'Changed staged guest hook: '+str(path))
            ticks+=path.read_text().count('dkr_legacy_character_event(rdram,&vc,204U,(uint32_t)ctx->r4);')
        require(ticks==1,'Missing or duplicate native wheel-update hook: '+target)
        wheel_hooks[target]=ticks
    symbols=(native/'vehicle_symbols.hpp').read_text()
    for address in ('0x8011d55cU','0x8011aeecU','0x8011dadcU','0x8011b46cU'):
        require(address in symbols,'Missing revision-specific finish handoff symbol: '+address)
    if binary:
        data=binary.read_bytes()
        for marker in (b'per-base-driver-v1',b'kart-fixed-base-reference-v1',b'geometry=origin-scale-yaw-translate-v1',
                       b'Replace older copies',b'Keep both versions',b'dkr-vehicles-v2:fixed-reference-seats:cars',b'detail-policy=character-effective-v1',
                       b'[custom-vehicles][finish-wheels]',b'texture-policy=source-colour-point-v1',b'independent-driver-detail-v1',b'native-lod-selector-v2'):
            require(marker in data,'Missing linked seating support: '+repr(marker))
    return {'schema':1,'scope':'Static archive, source/receipt and optional linked-byte inspection; no target execution',
        'bundle':bundle(cars),'vehicle_source_invariants':sum(map(len,invariants.values())),
        'rollback_build_ids':contracts,'native_wheel_update_hooks':wheel_hooks,'accepted_runtime_repairs':repair,
        'driver_detail_invariants':sum(map(len,driver_checks.values())),'primary_driver_residency_hooks':primary_hooks}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('stage','repair-stage','cars'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--binary',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args();result=inspect(a.stage,a.repair_stage,a.cars,a.binary)
    if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
