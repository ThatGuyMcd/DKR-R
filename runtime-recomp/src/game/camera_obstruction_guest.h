#pragma once

#include "camera_obstruction_math.h"
#include "camera_clearance_metadata.h"
#include <string.h>

/* Included after the caller's recomp.h: the owned adapter therefore retains
   its checked-memory macros, and never calls the live game's native services. */
static inline int dkr_cam_span(uint32_t p,uint32_t bytes,unsigned alignment) {
    return bytes<=0x800000U && p>=0x80000000U && p<=0x80800000U-bytes &&
        !(p&(alignment-1));
}
static inline gpr dkr_cam_addr(uint32_t p) {return (gpr)(int32_t)p;}
static inline uint32_t dkr_cam_word(uint8_t* rdram,uint32_t p) {
    return (uint32_t)MEM_W(0,dkr_cam_addr(p));
}
static inline float dkr_cam_float(uint8_t* rdram,uint32_t p) {
    const uint32_t bits=dkr_cam_word(rdram,p);float f;memcpy(&f,&bits,4);return f;
}
static inline void dkr_cam_store(uint8_t* rdram,uint32_t p,double value) {
    const float f=(float)value;uint32_t bits;memcpy(&bits,&f,4);MEM_W(0,dkr_cam_addr(p))=bits;
}
static inline dkr_cam_vec dkr_cam_position(uint8_t* rdram,uint32_t p) {
    return dkr_cam_v(dkr_cam_float(rdram,p+12),dkr_cam_float(rdram,p+16),dkr_cam_float(rdram,p+20));
}
static inline int dkr_cam_overlap(dkr_cam_vec lo,dkr_cam_vec hi,dkr_cam_vec a,dkr_cam_vec b) {
    return lo.x<=b.x&&hi.x>=a.x&&lo.y<=b.y&&hi.y>=a.y&&lo.z<=b.z&&hi.z>=a.z;
}
static inline dkr_cam_vec dkr_cam_min(dkr_cam_vec a,dkr_cam_vec b) {
    return dkr_cam_v(fmin(a.x,b.x),fmin(a.y,b.y),fmin(a.z,b.z));
}
static inline dkr_cam_vec dkr_cam_max(dkr_cam_vec a,dkr_cam_vec b) {
    return dkr_cam_v(fmax(a.x,b.x),fmax(a.y,b.y),fmax(a.z,b.z));
}
static inline dkr_cam_vec dkr_cam_vertex(uint8_t* rdram,uint32_t p) {
    return dkr_cam_v(MEM_H(0,dkr_cam_addr(p)),MEM_H(2,dkr_cam_addr(p)),MEM_H(4,dkr_cam_addr(p)));
}

/* Read-only broad/narrow phase. No shared retail candidate list is populated.
   Traversal has a total 8192-face budget over all passes, no allocations, and
   no dependency on elapsed wall time. A malformed/excessive model fails
   transactionally back to the untouched retail camera and void-cover path. */
static inline int dkr_cam_query(uint8_t* rdram,uint32_t model,dkr_cam_vec origin,
    dkr_cam_vec point,double radius,int sweep,unsigned* budget,double* answer,dkr_cam_vec* push) {
    const unsigned count=(unsigned)MEM_H(0x1A,dkr_cam_addr(model));
    const unsigned texture_count=(unsigned)MEM_H(0x18,dkr_cam_addr(model));
    const uint32_t segments=dkr_cam_word(rdram,model+4),boxes=dkr_cam_word(rdram,model+8);
    const uint32_t textures=dkr_cam_word(rdram,model);
    if(!count||count>512||texture_count>256||!dkr_cam_span(segments,count*0x44,4)||
        !dkr_cam_span(boxes,count*12,2)||
        (texture_count&&!dkr_cam_span(textures,texture_count*8,4)))return 0;
    const dkr_cam_vec padding=dkr_cam_v(radius,radius,radius);
    const dkr_cam_vec lo=dkr_cam_sub(sweep?dkr_cam_min(origin,point):point,padding);
    const dkr_cam_vec hi=dkr_cam_add(sweep?dkr_cam_max(origin,point):point,padding);
    *answer=sweep?1.0:0.0;*push=dkr_cam_v(0,0,0);
    for(unsigned s=0;s<count;++s) {
        const uint32_t box=boxes+s*12;
        const dkr_cam_vec bl=dkr_cam_vertex(rdram,box),bh=dkr_cam_vertex(rdram,box+6);
        if(!dkr_cam_overlap(lo,hi,bl,bh))continue;
        const uint32_t seg=segments+s*0x44;
        const unsigned vertices=(unsigned)MEM_H(0x1C,dkr_cam_addr(seg));
        const unsigned triangles=(unsigned)MEM_H(0x1E,dkr_cam_addr(seg));
        const unsigned batches=(unsigned)MEM_H(0x20,dkr_cam_addr(seg));
        const uint32_t vp=dkr_cam_word(rdram,seg),tp=dkr_cam_word(rdram,seg+4),bp=dkr_cam_word(rdram,seg+12);
        if(vertices>32767||triangles>32767||batches>4096||
            !dkr_cam_span(vp,vertices*10,2)||!dkr_cam_span(tp,triangles*16,4)||
            !dkr_cam_span(bp,(batches+1)*12,4))return 0;
        for(unsigned batch=0;batch<batches;++batch) {
            const uint32_t base=bp+batch*12;
            const unsigned texture=MEM_BU(0,dkr_cam_addr(base));
            int surface=255;
            if(texture!=255) {
                if(texture>=texture_count)return 0;
                surface=MEM_BU(7,dkr_cam_addr(textures+texture*8));
            }
            if(!dkr_cam_solid_batch(dkr_cam_word(rdram,base+8),surface))continue;
            const unsigned first=(unsigned)MEM_H(4,dkr_cam_addr(base));
            const unsigned end=(unsigned)MEM_H(16,dkr_cam_addr(base));
            const unsigned offset=(unsigned)MEM_H(2,dkr_cam_addr(base));
            if(first>end||end>triangles||offset>vertices)return 0;
            for(unsigned tri=first;tri<end;++tri) {
                if(!*budget)return 0;--*budget;
                const uint32_t face=tp+tri*16;
                if(MEM_BU(0,dkr_cam_addr(face))&0x80)continue;
                dkr_cam_vec v[3];
                for(unsigned i=0;i<3;++i) {
                    const unsigned index=offset+MEM_BU(i+1,dkr_cam_addr(face));
                    if(index>=vertices)return 0;
                    v[i]=dkr_cam_vertex(rdram,vp+index*10);
                }
                if(!dkr_cam_overlap(lo,hi,dkr_cam_min(v[0],dkr_cam_min(v[1],v[2])),
                    dkr_cam_max(v[0],dkr_cam_max(v[1],v[2]))))continue;
                if(sweep) {
                    const double t=dkr_cam_triangle_hit(origin,point,v[0],v[1],v[2],radius);
                    if(t<*answer)*answer=t;
                } else {
                    dkr_cam_vec candidate;
                    const double depth=dkr_cam_triangle_push(origin,point,v[0],v[1],v[2],radius,&candidate);
                    if(depth>*answer){*answer=depth;*push=candidate;}
                }
            }
        }
    }
    return 1;
}

/* fields: game mode, level header, layout, cutscene flag, current level
   model, current racer camera, dialogue angle, current player index. */
static inline void dkr_cam_resolve(uint8_t* rdram,uint32_t object,const uint32_t fields[8]) {
    if(!dkr_cam_span(object,0x68,4)||dkr_cam_word(rdram,fields[0])!=0||
        MEM_BU(0,dkr_cam_addr(fields[3]))||MEM_H(0,dkr_cam_addr(fields[6]))||
        dkr_cam_word(rdram,fields[7])>3)return;
    const uint32_t header=dkr_cam_word(rdram,fields[1]),layout=dkr_cam_word(rdram,fields[2]);
    const uint32_t model=dkr_cam_word(rdram,fields[4]),cam=dkr_cam_word(rdram,fields[5]);
    if(!dkr_cam_span(header,0xA0,4)||!dkr_cam_span(model,0x40,4)||!dkr_cam_span(cam,0x44,4)||
        !dkr_world_projection_eligible(0,MEM_BU(0x4C,dkr_cam_addr(header)),layout,1)||
        !dkr_cam_follow_mode(MEM_H(0x36,dkr_cam_addr(cam))))return;
    const dkr_cam_vec racer=dkr_cam_position(rdram,object),desired=dkr_cam_position(rdram,cam);
    const dkr_cam_vec origin=dkr_cam_add(racer,dkr_cam_v(0,24,0));
    const double length2=dkr_cam_dot(dkr_cam_sub(desired,origin),dkr_cam_sub(desired,origin));
    if(!dkr_cam_finite(origin)||!dkr_cam_finite(desired)||length2<1||length2>1000000)return;
    unsigned budget=8192;double fraction;dkr_cam_vec push;
    const double radius=dkr_cam_authored_radius(layout,MEM_BU(0x9C,dkr_cam_addr(header)));
    if(!dkr_cam_query(rdram,model,origin,desired,radius,1,&budget,&fraction,&push))return;
    /* Conservative quantisation suppresses sub-ULP cross-platform contact
       differences. All changes remain in the already checkpointed Camera. */
    fraction=floor(fraction*4096)/4096;
    dkr_cam_vec corrected=dkr_cam_add(origin,dkr_cam_scale(dkr_cam_sub(desired,origin),fraction));
    int clear=0;
    for(unsigned pass=0;pass<5;++pass) {
        double depth;
        if(!dkr_cam_query(rdram,model,origin,corrected,radius,0,&budget,&depth,&push))return;
        if(depth<=0){clear=1;break;}
        if(pass==4)return; /* Tight spaces: preserve retail fallback, not oscillation. */
        corrected=dkr_cam_add(corrected,push);
    }
    if(!clear||!dkr_cam_finite(corrected)||dkr_cam_dot(dkr_cam_sub(corrected,desired),
        dkr_cam_sub(corrected,desired))<0.000001)return;
    /* Resolve the camera segment using retail trackGetBlock's XZ/height rule.
       Rendering and BSP selection see the same corrected camera; no late
       matrix-only camera move can expose a segment culled for the old view. */
    const uint32_t boxes=dkr_cam_word(rdram,model+8);
    const unsigned count=(unsigned)MEM_H(0x1A,dkr_cam_addr(model));
    int segment=-1;double nearest=1000000;
    const int x=(int)corrected.x,y=(int)corrected.y,z=(int)corrected.z;
    for(unsigned s=0;s<count;++s) {
        const uint32_t box=boxes+s*12;
        const dkr_cam_vec bl=dkr_cam_vertex(rdram,box),bh=dkr_cam_vertex(rdram,box+6);
        if(x>bl.x&&x<bh.x&&z>bl.z&&z<bh.z) {
            const double distance=fabs(y-(((int)bl.y+(int)bh.y)>>1));
            if(distance<nearest){nearest=distance;segment=(int)s;}
        }
    }
    dkr_cam_store(rdram,cam+12,corrected.x);dkr_cam_store(rdram,cam+16,corrected.y);
    dkr_cam_store(rdram,cam+20,corrected.z);
    if(segment>=0)MEM_H(0x34,dkr_cam_addr(cam))=(int16_t)segment;
    const float boom=dkr_cam_float(rdram,cam+0x1C);
    if(fraction<1&&isfinite(boom)&&boom>0)dkr_cam_store(rdram,cam+0x1C,boom*fraction);
    /* yaw/pitch/roll, racer position, velocity, controls, RNG, allocations,
       collision candidates and the original void-cover code are untouched. */
}
