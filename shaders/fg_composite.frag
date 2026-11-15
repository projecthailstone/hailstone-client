#version 450

// A presented frame under frame generation (render/vk_context.cpp end_frame): the picture without the UI (a real
// frame's or a generated one, display-encoded, in the 3D view's rectangle; black round it) with the UI over it. The UI
// was drawn once into an image of its own, cleared to nothing, with the UI's usual blending: its colour premultiplied,
// its alpha the coverage, so it lies over every frame as it would have been drawn over it.

layout(set = 0, binding = 0) uniform sampler2D picture; // the view's size
layout(set = 0, binding = 1) uniform sampler2D overlay; // the window's size, the swapchain's format

layout(push_constant) uniform Push {
    vec4 area;  // the 3D view's rectangle on the window: x, y, width, height (pixels)
    uint flags; // 1: the swapchain is sRGB (the overlay reads back linear, and the output is encoded by it)
} pc;

layout(location = 0) out vec4 o_color;

vec3 srgb_to_linear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045))); }

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 local = gl_FragCoord.xy - pc.area.xy;
    vec3 base = vec3(0.0);
    if (all(greaterThanEqual(local, vec2(0.0))) && all(lessThan(local, pc.area.zw)))
        base = texelFetch(picture, ivec2(local), 0).rgb;
    if ((pc.flags & 1u) != 0u) base = srgb_to_linear(base);
    vec4 ui = texelFetch(overlay, p, 0);
    o_color = vec4(ui.rgb + base * (1.0 - ui.a), 1.0);
}
