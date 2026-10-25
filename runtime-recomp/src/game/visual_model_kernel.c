/* Compiled twice with NEW, isolated Patch Pipeline model-only output.
   Not linked into the rollback CPU. No native game import is reachable. */
#include "visual_model_addresses.h"
#include "funcs.h"
#include "visual_model_kernel.h"
#include <setjmp.h>
#include <string.h>
#include <fenv.h>

enum { ARENA=0xC10000, ARENA_END=0xD00000, OBJECT=0xC10000,
    HEADER=0xC10100, INSTANCE=0xC10200, SLOTS=0xC10300,
    VERTICES=0xC20000, VERTEX2=0xC40000, DELTAS=0xC60000,
    FRACTION=0xC80000, COMMANDS=0xC90000, COMMAND_END=0xCD0000,
    STACK=0xCF0000, MAX_VERTICES=8192, MAX_SHADOWS=256 };
/* Separate TLS for the two revisions, and separate from the owned CPU fence. */
#if defined(_MSC_VER)
#define VISUAL_TLS __declspec(thread)
#else
#define VISUAL_TLS _Thread_local
#endif
static VISUAL_TLS struct {
    uint8_t* ram; unsigned operations,shadows; int active;
    struct {uint32_t address,value;} shadow[MAX_SHADOWS];
    const char* failure; jmp_buf fence;
    fenv_t environment;
    uint32_t textures[128],texture_count;
} visual;
static void fail(const char* why) {visual.failure=why;longjmp(visual.fence,1);}
void dkr_visual_step(void) {
    if(!visual.active || ++visual.operations>2000000U)fail("model presentation operation budget");
}
static uint32_t offset(uint64_t address,unsigned width) {
    dkr_visual_step();
    if(address<UINT64_C(0xffffffff80000000) || address>UINT64_C(0xffffffff80ffffff) ||
       (width!=1 && width!=2 && width!=4))fail("model presentation pointer");
    const uint32_t p=(uint32_t)address-0x80000000U;
    if((p&(width-1)) || (p>=0x800000U && (p<ARENA || p>ARENA_END-width)))
        fail("model presentation memory bounds");
    return p;
}
void* dkr_visual_read(uint64_t address,unsigned width) {
    const uint32_t p=offset(address,width),word=p&~3U,lane=p^(width==1?3U:width==2?2U:0U);
    if(p<0x800000U)for(unsigned i=0;i<visual.shadows;++i)
        if(visual.shadow[i].address==word)return (uint8_t*)&visual.shadow[i].value+(lane&3U);
    return visual.ram+lane;
}
void dkr_visual_write(uint64_t address,unsigned width,uint32_t value) {
    const uint32_t p=offset(address,width),word=p&~3U,lane=p^(width==1?3U:width==2?2U:0U);
    uint8_t* destination;
    if((uint32_t)address==DKR_VISUAL_gCurrentTextureHeader && width==4 && value) {
        unsigned i=0;while(i<visual.texture_count && visual.textures[i]!=value)++i;
        if(i==visual.texture_count) {
            if(i==128)fail("model presentation texture budget");
            visual.textures[visual.texture_count++]=value;
        }
    }
    if(p>=ARENA) {
        const int private_span=(p>=OBJECT && p+width<=SLOTS+128) ||
            (p>=VERTICES && p+width<=VERTICES+MAX_VERTICES*10) ||
            (p>=VERTEX2 && p+width<=VERTEX2+MAX_VERTICES*10) ||
            (p>=DELTAS && p+width<=DELTAS+MAX_VERTICES*10) ||
            (p>=FRACTION && p+width<=COMMANDS) ||
            (p>=COMMANDS && p+width<=COMMAND_END) ||
            (p>=STACK-65536 && p+width<=STACK+64);
        if(!private_span)fail("model presentation private write span");
        destination=visual.ram+lane;
    }
    else {
        unsigned i=0;while(i<visual.shadows && visual.shadow[i].address!=word)++i;
        if(i==visual.shadows) {
            if(i==MAX_SHADOWS)fail("model presentation scalar budget");
            visual.shadow[i].address=word;memcpy(&visual.shadow[i].value,visual.ram+word,4);++visual.shadows;
        }
        destination=(uint8_t*)&visual.shadow[i].value+(lane&3U);
    }
    if(width==4)memcpy(destination,&value,4);
    else if(width==2){const uint16_t half=(uint16_t)value;memcpy(destination,&half,2);}
    else *destination=(uint8_t)value;
}
static uint64_t guest(uint32_t p){return (uint64_t)(int64_t)(int32_t)p;}
static void asset_span(uint32_t p,unsigned bytes,unsigned alignment) {
    if(p<0x80000000U || p>=0x80800000U || bytes>0x80800000U-p || (p&(alignment-1)))
        fail("model presentation asset ownership");
}
static uint32_t read32(uint32_t p){return *(uint32_t*)dkr_visual_read(guest(p),4);}
static uint16_t read16(uint32_t p){return *(uint16_t*)dkr_visual_read(guest(p),2);}
static uint8_t read8(uint32_t p){return *(uint8_t*)dkr_visual_read(guest(p),1);}
static void put(uint32_t p,unsigned width,uint32_t v){dkr_visual_write(guest(p),width,v);}
static void copy(uint32_t to,uint32_t from,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i)put(to+i,1,read8(from+i));
}
/* Exact cockpit-only correction, using the same independently checked model,
   batch, texture and vehicle identity as the ordinary port hook. */
void dkr_fix_car_steering_wheel_material(uint8_t* rdram,recomp_context* ctx) {
    (void)rdram;
    const uint32_t model=(uint32_t)ctx->r23,batch=(uint32_t)ctx->r2,tex=(uint32_t)ctx->r18;
    const uint32_t obj=read32((uint32_t)ctx->r29+0xB4),racer=read32(obj+0x64);
    if(read16(obj+0x48)!=1 || !racer || read8(racer+0x1D6)!=0 ||
       (uint32_t)ctx->r10+1U!=read16(model+0x28) || read8(batch)==255 ||
       (int32_t)ctx->r17!=4 || (int32_t)ctx->r21!=2 || (uint32_t)ctx->r8!=0x80FU ||
       read8(tex)!=16 || read8(tex+1)!=16 || (read8(tex+2)&15)!=0 ||
       (read16(tex+6)&0xC0)!=0xC0)return;
    const unsigned index=read8(batch),count=read16(model+0x22);const uint32_t table=read32(model);
    if(index<count && read32(table+index*8)==tex && read8(table+index*8+4)==16 &&
       read8(table+index*8+5)==16 && !(read8(table+index*8+6)&15))ctx->r8&=~(gpr)0x800;
}
int DKR_VISUAL_ENTRY(uint8_t* ram,size_t bytes,const dkr_visual_model_request* request,dkr_visual_model_result* result) {
    if(!result)return 0;memset(result,0,sizeof(*result));
    if(!ram || bytes<0x1000000U || !request || visual.active){result->failure="model presentation arguments";return 0;}
    if(fegetenv(&visual.environment)){result->failure="model floating environment";return 0;}
    visual.ram=ram;visual.operations=visual.shadows=visual.texture_count=0;visual.failure=0;visual.active=1;
    if(setjmp(visual.fence)) {result->failure=visual.failure;result->operations=visual.operations;visual.active=0;fesetenv(&visual.environment);return 0;}
    asset_span(request->object,0x80,4);asset_span(request->instance,0x24,4);
    const uint32_t instance=request->instance,model=read32(instance);
    asset_span(model,0x80,4);asset_span(read32(request->object+0x40),0x78,4);
    const unsigned count=read16(model+0x24),animated=read16(model+0x4A);
    if(!count || count>MAX_VERTICES || animated>MAX_VERTICES)fail("model presentation vertex budget");
    asset_span(read32(model+4),count*10,2);
    copy(0x80000000U+OBJECT,request->object,0x80);
    copy(0x80000000U+HEADER,read32(request->object+0x40),0x78);
    copy(0x80000000U+INSTANCE,instance,0x24);
    put(0x80000000U+OBJECT+0x40,4,0x80000000U+HEADER);
    put(0x80000000U+HEADER+0x55,1,1);
    put(0x80000000U+OBJECT+0x68,4,0x80000000U+SLOTS);
    put(0x80000000U+SLOTS,4,0x80000000U+INSTANCE);
    put(0x80000000U+OBJECT+0x3A,1,0);
    put(0x80000000U+OBJECT+0x3B,1,request->animation_id);
    put(0x80000000U+OBJECT+0x18,2,request->animation_frame);
    for(unsigned i=0;i<2;++i) {
        copy(0x80000000U+(i?VERTEX2:VERTICES),read32(model+4),count*10);
        put(0x80000000U+INSTANCE+4+i*4,4,0x80000000U+(i?VERTEX2:VERTICES));
    }
    put(0x80000000U+INSTANCE+12,4,0x80000000U+DELTAS);
    put(0x80000000U+INSTANCE+0x10,2,65535);
    put(0x80000000U+INSTANCE+0x1F,1,0);
    put(DKR_VISUAL_D_8011D644,4,0x80000000U+FRACTION);
    recomp_context ctx={0};ctx.f_odd=&ctx.f0.u32h;ctx.r29=guest(0x80000000U+STACK);ctx.r4=guest(0x80000000U+OBJECT);
    if(read8(instance+0x1E)==2 && read32(model+0x44))obj_animate(ram,&ctx);
    const uint32_t vertices=read32(0x80000000U+INSTANCE+4+read8(0x80000000U+INSTANCE+0x1F)*4);
    put(0x80000000U+OBJECT+0x44,4,vertices);
    if(read8(instance+0x1E)!=0 && read32(model+0x40) && read32(request->object+0x54)) {
        ctx.r4=guest(model);ctx.r5=guest(0x80000000U+OBJECT);ctx.r6=request->intensity;obj_shade_fast(ram,&ctx);
    }
    unsigned first=0,batches=read16(model+0x28);const uint32_t batch=read32(model+0x38);
    if(!batches || batches>1024)fail("model presentation batch budget");
    asset_span(batch,(batches+1)*12,4);
    const unsigned triangle_count=read16(model+0x26);
    if(triangle_count>16384)fail("model presentation face budget");
    asset_span(read32(model+8),triangle_count*16,2);
    for(unsigned i=0;i<batches;++i) {
        const unsigned a=read16(batch+i*12+2),b=read16(batch+(i+1)*12+2);
        const unsigned fa=read16(batch+i*12+4),fb=read16(batch+(i+1)*12+4);
        if(b<a || b>count || b-a>32 || fb<fa || fb>triangle_count || fb-fa>16 ||
           (request->head_override && (int8_t)read8(batch+i*12+1)>(int)(b-a)))
            fail("model presentation batch spans");
    }
    /* The selected low LOD received this frame's brake/night texture index.
       Transfer that presentation value to matching high-model batches using
       scalar shadows; never write either live model's material data. */
    if(read16(request->object+0x48)==1) {
        const uint32_t slots=read32(request->object+0x68);
        const uint32_t low=read32(read32(slots+read8(request->object+0x3A)*4));
        const unsigned low_count=read16(low+0x28);const uint32_t low_batch=read32(low+0x38);
        if(low_count>1024)fail("model presentation source batch budget");
        unsigned texture_offset=0;int have_offset=0;
        for(unsigned i=0;i<low_count;++i)if((read32(low_batch+i*12+8)&0x810000U)==0x10000U) {
            texture_offset=read8(low_batch+i*12+7);have_offset=1;break;
        }
        if(have_offset)for(unsigned i=0;i<batches;++i)if((read32(batch+i*12+8)&0x810000U)==0x10000U)
            put(batch+i*12+7,1,texture_offset);
    }
    if(request->transparent_tail)while(first<batches && !(read32(batch+first*12+8)&4U))++first;
    put(DKR_VISUAL_gObjectCurrDisplayList,4,0x80000000U+COMMANDS);
    put(DKR_VISUAL_gObjectTexAnim,4,0);put(DKR_VISUAL_gCurrentTextureHeader,4,0);
    put(DKR_VISUAL_gCurrentRenderFlags,4,0);put(DKR_VISUAL_gForceFlags,2,1);
    put(DKR_VISUAL_gUsingTexture,2,0);put(DKR_VISUAL_gBlockedRenderFlags,4,0);
    put(DKR_VISUAL_gUseDirectionalLighting,2,read8(0x80000000U+HEADER+0x71)!=0);
    ctx.r4=guest(model);ctx.r5=guest(0x80000000U+OBJECT);ctx.r6=first;ctx.r7=request->flags;
    put(0x80000000U+STACK+16,4,request->head_override);
    render_mesh(ram,&ctx);
    const uint32_t end=read32(DKR_VISUAL_gObjectCurrDisplayList)-0x80000000U;
    if(end<COMMANDS || end>COMMAND_END || ((end-COMMANDS)&7))fail("model presentation command budget");
    result->commands=COMMANDS;result->count=(end-COMMANDS)/8;
    result->vertices=vertices-0x80000000U;result->vertex_count=count;
    result->triangles=read32(model+8)-0x80000000U;result->triangle_count=triangle_count;
    for(unsigned i=0;i<4;++i)result->head[i]=(int16_t)read16(0x80000000U+INSTANCE+0x16+i*2);
    result->texture_count=visual.texture_count;
    result->operations=visual.operations;
    memcpy(result->textures,visual.textures,visual.texture_count*sizeof(uint32_t));
    visual.active=0;if(fesetenv(&visual.environment)){result->failure="model floating environment restore";return 0;}return 1;
}
