#pragma once
/* Authored Patch Pipeline leaf: only bounded guest memory, no file/native
 * state. Used unchanged by both normal and checked replay CPU payloads. */
static const unsigned dkr_sl_courses[34]={1,3,4,5,6,7,8,9,10,11,13,15,17,18,19,20,25,26,27,28,29,30,31,32,33,37,38,40,41,46,52,53,54,55};
static const unsigned dkr_sl_hubs[6]={0,12,14,24,2,35};
static const unsigned dkr_sl_records[47][2]={{3,0},{3,1},{3,2},{4,1},{5,0},{5,1},{5,2},{6,0},{6,1},{7,1},{7,2},{8,1},{9,0},{9,1},{10,0},{10,1},{13,0},{13,1},{13,2},{15,0},{15,1},{15,2},{17,0},{17,1},{17,2},{18,0},{18,1},{19,1},{20,0},{20,1},{20,2},{28,0},{28,1},{28,2},{29,0},{29,1},{29,2},{30,0},{30,1},{30,2},{31,0},{31,1},{32,0},{32,1},{33,0},{33,1},{33,2}};
typedef struct { unsigned bit,limit,ok; uint8_t bytes[384]; } dkr_sl_bits;
static inline int dkr_sl_span(uint32_t p,unsigned n,unsigned alignment) {
    return p>=0x80000000U&&p<0x80800000U&&n<=0x80800000U-p&&!(p&(alignment-1));
}
static inline uint32_t dkr_sl_word(uint8_t* rdram,uint32_t p) {return (uint32_t)MEM_W(0,(gpr)(int32_t)p);}
static inline unsigned dkr_sl_half(uint8_t* rdram,uint32_t p) {return MEM_HU(0,(gpr)(int32_t)p);}
static inline unsigned dkr_sl_byte(uint8_t* rdram,uint32_t p) {return MEM_BU(0,(gpr)(int32_t)p);}
static inline void dkr_sl_put_word(uint8_t* rdram,uint32_t p,uint32_t v) {MEM_W(0,(gpr)(int32_t)p)=v;}
static inline void dkr_sl_put_half(uint8_t* rdram,uint32_t p,unsigned v) {MEM_H(0,(gpr)(int32_t)p)=(uint16_t)v;}
static inline void dkr_sl_put_byte(uint8_t* rdram,uint32_t p,unsigned v) {MEM_B(0,(gpr)(int32_t)p)=(uint8_t)v;}
static inline uint32_t dkr_sl_read(dkr_sl_bits* b,unsigned n) {
    uint32_t v=0;if(n>32||b->bit+n>b->limit){b->ok=0;return 0;}
    while(n--){v=(v<<1)|((b->bytes[b->bit/8]>>(7-b->bit%8))&1);++b->bit;}return v;
}
static inline void dkr_sl_write(dkr_sl_bits* b,unsigned n,uint32_t v) {
    if(n>32||b->bit+n>b->limit){b->ok=0;return;}
    while(n--){unsigned mask=1U<<(7-b->bit%8);uint8_t* out=&b->bytes[b->bit/8];
        *out=(uint8_t)((*out&~mask)|(((v>>n)&1)?mask:0));++b->bit;}
}
static inline int dkr_sl_checksum(dkr_sl_bits* b,unsigned offset,unsigned size,int write) {
    unsigned sum=5;for(unsigned i=2;i<size;++i)sum+=b->bytes[offset+i];sum&=65535;
    if(!write)return sum==(((unsigned)b->bytes[offset]<<8)|b->bytes[offset+1]);
    b->bytes[offset]=(uint8_t)(sum>>8);b->bytes[offset+1]=(uint8_t)sum;return 1;
}
static inline int dkr_sl_overlap(uint32_t a,unsigned an,uint32_t b,unsigned bn) {return a<b+bn&&b<a+an;}
/* op: Adventure read/write, records read/write, record defaults. The live
 * course count sizes RAM arrays only. It NEVER sizes the EEPROM format. */
static inline int dkr_sl_apply(uint8_t* rdram,recomp_context* ctx,unsigned op,unsigned revision,uint32_t defaults) {
    const uint32_t settings=(uint32_t)ctx->r4,buffer=(uint32_t)ctx->r5;
    const unsigned flags=(unsigned)(op==4?ctx->r5:ctx->r6)&3U;
    const unsigned adventure=op<2,writing=op==1||op==3;
    const unsigned levels=dkr_sl_word(rdram,revision==80?0x801216F0U:0x80121170U);
    const unsigned worlds=dkr_sl_word(rdram,revision==80?0x801216F4U:0x80121174U);
    uint32_t progress=0,balloons=0,table[4][3]={{0}};
    if(op>4||levels<65||levels>128||worlds<6||worlds>128||!dkr_sl_span(settings,0x54,4))return 0;
    unsigned extent=adventure?40:384;
    if(op!=4&&!dkr_sl_span(buffer,extent,1))return 0;
    if(op==2||op==4)if(!dkr_sl_span(defaults,65*24,2))return 0;
    if(adventure) {
        balloons=dkr_sl_word(rdram,settings);progress=dkr_sl_word(rdram,settings+4);
        if(!dkr_sl_span(balloons,worlds*2,2)||!dkr_sl_span(progress,levels*4,4))return 0;
        if(dkr_sl_overlap(buffer,extent,settings,0x54)||dkr_sl_overlap(buffer,extent,balloons,worlds*2)||dkr_sl_overlap(buffer,extent,progress,levels*4))return 0;
    }else {
      if(op!=4&&dkr_sl_overlap(buffer,extent,settings,0x54))return 0;
      for(unsigned t=0;t<4;++t)for(unsigned v=0;v<3;++v) {
        table[t][v]=dkr_sl_word(rdram,settings+0x18+t*12+v*4);
        if(!dkr_sl_span(table[t][v],levels*2,2)||dkr_sl_overlap(settings,0x54,table[t][v],levels*2)||(op!=4&&dkr_sl_overlap(buffer,extent,table[t][v],levels*2)))return 0;
        if((op==2||op==4)&&dkr_sl_overlap(defaults,65*24,table[t][v],levels*2))return 0;
        for(unsigned prior=0;prior<t*3+v;++prior)
            if(dkr_sl_overlap(table[prior/3][prior%3],levels*2,table[t][v],levels*2))return 0;
      }
    }
    dkr_sl_bits b={0};b.bit=16;b.limit=extent*8;b.ok=1;
    if(op!=4&&!writing)for(unsigned i=0;i<extent;++i)b.bytes[i]=(uint8_t)dkr_sl_byte(rdram,buffer+i);
    /* The two opaque tail bytes of each stock record block are not records. */
    if(op==3)for(unsigned kind=0;kind<2;++kind)for(unsigned i=190;i<192;++i)
        b.bytes[kind*192+i]=(uint8_t)dkr_sl_byte(rdram,buffer+kind*192+i);
    if(op==0) {
        for(unsigned i=0;i<levels;++i)dkr_sl_put_word(rdram,progress+i*4,0);
        for(unsigned i=0;i<worlds;++i)dkr_sl_put_half(rdram,balloons+i*2,0);
        dkr_sl_put_half(rdram,settings+8,0);dkr_sl_put_half(rdram,settings+10,0);
        dkr_sl_put_half(rdram,settings+12,0);dkr_sl_put_half(rdram,settings+14,0);
        dkr_sl_put_word(rdram,settings+16,0);dkr_sl_put_word(rdram,settings+20,0);
        dkr_sl_put_byte(rdram,settings+0x4B,1);
        if(!dkr_sl_checksum(&b,0,40,0))return 1;
    }
    if(adventure) {
        for(unsigned i=0;i<34;++i) {
            unsigned at=progress+dkr_sl_courses[i]*4;
            if(writing){unsigned f=dkr_sl_word(rdram,at);dkr_sl_write(&b,2,((f&1)!=0)+((f&2)!=0)+((f&4)!=0));}
            else {unsigned f=dkr_sl_read(&b,2);dkr_sl_put_word(rdram,at,(f>0?1:0)|(f>1?2:0)|(f>2?4:0));}
        }
        const unsigned offsets[3]={20,14,12},widths[3]={6,10,12};
        for(unsigned i=0;i<3;++i)if(writing)dkr_sl_write(&b,widths[i],dkr_sl_half(rdram,settings+offsets[i]));else dkr_sl_put_half(rdram,settings+offsets[i],dkr_sl_read(&b,widths[i]));
        for(unsigned i=0;i<6;++i)if(writing)dkr_sl_write(&b,7,dkr_sl_half(rdram,balloons+i*2));else dkr_sl_put_half(rdram,balloons+i*2,dkr_sl_read(&b,7));
        for(unsigned i=22;i<24;++i)if(writing)dkr_sl_write(&b,3,dkr_sl_byte(rdram,settings+i));else dkr_sl_put_byte(rdram,settings+i,dkr_sl_read(&b,3));
        for(unsigned i=0;i<6;++i) {
            unsigned at=progress+dkr_sl_hubs[i]*4;
            if(writing)dkr_sl_write(&b,16,dkr_sl_word(rdram,at)>>16);
            else dkr_sl_put_word(rdram,at,dkr_sl_word(rdram,at)|(dkr_sl_read(&b,16)<<16));
        }
        if(writing) {
            dkr_sl_write(&b,8,dkr_sl_half(rdram,settings+8));dkr_sl_write(&b,32,dkr_sl_word(rdram,settings+16));
            dkr_sl_write(&b,16,dkr_sl_word(rdram,settings+0x50));dkr_sl_write(&b,8,0);
            if(b.bit!=320)return 0;dkr_sl_checksum(&b,0,40,1);
        }else {
            dkr_sl_put_half(rdram,settings+8,dkr_sl_read(&b,8));dkr_sl_put_word(rdram,settings+16,dkr_sl_read(&b,32));
            dkr_sl_put_word(rdram,settings+0x50,dkr_sl_read(&b,16));dkr_sl_read(&b,8);dkr_sl_put_byte(rdram,settings+0x4B,0);
        }
    }else {
        if(!writing)for(unsigned i=0;i<levels;++i)for(unsigned v=0;v<3;++v)for(unsigned kind=0;kind<2;++kind) {
            if(!(flags&(1U<<kind)))continue;
            unsigned row=defaults+i*24+v*8;
            unsigned time=i<65?dkr_sl_half(rdram,row+(kind?0:4)):0;
            unsigned name=i<65?dkr_sl_half(rdram,row+(kind?2:6)):0x7FFF;
            dkr_sl_put_half(rdram,table[kind*2][v]+i*2,name);dkr_sl_put_half(rdram,table[kind*2+1][v]+i*2,time);
        }
        if(op==4)return 1;
        for(unsigned kind=0;kind<2;++kind) {
            if(!writing&&(!(flags&(1U<<kind))||!dkr_sl_checksum(&b,kind*192,192,0)))continue;
            b.bit=kind*1536+16;b.limit=(kind+1)*1536;
            for(unsigned i=0;i<47;++i) {
                unsigned level=dkr_sl_records[i][0],v=dkr_sl_records[i][1];
                unsigned time=table[kind*2+1][v]+level*2,name=table[kind*2][v]+level*2;
                if(writing){dkr_sl_write(&b,16,dkr_sl_half(rdram,time));dkr_sl_write(&b,16,dkr_sl_half(rdram,name));}
                else {dkr_sl_put_half(rdram,time,dkr_sl_read(&b,16));dkr_sl_put_half(rdram,name,dkr_sl_read(&b,16));}
            }
            if(writing)dkr_sl_checksum(&b,kind*192,192,1);
        }
    }
    if(!b.ok)return 0;
    if(writing)for(unsigned i=0;i<extent;++i)dkr_sl_put_byte(rdram,buffer+i,b.bytes[i]);
    return 1;
}
