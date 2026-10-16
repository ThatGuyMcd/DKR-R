// Headless readback oracle for shared optics and compact per-sample captures.
struct ProbeParams { uint width; uint height; uint samples; uint unused; };
[[vk::push_constant]] ConstantBuffer<ProbeParams> params : register(b0);
Texture2D<float4> opticalColor : register(t1);
Texture2D<float4> originalColor : register(t2);
Texture2D<float> opticalDepth : register(t3);
#ifdef CAPTURE_MSAA
Texture2DMS<float> privateDepth : register(t4);
#else
Texture2D<float> privateDepth : register(t4);
#endif
RWStructuredBuffer<float4> errors : register(u5);
[numthreads(8,8,1)]
void CSMain(uint2 pixel : SV_DispatchThreadID) {
    if(any(pixel>=uint2(params.width,params.height)))return;
    const float3 expectedOriginal=pixel.x<64 && pixel.y<64 ? float3(0,0,1) : float3(0,1,0);
    const uint2 sourcePixel=pixel+uint2(64,32);
    const float opticalError=length(opticalColor.Load(int3(sourcePixel,0)).rgb-float3(1,0,0));
    const float depthError=abs(opticalDepth.Load(int3(sourcePixel,0))-0.25);
    for(uint sample=0;sample<params.samples;sample++) {
        const uint2 at=pixel+uint2((sample&1)*params.width,(sample>>1)*params.height);
        const float4 original=originalColor.Load(int3(at,0));
#ifdef CAPTURE_MSAA
        const float z=privateDepth.Load(sourcePixel,sample);
#else
        const float z=privateDepth.Load(int3(sourcePixel,0));
#endif
        errors[(sample*params.height+pixel.y)*params.width+pixel.x]=
            float4(opticalError,0,depthError,abs(z-0.25));
    }
}
