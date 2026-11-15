#version 450
#extension GL_GOOGLE_include_directive : require

// A precipitation particle (shaders/precipitation.vert): its texture's frame times the sky's colour for it, alpha
// blended. A far facing one (v_dot, the modern style) fades into a soft round dot of the texture's colour.

layout(set = 0, binding = 0) uniform sampler2D textures[16];

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera;
    vec4 forward;
    vec4 style;
    uvec4 texture_slot;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec2 v_corner;
layout(location = 3) in float v_dot;
layout(location = 0) out vec4 o_color;
layout(location = 2) out float o_reactive; // the temporal upscaler's reactive mask (shaders/reactive.glsl)

#include "reactive.glsl"

void main() {
    vec4 t = texture(textures[pc.texture_slot.x], v_uv);
    float disc = clamp(1.0 - dot(v_corner, v_corner), 0.0, 1.0);
    float a = mix(t.a, disc * disc, v_dot) * v_color.a;
    if (a <= 0.004) discard;
    o_color = vec4(t.rgb * v_color.rgb, a);
    o_reactive = reactive_of(o_color);
}
