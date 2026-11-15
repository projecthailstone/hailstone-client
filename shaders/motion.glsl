// A draw's own motion for the temporal upscaler (docs/graphics.md "Temporal upscaling"; render/vk_context.h
// SceneOutput). The vertex shader gives the vertex's place this frame and a frame ago through motion_clips; the
// fragment shader writes motion_uv of them as o_motion (location 1): UV from this frame's place to the last one's,
// both without the jitter, as shaders/temporal_prep.frag's camera motion is. The pipeline masks it off where no
// temporal upscaler runs, and the same shader is used either way.
//
// Define MOTION_SET (the set the scene's shadow set is bound at) before including.

layout(set = MOTION_SET, binding = 5) uniform Temporal {
    mat4 view_proj;      // this frame's, unjittered
    mat4 prev_view_proj; // the last frame's, unjittered (this frame's without a history)
    vec4 info;           // x: 1 when the motion is drawn
} temporal;

// The clip-space places a frame ago and now of a vertex at `world` now and `prev_world` a frame ago.
void motion_clips(vec3 world, vec3 prev_world, out vec4 now, out vec4 then) {
    now = temporal.view_proj * vec4(world, 1.0);
    then = temporal.prev_view_proj * vec4(prev_world, 1.0);
}

// The motion in UV (the viewport is flipped: NDC y is up, UV y down); far off the screen (the resolve drops the
// history) when the place a frame ago was behind the camera.
vec2 motion_uv(vec4 now, vec4 then) {
    if (then.w <= 1e-6 || now.w <= 1e-6) return vec2(4.0);
    vec2 moved = then.xy / then.w - now.xy / now.w;
    return vec2(moved.x * 0.5, -moved.y * 0.5);
}

#include "reactive.glsl"
