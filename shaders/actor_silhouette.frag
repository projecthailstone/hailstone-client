#version 450

// The isometric camera's silhouette of the player (render/actor_renderer.h draw_silhouette; docs/isometric.md): drawn
// where the player is behind what is already in the scene (depth test LESS, no depth write), before the characters,
// so the player drawn after covers it wherever they are in sight. A flat colour, stronger toward the outline.

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 fog_color; // here: the silhouette's colour (linear) and alpha
    vec4 fog;
    vec4 camera_pos;
    uint texture_index;
    uint bone_offset;
    uint tint;
    uint texture2;
} pc;

layout(location = 1) in vec3 v_world;
layout(location = 4) in vec3 v_normal;

layout(location = 0) out vec4 out_color;
layout(location = 2) out float o_reactive; // the temporal upscaler's reactive mask (render/vk_context.h SceneOutput)

void main() {
    float len = length(v_normal);
    vec3 n = len > 1e-6 ? v_normal / len : vec3(0.0, 0.0, 1.0);
    vec3 to_eye = normalize(pc.camera_pos.xyz - v_world);
    float rim = 1.0 - abs(dot(n, to_eye));
    float a = pc.fog_color.a * (0.35 + 0.65 * rim * rim);
    out_color = vec4(pc.fog_color.rgb * a, a); // premultiplied
    o_reactive = clamp(a, 0.0, 1.0) * 0.9;
}
