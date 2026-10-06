#include "recomp.h"
#include "probe_bridge.h"
#include "probe_presentation.h"
#include "../../src/game/camera_clearance_metadata.h"
#include <string.h>
#include <math.h>

/* Patch Pipeline hook adapter. Observations are private/checkpoint-owned;
   no local setting, native submission registry, scheduler or device is read. */
static uint32_t a[24];
static uint32_t presentation_addresses[8];
static unsigned depth,overflow,camera_identity,sky;
static struct {uint32_t object,identity,first,camera,projection[3];dkr_owned_lifetime* life;} objects[DKR_OWNED_OBJECT_DEPTH];
static unsigned wave,viewport,block,selection,selection_valid;
static uint32_t transition_matrix;
static uint32_t segment=UINT32_MAX,segment_pass;
static int segment_active,surface_active,shadow_active,billboard_active;
static unsigned hud_group,hud_layout,hud_owner,hud_element,hud_widget,hud_slide,hud_type,hud_trial,hud_general;
static uint32_t widget_holder,map_holder,rect_holder,pass_holder[2],text_holder,text_box;
static int widget_active,map_active,rect_active,pass_active[2],colour_changed,text_active,text_bias;
static uint32_t timer_widget=UINT32_MAX;
static gpr addr(uint32_t p) {return (gpr)(int32_t)p;}
static int valid(uint32_t p,unsigned bytes,unsigned alignment) {
    return p>=0x80000000U && p<=0x80800000U-bytes && (p&(alignment-1))==0;
}
static uint32_t read32(uint8_t* rdram,uint32_t p) {return (uint32_t)MEM_W(0,addr(p));}
static uint32_t command(uint8_t* rdram,uint32_t holder) {
    if(!valid(holder,4,4))return 0;
    const uint32_t p=read32(rdram,holder);return valid(p,8,8)?p:0;
}
static void marker(uint8_t* rdram,uint32_t holder,unsigned mode,unsigned token,unsigned variant,dkr_owned_emit emit) {
    uint32_t p=command(rdram,holder);if(!p)return;
    dkr_owned_draw_event e={DKR_OWNED_GEOMETRY,p,token,{0}};
    e.parameters[0]=mode;e.parameters[1]=variant;emit(&e);
}
static void world_projection(uint8_t* rdram,uint32_t out[3]) {
    memset(out,0,3*sizeof(*out));
    const uint32_t header=read32(rdram,a[8]);if(!valid(header,0xA0,4))return;
    float w[3];
    for(unsigned i=0;i<3;++i){const uint32_t bits=read32(rdram,a[23]+i*16+12);memcpy(&w[i],&bits,4);}
    const int perspective=isfinite(w[0])&&isfinite(w[1])&&isfinite(w[2])&&
        (w[0]*w[0]+w[1]*w[1]+w[2]*w[2])>0.01f;
    const uint32_t layout=read32(rdram,a[4]);
    if(!dkr_world_projection_eligible((int32_t)read32(rdram,presentation_addresses[1]),
        MEM_BU(0x4C,addr(header)),layout,perspective))return;
    const uint32_t authored=MEM_BU(0x9C,addr(header)),effective=read32(rdram,a[15]);
    if(!authored||!dkr_world_projection_metadata_valid(authored,effective,layout))return;
    out[0]=authored;out[1]=effective;out[2]=layout;
}
static void matrix(uint32_t p,uint32_t identity,unsigned flags,unsigned weak,uint32_t scene,unsigned tag,
    const uint32_t projection[3],dkr_owned_emit emit) {
    if(!valid(p,64,8))return;
    dkr_owned_draw_event e={DKR_OWNED_MATRIX,p,identity,{0}};
    e.parameters[0]=flags;e.parameters[1]=weak;e.parameters[2]=scene;e.parameters[3]=tag;
    if(projection)memcpy(e.parameters+4,projection,3*sizeof(*projection));emit(&e);
    if(sky){e.kind=DKR_OWNED_SKY_MATRIX;e.token=0;memset(e.parameters,0,sizeof(e.parameters));emit(&e);}
}
void dkr_probe_parity_begin(void) {
    dkr_probe_parity_addresses(a);dkr_probe_presentation_addresses(presentation_addresses);depth=overflow=camera_identity=sky=0;
    wave=viewport=block=selection=selection_valid=0;segment=UINT32_MAX;transition_matrix=0;
    segment_active=surface_active=shadow_active=billboard_active=0;
    widget_active=map_active=rect_active=colour_changed=text_active=text_bias=0;
    pass_active[0]=pass_active[1]=0;hud_general=0;timer_widget=UINT32_MAX;hud_group=0;
}
void dkr_probe_parity_scene(dkr_owned_identity_state* s) {
    uint32_t scene=s->scene+1;if(!scene)scene=1;
    memset(s,0,sizeof(*s));s->scene=scene;s->next_generation=s->next_token=1;
}
static dkr_owned_lifetime* life(dkr_owned_identity_state* s,uint32_t object,int create) {
    if(!valid(object,0x68,4))return 0;
    dkr_owned_lifetime* empty=0;
    const unsigned start=((object>>3)*2654435761U)&(DKR_OWNED_LIFETIMES-1);
    for(unsigned probe=0;probe<DKR_OWNED_LIFETIMES;++probe) {
        dkr_owned_lifetime* entry=&s->lifetimes[(start+probe)&(DKR_OWNED_LIFETIMES-1)];
        if(entry->object==object){if(entry->generation)return entry;empty=entry;break;}
        if(!entry->generation&&!empty)empty=entry;
        if(!entry->object)break; // Freed entries keep a tombstone for the probe chain.
    }
    if(!create||!empty)return 0; // Unknown owners remain discrete; never halt gameplay.
    if(!s->next_generation)s->next_generation=1;
    if(!s->next_token)s->next_token=1;
    memset(empty,0,sizeof(*empty));empty->object=object;empty->generation=s->next_generation++;
    empty->token=s->next_token<=65535?s->next_token++:0;return empty;
}
void dkr_probe_parity_lifetime(dkr_owned_identity_state* s,uint32_t object,int spawn) {
    dkr_owned_lifetime* l=life(s,object,spawn);
    if(!l)return;
    if(!spawn){l->generation=l->token=0;memset(l->attachments,0,sizeof(l->attachments));return;}
    /* Some render paths first observe an object after its spawn callback.
       A real spawn always starts a new lifetime, even when RAM was reused. */
    memset(l->attachments,0,sizeof(l->attachments));
    l->generation=s->next_generation++;l->token=s->next_token<=65535?s->next_token++:0;
}
static void camera(uint8_t* rdram,uint32_t ref,unsigned role,dkr_owned_identity_state* s,uint32_t frame,dkr_owned_emit emit) {
    if(!valid(ref,4,4))return;
    uint32_t p=read32(rdram,ref);if(!valid(p,64,8))return;
    int slot=(int32_t)read32(rdram,a[14]);
    if(slot<0||slot>7)return;
    if(MEM_BU(0,addr(a[16]))) {if(slot>3)return;slot+=4;}
    uint32_t base=a[13]+(unsigned)slot*0x44;dkr_owned_camera* c=&s->cameras[slot];
    uint32_t sample[4]={read32(rdram,base+0xC),read32(rdram,base+0x10),read32(rdram,base+0x14),read32(rdram,a[15])};
    uint32_t mode=(uint32_t)(int32_t)MEM_H(0x36,addr(base));
    if(!c->valid){c->valid=1;c->epoch=1;}
    else if(dkr_probe_finish_shot(mode,c->mode,c->owner,c->node,c->shot_owner,c->shot_node)||
        (frame!=c->frame&&dkr_probe_camera_discontinuous(c->position,sample))) {if(!++c->epoch)c->epoch=1;}
    memcpy(c->position,sample,sizeof(sample));c->frame=frame;
    /* FinishCameraShot stores zero observations outside its finish camera. */
    c->shot_owner=mode==7?c->owner:0;c->shot_node=mode==7?c->node:0;c->mode=mode;
    camera_identity=dkr_probe_camera_identity(s->scene,(unsigned)slot,c->epoch,2);
    uint32_t projection[3]={0};if(role==1)world_projection(rdram,projection);
    matrix(p,dkr_probe_camera_identity(s->scene,(unsigned)slot,c->epoch,role),0,0,s->scene,0,projection,emit);
}
static void hud_frame(uint8_t* rdram) {
    hud_layout=read32(rdram,a[5]);unsigned players=MEM_BU(0,addr(a[6])),session=read32(rdram,a[7]);
    hud_group=(players||session)&&players<=2&&session<=2&&hud_layout<=1&&read32(rdram,a[4])<=1;
    uint32_t header=read32(rdram,a[8]);if(!valid(header,0x50,4)){hud_group=0;return;}
    hud_type=MEM_BU(0x4C,addr(header));hud_trial=MEM_BU(0,addr(a[10]))!=0;
    hud_slide=read32(rdram,a[9]);
}
static unsigned element_index(uint8_t* rdram,uint32_t element,unsigned* owner) {
    *owner=UINT32_MAX;
    for(unsigned i=0;i<4;++i) {
        uint32_t base=read32(rdram,a[0]+i*4);
        if(valid(base,59*32,4)&&element>=base&&element<base+59*32&&(element-base)%32==0){*owner=i;return (element-base)/32;}
    }
    uint32_t base=read32(rdram,a[1]);
    if(valid(base,59*32,4)&&element>=base&&element<base+59*32&&(element-base)%32==0)return (element-base)/32;
    return 59;
}
static int widget_begin(uint8_t* rdram,uint32_t holder,unsigned widget,unsigned owner,unsigned element,
    uint32_t x,unsigned slides,unsigned quadrant,dkr_owned_emit emit) {
    uint32_t p=command(rdram,holder);if(!p||owner>=4)return 0;
    float authored_x;memcpy(&authored_x,&x,sizeof(authored_x));
    if(!isfinite(authored_x))return 0;
    dkr_owned_draw_event e={DKR_OWNED_HUD_WIDGET,p,1,{0}};
    e.parameters[0]=hud_layout;e.parameters[1]=owner;e.parameters[2]=element;e.parameters[3]=widget;
    e.parameters[4]=hud_type;e.parameters[5]=hud_trial;e.parameters[6]=hud_general;
    e.parameters[7]=x;e.parameters[8]=hud_slide;e.parameters[9]=slides;e.parameters[10]=quadrant;emit(&e);return 1;
}
static void widget_end(uint8_t* rdram,uint32_t holder,int* active,dkr_owned_emit emit) {
    if(*active){uint32_t p=command(rdram,holder);if(p){dkr_owned_draw_event e={DKR_OWNED_HUD_WIDGET,p,0,{0}};emit(&e);}}
    *active=0;
}
static void hud_pass(uint8_t* rdram,uint32_t holder,unsigned begin,unsigned variant,dkr_owned_emit emit) {
    uint32_t p=command(rdram,holder);if(!p)return;
    dkr_owned_draw_event e={DKR_OWNED_HUD_PASS,p,variant,{0}};e.parameters[0]=begin;emit(&e);
}
static int rect_mark(uint8_t* rdram,uint32_t holder,unsigned begin,unsigned axes,dkr_owned_emit emit) {
    uint32_t p=command(rdram,holder);if(!p)return 0;
    dkr_owned_draw_event e={DKR_OWNED_HUD_RECT,p,begin,{0}};e.parameters[0]=axes;emit(&e);return 1;
}
int dkr_probe_parity_hook(const char* name,uint8_t* rdram,recomp_context* ctx,dkr_owned_identity_state* s,uint32_t frame,dkr_owned_emit emit) {
    if(strcmp(name,"dkr_transition_cover_begin")==0) {
        const uint32_t ref=(uint32_t)ctx->r17;transition_matrix=valid(ref,4,4)?read32(rdram,ref):0;
        marker(rdram,(uint32_t)ctx->r16,1,0,0,emit);return 1;
    }
    if(strcmp(name,"dkr_transition_cover_end")==0) {
        if(valid(transition_matrix,64,8)){dkr_owned_draw_event e={DKR_OWNED_TRANSITION,transition_matrix,0,{0}};emit(&e);}transition_matrix=0;return 1;
    }
    if(strcmp(name,"dkr_transition_interpolation_end")==0){marker(rdram,(uint32_t)ctx->r16,0,0,0,emit);return 1;}
    if(strcmp(name,"dkr_presentation_perspective_matrix")==0){camera(rdram,(uint32_t)ctx->r4,0,s,frame,emit);return 1;}
    if(strcmp(name,"dkr_presentation_world_origin_matrix")==0){camera(rdram,(uint32_t)ctx->r16,1,s,frame,emit);return 1;}
    if(strcmp(name,"dkr_presentation_finish_camera_node")==0) {
        uint32_t ref=(uint32_t)ctx->r16;
        if(valid(ref,4,4)){uint32_t p=read32(rdram,ref);if(p>=a[13]&&(p-a[13])%0x44==0&&(p-a[13])/0x44<8){dkr_owned_camera* c=&s->cameras[(p-a[13])/0x44];c->owner=(uint32_t)ctx->r7;c->node=(uint32_t)ctx->r4;}}
        return 1;
    }
    if(strcmp(name,"dkr_skybox_cover_begin")==0){sky=1;return 1;}
    if(strcmp(name,"dkr_skybox_cover_end")==0){sky=0;return 1;}
    if(strcmp(name,"dkr_presentation_object_begin")==0) {
        if(depth==DKR_OWNED_OBJECT_DEPTH){++overflow;return 1;}
        unsigned n=depth++;memset(&objects[n],0,sizeof(objects[n]));
        uint32_t object=(uint32_t)ctx->r7;dkr_owned_lifetime* l=life(s,object,1);if(!l)return 1;
        uint32_t ref=(uint32_t)MEM_W(0x24,ctx->r29);if(!valid(ref,4,4))return 1;
        uint32_t first=read32(rdram,ref);if(!valid(first,64,8))return 1;
        objects[n].object=object;objects[n].life=l;objects[n].first=first;objects[n].camera=camera_identity;
        world_projection(rdram,objects[n].projection);
        objects[n].identity=dkr_probe_object_identity(s->scene,object,l->generation,MEM_HU(0x4A,addr(object)),MEM_HU(0x48,addr(object)));return 1;
    }
    if(strcmp(name,"dkr_presentation_object_end")==0) {
        if(overflow){--overflow;return 1;}if(!depth)return 1;unsigned n=--depth;
        uint32_t end=read32(rdram,a[12]),first=objects[n].first;
        if(!objects[n].identity||end<first||(end-first)%64||(end-first)/64>256)return 1;
        for(unsigned i=0;i<(end-first)/64;++i)matrix(first+i*64,dkr_probe_matrix_identity(objects[n].identity,i,objects[n].camera),0,1,s->scene,0,objects[n].projection,emit);return 1;
    }
    if(strcmp(name,"dkr_presentation_wave_begin")==0){wave=1;viewport=(uint32_t)ctx->r6;block=selection_valid=0;return 1;}
    if(strcmp(name,"dkr_presentation_wave_end")==0){wave=block=selection_valid=0;return 1;}
    if(strcmp(name,"dkr_presentation_wave_block")==0){block=valid((uint32_t)ctx->r2,0x1C,4)?(uint32_t)ctx->r2:0;return 1;}
    if(strcmp(name,"dkr_presentation_wave_selection")==0){selection=(uint32_t)MEM_W(0x104,ctx->r29)&255;selection_valid=selection<=25;return 1;}
    if(strcmp(name,"dkr_presentation_wave_matrix")==0) {
        unsigned selected=selection_valid;selection_valid=0;uint32_t ref=(uint32_t)ctx->r5,t=(uint32_t)ctx->r6;
        if(wave&&block&&selected&&valid(ref,4,4)&&valid(t,16,4)){
            uint32_t projection[3];world_projection(rdram,projection);
            uint32_t f[7]={read32(rdram,a[17]),read32(rdram,a[17]+0x28),selection,read32(rdram,t+0xC),read32(rdram,t),read32(rdram,t+4),read32(rdram,t+8)};
            matrix(read32(rdram,ref),dkr_probe_wave_identity(s->scene,viewport,block,f,camera_identity),15,0,s->scene,
                dkr_probe_water_tag(read32(rdram,a[18]),read32(rdram,a[19])),projection,emit);}
        return 1;
    }
    if(strcmp(name,"dkr_level_segment_interpolation_begin")==0){segment=(uint32_t)ctx->r4;segment_pass=ctx->r5!=0;segment_active=0;return 1;}
    if(strcmp(name,"dkr_surface_interpolation_begin")==0) {
        uint32_t f[2];surface_active=0;
        if(!segment_active&&segment!=UINT32_MAX){dkr_probe_geometry_key(s->scene,0,segment,segment_pass,f);if(f[0]){marker(rdram,a[11],9,f[0],f[1],emit);segment_active=1;}}
        if(((uint32_t)ctx->r17&0x2000)&&valid((uint32_t)ctx->r5,12,4)) {dkr_probe_geometry_key(s->scene,(uint32_t)ctx->r5,UINT32_MAX,0,f);marker(rdram,a[11],7,f[0],f[1],emit);surface_active=1;}return 1;
    }
    if(strcmp(name,"dkr_surface_interpolation_end")==0){if(surface_active)marker(rdram,a[11],0,0,0,emit);surface_active=0;return 1;}
    if(strcmp(name,"dkr_level_segment_interpolation_end")==0){if(segment_active)marker(rdram,a[11],0,0,0,emit);segment=UINT32_MAX;segment_active=0;return 1;}
    if(strcmp(name,"dkr_billboard_interpolation_begin")==0||strcmp(name,"dkr_vehicle_part_interpolation_begin")==0) {
        billboard_active=0;if(!depth||overflow||!objects[depth-1].life)return 1;
        uint32_t sprite=(uint32_t)MEM_W(0x70,ctx->r29);if(!valid(sprite,4,4))return 1;
        unsigned token=objects[depth-1].life->token,variant=dkr_probe_address_variant(sprite),mode=6;
        if(strcmp(name,"dkr_vehicle_part_interpolation_begin")==0){uint32_t ref=(uint32_t)ctx->r6;if(!valid(ref,4,4))return 1;
            uint32_t p=read32(rdram,ref),first=objects[depth-1].first;if(p<first||(p-first)%64||(p-first)/64>=32)return 1;
            unsigned slot=(p-first)/64;uint32_t* attachment=&objects[depth-1].life->attachments[slot];
            if(!*attachment&&s->next_token<=65535)*attachment=s->next_token++;
            token=*attachment;variant=dkr_probe_part_variant((uint32_t)ctx->r18,MEM_HU(0,addr(sprite)));mode=4;}
        if(token){marker(rdram,(uint32_t)ctx->r17,mode,token,variant,emit);billboard_active=1;}return 1;
    }
    if(strcmp(name,"dkr_vehicle_part_matrix_identity")==0) {
        if(depth&&!overflow&&objects[depth-1].life){uint32_t ref=(uint32_t)MEM_W(0x64,ctx->r29);if(valid(ref,4,4))matrix(read32(rdram,ref),dkr_probe_part_identity(objects[depth-1].identity,0,(uint32_t)ctx->r16,MEM_W(0x30,ctx->r29)==0,objects[depth-1].camera),0,0,s->scene,0,objects[depth-1].projection,emit);}return 1;
    }
    if(strcmp(name,"dkr_vehicle_part_interpolation_end")==0){if(billboard_active)marker(rdram,(uint32_t)ctx->r17,0,0,0,emit);billboard_active=0;return 1;}
    if(strcmp(name,"dkr_shadow_interpolation_begin")==0) {
        uint32_t object=(uint32_t)ctx->r6,shadow=(uint32_t)ctx->r7;shadow_active=0;
        dkr_owned_lifetime* l=life(s,object,1);uint32_t p=command(rdram,a[11]);
        if(p){dkr_owned_draw_event e={DKR_OWNED_SHADOW,p,0,{0}};
            if(l&&valid(shadow,12,4)){int start=MEM_H(8,addr(shadow)),end=MEM_H(0xA,addr(shadow));
                if(start>=0&&end>start&&end<=400){e.token=l->token;e.parameters[0]=(unsigned)(end-start)>16?16:(unsigned)(end-start);
                    e.parameters[1]=MEM_HU(0x48,addr(object));e.parameters[2]=MEM_HU(0x4A,addr(object));
                    e.parameters[3]=UINT32_MAX;
                    uint32_t racer=read32(rdram,object+0x64);if(e.parameters[1]==1&&valid(racer,0x1D8,4))e.parameters[3]=(uint32_t)(int32_t)MEM_B(0x1D6,addr(racer));
                    uint32_t texture=read32(rdram,shadow+4);if(valid(texture,4,4))e.parameters[4]=MEM_BU(0,addr(texture));
                    e.parameters[5]=read32(rdram,object+0xC);e.parameters[6]=read32(rdram,object+0x10);e.parameters[7]=read32(rdram,object+0x14);e.parameters[8]=(uint32_t)(int32_t)MEM_H(2,addr(object));}}
            emit(&e);shadow_active=1;}return 1;
    }
    if(strcmp(name,"dkr_shadow_interpolation_end")==0){if(shadow_active)marker(rdram,a[11],0,0,0,emit);shadow_active=0;return 1;}
    if(strcmp(name,"dkr_hud_player_pass_begin")==0||strcmp(name,"dkr_hud_general_pass_begin")==0) {
        hud_frame(rdram);unsigned general=strcmp(name,"dkr_hud_general_pass_begin")==0;
        pass_holder[general]=(uint32_t)MEM_W(general?0x160:0x30,ctx->r29);pass_active[general]=hud_group;
        if(hud_group)hud_pass(rdram,pass_holder[general],1,3,emit);if(general)hud_general=1;return 1;
    }
    if(strcmp(name,"dkr_hud_player_pass_end")==0||strcmp(name,"dkr_hud_general_pass_end")==0) {
        unsigned general=strcmp(name,"dkr_hud_general_pass_end")==0;
        if(pass_active[general])hud_pass(rdram,pass_holder[general],0,0,emit);pass_active[general]=0;if(general)hud_general=0;return 1;
    }
    if(strcmp(name,"dkr_hud_element_begin")==0) {
        hud_frame(rdram);uint32_t raw=(uint32_t)ctx->r7;widget_active=colour_changed=0;if(!valid(raw,32,4))return 1;
        hud_element=element_index(rdram,raw,&hud_owner);uint32_t f[4];dkr_probe_hud_element(hud_element,f);hud_widget=f[0];
        if(hud_widget==UINT32_MAX&&timer_widget!=UINT32_MAX){hud_widget=timer_widget;element_index(rdram,read32(rdram,a[1]),&hud_owner);}
        widget_holder=(uint32_t)ctx->r4;
        if(hud_group&&hud_widget!=UINT32_MAX)widget_active=widget_begin(rdram,widget_holder,hud_widget,hud_owner,hud_element,read32(rdram,raw+0xC),MEM_H(6,addr(raw))!=40,0,emit);
        else if((hud_layout==2||hud_layout==3)&&hud_owner<4&&(hud_element==0||hud_element==1||hud_element==2||hud_element==49)){
            unsigned side=hud_owner&1?2:1,item=hud_element==2||hud_element==49;
            hud_pass(rdram,widget_holder,1,item?0:side,emit);
            widget_active=widget_begin(rdram,widget_holder,hud_widget,hud_owner,hud_element,read32(rdram,raw+0xC),0,item?side:0,emit);}
        /* Make qualified unit textures immediate INSIDE their semantic scope.
           Keep the native scaled-animation sentinel to retain alpha. */
        if(widget_active&&read32(rdram,raw+8)==0x3F800000&&read32(rdram,a[3])==0xFFFFFFFE){MEM_W(0,addr(a[3]))=0xFFFFFFFF;colour_changed=1;}
        return 1;
    }
    if(strcmp(name,"dkr_hud_element_end")==0) {
        widget_end(rdram,widget_holder,&widget_active,emit);
        if((hud_layout==2||hud_layout==3)&&hud_owner<4&&(hud_element==0||hud_element==1||hud_element==2||hud_element==49))hud_pass(rdram,widget_holder,0,0,emit);
        if(colour_changed)MEM_W(0,addr(a[3]))=0xFFFFFFFE;colour_changed=0;return 1;
    }
    if(strcmp(name,"dkr_hud_minimap_begin")==0){hud_frame(rdram);map_holder=a[2];uint32_t f[4];
        dkr_probe_hud_element(15,f);map_active=hud_group&&widget_begin(rdram,map_holder,f[0],0,59,0,1,0,emit);return 1;}
    if(strcmp(name,"dkr_hud_minimap_end")==0){widget_end(rdram,map_holder,&map_active,emit);return 1;}
    if(strcmp(name,"dkr_hud_rect_extent")==0){if(rect_active)ctx->r2=0x04000400;return 1;}
    if(strcmp(name,"dkr_hud_rect_end")==0){if(rect_active)rect_mark(rdram,rect_holder,0,3,emit);rect_active=0;return 1;}
    if(strcmp(name,"dkr_hud_timer_end")==0){timer_widget=UINT32_MAX;return 1;}
    if(strcmp(name,"dkr_hud_text_begin")==0) {
        text_active=text_bias=0;if(!hud_group)return 1;
        uint32_t ra=(uint32_t)ctx->r31,stopwatch[6]={0x800A3550,0x800A358C,0x800A35C8,0x800A3620,0x800A3658,0x800A3690},finish[3]={0x800A608C,0x800A6168,0x800A6244};
        unsigned slot=UINT32_MAX;for(unsigned i=0;i<6;++i)if(ra==stopwatch[i]+(DKR_PROBE_REVISION==80?0x548:0))slot=34;
        for(unsigned i=0;i<3;++i)if(ra==finish[i]+(DKR_PROBE_REVISION==80?0x548:0))slot=56;
        if(slot==UINT32_MAX)return 1;uint32_t f[4];dkr_probe_hud_element(slot,f);element_index(rdram,read32(rdram,a[1]),&hud_owner);text_holder=(uint32_t)ctx->r4;
        text_active=widget_begin(rdram,text_holder,f[0],hud_owner,59,0,1,0,emit);
        text_box=read32(rdram,DKR_PROBE_REVISION==77?0x8012A7E8U:0x8012ADA8U);
        int x=(int32_t)ctx->r5;if(text_active&&x>=-200&&x<=700&&valid(text_box,4,2)&&rect_mark(rdram,text_holder,1,1,emit)){ctx->r5=ADD32(ctx->r5,256);text_bias=1;}return 1;
    }
    if(strcmp(name,"dkr_hud_text_end")==0){if(text_bias){MEM_H(0,addr(text_box))=MEM_H(0,addr(text_box))-256;rect_mark(rdram,text_holder,0,1,emit);}text_bias=0;widget_end(rdram,text_holder,&text_active,emit);return 1;}
    return 0;
}
int dkr_probe_parity_args(const char* name,uint8_t* rdram,recomp_context* ctx,const uint32_t* arguments,unsigned count,dkr_owned_emit emit) {
    if(count!=1)return 0;
    if(strcmp(name,"dkr_hud_timer_select")==0){unsigned f[4];dkr_probe_hud_element(arguments[0]==0?10:arguments[0]==1?23:56,f);timer_widget=f[0];return 1;}
    if(strcmp(name,"dkr_hud_rect_begin")==0){rect_active=0;if(!widget_active)return 1;
        float x,y;uint32_t xb=(uint32_t)ctx->r6,yb=(uint32_t)ctx->r7;
        if(arguments[0]){memcpy(&x,&xb,4);memcpy(&y,&yb,4);}else{x=(float)(int32_t)xb;y=(float)(int32_t)yb;}
        if(!isfinite(x)||!isfinite(y)||x< -200||x>700||y< -100||y>600)return 1;
        rect_holder=(uint32_t)ctx->r4;if(!rect_mark(rdram,rect_holder,1,3,emit))return 1;rect_active=1;
        if(arguments[0]){x+=256;y+=128;memcpy(&xb,&x,4);memcpy(&yb,&y,4);ctx->r6=addr(xb);ctx->r7=addr(yb);}else{ctx->r6=ADD32(ctx->r6,256);ctx->r7=ADD32(ctx->r7,128);}return 1;
    }
    return 0;
}
