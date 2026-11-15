#version 450
#extension GL_GOOGLE_include_directive : require

// The modern lighting's spell bolts and comets (render/spell_fx_renderer.h, spell_fx.vert): light, added to the
// scene past 1 so it blooms. Each element has its own colours, edge to core, and its own way of moving:
//   fire: turbulence licking out of a white-hot core, a flame trail;
//   cold: a hard white core with a frost star, a misty trail with glitter in it;
//   magic: rings swirling in towards the core;
//   lightning: a crackling ball, the bolt a forked zigzag that jumps many times a second;
//   poison, disease: a bubbling glob, a dripping trail; corruption: a dark red rim round a dim heart;
//   chromatic: the colours turning; earth: a glowing rock in grit.
// Every piece fades where it meets the scene (the depth copy, as the fire) and with the haze.

layout(set = 0, binding = 2) uniform sampler2D scene_depth;

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera_time;
    vec4 screen;
    vec4 haze;
} pc;

layout(location = 0) in vec2 v_local;
layout(location = 1) in vec3 v_world;
layout(location = 2) flat in vec4 v_kind;
layout(location = 3) flat in vec4 v_extra;
layout(location = 4) flat in vec4 v_segment;

layout(location = 0) out vec4 o_color;
layout(location = 2) out float o_reactive; // the temporal upscaler's reactive mask (shaders/reactive.glsl)

#include "reactive.glsl"

const int kFire = 0, kCold = 1, kMagic = 2, kLightning = 3, kPoison = 4, kDisease = 5, kCorruption = 6,
          kChromatic = 7, kEarth = 8;

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

vec3 hue(float h) {
    return clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
}

// The element's colour at a heat of 0 (its edge) to 1 (its core).
vec3 palette(int e, float h) {
    vec3 edge, mid, core;
    if (e == kFire) { edge = vec3(0.7, 0.06, 0.01); mid = vec3(1.0, 0.36, 0.05); core = vec3(1.0, 0.88, 0.6); }
    else if (e == kCold) { edge = vec3(0.08, 0.25, 0.9); mid = vec3(0.35, 0.72, 1.0); core = vec3(0.92, 0.97, 1.0); }
    else if (e == kLightning) { edge = vec3(0.25, 0.3, 1.0); mid = vec3(0.55, 0.75, 1.0); core = vec3(1.0); }
    else if (e == kPoison) { edge = vec3(0.05, 0.35, 0.02); mid = vec3(0.3, 0.9, 0.1); core = vec3(0.85, 1.0, 0.55); }
    else if (e == kDisease) { edge = vec3(0.3, 0.25, 0.02); mid = vec3(0.7, 0.62, 0.12); core = vec3(0.98, 0.95, 0.6); }
    else if (e == kCorruption) { edge = vec3(0.25, 0.0, 0.04); mid = vec3(0.75, 0.04, 0.14); core = vec3(1.0, 0.45, 0.5); }
    else if (e == kEarth) { edge = vec3(0.35, 0.12, 0.02); mid = vec3(0.85, 0.45, 0.15); core = vec3(1.0, 0.85, 0.55); }
    else if (e == kChromatic) {
        vec3 c = hue(pc.camera_time.w * 0.6 + h * 0.3 + v_kind.w * 0.1);
        edge = c * 0.4; mid = c; core = mix(c, vec3(1.0), 0.7);
    } else { edge = vec3(0.25, 0.05, 0.75); mid = vec3(0.7, 0.3, 1.0); core = vec3(0.98, 0.88, 1.0); }
    vec3 col = mix(edge, mid, smoothstep(0.0, 0.5, h));
    return mix(col, core, smoothstep(0.55, 1.0, h));
}

// The heat in an orb (a bolt's head, a comet) at `uv` (-1..1 over its quad).
float orb_heat(int e, vec2 uv, float t, float seed) {
    float d = length(uv);
    float a = atan(uv.y, uv.x);
    float body = 1.0 - d / 0.6;
    if (e == kFire || e == kEarth) {
        float n = fbm(uv * 3.2 + vec2(seed, -t * 3.0)) - 0.5;
        float lick = fbm(vec2(a * 2.0 + seed, d * 3.0 - t * 5.0)) - 0.5;
        float h = body + n * 0.7 + lick * 0.6 * d;
        if (e == kEarth) h = max(h, smoothstep(0.3, 0.2, d + (noise(uv * 9.0 + seed) - 0.5) * 0.08) * 0.75);
        return h;
    }
    if (e == kCold) {
        float star = pow(max(cos(a * 3.0 + seed), 0.0), 18.0) + pow(max(cos(a * 3.0 + seed + 0.52), 0.0), 30.0) * 0.6;
        float facet = noise(vec2(floor(a * 3.0 + seed) * 7.0, floor(d * 6.0)));
        return smoothstep(0.42, 0.12, d) * (0.85 + 0.25 * facet) + star * (1.0 - d) * 1.1 + body * 0.35;
    }
    if (e == kMagic || e == kChromatic) {
        float swirl = sin(a * 3.0 + d * 14.0 + t * 7.0 + seed) * 0.5 + 0.5;
        return body + (swirl - 0.5) * 0.5 * (1.0 - d) + smoothstep(0.25, 0.0, d) * 0.6;
    }
    if (e == kLightning) {
        float frame = floor(t * 24.0);
        float n = fbm(uv * 5.0 + vec2(frame * 3.1, seed));
        float arcs = smoothstep(0.08, 0.0, abs(n - 0.5)) * smoothstep(1.0, 0.3, d);
        return smoothstep(0.35, 0.05, d) + arcs * 1.1;
    }
    if (e == kPoison || e == kDisease) {
        float n = fbm(uv * 2.5 + vec2(seed, t * 1.3));
        float bubble = smoothstep(0.62, 0.7, noise(uv * 7.0 + vec2(t * 2.0, seed))) * (1.0 - d);
        return body * 1.1 + (n - 0.5) * 0.6 + bubble * 0.5;
    }
    // Corruption: bright at the rim, dark at the heart.
    float n = fbm(uv * 3.0 + vec2(seed, -t * 2.0));
    float rim = smoothstep(0.25, 0.48, d + (n - 0.5) * 0.2) * smoothstep(0.62, 0.5, d + (n - 0.5) * 0.2);
    return rim * 1.0 + body * 0.25;
}

void main() {
    int kind = int(v_kind.x + 0.5);
    int e = int(v_kind.y + 0.5);
    float age = v_kind.z;
    float seed = v_kind.w;
    float t = pc.camera_time.w;
    float intensity = v_extra.x;
    vec3 light = vec3(0.0);
    float soft_scale = 0.5;

    if (kind == 0) {
        // An orb: flying, waiting at its target (pulsing), or fizzling out (shrinking and breaking up).
        vec2 uv = v_local;
        float phase = v_extra.y;
        float pulse = phase > 0.5 && phase < 1.5 ? 0.85 + 0.15 * sin(t * 18.0) : 1.0;
        if (phase > 1.5) uv /= max(1.0 - age * 0.8, 0.05);
        float h = orb_heat(e, uv / pulse, t, seed);
        if (phase > 1.5) h -= age * (0.6 + noise(uv * 6.0 + seed) * 0.8);
        h = clamp(h, 0.0, 1.0);
        float d = length(v_local);
        light = palette(e, h) * smoothstep(0.02, 0.25, h) * (0.4 + 2.6 * h * h * h);
        light += palette(e, 0.45) * exp(-d * d * 7.0) * 0.35 * smoothstep(1.0, 0.8, d); // its glow in the air
        soft_scale = 0.4;
    } else if (kind == 1 || kind == 2) {
        // A trail (or a spark's streak): hottest at the head, thinning and cooling to the tail, the noise in it
        // streaming back.
        float len = max(v_segment.x, 1e-3);
        float s = clamp(v_local.x / len, 0.0, 1.0);
        float r = mix(v_segment.y, v_segment.z, s);
        // Rounded ends: the distance from the segment.
        float along = v_local.x < 0.0 ? -v_local.x : max(v_local.x - len, 0.0);
        float dist = length(vec2(along, v_local.y)) / max(r, 1e-3);
        float body = clamp(1.0 - dist, 0.0, 1.0);
        if (kind == 2) {
            // A spark: a hot point with a soft falloff (dimmer than it is hot, so it does not clip flat).
            float h = body * body * body;
            light = palette(e, 0.35 + 0.65 * body) * h * 1.3;
        } else {
            float n = fbm(vec2(v_local.x * 0.45 - t * 9.0, v_local.y * 1.8) + seed);
            float h = body * (0.35 + 0.65 * s) + (n - 0.5) * 0.7 * body;
            if (e == kCold || e == kPoison || e == kDisease) {
                // Mist: softer, dimmer, with glints (frost) or drops in it.
                h *= 0.75;
                vec2 cell = floor(vec2(v_local.x * 2.0 - t * 2.0, v_local.y * 2.0) + seed);
                float glint = step(0.93, hash12(cell)) * (0.5 + 0.5 * sin(t * 20.0 + hash12(cell + 3.0) * 30.0));
                light += palette(e, 1.0) * glint * body * 1.5;
            }
            if (e == kCorruption) h = body * (0.6 * s) + (n - 0.4) * 0.8 * body;
            h = clamp(h, 0.0, 1.0) * smoothstep(0.0, 0.25, s);
            light += palette(e, h) * smoothstep(0.05, 0.3, h) * (0.3 + 2.0 * h * h);
        }
        soft_scale = 0.35;
    } else if (kind == 3) {
        // A burst: the head blown out, a flash and a ball swelling and burning away.
        vec2 uv = v_local;
        float d = length(uv);
        float grow = 0.35 + 0.65 * sqrt(age);
        // Torn: coarse and fine noise eat into it more as it ages, so it breaks into licks and wisps.
        float n = fbm(uv * 2.6 + vec2(seed, -t * 2.0)) - 0.5;
        float fine = fbm(uv * 7.0 + vec2(seed * 1.7, t * 3.0)) - 0.5;
        float h = (1.0 - d / (0.9 * grow)) + n * (0.8 + age) + fine * 0.5 - age * 1.1;
        if (e == kCold) h += pow(max(cos(atan(uv.y, uv.x) * 4.0 + seed), 0.0), 20.0) * (1.0 - d) * (1.0 - age);
        if (e == kLightning) h += smoothstep(0.07, 0.0, abs(fbm(uv * 4.0 + floor(t * 24.0)) - 0.5)) * (1.0 - age);
        h = clamp(h, 0.0, 1.0);
        float flash = exp(-age * 16.0) * exp(-d * d * 6.0) * 0.9;
        light = palette(e, h) * smoothstep(0.05, 0.35, h) * (0.25 + 1.5 * h * h * h) + palette(e, 0.9) * flash;
        light *= 0.7 * (1.0 - smoothstep(0.6, 1.0, age)) * smoothstep(1.0, 0.75, d);
        soft_scale = 0.3;
    } else if (kind == 4) {
        // A shockwave: a ring racing out over the ground, breaking up as it goes.
        float d = length(v_local);
        float edge = age;
        float w = 0.05 + 0.08 * age;
        float n = noise(vec2(atan(v_local.y, v_local.x) * 6.0 + seed, age * 4.0));
        float ring = smoothstep(w, 0.0, abs(d - edge)) * (0.5 + 0.7 * n);
        float glow = smoothstep(edge, 0.0, d) * 0.25 * (1.0 - age);
        light = palette(e, 0.6) * (ring + glow) * (1.0 - age) * (1.0 - age) * 2.0;
        soft_scale = 0.15;
    } else {
        // Lightning: a forked zigzag from the hands to the head, jumping to a new shape 24 times a second.
        float len = max(v_segment.x, 1e-3);
        float s = clamp(v_local.x / len, 0.0, 1.0);
        float frame = floor(t * 24.0);
        float bow = sin(s * 3.14159265);
        float swing = v_segment.w;
        float path = (fbm(vec2(v_local.x * 0.12, frame * 1.7 + seed)) - 0.5) * 2.0 * swing * bow;
        float fork = (fbm(vec2(v_local.x * 0.2 + 9.0, frame * 2.3 + seed)) - 0.5) * 2.6 * swing * bow;
        float d1 = abs(v_local.y - path), d2 = abs(v_local.y - fork);
        float core = exp(-d1 * d1 / 0.02) + exp(-d2 * d2 / 0.01) * 0.45 * step(0.4, s);
        float halo = exp(-d1 * d1 / 0.12) * 0.3;
        float flicker = 0.6 + 0.4 * hash12(vec2(frame, seed));
        light = (palette(e, 1.0) * core * 2.5 + palette(e, 0.5) * halo) * flicker;
        light *= smoothstep(0.0, 0.05, s) * smoothstep(-0.3, 0.0, len - v_local.x + 0.0);
        soft_scale = 0.2;
    }

    // Soft where it meets the scene.
    vec2 suv = gl_FragCoord.xy * pc.screen.zw;
    float sd = texture(scene_depth, suv).r;
    float scene_d = sd > 0.0 ? pc.screen.y / (sd + pc.screen.x) : 1e6;
    float own = pc.screen.y / (gl_FragCoord.z + pc.screen.x);
    float soft = clamp((scene_d - own) / soft_scale, 0.0, 1.0);
    // The haze between it and the eye.
    float through = 1.0;
    if (pc.haze.z > 0.5) {
        vec3 dv = v_world - pc.camera_time.xyz;
        float dist = length(dv) * pc.haze.x;
        through = exp(-3.5 * dist * dist * exp(-max(dv.z, 0.0) * pc.haze.y));
    }
    o_color = vec4(light * intensity * pc.haze.w * soft * through, 0.0);
    o_reactive = reactive_of(o_color);
}
