#pragma once
#include <stdint.h>
#include <stddef.h>

/* Renderer-only ABI. No simulation pointers or mutable owner are retained. */
typedef struct dkr_visual_model_request {
    uint32_t object, instance, animation_frame, animation_id;
    uint32_t flags, transparent_tail, head_override, intensity;
} dkr_visual_model_request;
typedef struct dkr_visual_model_result {
    uint32_t commands, count, vertices, vertex_count, operations;
    uint32_t triangles, triangle_count;
    int16_t head[4];
    uint32_t textures[128], texture_count;
    const char* failure;
} dkr_visual_model_result;
#ifdef __cplusplus
extern "C" {
#endif
int dkr_visual_v77_model(uint8_t*, size_t, const dkr_visual_model_request*, dkr_visual_model_result*);
int dkr_visual_v80_model(uint8_t*, size_t, const dkr_visual_model_request*, dkr_visual_model_result*);
#ifdef __cplusplus
}
#endif
