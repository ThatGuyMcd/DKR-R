#include "funcs.h"
#include "probe_pak.h"
#include <string.h>

static gpr address(uint32_t p) { return (int32_t)p; }
static void range(uint8_t* ram,gpr p,unsigned size) {
    if(!size)return;
    if((uint32_t)p<0x80000000U || (uint32_t)p>0x80FFFFFFU || size>0x81000000U-(uint32_t)p)
        dkr_probe_block("invalid-owned-pak-buffer");
    dkr_probe_memory(ram,p,1,3);dkr_probe_memory(ram,p+size-1,1,3);
}
static void word_range(uint8_t* ram,gpr p,unsigned size) {
    range(ram,p,size);dkr_probe_memory(ram,p,4,0);
}
static gpr argument(uint8_t* rdram,recomp_context* ctx,unsigned i) {
    if(i<4)return (&ctx->r4)[i];
    const gpr stack=address((uint32_t)ctx->r29);range(rdram,stack+i*4,4);return MEM_W(i*4,stack);
}
static unsigned channel(uint8_t* rdram,recomp_context* ctx) {
    const gpr pfs=address((uint32_t)ctx->r4);range(rdram,pfs,12);return MEM_W(8,pfs);
}
static int request(unsigned op,unsigned port,uint32_t args[4],uint8_t* data,unsigned size) {
    const int result=dkr_probe_pak_request(op,port,args,data,size);
    if(result== -100)dkr_probe_block("owned-pak-transaction-failed");return result;
}
static void metadata(uint8_t* rdram,recomp_context* ctx,uint32_t args[4],uint8_t data[20]) {
    args[0]=(uint16_t)ctx->r5;args[1]=(uint32_t)ctx->r6;
    const gpr name=address((uint32_t)ctx->r7),ext=address((uint32_t)argument(rdram,ctx,4));
    range(rdram,name,16);range(rdram,ext,4);
    for(unsigned b=0;b<16;++b)data[b]=MEM_BU(b,name);
    for(unsigned b=0;b<4;++b)data[16+b]=MEM_BU(b,ext);
}
int dkr_probe_pak_service(const char* name,uint8_t* rdram,recomp_context* ctx,int* native_result) {
    if(!dkr_probe_paks_enabled())return 0;
    uint32_t args[4]={0};uint8_t data[32768];int status;*native_result=0;
    if(strcmp(name,"dkr_virtual_pak_preferred_status")==0) {
        // Match the actual project override: memory Pak has priority over
        // rumble. No contact with the stable file-backed Pak singleton.
        status=request(DKR_PAK_STATUS,(uint32_t)ctx->r4,args,data,0);
        *native_result=status==0?0: -1;return 1;
    }
    if(strcmp(name,"osPfsIsPlug_recomp")==0) {
        const gpr output=address((uint32_t)ctx->r5);range(rdram,output,1);
        MEM_B(0,output)=dkr_probe_pak_mask();ctx->r2=0;return 1;
    }
    if(strcmp(name,"osPfsInitPak_recomp")==0 || strcmp(name,"osPfsInit_recomp")==0) {
        const unsigned port=(uint32_t)ctx->r6;
        status=request(DKR_PAK_STATUS,port,args,data,0);
        if(status==0) {
            const gpr pfs=address((uint32_t)ctx->r5);word_range(rdram,pfs,104);
            MEM_W(0,pfs)=1;MEM_W(4,pfs)=(uint32_t)ctx->r4;MEM_W(8,pfs)=port;
            MEM_W(76,pfs)=0x200;MEM_W(80,pfs)=16;MEM_B(102,pfs)=0;MEM_B(103,pfs)=1;
        }ctx->r2=(int32_t)status;return 1;
    }
    if(strcmp(name,"dkr_virtual_pak_reformat")==0) {
        const gpr pfs=address((uint32_t)ctx->r4);word_range(rdram,pfs,12);
        status=request(DKR_PAK_FORMAT,(uint32_t)ctx->r6,args,data,0);
        if(status==0){MEM_W(0,pfs)=1;MEM_W(8,pfs)=(uint32_t)ctx->r6;}
        *native_result=status;return 1;
    }
    const int free_blocks=strcmp(name,"osPfsFreeBlocks_recomp")==0;
    const int num_files=strcmp(name,"osPfsNumFiles_recomp")==0;
    if(free_blocks || num_files) {
        const unsigned port=channel(rdram,ctx);const gpr output=address((uint32_t)ctx->r5);
        word_range(rdram,output,4);if(num_files)word_range(rdram,address((uint32_t)ctx->r6),4);
        status=request(free_blocks?DKR_PAK_FREE:DKR_PAK_COUNT,port,args,data,0);
        if(status==0) {
            MEM_W(0,output)=free_blocks?args[0]:16;
            if(num_files)MEM_W(0,address((uint32_t)ctx->r6))=args[0];
        }ctx->r2=(int32_t)status;return 1;
    }
    const int find=strcmp(name,"osPfsFindFile_recomp")==0;
    const int allocate=strcmp(name,"osPfsAllocateFile_recomp")==0;
    const int erase=strcmp(name,"osPfsDeleteFile_recomp")==0;
    if(find || allocate || erase) {
        const unsigned port=channel(rdram,ctx);metadata(rdram,ctx,args,data);
        gpr output=0;
        if(allocate){args[2]=(uint32_t)argument(rdram,ctx,5);output=address((uint32_t)argument(rdram,ctx,6));}
        if(find)output=address((uint32_t)argument(rdram,ctx,5));
        if(output || !erase)word_range(rdram,output,4);
        if(allocate && (int32_t)args[2]<=0)status=5;
        else status=request(find?DKR_PAK_FIND:allocate?DKR_PAK_ALLOCATE:DKR_PAK_ERASE,port,args,data,20);
        if(status==0 && !erase)MEM_W(0,output)=args[0];ctx->r2=(int32_t)status;return 1;
    }
    if(strcmp(name,"osPfsFileState_recomp")==0) {
        const unsigned port=channel(rdram,ctx);const gpr output=address((uint32_t)ctx->r6);word_range(rdram,output,30);
        args[0]=(uint32_t)ctx->r5;status=request(DKR_PAK_STATE,port,args,data,20);
        if(status==0) {
            MEM_W(0,output)=args[0];MEM_W(4,output)=args[1];MEM_H(8,output)=args[2];
            for(unsigned b=0;b<20;++b)MEM_B(10+b,output)=data[b];
        }ctx->r2=(int32_t)status;return 1;
    }
    if(strcmp(name,"osPfsReadWriteFile_recomp")==0) {
        const unsigned port=channel(rdram,ctx),mode=(uint32_t)ctx->r6;
        const int32_t offset=(int32_t)ctx->r7,size=(int32_t)argument(rdram,ctx,4);
        const gpr buffer=address((uint32_t)argument(rdram,ctx,5));
        if(offset<0 || size<0 || size>32768 || mode>1)status=5;
        else {
            range(rdram,buffer,(unsigned)size);args[0]=(uint32_t)ctx->r5;args[1]=(uint32_t)offset;
            if(mode==1)for(unsigned b=0;b<(unsigned)size;++b)data[b]=MEM_BU(b,buffer);
            status=request(mode==0?DKR_PAK_READ:DKR_PAK_WRITE,port,args,data,(unsigned)size);
            if(status==0 && mode==0)for(unsigned b=0;b<(unsigned)size;++b)MEM_B(b,buffer)=data[b];
        }ctx->r2=(int32_t)status;return 1;
    }
    if(strcmp(name,"osPfsChecker_recomp")==0 || strcmp(name,"osPfsRepairId_recomp")==0) {
        ctx->r2=(int32_t)request(DKR_PAK_STATUS,channel(rdram,ctx),args,data,0);return 1;
    }
    // Exact virtual_pak.cpp project overrides: these low-level helpers only
    // return enablement. They must not be inherited from dependency Pak stubs.
    if(strcmp(name,"__osPfsSelectBank_recomp")==0 || strcmp(name,"__osContRamWrite_recomp")==0 ||
       strcmp(name,"__osPfsGetStatus_recomp")==0 || strcmp(name,"__osGetId_recomp")==0 ||
       strcmp(name,"__osContRamRead_recomp")==0) {ctx->r2=dkr_probe_pak_mask()?0:1;return 1;}
    return 0;
}
