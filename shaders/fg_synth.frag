#version 450

// Frame generation, step 3: the in-between frame at the view's size, from the last real frame and this one (the
// pictures without the UI, display-encoded) and step 2's motion at that time. Each pixel takes both frames along the
// motion and blends them by the time. Where the frames disagree there, one of them shows something the other hides:
// a pixel step 2 filled is being covered (only the last frame shows it); one whose last-frame sample lands where
// something nearer was then (fg_scatter.comp's keys at the last frame's places) came out from behind it and takes this
// frame, however alike the colours (a dark character the world turns behind, at night); anything else that disagrees
// was uncovered too and takes this frame. Where a sample leads off the
// screen the other frame is used. Draws without motion of their own (particles, spells, rain: the reactive mask) take
// this frame along the motion, sharp, rather than two copies a little apart. The choices follow kettle-framegen's
// (the same author's Vulkan layer), with the engine's motion and depth in place of its motion search.

layout(set = 0, binding = 8) uniform sampler2D motion_t;     // fg_fill.comp's (scene size; nearest)
layout(set = 0, binding = 5) uniform sampler2D prev_img;     // the last real frame without the UI (view size)
layout(set = 0, binding = 6) uniform sampler2D cur_img;      // this frame without the UI
layout(set = 0, binding = 7) uniform sampler2D reactive_map; // the scene's reactive mask (scene size)
layout(set = 0, binding = 0) uniform sampler2D depth_map;               // this frame's depth (scene size; nearest)
layout(set = 0, binding = 1) uniform sampler2D motion_raw; // its motion; blue a see-through part's depth (0 none)
layout(set = 0, binding = 2, r32ui) uniform readonly uimage2D key_img;      // fg_scatter.comp's: the key at each place
layout(set = 0, binding = 9, r32ui) uniform readonly uimage2D prev_key_img; // and at each place a frame ago

layout(push_constant) uniform Push {
    vec2 view;  // the view's size, pixels
    float t;    // the in-between time
    float reactive; // 1: reactive_map is the scene's reactive mask; 0: there is none (it holds something else)
} pc;

layout(location = 0) out vec4 o_color;

bool inside(vec2 uv) { return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))); }

float max3(vec3 d) { return max(d.r, max(d.g, d.b)); }

// fg_scatter.comp's keys are a reverse-Z depth's bits + 1 (0: nothing there).
float key_depth(uint k) { return uintBitsToFloat(k - 1u); }

// The nearest key a frame ago round `up` (the 3 x 3 texels): the pictures are at the view's size and the keys at the
// scene's, so a picture's pixel at an edge holds some of what the texels beside it mark (a character's outline).
uint prev_key_round(vec2 up, ivec2 size) {
    ivec2 q = ivec2(up * vec2(size));
    uint k = 0u;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) k = max(k, imageLoad(prev_key_img, clamp(q + ivec2(x, y), ivec2(0), size - 1)).r);
    return k;
}

// This frame's sample at `uc` holds some of something nearer than the point (`at`'s key): an edge's pixels at the
// view's size are partly what the scene's texel beside them shows (a character's outline, carried along the
// background's motion, would trace the character beside it).
bool edge_now(ivec2 at, vec2 uc, ivec2 size) {
    uint k = imageLoad(key_img, at).r;
    if (k == 0u) return false;
    float d = key_depth(k) * 1.05 + 1e-6;
    ivec2 q = ivec2(uc * vec2(size));
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            if (max(texelFetch(depth_map, clamp(q + ivec2(x, y), ivec2(0), size - 1), 0).r,
                    texelFetch(motion_raw, clamp(q + ivec2(x, y), ivec2(0), size - 1), 0).z) > d)
                return true;
    return false;
}

// The last frame's sample at `up` is of something nearer than `depth` (reverse Z; 0 unknown: no).
bool hidden_from(vec2 up, float depth, ivec2 size) {
    if (depth <= 0.0) return false;
    uint k0 = prev_key_round(up, size);
    return k0 != 0u && key_depth(k0) > depth * 1.05 + 1e-6;
}

// The last frame's sample along the motion is of something else when, where the point was then, something nearer
// was (a still character the world turns behind): however alike their colours, they must not be blended.
bool hidden_then(ivec2 at, vec2 up, ivec2 size) {
    uint k = imageLoad(key_img, at).r;
    uint k0 = prev_key_round(up, size);
    if (k == 0u || k0 == 0u) return false;
    return key_depth(k0) > key_depth(k) * 1.05 + 1e-6;
}

void main() {
    vec2 uv = gl_FragCoord.xy / pc.view;
    vec4 f = textureLod(motion_t, uv, 0.0);
    ivec2 size = textureSize(motion_t, 0);
    ivec2 at = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1); // the motion texel chosen (its key is the point's)
    // At time t the point is (1 - t) of the way from this frame back to the last.
    vec2 uc = uv - (1.0 - pc.t) * f.xy;
    vec2 up = uv + pc.t * f.xy;
    vec3 c = textureLod(cur_img, uc, 0.0).rgb;
    vec3 p = textureLod(prev_img, up, 0.0).rgb;
    bool in_c = inside(uc), in_p = inside(up);
    // The motion is at the scene's size and this pixel at the view's: at an edge (a character still on the screen
    // against a turning world) the nearest motion texel can be the other side's, which flings the pixel away. Of the
    // 3 x 3 texels round it, take the motion along which the two frames agree best (the centre, a little favoured,
    // when they agree as well). What is nearer is favoured and what is farther held back (kettle-framegen's rule): the
    // background's motion also agrees through a figure thinner than it moves (a sword), with background on both
    // sides, and taking it would cut the figure out. A hole (being covered) keeps its own: this frame shows what
    // covers it there.
    if (f.z < 0.5 && in_c && in_p) {
        vec2 texel = 1.0 / vec2(textureSize(motion_t, 0));
        float best = max3(abs(c - p));
        uint k_centre = imageLoad(key_img, at).r;
        ivec2 at_centre = at;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                if (x == 0 && y == 0) continue;
                vec4 g = textureLod(motion_t, uv + vec2(x, y) * texel, 0.0);
                if (g.z > 0.5) continue;
                vec2 gc = uv - (1.0 - pc.t) * g.xy, gp = uv + pc.t * g.xy;
                if (!inside(gc) || !inside(gp)) continue;
                vec3 cc = textureLod(cur_img, gc, 0.0).rgb, pp = textureLod(prev_img, gp, 0.0).rgb;
                float cost = max3(abs(cc - pp)) + 0.02;
                uint k = imageLoad(key_img, clamp(at_centre + ivec2(x, y), ivec2(0), size - 1)).r;
                if (k != 0u && k_centre != 0u) {
                    if (key_depth(k) > key_depth(k_centre) * 1.05 + 1e-6) cost -= 0.04; // nearer: a figure's edge
                    else if (key_depth(k) * 1.05 + 1e-6 < key_depth(k_centre)) cost += 0.15; // farther
                }
                if (cost < best) {
                    best = cost;
                    f = g;
                    at = clamp(at_centre + ivec2(x, y), ivec2(0), size - 1);
                    uc = gc;
                    up = gp;
                    c = cc;
                    p = pp;
                }
            }
    }
    vec3 result;
    if (!in_p) {
        result = c;
    } else if (!in_c) {
        result = p;
    } else if (f.z > 0.5) {
        // Being covered: only the last frame shows it. Where the last frame had something nearer at that place (a
        // sword the background passes behind, thinner than what this frame leaves of it), the background is in
        // neither frame there: the nearest sample of the last frame round it that shows background stands in.
        result = p;
        if (hidden_from(up, f.w, size)) {
            vec2 texel = 1.0 / vec2(size);
            const vec2 kDirs[4] = vec2[](vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0));
            bool found = false;
            for (int r = 1; r <= 16 && !found; r *= 2)
                for (int d = 0; d < 4 && !found; ++d) {
                    vec2 q = up + kDirs[d] * float(r) * texel;
                    if (inside(q) && !hidden_from(q, f.w, size)) {
                        result = textureLod(prev_img, q, 0.0).rgb;
                        found = true;
                    }
                }
        }
    } else if (hidden_then(at, up, size)) {
        result = c; // the last frame had something nearer there (the point came out from behind it): this frame's
    } else if (edge_now(at, uc, size)) {
        result = p; // this frame's sample is at a nearer thing's edge, the last frame's is clear: the last frame's
    } else {
        vec3 d = abs(c - p);
        float disagree = smoothstep(0.08, 0.25, max(d.r, max(d.g, d.b)));
        result = mix(mix(p, c, pc.t), c, disagree);
    }
    float reactive = pc.reactive > 0.5 ? textureLod(reactive_map, uv, 0.0).r : 0.0;
    // Not in a hole: there this frame's sample is what covers the place (a character), and taking some of it would
    // lay a see-through copy of the character over water, rain or a spell beside it.
    if (in_c && f.z < 0.5) result = mix(result, c, clamp(reactive / 0.9, 0.0, 1.0));
    o_color = vec4(result, 1.0);
}
