#pragma once

/* Shared by the native and checkpoint-owned Patch Pipeline adapters. No
   window, settings, RNG, allocation or persistent native state is consulted. */
#include <math.h>
#include <stdint.h>

#ifdef __cplusplus
#define DKR_CAM_CONSTEXPR constexpr
#else
#define DKR_CAM_CONSTEXPR
#endif

typedef struct dkr_cam_vec { double x, y, z; } dkr_cam_vec;

static inline DKR_CAM_CONSTEXPR dkr_cam_vec dkr_cam_v(double x,double y,double z) {
    dkr_cam_vec p={x,y,z};return p;
}
static inline DKR_CAM_CONSTEXPR dkr_cam_vec dkr_cam_sub(dkr_cam_vec a,dkr_cam_vec b) {
    return dkr_cam_v(a.x-b.x,a.y-b.y,a.z-b.z);
}
static inline DKR_CAM_CONSTEXPR dkr_cam_vec dkr_cam_add(dkr_cam_vec a,dkr_cam_vec b) {
    return dkr_cam_v(a.x+b.x,a.y+b.y,a.z+b.z);
}
static inline DKR_CAM_CONSTEXPR dkr_cam_vec dkr_cam_scale(dkr_cam_vec a,double s) {
    return dkr_cam_v(a.x*s,a.y*s,a.z*s);
}
static inline DKR_CAM_CONSTEXPR double dkr_cam_dot(dkr_cam_vec a,dkr_cam_vec b) {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
static inline DKR_CAM_CONSTEXPR dkr_cam_vec dkr_cam_cross(dkr_cam_vec a,dkr_cam_vec b) {
    return dkr_cam_v(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);
}
static inline DKR_CAM_CONSTEXPR int dkr_cam_follow_mode(int mode) {
    return mode==0 || mode==1 || mode==4; /* car, plane, hovercraft only */
}
static inline DKR_CAM_CONSTEXPR int dkr_cam_solid_batch(uint32_t flags,int surface) {
    /* Match retail non-vehicle visibility/collision exclusions. Animated
       water and decals are not walls; frozen water remains solid. */
    return !(flags&(0x100U|0x200U|0x800U|0x2000U)) &&
        surface!=11 && surface!=14 && surface!=15 && surface!=18;
}
static inline DKR_CAM_CONSTEXPR double dkr_cam_radius(unsigned layout) {
    /* Fixed canonical clearance, including the larger horizontal 2P near
       rectangle. The presentation near-footprint correction contains local
       widescreen/FOV growth; it must not enter this shared camera decision. */
    return layout==1 ? 21.0 : 15.0;
}
static inline double dkr_cam_authored_radius(unsigned layout,unsigned fov) {
    if(fov<1||fov>=120)return dkr_cam_radius(layout);
    const double aspect=layout==1 ? 8.0/3.0 : 4.0/3.0;
    const double tangent=tan(fov*(3.14159265358979323846/360.0));
    const double footprint=sqrt(100*(1+tangent*tangent*(1+aspect*aspect)));
    return fmax(dkr_cam_radius(layout),ceil(footprint*8)/8+1.0);
}
static inline int dkr_cam_finite(dkr_cam_vec p) {
    return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z);
}
static inline int dkr_cam_inside(dkr_cam_vec p,dkr_cam_vec a,dkr_cam_vec b,dkr_cam_vec c) {
    const dkr_cam_vec u=dkr_cam_sub(b,a),v=dkr_cam_sub(c,a),w=dkr_cam_sub(p,a);
    const double uu=dkr_cam_dot(u,u),uv=dkr_cam_dot(u,v),vv=dkr_cam_dot(v,v);
    const double wu=dkr_cam_dot(w,u),wv=dkr_cam_dot(w,v),den=uu*vv-uv*uv;
    if(den<=1e-8)return 0;
    const double s=(vv*wu-uv*wv)/den,t=(uu*wv-uv*wu)/den;
    return s>=-1e-8 && t>=-1e-8 && s+t<=1.00000001;
}
static inline dkr_cam_vec dkr_cam_closest(dkr_cam_vec p,dkr_cam_vec a,dkr_cam_vec b,dkr_cam_vec c) {
    const dkr_cam_vec ab=dkr_cam_sub(b,a),ac=dkr_cam_sub(c,a),ap=dkr_cam_sub(p,a);
    const double d1=dkr_cam_dot(ab,ap),d2=dkr_cam_dot(ac,ap);
    if(d1<=0&&d2<=0)return a;
    const dkr_cam_vec bp=dkr_cam_sub(p,b);
    const double d3=dkr_cam_dot(ab,bp),d4=dkr_cam_dot(ac,bp);
    if(d3>=0&&d4<=d3)return b;
    const double vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0)return dkr_cam_add(a,dkr_cam_scale(ab,d1/(d1-d3)));
    const dkr_cam_vec cp=dkr_cam_sub(p,c);
    const double d5=dkr_cam_dot(ab,cp),d6=dkr_cam_dot(ac,cp);
    if(d6>=0&&d5<=d6)return c;
    const double vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0)return dkr_cam_add(a,dkr_cam_scale(ac,d2/(d2-d6)));
    const double va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0)
        return dkr_cam_add(b,dkr_cam_scale(dkr_cam_sub(c,b),(d4-d3)/((d4-d3)+(d5-d6))));
    const double den=va+vb+vc;
    return dkr_cam_add(a,dkr_cam_add(dkr_cam_scale(ab,vb/den),dkr_cam_scale(ac,vc/den)));
}
static inline double dkr_cam_root(double aa,double bb,double cc) {
    /* An initial overlap moving outward is resolved at the final camera,
       rather than trapping a racer that is driving alongside a wall. */
    if(cc<=0)return bb<0 ? 0.0 : 2.0;
    if(aa<=1e-12 || bb>=0)return 2.0;
    const double disc=bb*bb-4*aa*cc;
    if(disc<0)return 2.0;
    const double t=(-bb-sqrt(disc))/(2*aa);
    return t>=0&&t<=1 ? t : 2.0;
}
static inline double dkr_cam_edge_hit(dkr_cam_vec origin,dkr_cam_vec velocity,
    dkr_cam_vec a,dkr_cam_vec b,double radius) {
    const dkr_cam_vec edge=dkr_cam_sub(b,a),q=dkr_cam_sub(origin,a);
    const double ee=dkr_cam_dot(edge,edge);
    if(ee<=1e-8)return 2.0;
    const double qe=dkr_cam_dot(q,edge)/ee,ve=dkr_cam_dot(velocity,edge)/ee;
    const dkr_cam_vec qp=dkr_cam_sub(q,dkr_cam_scale(edge,qe));
    const dkr_cam_vec vp=dkr_cam_sub(velocity,dkr_cam_scale(edge,ve));
    const double t=dkr_cam_root(dkr_cam_dot(vp,vp),2*dkr_cam_dot(qp,vp),dkr_cam_dot(qp,qp)-radius*radius);
    const double along=qe+t*ve;
    return t<=1&&along>=0&&along<=1 ? t : 2.0;
}
static inline double dkr_cam_triangle_hit(dkr_cam_vec origin,dkr_cam_vec target,
    dkr_cam_vec a,dkr_cam_vec b,dkr_cam_vec c,double radius) {
    const dkr_cam_vec cross=dkr_cam_cross(dkr_cam_sub(b,a),dkr_cam_sub(c,a));
    const double nn=dkr_cam_dot(cross,cross);
    if(nn<=1e-8)return 2.0;
    dkr_cam_vec normal=dkr_cam_scale(cross,1/sqrt(nn));
    const dkr_cam_vec velocity=dkr_cam_sub(target,origin);
    double d=dkr_cam_dot(dkr_cam_sub(origin,a),normal);
    if(d<0){normal=dkr_cam_scale(normal,-1);d=-d;}
    const double speed=dkr_cam_dot(velocity,normal);
    double best=2.0;
    if(speed< -1e-10) {
        const double t=d>radius ? (radius-d)/speed : 0.0;
        if(t>=0&&t<=1) {
            const dkr_cam_vec centre=dkr_cam_add(origin,dkr_cam_scale(velocity,t));
            const double distance=dkr_cam_dot(dkr_cam_sub(centre,a),normal);
            if(dkr_cam_inside(dkr_cam_sub(centre,dkr_cam_scale(normal,distance)),a,b,c))best=t;
        }
    }
    const dkr_cam_vec points[3]={a,b,c};
    for(unsigned i=0;i<3;++i) {
        const double edge=dkr_cam_edge_hit(origin,velocity,points[i],points[(i+1)%3],radius);
        if(edge<best)best=edge;
        const dkr_cam_vec q=dkr_cam_sub(origin,points[i]);
        const double vertex=dkr_cam_root(dkr_cam_dot(velocity,velocity),2*dkr_cam_dot(q,velocity),
            dkr_cam_dot(q,q)-radius*radius);
        if(vertex<best)best=vertex;
    }
    return best;
}
static inline double dkr_cam_triangle_push(dkr_cam_vec origin,dkr_cam_vec point,
    dkr_cam_vec a,dkr_cam_vec b,dkr_cam_vec c,double radius,dkr_cam_vec* push) {
    const dkr_cam_vec cross=dkr_cam_cross(dkr_cam_sub(b,a),dkr_cam_sub(c,a));
    const double nn=dkr_cam_dot(cross,cross);
    if(nn<=1e-8)return 0;
    const dkr_cam_vec closest=dkr_cam_closest(point,a,b,c),delta=dkr_cam_sub(point,closest);
    const double dd=dkr_cam_dot(delta,delta);
    if(dd>=(radius-0.001)*(radius-0.001))return 0;
    const double distance=sqrt(dd),depth=radius-distance+0.01;
    dkr_cam_vec direction;
    if(distance>1e-6)direction=dkr_cam_scale(delta,1/distance);
    else {
        direction=dkr_cam_scale(cross,1/sqrt(nn));
        if(dkr_cam_dot(dkr_cam_sub(origin,a),direction)<0)direction=dkr_cam_scale(direction,-1);
    }
    *push=dkr_cam_scale(direction,depth);return depth;
}

#undef DKR_CAM_CONSTEXPR
