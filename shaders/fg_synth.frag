#version 450

// Frame generation, step 3: the in-between frame at the view's size, from the last real frame and this one (the
// pictures without the UI, display-encoded) and step 2's motion at that time. Each pixel takes both frames along the
// motion and blends them by the time. Where the frames disagree there, one of them shows something the other hides:
// a pixel step 2 filled is being covered (only the last frame shows it); anything else that disagrees was uncovered
// (this frame shows it; the last one has what stood in front) and takes this frame. Where a sample leads off the
// screen the other frame is used. Draws without motion of their own (particles, spells, rain: the reactive mask) take
// this frame along the motion, sharp, rather than two copies a little apart. The choices follow kettle-framegen's
// (the same author's Vulkan layer), with the engine's motion and depth in place of its motion search.

layout(set = 0, binding = 8) uniform sampler2D motion_t;     // fg_fill.comp's (scene size; nearest)
layout(set = 0, binding = 5) uniform sampler2D prev_img;     // the last real frame without the UI (view size)
layout(set = 0, binding = 6) uniform sampler2D cur_img;      // this frame without the UI
layout(set = 0, binding = 7) uniform sampler2D reactive_map; // the scene's reactive mask (scene size)

layout(push_constant) uniform Push {
    vec2 view;  // the view's size, pixels
    float t;    // the in-between time
    float reactive; // 1: reactive_map is the scene's reactive mask; 0: there is none (it holds something else)
} pc;

layout(location = 0) out vec4 o_color;

bool inside(vec2 uv) { return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))); }

void main() {
    vec2 uv = gl_FragCoord.xy / pc.view;
    vec4 f = textureLod(motion_t, uv, 0.0);
    vec2 v = f.xy;
    // At time t the point is (1 - t) of the way from this frame back to the last.
    vec2 uc = uv - (1.0 - pc.t) * v;
    vec2 up = uv + pc.t * v;
    vec3 c = textureLod(cur_img, uc, 0.0).rgb;
    vec3 p = textureLod(prev_img, up, 0.0).rgb;
    bool in_c = inside(uc), in_p = inside(up);
    vec3 result;
    if (!in_p) {
        result = c;
    } else if (!in_c) {
        result = p;
    } else if (f.z > 0.5) {
        result = p; // being covered: only the last frame shows it
    } else {
        vec3 d = abs(c - p);
        float disagree = smoothstep(0.08, 0.25, max(d.r, max(d.g, d.b)));
        result = mix(mix(p, c, pc.t), c, disagree);
    }
    float reactive = pc.reactive > 0.5 ? textureLod(reactive_map, uv, 0.0).r : 0.0;
    if (in_c) result = mix(result, c, clamp(reactive / 0.9, 0.0, 1.0));
    o_color = vec4(result, 1.0);
}
