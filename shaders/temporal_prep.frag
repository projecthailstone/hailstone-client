#version 450
#extension GL_GOOGLE_include_directive : require

// The temporal upscaler's inputs at the scene's size (render/vk_context.cpp begin_overlay; docs/graphics.md
// "Temporal upscaling"): the scene with its AO applied (the AO belongs to the lighting, before the resolve, so the
// history smooths its pattern), and each pixel's motion: where the surface it shows was a frame ago, in UV.
//
// The motion is what the draw in front wrote of its own (the object-motion attachment: characters, doors, swaying
// plants and the zone; shaders/motion.glsl) or, where none did (the sky, particles), the camera's, rebuilt from the
// depth (render/temporal.h reprojection). The reactive mask goes on in the colour's alpha: taa_resolve.frag lets the
// history give way where it is high.

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D ao_map;
layout(set = 0, binding = 2) uniform sampler2D depth_map;
layout(set = 0, binding = 4) uniform sampler2D object_motion; // kMotionUnset where no draw wrote one
layout(set = 0, binding = 5) uniform sampler2D reactive_map;

layout(push_constant) uniform Push {
    mat4 reproject; // this frame's clip space (unjittered) to the last frame's
    vec4 depth;     // view depth = y / (d + x); z: the AO's strength; w: 1 to apply the AO
    vec4 jitter;    // xy: this frame's jitter in NDC (render/temporal.h jitter_ndc); z: 1 when bindings 4 and 5 were drawn
} pc;

layout(location = 0) out vec4 o_color;
layout(location = 1) out vec2 o_motion;

#include "ao_blur.glsl"

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 c = texelFetch(scene, p, 0).rgb;
    float d = texelFetch(depth_map, p, 0).r;
    if (pc.depth.w > 0.0 && d > 0.0) c *= mix(1.0, blurred_ao(ao_map, depth_map, p, pc.depth.xy), pc.depth.z);
    bool drawn = pc.jitter.z > 0.5;
    o_color = vec4(c, drawn ? texelFetch(reactive_map, p, 0).r : 0.0);
    vec2 own = drawn ? texelFetch(object_motion, p, 0).xy : vec2(30000.0);
    if (own.x < 10000.0) { // render/vk_context.h kMotionUnset (30000) is far above any real motion (a few UV)
        o_motion = own;
        return;
    }

    // The pixel's centre in NDC (the viewport is flipped: y up), less the jitter: where the surface lies unjittered.
    vec2 size = vec2(textureSize(depth_map, 0));
    vec2 uv = gl_FragCoord.xy / size;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0) - pc.jitter.xy;
    vec4 prev = pc.reproject * vec4(ndc, d, 1.0);
    if (prev.w <= 1e-6) {
        // Behind last frame's camera: nothing to follow. Far outside the screen, so the resolve drops its history.
        o_motion = vec2(4.0);
        return;
    }
    vec2 moved = prev.xy / prev.w - ndc;
    o_motion = vec2(moved.x * 0.5, -moved.y * 0.5);
}
