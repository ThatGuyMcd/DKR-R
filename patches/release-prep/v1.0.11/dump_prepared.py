"""Inspect prepared numeric data using unchanged production libraries; no game."""
from pathlib import Path
import argparse,json,os,shlex,subprocess
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2]
INJECT=r'''
    Json diagnostic;
    const auto bits=[](float f){return std::bit_cast<unsigned>(f);};
    const auto vector=[&](Vec3 v){return Json::array({bits(v.x),bits(v.y),bits(v.z)});};
    diagnostic["identity"]={p.id,p.name,p.digest};
    diagnostic["placement"]={bits(p.scale),bits(p.yaw),bits(p.driver_yaw)};
    diagnostic["seat"]=vector(p.seat);diagnostic["driver_offset"]=vector(p.driver_offset);
    diagnostic["origin"]=vector(p.origin);diagnostic["kart_offset"]=vector(p.kart_offset);
    diagnostic["mode"]={unsigned(p.seating),unsigned(p.authored_anchors),p.axle};
    for(auto v:p.driver_anchors)diagnostic["anchors"].push_back(vector(v));
    for(auto v:p.driver_seats)diagnostic["seats"].push_back(vector(v));
    for(auto v:p.pivots)diagnostic["pivots"].push_back(vector(v));
    diagnostic["wheels"]=p.wheels;
    for(const auto& t:p.textures)diagnostic["textures"].push_back({t.image.width,t.image.height,t.repeat_s,t.repeat_t,sha256(t.image.rgba)});
    diagnostic["portrait"]={p.portrait.width,p.portrait.height,sha256(p.portrait.rgba)};
    for(const auto& l:p.lods){Json lod;lod["triangles"]=l.triangles;
      for(const auto& m:l.meshes){Json mesh;mesh["id"]=m.id;mesh["name"]=m.name;mesh["texture"]=m.texture;
        mesh["double_sided"]=m.double_sided;mesh["colour"]=m.colour;mesh["faces"]=m.faces;
        for(auto v:m.vertices)mesh["vertices"].push_back(vector(v));
        for(auto v:m.uv)mesh["uv"].push_back({bits(v.x),bits(v.y)});
        lod["meshes"].push_back(std::move(mesh));}
      diagnostic["lods"].push_back(std::move(lod));}
    std::ofstream diagnostic_file(DIAGNOSTIC_PATH,std::ios::binary);diagnostic_file<<diagnostic.dump();
'''
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','output','input'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--emulator',type=Path);p.add_argument('--sysroot',type=Path);p.add_argument('--no-contraction',action='store_true');a=p.parse_args()
    if a.output.exists() or a.output.resolve().is_relative_to(ROOT):raise ValueError('Choose fresh external diagnostic outputs')
    a.output.mkdir(parents=True)
    source=ROOT/'patches/vehicle-original-textures/tests/package_checks.cpp';text=source.read_text();marker='return sha256(out);'
    if text.count(marker)!=1:raise ValueError('Accepted fingerprint serializer changed')
    cpp=a.output/'inspect.cpp';cpp.write_text(text.replace(marker,INJECT.replace('DIAGNOSTIC_PATH',json.dumps(str(a.output/'prepared.json')))+marker))
    commands=subprocess.check_output(['ninja','-t','commands','DKRVehicleOriginalChecks'],cwd=a.build,text=True).splitlines()
    compile_line=next(x for x in commands if ' -c '+str(source) in x)
    args=shlex.split(compile_line);old_obj=args[args.index('-o')+1];obj=a.output/'inspect.o';args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(cpp)
    if a.no_contraction:args.append('-ffp-contract=off')
    subprocess.run(args,cwd=a.build,check=True,stdout=subprocess.DEVNULL)
    package_object=None
    if a.no_contraction:
        package_source=a.build/'generated/vehicle-original-textures/DKRCustomVehicles/vehicle_package.cpp'
        line=next(x for x in commands if ' -c '+str(package_source) in x)
        package_args=shlex.split(line);package_object=a.output/'package.o';package_args[package_args.index('-o')+1]=str(package_object);package_args.append('-ffp-contract=off')
        subprocess.run(package_args,cwd=a.build,check=True,stdout=subprocess.DEVNULL)
    line=next(x for x in commands if ' -o DKRVehicleOriginalChecks ' in x)
    if not line.startswith(': && ') or not line.endswith(' && :'):raise ValueError('Unexpected linker wrapper')
    args=shlex.split(line[5:-5]);args=[str(obj) if x==old_obj else x for x in args];exe=a.output/'inspect';args[args.index('-o')+1]=str(exe)
    if package_object:args.insert(args.index(str(obj))+1,str(package_object))
    subprocess.run(args,cwd=a.build,check=True,stdout=subprocess.DEVNULL)
    env=os.environ.copy()
    if a.sysroot:env['QEMU_LD_PREFIX']=str(a.sysroot)
    command=([str(a.emulator)] if a.emulator else [])+[str(exe),'--fingerprint',str(a.input)]
    result=subprocess.run(command,env=env,capture_output=True,check=True)
    (a.output/'fingerprint.txt').write_bytes(result.stdout)
    print('Prepared-data diagnostic captured from unchanged production libraries:',a.output)
