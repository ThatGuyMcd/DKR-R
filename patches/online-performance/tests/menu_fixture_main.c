static int call(unsigned event){
    if(setjmp(rejected))return -1;
    return dkr_probe_native_fields("dkr_legacy_track_menu",0,0,event,0);
}
int main(void){
    unsigned checks=0;
    for(unsigned event=0;event<256;++event){
        const int expected=(event<=23||(event>=100&&event<=105))?0:-1;
        if(call(event)!=expected)return 1;++checks;
        owner=1;if(call(event)!=73)return 2;++checks;owner=0;
    }
    offline=0;if(call(100)!=-1)return 3;++checks;offline=1;
    canonical_presentation=0;if(call(18)!=-1)return 4;++checks;
    printf("%u actual menu-dispatch checks passed\n",checks);return 0;
}
