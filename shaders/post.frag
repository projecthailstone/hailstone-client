#version 450
#extension GL_GOOGLE_include_directive : require

// The scene target (linear) into the swapchain: ambient occlusion, exposure, tonemap, encode, dither
// (render/vk_context.h SceneOptions).

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D ao_map;    // shaders/ao.frag
layout(set = 0, binding = 2) uniform sampler2D depth_map; // the scene's depth (sample 0)
layout(set = 0, binding = 3) uniform sampler2D bloom_map; // shaders/bloom_up.frag: the sum of 6 levels, half size

layout(push_constant) uniform Push {
    float exposure; // linear scale
    uint tonemap;   // 0 none, 1 AgX, 2 ACES
    uint flags;
    uint frame;
    vec4 depth;     // view depth = y / (d + x); z: the AO's strength; w: the far plane
    vec4 grade;     // brightness, contrast, gamma (render/vk_context.h SceneOptions); w: the bloom's share
    vec4 sun;       // the shafts (render/vk_context.h SunShafts): the sun's UV, strength (0 none)
    vec4 sun_color; // their light, linear
    vec4 offset;    // the scene's place on the image (xy, pixels; /viewport)
    vec4 mood;      // the colour grade (render/atmosphere.h ColorGrade): rgb a tint before the tonemap, w saturation
} pc;

layout(location = 0) out vec4 o_color;

const uint kEncodeOutput = 2u;
const uint kDither = 4u;
const uint kAo = 8u;
const uint kBlur = 16u;

#include "ao_blur.glsl"

float view_depth(float d) { return ao_view_depth(d, pc.depth.xy); }

// The depth texel under view texel `p`. Under the temporal upscaler this pass runs at the view's size while the
// depth stays at the scene's; otherwise the two are the same.
ivec2 depth_texel(ivec2 p) {
    vec2 scale = vec2(textureSize(depth_map, 0)) / vec2(textureSize(scene, 0));
    return ivec2((vec2(p) + 0.5) * scale);
}

vec3 linear_to_srgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, greaterThan(c, vec3(0.0031308)));
}

// AgX (Troy Sobotka), the minimal fit by Benjamin Wrensch: linear Rec.709 in, linear Rec.709 out.
vec3 agx_contrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 c) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float min_ev = -12.47393;
    const float max_ev = 4.026069;
    c = inset * c;
    c = clamp(log2(max(c, vec3(1e-10))), min_ev, max_ev);
    c = (c - min_ev) / (max_ev - min_ev);
    c = agx_contrast(c);
    // A little more saturation than the base look ("punchy"), so warm lamplight and skies keep their colour.
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = max(luma + 1.2 * (c - luma), vec3(0.0));
    c = outset * c;
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

// ACES filmic, Narkowicz's fit.
vec3 aces(vec3 c) {
    return clamp(c * (2.51 * c + 0.03) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
}

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// God rays: from the pixel toward the sun, 40 steps over the depth, adding the light of the open sky near the sun
// (a glow round it) and fading with each step; what blocks the sky (trees, walls) leaves dark gaps, the shafts between
// them. The start is jittered per pixel and frame against banding.
vec3 sun_shafts(ivec2 p) {
    vec2 size = vec2(textureSize(scene, 0));
    vec2 uv = (vec2(p) + 0.5) / size;
    const int kSteps = 40;
    vec2 delta = (pc.sun.xy - uv) / float(kSteps);
    vec2 s = uv + delta * hash(gl_FragCoord.xy + float(pc.frame % 64u) * vec2(13.0, 7.0));
    float aspect = size.x / size.y;
    float sum = 0.0, decay = 1.0;
    for (int i = 0; i < kSteps; ++i) {
        if (s.x >= 0.0 && s.y >= 0.0 && s.x <= 1.0 && s.y <= 1.0 && textureLod(depth_map, s, 0.0).r <= 0.0) {
            vec2 off = (s - pc.sun.xy) * vec2(aspect, 1.0);
            sum += exp(-dot(off, off) * 30.0) * decay;
        }
        decay *= 0.97;
        s += delta;
    }
    return pc.sun_color.rgb * (sum / float(kSteps)) * pc.sun.z;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy - pc.offset.xy);
    vec3 c = texelFetch(scene, p, 0).rgb;
    if ((pc.flags & kBlur) != 0u) {
        // The distance softens: up to 1.75 pixels at the far plane, from a quarter of the way out. Not the sky: its
        // stars would smear into blobs.
        float d = texelFetch(depth_map, depth_texel(p), 0).r;
        float z = d > 0.0 ? view_depth(d) : 0.0;
        float r = smoothstep(0.25, 1.0, z / pc.depth.w) * 1.75;
        if (r > 0.2) {
            vec2 size = vec2(textureSize(scene, 0));
            vec2 uv = (vec2(p) + 0.5) / size;
            vec3 sum = c;
            for (int i = 0; i < 6; ++i) {
                float a = float(i) * 1.0471976 + 0.5;
                sum += texture(scene, uv + vec2(cos(a), sin(a)) * r / size).rgb;
            }
            c = sum / 7.0;
        }
    }
    // (Under the temporal upscaler temporal_prep.frag applied it, at the scene's size, and kAo is not set.)
    if ((pc.flags & kAo) != 0u && texelFetch(depth_map, p, 0).r > 0.0)
        c *= mix(1.0, blurred_ao(ao_map, depth_map, p, pc.depth.xy), pc.depth.z);
    if (pc.grade.w > 0.0) {
        vec2 uv = (vec2(p) + 0.5) / vec2(textureSize(scene, 0));
        c = mix(c, texture(bloom_map, uv).rgb / 6.0, pc.grade.w);
    }
    if (pc.sun.z > 0.0) c += sun_shafts(p);
    c *= pc.exposure * pc.mood.rgb;
    if (pc.tonemap == 1u) c = agx(c);
    else if (pc.tonemap == 2u) c = aces(c);
    c = linear_to_srgb(c);
    // The grade's saturation, round the pixel's own grey.
    c = clamp(mix(vec3(dot(c, vec3(0.2126, 0.7152, 0.0722))), c, pc.mood.w), 0.0, 1.0);
    // The player's grade, in display values: gamma, then contrast around mid grey, then brightness.
    c = pow(c, vec3(1.0 / pc.grade.z));
    c = clamp((c - 0.5) * pc.grade.y + 0.5 + pc.grade.x, 0.0, 1.0);
    if ((pc.flags & kDither) != 0u) {
        // Triangular noise of +-1 step of 8 bits, moving each frame.
        vec2 p = gl_FragCoord.xy + float(pc.frame % 64u) * vec2(17.0, 29.0);
        c += (hash(p) + hash(p + 71.3) - 1.0) / 255.0;
    }
    // An SRGB swapchain encodes it again.
    if ((pc.flags & kEncodeOutput) == 0u) c = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
    o_color = vec4(c, 1.0);
}
