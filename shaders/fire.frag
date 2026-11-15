#version 450
#extension GL_GOOGLE_include_directive : require

// The modern lighting's fire (render/fire_renderer.h, fire.vert). A flame is heat in a teardrop: widest at its foot,
// narrowing to its tip one height up. Two fields of noise rise through it, the finer faster, bending it and eating
// tongues out of it; the whole flame sways, more at the top. The heat sets the colour, deep red at the edges through
// orange to a white-yellow core, and the light, which is added to the scene (past 1, so it blooms). Embers drift up
// out of it and fade. Where the flame meets the scene (the opaque world, VkContext::begin_translucent's depth copy)
// it fades over a fraction of its width, and it fades with the haze like what is behind it.

layout(set = 0, binding = 2) uniform sampler2D scene_depth; // render/vk_context.h shadow_set: the depth copy

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera_time;
    vec4 screen;
    vec4 haze;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_world;
layout(location = 2) flat in vec4 v_shape;
layout(location = 3) flat in vec4 v_tint;

layout(location = 0) out vec4 o_color;
layout(location = 2) out float o_reactive; // the temporal upscaler's reactive mask (shaders/reactive.glsl)

#include "reactive.glsl"

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x),
               u.y);
}

float fbm(vec2 p) {
    float sum = 0.0, amp = 0.5;
    for (int i = 0; i < 4; ++i) {
        sum += noise(p) * amp;
        p = p * 2.03 + vec2(1.7, 9.2);
        amp *= 0.5;
    }
    return sum / 0.9375;
}

// The colour of the fire at a heat of 0 (its edge) to 1 (its core).
vec3 fire_color(float h) {
    vec3 c = mix(vec3(0.7, 0.06, 0.01), vec3(1.0, 0.3, 0.04), smoothstep(0.05, 0.4, h));
    c = mix(c, vec3(1.0, 0.62, 0.2), smoothstep(0.4, 0.8, h));
    return mix(c, vec3(1.0, 0.88, 0.62), smoothstep(0.85, 1.0, h));
}

void main() {
    float t = pc.camera_time.w;
    float seed = v_shape.z;
    float x = v_uv.x, y = v_uv.y;

    // The rising turbulence, and the sway.
    float n = fbm(vec2(x * 1.1, y * 1.8 - t * 2.1) + seed * 13.1);
    float fine = fbm(vec2(x * 2.8, y * 4.2 - t * 3.9) + seed * 7.7 + 5.0);
    float sway = (noise(vec2(t * 0.8, seed * 3.0)) - 0.5) * 0.8 * y * y;
    float bend = x - sway - (n - 0.5) * 0.9 * y;
    // The teardrop's half-width at this height: full a little above the foot, a point at one height.
    // Narrow where it burns from, widest a fifth of the way up, drawn out to its tip.
    float width = 0.9 * pow(clamp(1.0 - y * 0.85, 0.0, 1.0), 0.7) * (0.35 + 0.65 * smoothstep(-0.1, 0.22, y));
    float body = clamp(1.0 - abs(bend) / max(width, 1e-3), -1.0, 1.0);
    // Heat: the body, cooler going up, the fine noise breaking it into tongues that tear off near the top.
    float heat = body * max(1.2 - y, 0.0) + (fine - 0.5) * 0.9 - (1.0 - n) * y * 1.1;
    heat = clamp(heat, 0.0, 1.0) * smoothstep(-0.05, 0.14, y);
    vec3 light = fire_color(heat) * smoothstep(0.05, 0.3, heat) * (0.3 + 2.0 * heat * heat * heat);

    // Embers: sparks that rise out of the flame on their own paths and die out.
    float half_width = v_shape.x, height = v_shape.y;
    for (int i = 0; i < 6; ++i) {
        float fi = float(i);
        float h = hash12(vec2(seed * 31.0 + fi, fi * 7.3));
        float period = 1.4 + h * 1.6;
        float cycle = t / period + h * 5.0;
        float age = fract(cycle);
        float id = floor(cycle);
        float start = (hash12(vec2(id, fi + seed)) - 0.5) * 1.0;
        vec2 at = vec2(start + sin(t * 2.1 + fi * 1.7) * 0.18 * age, 0.35 + age * 1.3);
        vec2 d = vec2((x - at.x) * half_width, (y - at.y) * height);
        float r = 0.05 + 0.03 * h;
        float spark = smoothstep(r, 0.0, length(d)) * (1.0 - age) * step(0.4, hash12(vec2(id * 1.3, fi)));
        light += vec3(1.0, 0.42, 0.08) * spark * 3.0;
    }

    // Soft where it meets the scene.
    vec2 suv = gl_FragCoord.xy * pc.screen.zw;
    float sd = texture(scene_depth, suv).r;
    float scene_depth_v = sd > 0.0 ? pc.screen.y / (sd + pc.screen.x) : 1e6;
    float own = pc.screen.y / (gl_FragCoord.z + pc.screen.x);
    float soft = clamp((scene_depth_v - own) / max(half_width * 0.5, 0.1), 0.0, 1.0);

    // The haze between it and the eye (render::set_haze).
    float through = 1.0;
    if (pc.haze.z > 0.5) {
        vec3 dv = v_world - pc.camera_time.xyz;
        float dist = length(dv) * pc.haze.x;
        through = exp(-3.5 * dist * dist * exp(-max(dv.z, 0.0) * pc.haze.y));
    }
    // A magic flame (blue, green...): the fire's brightness in its hue, its hottest parts going white.
    if (v_tint.w > 0.0) {
        float lum = dot(light, vec3(0.3, 0.55, 0.15));
        vec3 magic = v_tint.rgb * lum * 1.4 + vec3(max(lum - 2.0, 0.0) * 0.3);
        light = mix(light, magic, v_tint.w);
    }
    o_color = vec4(light * v_shape.w * pc.haze.w * soft * through, 0.0);
    o_reactive = reactive_of(o_color);
}
