#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

// The flora (render/flora_renderer.h, flora.vert): the picture cut out at half alpha. Modern: the sun or moon through
// its shadow (so the grass in a tree's shadow darkens with the ground), the vertex's other light, a little darker at
// the roots, into the haze. Classic: the scene's one light.

#include "lighting.glsl"

// render/scene_lighting.h SceneLighting.
layout(std430, set = 0, binding = 0) readonly buffer Lighting {
    SCENE_LIGHTING_FIELDS
} lighting;

layout(std430, set = 0, binding = 1) readonly buffer Frame {
    vec4 classic;
    vec4 params;
    vec4 pushers[8];
} frame;

layout(set = 0, binding = 2) uniform sampler2D textures[];

layout(set = 1, binding = 0) uniform sampler2DArrayShadow shadow_map; // render/vk_context.h shadow_set

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_light;
layout(location = 2) in vec3 v_world;
layout(location = 3) flat in vec3 v_tint;
layout(location = 4) flat in uint v_texture;
layout(location = 5) in float v_root;
layout(location = 6) in vec4 v_now;
layout(location = 7) in vec4 v_then;

layout(location = 0) out vec4 o_color;
layout(location = 1) out vec2 o_motion; // the plants' own motion in the wind (render/vk_context.h SceneOutput)

#define MOTION_SET 1
#include "motion.glsl"

// The sun's shadow at a blade: shadow_visibility's cascade and fade with one hardware-filtered comparison (grass is
// small and drawn by the thousand; its 3 x 3 grid would cost nine).
float blade_shadow(vec3 p) {
    if (lighting.shadow_params.x < 0.5) return 1.0;
    vec4 splits = lighting.shadow_splits;
    float depth = dot(p - lighting.shadow_origin.xyz, lighting.shadow_forward.xyz);
    int c = depth < splits.x ? 0 : depth < splits.y ? 1 : depth < splits.z ? 2 : depth < splits.w ? 3 : 4;
    if (c > 3) return 1.0;
    vec4 s = lighting.shadow_view_proj[c] * vec4(p + vec3(0.0, 0.0, lighting.shadow_texel[c] * 1.5), 1.0);
    vec3 q = s.xyz / s.w;
    float lit = texture(shadow_map, vec4(q.xy * 0.5 + 0.5, float(c), q.z));
    return mix(lit, 1.0, smoothstep(splits.w * 0.85, splits.w, depth));
}

void main() {
    o_motion = motion_uv(v_now, v_then);
    // The isometric camera's cut (game/iso_occlusion.h).
    if (reveal_discard(v_world, gl_FragCoord.xy, lighting.reveal_origin, lighting.reveal_axis, lighting.reveal_shape,
                       lighting.reveal_roof))
        discard;
    vec4 c = texture(textures[nonuniformEXT(v_texture)], v_uv);
    if (c.a < 0.5) discard;
    vec3 albedo = to_linear(c.rgb) * v_tint;
    if (frame.classic.w < 0.5) {
        o_color = vec4(albedo * v_light, 1.0);
        return;
    }
    // Lit from above and through the blade: the alpha-tested plants' wrapped light (object.frag).
    float ndl = clamp(lighting.to_light.z * 0.6 + 0.4, 0.0, 1.0) * blade_shadow(v_world);
    float roots = mix(0.65, 1.0, clamp(v_root, 0.0, 1.0));
    vec3 light = (v_light + lighting.light.rgb * ndl) * roots;
    vec3 surface = albedo * max(light, vec3(lighting.params.w));
    o_color = vec4(apply_haze(surface, v_world, lighting.camera, lighting.haze, lighting.haze_color, lighting.to_light,
                              lighting.light),
                   1.0);
}
