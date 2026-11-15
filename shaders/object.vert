#version 450
#extension GL_GOOGLE_include_directive : require

// Doors and ground objects: static meshes drawn with a per-instance model
// matrix (render/object_renderer.h). Same look as zone objects (zone.frag).

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color; // a: fog density
    vec4 fog; // start, end, alpha_ref, opacity
    vec4 camera_pos; // w: time-of-day brightness
    uint texture_index;
    uint instance; // index into the model matrices
} pc;

layout(std430, set = 0, binding = 0) readonly buffer Instances {
    mat4 models[];
};

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_world;
layout(location = 2) out float v_fog;
layout(location = 3) out vec3 v_normal; // world space (the modern lighting, object.frag)
layout(location = 4) out vec4 v_now;    // the motion (shaders/motion.glsl): this frame's place, unjittered
layout(location = 5) out vec4 v_then;   // and the last frame's

#define MOTION_SET 1
#include "motion.glsl"

// The last frame's transforms sit kPrevInstances after this frame's (render/object_renderer.h kMaxInstances).
const uint kPrevInstances = 8192u;

// The effects' fog (RegionC1 vs_1_1): per
// vertex, f = clamp((|eye - pos| - FogStart) FogRangeInv fFogRange, 0, fFogRange)
// with FogRangeInv = 1 / (end - start), fFogRange = 10, and
// oFog = exp(-(f FogDensity)^2) (1: no fog); the pixel is lerp(fog, colour, oFog).
float fog_factor(vec3 p) {
    float dist = length(pc.camera_pos.xyz - p);
    float range_inv = 1.0 / max(pc.fog.y - pc.fog.x, 1.0);
    float f = clamp((dist - pc.fog.x) * range_inv * 10.0, 0.0, 10.0);
    float x = f * pc.fog_color.a;
    return exp(-(x * x));
}

void main() {
    vec4 world = models[pc.instance] * vec4(in_position, 1.0);
    v_normal = mat3(models[pc.instance]) * in_normal;
    v_uv = in_uv;
    v_world = world.xyz;
    v_fog = fog_factor(world.xyz);
    gl_Position = pc.view_proj * world;
    if (temporal.info.x > 0.0)
        motion_clips(world.xyz, (models[pc.instance + kPrevInstances] * vec4(in_position, 1.0)).xyz, v_now, v_then);
    else
        v_now = v_then = vec4(0.0, 0.0, 0.0, 1.0);
}
