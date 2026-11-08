"""Retired development pulse and explicit idle output in the owned CPU.

The new control journal distinguishes a network frame from a guest gameplay
update. Never fabricate SP/DP completion, a VI or an extra authored tick.
"""
FILES=('tests/replay_probe/probe_component.c','tests/replay_probe/probe_component.h',
       'tests/replay_probe/probe_driver.cpp')


def transform(name,text,once):
    def one(a,b):
        nonlocal text
        text=once(text,a,b)
    if name.endswith('probe_component.h'):
        one('void dkr_probe_authored_component_tick(', '#if DKR_PROBE_REVISION == 77\n#define dkr_probe_sdk2_pulse dkr_experimental_v77_sdk2_pulse\n#else\n#define dkr_probe_sdk2_pulse dkr_experimental_v80_sdk2_pulse\n#endif\nvoid dkr_probe_sdk2_pulse(uint8_t*,struct recomp_context*);\nvoid dkr_probe_authored_component_tick(')
    elif name.endswith('probe_component.c'):
        one('void dkr_probe_component_inputs(', '''void dkr_probe_sdk2_pulse(uint8_t* ram,struct recomp_context* context) {
    context->r2=1;
#if DKR_PROBE_HAS_MOD_SERVICES
    uint64_t result=1;
    if(dkr_probe_mod_dispatch("dkr_sdk2_pulse",ram,context,0,0,0,0,&result)!=1)
        dkr_probe_block("owned-mod-service-failure");
    context->r2=result;
#endif
}
void dkr_probe_component_inputs(''')
        one('MEM_W(0,rate)!=2 ||','(MEM_W(0,rate)<1 || MEM_W(0,rate)>4) ||')
    elif name.endswith('probe_driver.cpp'):
        one('        if(!menu_tick_active())return RestoreStep::Ready;', '''#if DKR_PROBE_HAS_MOD_SERVICES
        if(code_)code_->sdk2_frame_agreed(agreed);
#endif
        if(!menu_tick_active())return RestoreStep::Ready;''')
        one('    bool tick(std::uint32_t frame,const FrameInputs& inputs,TickOutput& output,std::string& error) override {', '''    bool development_command(std::uint64_t epoch,std::uint32_t frame,const dkr::sdk2::Command& command,std::string& error)override {
        return check(error)&&world_.development_command(epoch,frame,command,error);
    }
    bool tick(std::uint32_t frame,const FrameInputs& inputs,TickOutput& output,std::string& error) override {''')
        one('                if(std::strcmp(name,"dkr_code_enter")==0', '''                if(std::strcmp(name,"dkr_sdk2_pulse")==0) {
                    const int status=world.code_?world.code_->sdk2_dispatch_pulse():1;
                    if(status<0)return -1;*result=unsigned(status);return 1;
                }
                if(std::strcmp(name,"dkr_code_enter")==0''')
        one('        const auto previous_preview_loads=native_.preview_loads;', '''        bool sdk2_idle=false;
#if DKR_PROBE_HAS_MOD_SERVICES
        if(code_) {
            const auto control=run_private(dkr_probe_sdk2_pulse,ram_.data(),ram_.size(),&context_,20000000);
            if(!control.completed){error=code_->error().empty()?"Development control boundary failed.":code_->error();return false;}
            sdk2_idle=context_.r2==0;
        }
#endif
        const auto previous_preview_loads=native_.preview_loads;''')
        one('            result = run_private(authored_cpu_', '            if(sdk2_idle){result={};result.completed=1;}\n            else result = run_private(authored_cpu_')
        one('        draw_events_.resize(observed_count);','        if(!sdk2_idle)draw_events_.resize(observed_count);')
        one('        const auto draw_count=dkr_probe_draw_capture(draw_events_.data(),unsigned(draw_events_.size()));\n        if(draw_count!=observed_count)', '        const auto draw_count=sdk2_idle?observed_count:dkr_probe_draw_capture(draw_events_.data(),unsigned(draw_events_.size()));\n        if(draw_count!=observed_count)')
        # DSP mixing continues at physical transport time, as native audio
        # continues during an offline tool pause. Gameplay callbacks do not.
        one('        if(code_)try {\n            const auto logs=', '''        if(code_)try {
            prepend_effect_envelope(output.effects,{'D','K','D','T'},std::array{unsigned(output.effects.size()),frame,unsigned(!sdk2_idle)});
            const auto logs=''')
        one('        if(authored_cpu_ && !boundary) {\n            if(effects.size()<56', '''        bool has_development_journal=false;
#if DKR_PROBE_HAS_MOD_SERVICES
        if(code_) {
            if(effects.size()<16||!std::equal(effects.begin(),effects.begin()+4,"DKDT"))return false;
            const auto control_word=[&](unsigned offset){std::uint32_t value=0;for(unsigned b=0;b<4;++b)value|=std::uint32_t(effects[offset+b])<<(b*8);return value;};
            if(control_word(8)!=frame||control_word(12)>1||control_word(4)!=effects.size()-16)return false;
            has_development_journal=true;effects=effects.subspan(16);
        }
#endif
        if(authored_cpu_ && !boundary) {
            if(effects.size()<56''')
        one('word(32)>1 || word(36)!=frame+1','word(32)>1 || !word(36) || word(36)>frame+1')
        one('            // Zero explicitly denotes omitted private evidence in production.', '''            if(!has_development_journal&&word(36)!=frame+1)return false;
            // Zero explicitly denotes omitted private evidence in production.''')
    else:raise ValueError('Unreviewed simulation control source: '+name)
    return text
