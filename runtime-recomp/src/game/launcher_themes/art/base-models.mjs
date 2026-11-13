// Offline artwork authoring only. three@0.180.0; never loaded by the game.
import * as T from 'three';
const C = hex => new T.Color(hex);
const material = (color, opts={}) => new T.MeshPhongMaterial({color, shininess:9, specular:'#17101b', ...opts});
function sphere(parent, scale, at, color, ws=18, hs=12){
  const mesh=new T.Mesh(new T.SphereGeometry(1,ws,hs),material(color));
  mesh.scale.set(...scale);mesh.position.set(...at);parent.add(mesh);return mesh;
}
function tube(parent, points, radius, color){
  const curve=new T.CatmullRomCurve3(points.map(p=>new T.Vector3(...p)));
  const mesh=new T.Mesh(new T.TubeGeometry(curve,12,radius,6,false),material(color));parent.add(mesh);return mesh;
}
function surfaceShape(points, z, mat){
  const shape=new T.Shape();shape.moveTo(...points[0]);for(const p of points.slice(1))shape.lineTo(...p);shape.closePath();
  const base=new T.ShapeGeometry(shape).toNonIndexed();const a=base.getAttribute('position');const verts=[];
  function subdiv(p,q,r,depth){
    if(depth){const pq=p.map((x,i)=>(x+q[i])/2),qr=q.map((x,i)=>(x+r[i])/2),rp=r.map((x,i)=>(x+p[i])/2);subdiv(p,pq,rp,depth-1);subdiv(pq,q,qr,depth-1);subdiv(rp,qr,r,depth-1);subdiv(pq,qr,rp,depth-1);return;}
    for(const v of [p,q,r])verts.push(v[0],v[1],z(v[0],v[1]));
  }
  for(let i=0;i<a.count;i+=3)subdiv([a.getX(i),a.getY(i)],[a.getX(i+1),a.getY(i+1)],[a.getX(i+2),a.getY(i+2)],3);
  const g=new T.BufferGeometry();g.setAttribute('position',new T.Float32BufferAttribute(verts,3));g.computeVertexNormals();return new T.Mesh(g,mat);
}
function pumpkin(){
  const root=new T.Group();const geom=new T.SphereGeometry(1,48,24);const p=geom.getAttribute('position');const col=[];
  for(let i=0;i<p.count;i++){
    let x=p.getX(i),y=p.getY(i),z=p.getZ(i);const angle=Math.atan2(x,z),lobes=.96+.045*Math.cos(angle*9);
    x*=1.10*lobes;z*=1.00*lobes;y*=.83;const indent=.09*Math.pow(Math.abs(y/.83),8);y-=Math.sign(y)*indent;
    p.setXYZ(i,x,y,z);const base=C('#ff8b13');const stripe=.87+.13*Math.cos(angle*9);base.multiplyScalar(stripe);
    const paint=Math.exp(-((x+.39)**2/.15+(y-.39)**2/.09+(z-.66)**2/.18))*.30;
    base.lerp(C('#ffd14b'),paint);col.push(base.r,base.g,base.b);
  }
  geom.setAttribute('color',new T.Float32BufferAttribute(col,3));geom.computeVertexNormals();root.add(new T.Mesh(geom,material('#ffffff',{vertexColors:true,specular:'#271604',shininess:12})));
  const zAt=(x,y)=>{const s=Math.sqrt(Math.max(.005,1-(x/1.10)**2-(y/.83)**2));const angle=Math.atan2(x,s);return s*(.96+.045*Math.cos(angle*9))+.026;};
  const dark=material('#301b0a',{side:T.DoubleSide});
  root.add(surfaceShape([[-.57,.08],[-.34,.43],[-.18,.04]],zAt,dark));
  root.add(surfaceShape([[.16,.04],[.32,.43],[.58,.08]],zAt,dark));
  root.add(surfaceShape([[-.09,-.12],[.01,.03],[.11,-.12]],zAt,dark));
  root.add(surfaceShape([[-.70,-.15],[-.48,-.23],[-.36,-.24],[-.36,-.34],[-.22,-.35],[-.22,-.25],[.19,-.25],[.19,-.35],[.33,-.34],[.33,-.24],[.47,-.23],[.68,-.13],[.56,-.43],[.36,-.55],[.15,-.60],[.15,-.48],[-.01,-.49],[-.01,-.62],[-.25,-.59],[-.45,-.49],[-.59,-.35]],zAt,dark));
  const bottom=material('#ce5e12',{side:T.DoubleSide});
  root.add(surfaceShape([[-.57,.08],[-.18,.04],[-.18,.015],[-.55,.055]],(x,y)=>zAt(x,y)+.003,bottom));
  root.add(surfaceShape([[.16,.04],[.58,.08],[.56,.055],[.17,.015]],(x,y)=>zAt(x,y)+.003,bottom));
  tube(root,[[0,.70,0],[.02,.87,-.01],[.03,1.04,-.03],[.18,1.12,-.04],[.28,1.09,-.05]],.11,'#54a825');
  sphere(root,[.16,.045,.14],[.01,.73,-.01],'#4b9e25',16,8);
  root.rotation.y=-.20;root.rotation.z=.025;return root;
}
function bat(){
  const root=new T.Group();
  function wing(sign){
    const sh=new T.Shape();sh.moveTo(.15,.12);sh.bezierCurveTo(.42,.66,.91,.67,1.30,.72);sh.bezierCurveTo(1.13,.43,1.13,.08,1.32,-.33);sh.bezierCurveTo(1.00,-.12,.87,-.16,.78,-.55);sh.bezierCurveTo(.59,-.30,.44,-.22,.27,-.40);sh.lineTo(.15,.12);
    const geom=new T.ExtrudeGeometry(sh,{depth:.08,bevelEnabled:true,bevelSegments:3,steps:1,bevelSize:.022,bevelThickness:.026,curveSegments:9});
    const a=geom.getAttribute('position');for(let i=0;i<a.count;i++){const x=a.getX(i);a.setX(i,x*sign);a.setZ(i,a.getZ(i)-x*x*.10);}if(sign<0){const idx=geom.index;if(idx){for(let i=0;i<idx.count;i+=3){const b=idx.getX(i+1);idx.setX(i+1,idx.getX(i+2));idx.setX(i+2,b);}}}
    geom.computeVertexNormals();const mesh=new T.Mesh(geom,material('#a268d4',{side:T.DoubleSide}));root.add(mesh);
    const mirror=pts=>pts.map(p=>[p[0]*sign,p[1],p[2]]);
    tube(root,mirror([[.18,.13,.10],[.42,.46,.08],[.82,.64,.03],[1.27,.71,-.06]]),.028,'#7142a5');
    tube(root,mirror([[.21,.12,.11],[.58,.08,.10],[.78,-.53,.04]]),.022,'#8853b8');
    tube(root,mirror([[.21,.12,.11],[.68,.29,.06],[1.30,-.31,-.06]]),.022,'#8853b8');
  }
  wing(1);wing(-1);
  sphere(root,[.33,.44,.27],[0,-.10,.12],'#7545bc',24,16);
  sphere(root,[.25,.30,.085],[0,-.18,.37],'#ba94d5',24,16);
  for(const sign of [-1,1]){
    const shape=new T.Shape();shape.moveTo(.09*sign,.39);shape.quadraticCurveTo(.19*sign,.68,.36*sign,.91);shape.quadraticCurveTo(.57*sign,.53,.34*sign,.30);shape.closePath();
    const geo=new T.ExtrudeGeometry(shape,{depth:.12,bevelEnabled:true,bevelSegments:3,steps:1,bevelSize:.035,bevelThickness:.035,curveSegments:8});
    const ear=new T.Mesh(geo,material('#8554c1',{side:T.DoubleSide}));ear.position.z=.07;root.add(ear);
    root.add(surfaceShape([[.18*sign,.44],[.36*sign,.76],[.37*sign,.44]],()=>.248,material('#dc91b5',{side:T.DoubleSide})));
  }
  sphere(root,[.39,.32,.30],[0,.29,.23],'#8656c4',28,20);
  sphere(root,[.22,.105,.07],[0,.125,.508],'#ba8acb',24,16);
  sphere(root,[.112,.055,.026],[0,.090,.57],'#432950',20,12);
  sphere(root,[.055,.015,.012],[.016,.065,.596],'#e0789e',16,8);
  for(const sign of [-1,1]){
    sphere(root,[.091,.135,.044],[sign*.117,.36,.505],'#fff9dd',24,16);
    sphere(root,[.040,.083,.024],[sign*.117+.012,.35,.542],'#2179b5',20,12);
    sphere(root,[.022,.059,.014],[sign*.117+.017,.349,.562],'#172a47',16,12);
    sphere(root,[.010,.020,.008],[sign*.117+.007,.378,.576],'#fff9e6',12,8);
    tube(root,[[sign*.047,.50,.45],[sign*.115,.52,.46],[sign*.205,.48,.43]],.032,'#7747b3');
    const fang=new T.Mesh(new T.ConeGeometry(.027,.07,8),material('#fff4d9'));fang.rotation.z=Math.PI;fang.position.set(sign*.060,.093,.586);root.add(fang);
    sphere(root,[.075,.045,.10],[sign*.15,-.51,.16],'#7447a4',18,10);
  }
  sphere(root,[.069,.049,.053],[0,.20,.575],'#d58fa3',20,12);
  root.rotation.y=-.16;root.rotation.z=-.065;return root;
}

export {T,C,material,sphere,tube,surfaceShape,pumpkin,bat};
