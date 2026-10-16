#ifndef DKR_WATERFALL_FLOW
#define DKR_WATERFALL_FLOW
// Follow the authored texture frame, including channels/slopes that reuse
// waterfall art. Integer harmonics make every UV repeat exactly seamless.
// No accumulating/wrapping scroll offset and no camera-dependent direction.
float3 waterfallFilm(float2 coordinate,float time,float direction,float3 shade,float footprint) {
    const float tau=6.28318530718;
    float lateral=coordinate.x*tau;
    float falling=coordinate.y*tau-time*3.1*direction;
    float broad=sin(lateral*3+sin(falling)*0.6);
    float fine=sin(lateral*9+sin(falling*2+lateral)*0.7);
    float filter=1-smoothstep(0.4,1.3,footprint*tau*9);
    float strands=saturate(0.52+broad*0.24+fine*0.20*filter);
    float foam=smoothstep(0.57,0.96,0.5+0.5*sin(falling+sin(lateral*2)))*0.18;
    float3 film=lerp(float3(0.30,0.57,0.71),float3(0.91,0.98,1.0),strands)+foam;
    return saturate(film*lerp(float3(0.55,0.55,0.55),saturate(shade),0.7));
}
float3 waterfallComposite(float3 original,float2 coordinate,float time,float direction,float3 shade,float footprint,float strength) {
    if(direction==0)return original; // Unqualified mapping retains authored art.
    float3 film=waterfallFilm(coordinate,time,direction,shade,footprint);
    film=saturate(film*(1+max(0,strength-1)*0.16));
    return lerp(original,film,saturate(strength));
}
#endif
