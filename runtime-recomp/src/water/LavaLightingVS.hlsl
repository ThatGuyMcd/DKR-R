// A single viewport-clipped triangle; no guest vertex stream or camera writes.
float4 VSMain(uint vertex : SV_VertexID) : SV_Position {
    float2 at=float2((vertex<<1)&2,vertex&2);
    return float4(at*float2(2,-2)+float2(-1,1),0,1);
}
