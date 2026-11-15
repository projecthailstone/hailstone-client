// The weather's value noise, and where water stands on the ground (shaders/weather.glsl). Written so that it also
// compiles as C++ with GLM (tests/lighting_test.cpp checks the puddles stay off slopes): no swizzles, float literals.
#ifndef HAILSTONE_PUDDLE_COVER_GLSL
#define HAILSTONE_PUDDLE_COVER_GLSL

float weather_hash(vec2 p) {
    vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031f);
    p3 += dot(p3, vec3(p3.y, p3.z, p3.x) + 33.33f);
    return fract((p3.x + p3.y) * p3.z);
}

float weather_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0f - 2.0f * f);
    return mix(mix(weather_hash(i), weather_hash(i + vec2(1.0f, 0.0f)), u.x),
               mix(weather_hash(i + vec2(0.0f, 1.0f)), weather_hash(i + vec2(1.0f, 1.0f)), u.x), u.y);
}

// The water standing on the ground at world point p (x: 0..1 how much of it is under water, y: 0..1 how deep, for the
// look; z: 0..1 the damp rim round it). Only flat ground holds water: `n` is the smoothed normal, `face` the triangle's
// own (the smoothed one reads level on a slope's foot or a rounded crest), and either tilted past about 11 degrees
// keeps it dry (full puddles within about 6). Warped noise picks the low spots, which fill first and spread as the
// puddles grow, to about a quarter of the flat ground in a long storm; a finer noise frays their shores.
vec3 puddle_cover(float puddles, vec3 p, vec3 n, vec3 face, float open) {
    float level = smoothstep(0.982f, 0.994f, n.z) * smoothstep(0.97f, 0.988f, abs(face.z)) * open;
    if (puddles <= 0.0f || level <= 0.0f) return vec3(0.0f);
    vec2 q = vec2(p.x, p.y) * 0.045f;
    q += vec2(weather_noise(q * 2.3f + 1.7f), weather_noise(q * 2.3f + 9.2f)) * 0.9f;
    float low = weather_noise(q) * 0.6f + weather_noise(q * 3.1f + 5.3f) * 0.28f +
                weather_noise(vec2(p.x, p.y) * 1.3f) * 0.12f;
    // Ground nearer the limit drains: the more it tilts, the deeper a spot must be to hold water.
    float edge = 0.86f - 0.26f * puddles + (1.0f - level) * 0.12f;
    float water = smoothstep(edge, edge + 0.025f, low);
    float depth = smoothstep(edge, edge + 0.14f, low);
    float rim = smoothstep(edge - 0.07f, edge, low) * (1.0f - water);
    return vec3(water, depth, rim * min(puddles * 3.0f, 1.0f)) * vec3(level, 1.0f, level);
}

#endif
