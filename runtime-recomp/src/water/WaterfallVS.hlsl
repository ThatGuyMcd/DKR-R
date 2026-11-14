#define DYNAMIC_RENDER_PARAMS
#define VSMain RetailVSMain
#include "RasterVS.hlsl"
#undef VSMain
void VSMain(float4 position:POSITION0,float2 uv:TEXCOORD0,float4 color:COLOR0,
    float4 flow:POSITION1,out float4 clip:SV_POSITION,out float2 texcoord:TEXCOORD0,
    // Keep the packed varying order identical to RasterPS's waterfall inputs.
    // D3D12 does not remap mismatched hardware registers by semantic name.
    out float4 flowCoordinate:TEXCOORD1,out float4 smoothColor:COLOR0,
    nointerpolation out float4 flatColor:COLOR1) {
    RasterVS(getRenderParams(),position,uv,color,clip,texcoord,smoothColor,flatColor);
    flowCoordinate=flow;
}
