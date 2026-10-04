#include "funcs.h"
#include "probe_pak.h"
#include "probe_input.h"
static unsigned frame,value;
static gpr address(uint32_t p) {return (int32_t)p;}
void dkr_probe_retail_pak_arguments(unsigned f,unsigned v) {frame=f;value=v;}
void dkr_probe_retail_pak_tick(uint8_t* rdram,recomp_context* ctx) {
    dkr_probe_enter("private-actual-retail-pak-file-tick");
    const gpr queue=address(DKR_PROBE_REVISION==77?0x801210E0U:0x80121660U);
    const gpr pfs=address(DKR_PROBE_REVISION==77?0x80124018U:0x80124598U);
    const gpr owner_queue=address(DKR_PROBE_REVISION==77?0x80124010U:0x80124590U);
    const gpr name=address(0x80F60000U),ext=name+32,number=name+64,size=name+68;
    const gpr data=name+512,read=name+2048;
    const unsigned port=frame%4;
    if(!frame) {
        // Explicit private idle-controller handover. Initialise the actual
        // project-owned PFS ABI; there is no retail/native SI thread here.
        MEM_W(0,owner_queue)=queue;
        for(unsigned p=0;p<4;++p) {
            ctx->r4=queue;ctx->r5=pfs+p*104;ctx->r6=p;
            dkr_probe_native("osPfsInit_recomp",rdram,ctx);
            if(ctx->r2)dkr_probe_block("private-retail-pak-init-failed");
        }
    }
    // Consume the owned logical sample through the ACTUAL retail input path
    // before Pak I/O, exactly as the main loop does. Do not clear an SI count
    // just to hide a pending read; all later Pak read requests coalesce onto
    // the next frozen sample. This isolated Pak test has no EEPROM requests.
    const dkr_probe_pad pads[4]={{0,0,0},{0,0,0},{0,0,0},{0,0,0}};
    dkr_probe_input_arguments(pads,0);dkr_probe_input_frame(rdram,ctx);
    const char label[]="DKR-ROLLBACK";
    for(unsigned b=0;b<sizeof(label);++b)MEM_B(b,name)=label[b];MEM_B(0,ext)=0;
    for(unsigned b=0;b<1024;++b) {
        MEM_B(b,data)=(uint8_t)(b*19+value+frame);
        MEM_B(b,read)=0;
    }
    ctx->r4=port;ctx->r5=(gpr)-1;ctx->r6=name;ctx->r7=ext;
    MEM_W(16,ctx->r29)=data;MEM_W(20,ctx->r29)=1024;
    write_controller_pak_file(rdram,ctx);
    if(ctx->r2)dkr_probe_block("actual-retail-pak-write-failed");
    ctx->r4=port;ctx->r5=name;ctx->r6=ext;ctx->r7=number;get_file_number(rdram,ctx);
    if(ctx->r2)dkr_probe_block("actual-retail-pak-find-failed");
    ctx->r4=port;ctx->r5=MEM_W(0,number);ctx->r6=size;get_file_size(rdram,ctx);
    if(ctx->r2 || MEM_W(0,size)!=1024)dkr_probe_block("actual-retail-pak-size-failed");
    ctx->r4=port;ctx->r5=MEM_W(0,number);ctx->r6=read;ctx->r7=1024;
    read_data_from_controller_pak(rdram,ctx);
    if(ctx->r2)dkr_probe_block("actual-retail-pak-read-failed");
    for(unsigned b=0;b<1024;++b)if(MEM_BU(b,read)!=MEM_BU(b,data))dkr_probe_block("actual-retail-pak-byte-mismatch");
    mempool_free_queue_clear(rdram,ctx);
    dkr_probe_leave();
}
