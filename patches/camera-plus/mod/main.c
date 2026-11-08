#include "dkr/presentation.h"

/* Camera behaviour belongs to the importable package, not the executable.
 * DKR-R only supplies snapshots, local actions, queries and validated views.
 * Yaw zero looks down -Z; positive yaw turns towards -X. Pitch is positive
 * looking down, matching DKR's camera convention. No raw game references.
 */
#define PI 3.14159265358979323846F
typedef struct CameraState {
    DkrU32 valid,scene,epoch,racer,generation,vehicle,mode,requested_mode;
    float position[3],last_position[3],forward_position[3];
    float yaw,pitch,roll,forward_yaw,forward_pitch;
    float orbit_yaw,orbit_pitch,zoom,zoom_target,still_for,airtime;
    float boost,landing,impact,clearance,shake_phase;
    float last_speed,last_yaw;
    DkrU32 grounded,rear_toggle,rear;
} CameraState;
static CameraState states[4];

static float clamp(float x,float lo,float hi){return x<lo?lo:x>hi?hi:x;}
static float absolute(float x){return x<0?-x:x;}
static float mix(float a,float b,float t){return a+(b-a)*t;}
static float wrap(float a){while(a>PI)a-=2*PI;while(a<-PI)a+=2*PI;return a;}
static float follow(float rate,float dt){return rate<=0?1:1-dkr_view_exp(-clamp(rate*dt,0,50));}
static float number(const char* id){return (float)dkr_setting_number(id);}
static float profile(const char* car,const char* hover,const char* plane,DkrU32 vehicle){return number(vehicle==1?hover:vehicle==2?plane:car);}
static float squared_distance(const float* a,const float* b){float sum=0;for(unsigned i=0;i<3;++i){float x=a[i]-b[i];sum+=x*x;}return sum;}
static float limited(float value,float maximum){return clamp(value,-maximum,maximum);}
static void copy3(float* a,const float* b){for(unsigned i=0;i<3;++i)a[i]=b[i];}
static void reset(CameraState* c,const DkrViewSnapshot* s,DkrU32 mode) {
    CameraState fresh={0};*c=fresh;c->valid=1;c->scene=s->scene;c->epoch=s->epoch;
    c->racer=s->racer_id;c->generation=s->racer_generation;c->vehicle=s->vehicle;c->mode=mode;c->requested_mode=mode;
    c->yaw=c->forward_yaw=s->yaw;c->pitch=c->forward_pitch=s->pitch;
    c->last_yaw=s->yaw;c->last_speed=s->speed;c->grounded=(s->flags&DKR_VIEW_GROUNDED)!=0;
    c->clearance=1;copy3(c->position,s->original_position);copy3(c->last_position,s->position);
}
static DkrU32 supported(DkrU32 mode,const DkrViewSnapshot* s) {
    if(!(s->capabilities&DKR_VIEW_POSE))return DKR_VIEW_ORIGINAL;
    if(mode==DKR_VIEW_VR_DRIVER&&!(s->capabilities&DKR_VIEW_STEREO))return DKR_VIEW_COMFORT_FIRST;
    if(mode==DKR_VIEW_ORBIT&&(!(s->capabilities&DKR_VIEW_ORBIT_COVERAGE)||
       !(s->flags&DKR_VIEW_INSPECTION_ALLOWED)||(s->flags&DKR_VIEW_ONLINE)))return DKR_VIEW_CHASE;
    return mode;
}
static void clearance(const DkrViewSnapshot* s,CameraState* c,float* position,float dt,float radius) {
    DkrViewQuery q={0};DkrViewHit hit={0};q.api=1;q.size=sizeof(q);q.view=s->view;q.exclude_racer=s->racer_id;
    copy3(q.origin,s->position);q.origin[1]+=number("target_height");copy3(q.target,position);q.radius=radius;
    q.include_dynamic=(s->capabilities&DKR_VIEW_DYNAMIC_COLLISION)!=0;
    DkrU32 result=dkr_view_query(&q,&hit);
    if(result==DKR_VIEW_QUERY_UNSUPPORTED||result==DKR_VIEW_QUERY_BUDGET) {
        /* A failed query is not proof of clearance. Preserve the native safe
         * position rather than exposing a wall while a query budget recovers. */
        copy3(position,s->original_position);c->clearance=1;return;
    }
    float fraction=result==DKR_VIEW_QUERY_HIT?clamp(hit.fraction-0.015F,0,1):1;
    if(fraction<c->clearance)c->clearance=fraction;
    else if(fraction>c->clearance+0.015F)c->clearance=mix(c->clearance,fraction,follow(number("clearance_return"),dt));
    for(unsigned i=0;i<3;++i)position[i]=mix(q.origin[i],position[i],c->clearance);
}

DKR_CALLBACK(on_presentation) void camera_frame(void) {
    DkrViewSnapshot s;DkrViewInput input;
    if(!dkr_view_snapshot(&s)||!dkr_view_input(&input)||s.view>=4)return;
    CameraState* c=&states[s.view];
    DkrU32 mode=supported((DkrU32)number("mode"),&s);
    DkrViewPose output={0};output.api=1;output.size=sizeof(output);output.view=s.view;
    output.racer_id=s.racer_id;output.racer_generation=s.racer_generation;
    output.vertical_fov=s.original_fov;output.near_plane=s.original_near;output.far_plane=s.original_far;
    if(mode==DKR_VIEW_ORIGINAL){output.mode=mode;dkr_view_submit(&output);return;}
    int discontinuity=!c->valid||c->scene!=s.scene||c->epoch!=s.epoch||c->racer!=s.racer_id||
        c->generation!=s.racer_generation||c->vehicle!=s.vehicle||(s.flags&DKR_VIEW_CUT)||
        squared_distance(c->last_position,s.position)>250000;
    if(discontinuity)reset(c,&s,mode);
    /* The selected profile stays local to this view. Cycling is session-only;
     * changing the saved mode deliberately resets it on the next callback. */
    if(mode!=c->requested_mode){c->requested_mode=mode;c->mode=mode;output.cut=1;}
    if(input.pressed&DKR_VIEW_CYCLE){
        DkrU32 count=(s.capabilities&DKR_VIEW_ORBIT_COVERAGE)&&(s.flags&DKR_VIEW_INSPECTION_ALLOWED)?6:5;
        c->mode=1+(c->mode%count);output.cut=1;
    }
    if(input.pressed&DKR_VIEW_FIRST_PERSON){c->mode=c->mode==DKR_VIEW_FIRST?DKR_VIEW_CHASE:DKR_VIEW_FIRST;output.cut=1;}
    mode=supported(c->mode,&s);output.mode=mode;
    float dt=clamp(s.dt,0,0.1F),speed=absolute(s.speed),speed01=clamp(speed/number("speed_reference"),0,1);
    int first=mode==DKR_VIEW_FIRST||mode==DKR_VIEW_COMFORT_FIRST||mode==DKR_VIEW_VR_DRIVER;
    int comfort=mode==DKR_VIEW_COMFORT_FIRST||mode==DKR_VIEW_VR_DRIVER;
    float reduced=number("reduced_motion")!=0||comfort?0:1;
    float effect=number("effects")*reduced;
    if(mode==DKR_VIEW_CINEMATIC)effect=clamp(effect*1.35F,0,1.5F);
    if(input.pressed&DKR_VIEW_RECENTER){c->orbit_yaw=0;c->orbit_pitch=0;}
    float orbit_limit=mode==DKR_VIEW_ORBIT?PI:PI*0.6F;
    c->orbit_yaw=limited(c->orbit_yaw+input.orbit_x*number("orbit_sensitivity")*dt,orbit_limit);
    c->orbit_pitch=clamp(c->orbit_pitch+input.orbit_y*number("orbit_sensitivity")*
        (number("invert_y")!=0?-1:1)*dt,-1.0F,1.0F);
    if(absolute(input.orbit_x)+absolute(input.orbit_y)>0.03F)c->still_for=0;
    else {
        c->still_for+=dt;
        if(c->still_for>number("recenter_delay")&&mode!=DKR_VIEW_ORBIT) {
            float t=follow(number("recenter_speed"),dt);c->orbit_yaw=mix(c->orbit_yaw,0,t);c->orbit_pitch=mix(c->orbit_pitch,0,t);
        }
    }
    if(input.pressed&DKR_VIEW_ZOOM_IN)c->zoom_target-=15;
    if(input.pressed&DKR_VIEW_ZOOM_OUT)c->zoom_target+=15;
    c->zoom_target=clamp(c->zoom_target+input.zoom*60*dt,-80,120);
    c->zoom=mix(c->zoom,c->zoom_target,follow(number("zoom_response"),dt));
    float direction=s.yaw;
    /* Near rest or reversing, keep nose-follow stable instead of inferring a
     * heading from a tiny/negative velocity vector. Drift has its own mix. */
    float horizontal=s.velocity[0]*s.velocity[0]+s.velocity[2]*s.velocity[2];
    if(horizontal>9&&s.speed>2) {
        float travel=dkr_view_atan2(-s.velocity[0],-s.velocity[2]);
        float alignment=number((s.flags&DKR_VIEW_DRIFTING)?"drift_alignment":"travel_alignment");
        direction+=wrap(travel-s.yaw)*alignment;
    }
    c->forward_yaw=wrap(c->forward_yaw+wrap(direction-c->forward_yaw)*follow(number("rotation_response"),dt));
    float rear_before=(float)c->rear;
    if(number("look_back_toggle")!=0) {
        if(input.pressed&DKR_VIEW_LOOK_BACK)c->rear_toggle=!c->rear_toggle;
        c->rear=c->rear_toggle;
    } else c->rear=(input.held&DKR_VIEW_LOOK_BACK)!=0;
    if(!(s.capabilities&DKR_VIEW_REAR_COVERAGE)||mode==DKR_VIEW_VR_DRIVER)c->rear=0;
    if(rear_before!=(float)c->rear)output.cut=1;
    /* Maintain the forward solver even during look-back. A rear transition is
     * a cut by default, never a 180-degree blend through the racer's body. */
    float yaw=wrap(c->forward_yaw+c->orbit_yaw+(c->rear?PI:0));
    float pitch_follow=profile("car_pitch","hover_pitch","plane_pitch",s.vehicle);
    float bank_follow=profile("car_bank","hover_bank","plane_bank",s.vehicle);
    float vertical_response=number("vertical_response")*profile("car_vertical","hover_vertical","plane_vertical",s.vehicle);
    float pitch=s.pitch*pitch_follow+c->orbit_pitch+number("pitch_offset")*PI/180;
    float distance=mix(number("distance_low"),number("distance_high"),speed01)+c->zoom;
    distance*=profile("car_distance","hover_distance","plane_distance",s.vehicle);
    if(mode==DKR_VIEW_BONNET)distance=number("bonnet_distance");
    if(c->rear)distance=number("rear_distance");
    float boost_target=(s.flags&DKR_VIEW_BOOSTING)?clamp(s.boost,0,1):0;
    c->boost=mix(c->boost,boost_target,follow(boost_target>c->boost?10:number("effect_recovery"),dt));
    int grounded=(s.flags&DKR_VIEW_GROUNDED)!=0;
    if(grounded&&!c->grounded&&c->airtime>0.15F)c->landing=clamp(c->airtime,0,1);
    if(!grounded)c->airtime+=dt;else c->airtime=0;
    if(s.flags&DKR_VIEW_IMPACT)c->impact=1;
    c->landing=mix(c->landing,0,follow(number("effect_recovery"),dt));
    c->impact=mix(c->impact,0,follow(number("effect_recovery"),dt));
    c->grounded=(DkrU32)grounded;c->shake_phase+=dt*29;
    if(c->shake_phase>2*PI)c->shake_phase-=2*PI;
    distance=clamp(distance+c->boost*number("boost_pullback")*effect,2,700);
    float sx=dkr_view_sin(yaw),cz=dkr_view_cos(yaw),right_x=cz,right_z=-sx;
    float desired[3];
    float side=number("side_offset")+clamp(s.steer,-1,1)*number("sway")*effect;
    float height=number("height")-c->landing*number("landing_dip")*effect;
    float lookahead=number("look_ahead")*speed01;
    float lead_x=limited(s.velocity[0]*lookahead,80),lead_z=limited(s.velocity[2]*lookahead,80);
    if(first||mode==DKR_VIEW_BONNET) {
        float nose_sin=dkr_view_sin(s.yaw),nose_cos=dkr_view_cos(s.yaw);
        float seat_forward=mode==DKR_VIEW_BONNET?number("bonnet_distance"):number("seat_forward"),seat_side=number("seat_side");
        desired[0]=s.position[0]-nose_sin*seat_forward+nose_cos*seat_side;
        desired[1]=s.position[1]+number("seat_height");
        desired[2]=s.position[2]-nose_cos*seat_forward-nose_sin*seat_side;
        yaw=wrap(s.yaw+c->orbit_yaw+(c->rear?PI:0));
    } else {
        desired[0]=s.position[0]+sx*distance+right_x*side+lead_x;
        desired[1]=s.position[1]+height;
        desired[2]=s.position[2]+cz*distance+right_z*side+lead_z;
        float target_height=number("target_height");
        pitch+=dkr_view_atan2(height-target_height,distance);
    }
    /* One bounded target, no accumulated positional kick or unchecked speed
     * multiplier. Native position is never changed, even in inspection mode. */
    for(unsigned i=0;i<3;++i) {
        float response=i==1?vertical_response:number("horizontal_response");
        c->position[i]=discontinuity||output.cut?desired[i]:mix(c->position[i],desired[i],follow(response,dt));
        c->position[i]=clamp(c->position[i],desired[i]-number("maximum_lag"),desired[i]+number("maximum_lag"));
        output.position[i]=c->position[i];
    }
    float roll=s.roll*bank_follow+clamp(s.steer,-1,1)*number("corner_roll")*effect*PI/180;
    float shake=number("shake")*effect*c->impact;
    if(shake>0) {
        output.position[1]+=dkr_view_sin(c->shake_phase)*shake;
        pitch+=dkr_view_cos(c->shake_phase)*shake*0.002F;
    }
    if(s.capabilities&DKR_VIEW_PROJECTION) {
        float fov=number("fov")+number("speed_fov")*speed01*effect+number("boost_fov")*c->boost*effect;
        output.vertical_fov=clamp(fov,30,120)*PI/180;output.near_plane=first||mode==DKR_VIEW_BONNET?1:s.original_near;
    }
    /* A centre ray can clear while near-plane corners expose a wall at high
     * FOV / ultrawide aspects. Include those corners in the clearance volume. */
    if(!first) {
        float half=output.vertical_fov*0.5F;
        float near_radius=output.near_plane*dkr_view_sin(half)/dkr_view_cos(half)*
            dkr_view_sqrt(1+s.aspect*s.aspect)+1;
        if(near_radius>128) {
            output.mode=DKR_VIEW_ORIGINAL;dkr_view_submit(&output);return;
        }
        float radius=number("clearance_radius");
        clearance(&s,c,output.position,dt,radius>near_radius?radius:near_radius);
    }
    /* Clearance is the last positional operation. Cuts also retire angular
     * smoothing, rather than borrowing the previous mode's pitch. */
    c->pitch=discontinuity||output.cut?pitch:mix(c->pitch,pitch,follow(number("rotation_response"),dt));
    c->roll=discontinuity||output.cut?(comfort?0:roll):mix(c->roll,comfort?0:roll,follow(number("rotation_response"),dt));
    output.yaw=yaw;output.pitch=c->pitch;output.roll=c->roll;
    if(first) {
        DkrU32 visibility=(DkrU32)number("first_visibility");
        if(visibility==DKR_VIEW_HIDE_RIDER_ONLY&&!(s.capabilities&DKR_VIEW_HIDE_RIDER))visibility=DKR_VIEW_HIDE_VEHICLE;
        if(visibility==DKR_VIEW_HIDE_VEHICLE&&!(s.capabilities&DKR_VIEW_HIDE_LOCAL_MODEL))visibility=DKR_VIEW_SHOW_MODEL;
        output.visibility=visibility;
    }
    if(discontinuity)output.cut=1;
    copy3(c->last_position,s.position);c->last_speed=s.speed;c->last_yaw=s.yaw;
    dkr_view_submit(&output);
}
