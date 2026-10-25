namespace sdk2 {
struct Vec {float x=0,y=0,z=0;
    Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
    Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
    Vec operator*(float k)const{return {x*k,y*k,z*k};}
};
float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec normalized(Vec a){const auto length=std::sqrt(dot(a,a));return length>1e-10F?a*(1.F/length):Vec{0,1,0};}
Vec closest_triangle(Vec p,Vec a,Vec b,Vec c) {
    const auto ab=b-a,ac=c-a,ap=p-a;const float d1=dot(ab,ap),d2=dot(ac,ap);if(d1<=0&&d2<=0)return a;
    const auto bp=p-b;const float d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return b;
    const float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return a+ab*(d1/(d1-d3));
    const auto cp=p-c;const float d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return c;
    const float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return a+ac*(d2/(d2-d6));
    const float va=d3*d6-d5*d4;if(va<=0&&d4-d3>=0&&d5-d6>=0)return b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6)));
    const float sum=va+vb+vc;if(std::abs(sum)<1e-12F)return a;
    return a+ab*(vb/sum)+ac*(vc/sum);
}
bool inside(Vec p,Vec a,Vec b,Vec c,Vec n) {
    return dot(cross(b-a,p-a),n)>=-1e-4F&&dot(cross(c-b,p-b),n)>=-1e-4F&&dot(cross(a-c,p-c),n)>=-1e-4F;
}
struct CastHit {float time=2;Vec point,normal;unsigned segment=0,triangle=0,texture=255;};
void offer(CastHit& hit,float t,Vec point,Vec normal) {
    if(std::isfinite(t)&&t>=0&&t<=1&&t<hit.time){hit.time=t;hit.point=point;hit.normal=normalized(normal);}
}
void sphere(CastHit& hit,Vec origin,Vec direction,Vec centre,float radius) {
    const Vec offset=origin-centre;const float aa=dot(direction,direction),bb=dot(offset,direction),cc=dot(offset,offset)-radius*radius;
    if(cc<=0){offer(hit,0,centre,offset);return;}if(aa<=1e-12F)return;
    const float discriminant=bb*bb-aa*cc;if(discriminant<0)return;
    const float t=(-bb-std::sqrt(discriminant))/aa;offer(hit,t,centre,origin+direction*t-centre);
}
void edge(CastHit& hit,Vec origin,Vec direction,Vec a,Vec b,float radius) {
    const auto e=b-a,m=origin-a;const float ee=dot(e,e),ed=dot(e,direction),em=dot(e,m);
    const float aa=ee*dot(direction,direction)-ed*ed,bb=ee*dot(m,direction)-em*ed;
    const float cc=ee*(dot(m,m)-radius*radius)-em*em,discriminant=bb*bb-aa*cc;
    if(ee>1e-12F&&aa>1e-12F&&discriminant>=0) {
        const float t=(-bb-std::sqrt(discriminant))/aa,y=em+t*ed;
        if(y>=0&&y<=ee){const auto p=a+e*(y/ee);offer(hit,t,p,origin+direction*t-p);}
    }
    sphere(hit,origin,direction,a,radius);sphere(hit,origin,direction,b,radius);
}
void triangle(CastHit& hit,Vec origin,Vec direction,float radius,Vec a,Vec b,Vec c) {
    const auto normal=cross(b-a,c-a);const float length=std::sqrt(dot(normal,normal));if(length<1e-8F)return;
    const auto n=normal*(1.F/length);const float velocity=dot(direction,n),distance=dot(origin-a,n);
    if(radius>0) {
        const auto initial=closest_triangle(origin,a,b,c),offset=origin-initial;
        if(dot(offset,offset)<=radius*radius){offer(hit,0,initial,dot(offset,offset)>1e-12F?offset:n);return;}
    }
    if(std::abs(velocity)>1e-9F)for(unsigned side=0;side<(radius>0?2U:1U);++side) {
        const float signed_radius=side?-radius:radius,t=(signed_radius-distance)/velocity;
        const auto centre=origin+direction*t,point=centre-n*signed_radius;
        if(t>=0&&t<=1&&inside(point,a,b,c,normal))offer(hit,t,point,radius>0?centre-point:(velocity>0?n*-1.F:n));
    }
    if(radius>0){edge(hit,origin,direction,a,b,radius);edge(hit,origin,direction,b,c,radius);edge(hit,origin,direction,c,a,radius);}
}
bool box(Vec origin,Vec direction,const float* low,const float* high,float radius) {
    float enter=0,leave=1;const float o[]={origin.x,origin.y,origin.z},d[]={direction.x,direction.y,direction.z};
    for(unsigned i=0;i<3;++i) {
        if(std::abs(d[i])<1e-9F){if(o[i]<low[i]-radius||o[i]>high[i]+radius)return false;}
        else {float a=(low[i]-radius-o[i])/d[i],b=(high[i]+radius-o[i])/d[i];if(a>b)std::swap(a,b);enter=std::max(enter,a);leave=std::min(leave,b);if(enter>leave)return false;}
    }return true;
}
}
int GameWorld::sdk2_cast(Memory& bus,std::uint32_t query,std::uint32_t output,Budget& budget) {
    const unsigned flags=unsigned(bus.load(query+8,4));
    if(bus.load(query,4)<40||bus.load(output,4)<48||(flags&~3U))return -1;
    const auto real=[&](unsigned at){return std::bit_cast<float>(std::uint32_t(bus.load(at,4)));};
    const auto segment=std::uint32_t(bus.load(query+4,4));const float radius=real(query+12);
    sdk2::Vec from{real(query+16),real(query+20),real(query+24)},to{real(query+28),real(query+32),real(query+36)};
    for(float value:{radius,from.x,from.y,from.z,to.x,to.y,to.z})if(!std::isfinite(value)||std::abs(value)>1e6F)return -1;
    if(radius<0||radius>512)return -1;
    bus.fill(output,48);bus.store(output,4,48);
    const auto model=std::uint32_t(bus.load(symbol("gCurrentLevelModel"),4));if(!sdk2::guest(model,0x4c))return 1;
    const auto count=unsigned(bus.load(model+0x1a,2)),segments=std::uint32_t(bus.load(model+4,4)),boxes=std::uint32_t(bus.load(model+8,4));
    if(count>1024||!sdk2::guest(segments,count*0x44)||!sdk2::guest(boxes,count*12))return -1;
    if(segment!=UINT32_MAX&&segment>=count)return -1;
    sdk2::CastHit best;const auto direction=to-from;
    for(unsigned s=0;s<count;++s) {
        if(flags&2U)break;
        if(segment!=UINT32_MAX&&segment!=s)continue;
        float low[3],high[3];for(unsigned i=0;i<3;++i){low[i]=float(std::int16_t(bus.load(boxes+s*12+i*2,2)));high[i]=float(std::int16_t(bus.load(boxes+s*12+6+i*2,2)));}
        if(!sdk2::box(from,direction,low,high,radius))continue;
        const auto entry=segments+s*0x44,vertices=std::uint32_t(bus.load(entry,4)),triangles=std::uint32_t(bus.load(entry+4,4)),batches=std::uint32_t(bus.load(entry+0xc,4));
        const unsigned nv=unsigned(bus.load(entry+0x1c,2)),nt=unsigned(bus.load(entry+0x1e,2)),nb=unsigned(bus.load(entry+0x20,2));
        const unsigned cost=nt*(radius>0?120U:48U);
        if(nt>8192||cost>budget.instructions)return -6;budget.instructions-=cost;
        if(!sdk2::guest(vertices,nv*10)||!sdk2::guest(triangles,nt*16)||!sdk2::guest(batches,(nb+1)*12))return -1;
        for(unsigned batch=0;batch<nb;++batch) {
            const auto b=batches+batch*12;const unsigned begin=unsigned(bus.load(b+4,2)),end=unsigned(bus.load(b+16,2)),base=unsigned(bus.load(b+2,2));if(begin>end||end>nt)return -1;
            for(unsigned triangle=begin;triangle<end;++triangle) {
                sdk2::Vec v[3];for(unsigned i=0;i<3;++i) {
                    const unsigned index=base+unsigned(bus.load(triangles+triangle*16+i+1,1));if(index>=nv)return -1;
                    const auto at=vertices+index*10;v[i]={float(std::int16_t(bus.load(at,2))),float(std::int16_t(bus.load(at+2,2))),float(std::int16_t(bus.load(at+4,2)))};
                }
                const float previous=best.time;sdk2::triangle(best,from,direction,radius,v[0],v[1],v[2]);
                if(best.time<previous){best.segment=s;best.triangle=triangle;best.texture=unsigned(bus.load(b,1));}
            }
        }
    }
    if(flags&1U) {
        const auto objects=std::uint32_t(bus.load(symbol("gObjPtrList"),4));const auto n=unsigned(bus.load(symbol("gObjectCount"),4));
        if(n>512||!sdk2::guest(objects,n*4))return -1;
        if(budget.instructions<n*64U)return -6;budget.instructions-=n*64U;
        for(unsigned i=0;i<n;++i) {
            const auto object=std::uint32_t(bus.load(objects+i*4,4));if(!sdk2::guest(object,0x68))continue;
            const auto interaction=std::uint32_t(bus.load(object+0x4c,4));if(!sdk2::guest(interaction,0x28))continue;
            const int hitbox=std::int8_t(bus.load(interaction+0x10,1));if(hitbox<=0)continue;
            const auto object_segment=unsigned(bus.load(object+0x2e,2));if(segment!=UINT32_MAX&&segment!=object_segment)continue;
            sdk2::Vec centre{real(object+12),real(object+16),real(object+20)};if(!std::isfinite(centre.x)||!std::isfinite(centre.y)||!std::isfinite(centre.z))return -1;
            const auto previous=best.time;sdk2::sphere(best,from,direction,centre,radius+float(hitbox));
            if(best.time<previous) {
                best.segment=object_segment;best.triangle=UINT32_MAX;best.texture=unsigned(bus.load(object+0x4a,2))&511U;
                best.point=centre+best.normal*float(hitbox);
            }
        }
    }
    if(best.time>1)return 1;
    bus.store(output+4,4,1);bus.store(output+8,4,std::bit_cast<std::uint32_t>(best.time));
    const float values[]={best.point.x,best.point.y,best.point.z,best.normal.x,best.normal.y,best.normal.z};
    for(unsigned i=0;i<6;++i)bus.store(output+12+i*4,4,std::bit_cast<std::uint32_t>(values[i]));
    bus.store(output+36,4,best.segment);bus.store(output+40,4,best.triangle);bus.store(output+44,4,best.texture);return 0;
}
