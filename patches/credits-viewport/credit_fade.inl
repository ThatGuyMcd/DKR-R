    bool fill(RT64::State& state,const RT64::DisplayList& command) const {
        if(!enabled_ || state.rdp->colorImage.width!=320)return false;
        const int left=command.p1(12,12),top=command.p1(0,12);
        const int right=command.p0(12,12),bottom=command.p0(0,12);
        if(!inset(left,top,right,bottom))return false;
        auto& rdp=*state.rdp;
        state.flush();
        const auto at=rdp.scissorStackSize-1;
        const auto previous=rdp.scissorRectStack[at];
        const auto left_origin=rdp.extended.scissorLeftOriginStack[at];
        const auto right_origin=rdp.extended.scissorRightOriginStack[at];
        const auto aspect=rdp.extended.global.rectAspect;
        const int height=top==40*4?240:288;
        const int gutter=int(std::ceil(640.F*(cover_-1.F)));
        // Rectangle origins alone cannot escape a still-native draw scissor.
        // Use the same signed window-domain clip as the accepted font path.
        auto& clip=rdp.scissorRectStack[at];
        clip.ulx=-gutter;clip.uly=0;clip.lrx=1280+gutter;clip.lry=height*4;
        rdp.extended.scissorLeftOriginStack[at]=rdp.extended.scissorRightOriginStack[at]=G_EX_ORIGIN_NONE;
        state.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
        rdp.setRectAspect(G_EX_ASPECT_ADJUST);
        RT64::ExtendedAlignment alignment{};
        alignment.leftOrigin=alignment.rightOrigin=G_EX_ORIGIN_NONE;
        rdp.fillRect(-gutter,0,1280+gutter,height*4,alignment);
        state.flush();
        rdp.scissorRectStack[at]=previous;
        rdp.extended.scissorLeftOriginStack[at]=left_origin;
        rdp.extended.scissorRightOriginStack[at]=right_origin;
        state.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
        rdp.setRectAspect(aspect);
        static thread_local bool notice=false;
        if(!notice){std::fprintf(stderr,"[credits][fade] signed full-window rectangle and scissor; original opacity\n");notice=true;}
        return true;
    }
