#version 450

// The temporal upscaler's resolve (render/vk_context.cpp begin_overlay; docs/graphics.md "Temporal upscaling"): this
// frame's jittered samples at the scene's size, accumulated into a history at the view's size. Runs on linear colour
// before the bloom and the tonemap; the post pass reads the result.
//
// Each output pixel takes the samples round it, weighted by how near each fell (the jitter moves them across the
// pixel from frame to frame), and last frame's history at the place the motion says the surface was. The history is
// clipped to the colours the samples round it span, so what changed (a disocclusion, a moving character, a flicker)
// cannot leave a trail. The history's alpha counts the weight it holds: a new or rejected history takes the frame
// as it is, and an old one takes a little of each frame, never less than 1 / kMaxWeight of it.

layout(set = 0, binding = 0) uniform sampler2D lit;        // temporal_prep.frag's colour (a: reactive), scene size
layout(set = 0, binding = 1) uniform sampler2D motion_map; // its motion, UV to last frame (nearest)
layout(set = 0, binding = 2) uniform sampler2D depth_map;  // the scene's depth (nearest)
layout(set = 0, binding = 3) uniform sampler2D history;    // last frame's result, view size (linear filter)

layout(push_constant) uniform Push {
    vec4 sizes;  // the scene's width and height, the view's
    vec4 jitter; // xy: this frame's jitter in scene pixels (x right, y down); z: the exposure; w: 1 to drop the history
    vec4 params; // x: the most weight the history keeps (kMaxWeight)
} pc;

layout(location = 0) out vec4 o_color;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Bright samples squeezed before they are mixed (by the exposed luma), so one hot pixel cannot dominate its
// neighbourhood or flicker in the history; expand undoes it.
vec3 compress(vec3 c) { return c / (1.0 + luma(c) * pc.jitter.z); }
vec3 expand(vec3 c) { return c / max(1.0 - luma(c) * pc.jitter.z, 1e-4); }

vec3 to_ycocg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}
vec3 from_ycocg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

// The history at `uv` through a Catmull-Rom spline (sharper than bilinear, which would blur it a little more each
// frame), from five bilinear taps: the 4 x 4 footprint's corners carry little weight and are left out.
vec3 history_at(vec2 uv) {
    vec2 size = pc.sizes.zw;
    vec2 pos = uv * size;
    vec2 c = floor(pos - 0.5) + 0.5;
    vec2 f = pos - c;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t0 = (c - 1.0) / size;
    vec2 t3 = (c + 2.0) / size;
    vec2 t12 = (c + w2 / w12) / size;
    float a = w12.x * w0.y, b = w0.x * w12.y, m = w12.x * w12.y, d = w3.x * w12.y, e = w12.x * w3.y;
    vec3 sum = textureLod(history, vec2(t12.x, t0.y), 0.0).rgb * a + textureLod(history, vec2(t0.x, t12.y), 0.0).rgb * b +
               textureLod(history, t12, 0.0).rgb * m + textureLod(history, vec2(t3.x, t12.y), 0.0).rgb * d +
               textureLod(history, vec2(t12.x, t3.y), 0.0).rgb * e;
    return max(sum / (a + b + m + d + e), vec3(0.0)); // the spline's lobes can go below zero
}

// `q` pulled toward the box's centre until it lies inside it.
vec3 clip_to_box(vec3 q, vec3 lo, vec3 hi) {
    vec3 centre = 0.5 * (hi + lo);
    vec3 extent = 0.5 * (hi - lo) + 1e-5;
    vec3 v = q - centre;
    vec3 a = abs(v / extent);
    float m = max(a.x, max(a.y, a.z));
    return m > 1.0 ? centre + v / m : q;
}

void main() {
    vec2 scene_size = pc.sizes.xy;
    vec2 view_size = pc.sizes.zw;
    vec2 uv = gl_FragCoord.xy / view_size;
    // Where this pixel's centre lies among the samples: scene texel t shows the unjittered point t - jitter.
    vec2 pos = uv * scene_size + pc.jitter.xy;
    ivec2 centre = ivec2(floor(pos));
    ivec2 last = ivec2(scene_size) - 1;
    vec2 to_view = view_size / scene_size;

    vec3 sum = vec3(0.0), m1 = vec3(0.0), m2 = vec3(0.0);
    vec3 lo = vec3(1e9), hi = vec3(-1e9);
    float weight = 0.0, nearest = 0.0, front = -1.0;
    ivec2 front_at = clamp(centre, ivec2(0), last);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            ivec2 q = clamp(centre + ivec2(x, y), ivec2(0), last);
            vec3 c = to_ycocg(compress(texelFetch(lit, q, 0).rgb));
            m1 += c;
            m2 += c * c;
            lo = min(lo, c);
            hi = max(hi, c);
            // A smooth reconstruction (a Gaussian fit of Blackman-Harris) over the scene's pixels...
            vec2 off = vec2(q) + 0.5 - pos;
            float w = exp(-2.29 * dot(off, off));
            sum += c * w;
            weight += w;
            // ... and how much this frame is worth here: how near a sample fell, in the view's pixels.
            vec2 out_off = off * to_view;
            nearest = max(nearest, exp(-2.29 * dot(out_off, out_off)));
            // The nearest surface's motion (reverse Z: the greatest depth): edges move with what is in front.
            float d = texelFetch(depth_map, q, 0).r;
            if (d > front) {
                front = d;
                front_at = q;
            }
        }
    vec3 current = sum / max(weight, 1e-6);
    // Particles, spells and other draws without motion of their own: the history gives way to this frame there.
    float reactive = texelFetch(lit, clamp(centre, ivec2(0), last), 0).a;
    vec2 motion = texelFetch(motion_map, front_at, 0).xy;
    vec2 prev_uv = uv + motion;

    float kept = 0.0;
    vec3 blended = current;
    bool inside = all(greaterThanEqual(prev_uv, vec2(0.0))) && all(lessThanEqual(prev_uv, vec2(1.0)));
    if (pc.jitter.w < 0.5 && inside) {
        vec3 h = to_ycocg(compress(history_at(prev_uv)));
        kept = textureLod(history, prev_uv, 0.0).a;
        // The box the neighbourhood spans: its mean +- 1.25 standard deviations, inside its extremes.
        vec3 mean = m1 / 9.0;
        vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
        vec3 box_lo = max(mean - 1.25 * sigma, lo);
        vec3 box_hi = min(mean + 1.25 * sigma, hi);
        vec3 clipped = clip_to_box(h, box_lo, box_hi);
        // A history pulled far keeps less of its weight; so does one carried far (resampling softens it).
        float pulled = length(clipped - h) / (length(box_hi - box_lo) + 1e-4);
        kept *= clamp(1.0 - pulled, 0.0, 1.0) * (1.0 - reactive);
        float speed = length(motion * view_size);
        kept = min(kept, mix(pc.params.x, 4.0, clamp(speed / 32.0, 0.0, 1.0)));
        blended = (clipped * kept + current * nearest) / (kept + nearest);
    }
    o_color = vec4(expand(from_ycocg(blended)), min(kept + nearest, pc.params.x));
}
