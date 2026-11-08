        if(director) {
            static thread_local SnapshotReader reader;
            static thread_local std::shared_ptr<const CollisionScene> collision;
            static thread_local std::uint64_t collision_key=0;
            const auto* payload=dkr::runtime::active_payload();if(!payload){ranges_.clear();return;}
            const auto revision=payload->revision;
            const auto address=revision==rom::Revision::UsV77?0x800DC918U:0x800DCE88U;
            const auto model=word(retained,address);
            const auto identity=(std::uint64_t(stamp.epoch)<<32)^stamp.scene^model;
            if(identity!=collision_key) {collision_key=identity;collision.reset();try{collision=CollisionScene::capture(retained,model,identity);}catch(const std::exception&) {}}
            for(auto& range:ranges_) {
                const auto state=reader.capture(retained,revision,range.view,stamp);
                if(!state||!(state->flags&DKR_VIEW_DRIVING)||(state->flags&DKR_VIEW_SCRIPTED))continue;
                const auto& s=*state;auto& p=range.pose;p.api=1;p.size=sizeof(p);p.view=range.view;p.mode=DKR_VIEW_CINEMATIC;
                p.racer_id=s.racer_id;p.racer_generation=s.racer_generation;p.visibility=DKR_VIEW_SHOW_MODEL;p.cut=bool(s.flags&DKR_VIEW_CUT);
                const float side=(stamp.frame/240U)%2U?1.F:-1.F;
                p.position[0]=s.position[0]+std::sin(s.yaw)*160.F+std::cos(s.yaw)*240.F*side;
                p.position[1]=s.position[1]+100.F;
                p.position[2]=s.position[2]+std::cos(s.yaw)*160.F-std::sin(s.yaw)*240.F*side;
                if(collision) {
                    DkrViewQuery q{};q.api=1;q.size=sizeof(q);q.view=range.view;q.radius=8;
                    std::copy_n(s.position,3,q.origin);q.origin[1]+=30;std::copy_n(p.position,3,q.target);
                    const auto hit=collision->query(q);
                    if(hit.status==DKR_VIEW_QUERY_HIT)for(unsigned axis=0;axis<3;++axis)p.position[axis]=q.origin[axis]+(q.target[axis]-q.origin[axis])*std::max(0.F,hit.fraction-.04F);
                    else if(hit.status!=DKR_VIEW_QUERY_NONE){std::copy_n(s.original_position,3,p.position);}
                } else std::copy_n(s.original_position,3,p.position);
                const float dx=s.position[0]-p.position[0],dy=s.position[1]+20.F-p.position[1],dz=s.position[2]-p.position[2];
                p.yaw=std::atan2(-dx,-dz);p.pitch=std::atan2(-dy,std::sqrt(dx*dx+dz*dz));p.roll=0;
                p.vertical_fov=s.original_fov;p.near_plane=s.original_near;p.far_plane=s.original_far;
            }
        } else {
        const auto batch=session_->evaluate(retained,stamp,owners);
        if(!batch.diagnostic.empty())report(batch.diagnostic);
        for(auto& range:ranges_)if(batch.poses[range.view])range.pose=*batch.poses[range.view];
        }
