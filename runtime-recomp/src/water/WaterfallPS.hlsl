#define DYNAMIC_RENDER_PARAMS
#define DKR_WATERFALL
#include "shared/rt64_water_params.h"
#include "WaterfallFlow.hlsli"
StructuredBuffer<WaterDrawParams> waterfallDraws : register(t1,space4);
#include "RasterPS.hlsl"
