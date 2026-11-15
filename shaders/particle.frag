#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

// Particles (render/particle_renderer.cpp). Classic: texture x colour, in the light the CPU gave (1, or the glow
// for the additive ones), blended as the pipeline says. Modern: premultiplied for the one sorted stream, an additive
// particle with alpha 0; it fades where it meets the scene (VkContext::begin_translucent's depth copy) and close to
// the eye, and into the haze: smoke and dust towards the haze's colour, light by what reaches the eye.

#include "lighting.glsl"

layout(set = 0, binding = 0) uniform sampler2D textures[];
layout(set = 1, binding = 2) uniform sampler2D scene_depth; // render/vk_context.h shadow_set: the depth copy

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 screen; // SceneLighting::screen (modern)
    vec4 params; // x: 1 modern
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) flat in uint v_texture; // bit 31: additive (modern)
layout(location = 3) in vec4 v_shade;        // rgb the light (linear), w the depth it fades over (modern)
layout(location = 4) in vec4 v_haze;         // rgb the haze in front of it, w how much of it shows through

layout(location = 0) out vec4 o_color;
layout(location = 2) out float o_reactive; // the temporal upscaler's reactive mask (shaders/reactive.glsl)

#include "reactive.glsl"

void main() {
    vec4 c = texture(textures[nonuniformEXT(v_texture & 0x7fffffffu)], v_uv) * v_color;
    vec3 light = to_linear(c.rgb) * v_shade.rgb; // the scene target is linear
    if (pc.params.x < 0.5) {
        o_color = vec4(light, c.a);
        o_reactive = reactive_of(o_color);
        return;
    }
    // Soft where it meets the scene and as it nears the eye (reverse Z: view depth = y / (d + x)).
    float sd = texture(scene_depth, gl_FragCoord.xy * pc.screen.zw).r;
    float behind = sd > 0.0 ? pc.screen.y / (sd + pc.screen.x) : 1e6;
    float own = pc.screen.y / (gl_FragCoord.z + pc.screen.x);
    float soft = smoothstep(0.0, 1.0, clamp((behind - own) / v_shade.w, 0.0, 1.0)) *
                 clamp((own - 1.0) / max(v_shade.w, 1.0), 0.0, 1.0);
    float a = c.a * soft;
    if ((v_texture & 0x80000000u) != 0u)
        o_color = vec4(light * v_haze.w * a, 0.0);
    else
        o_color = vec4(mix(v_haze.rgb, light, v_haze.w) * a, a);
    o_reactive = reactive_of(o_color);
}
