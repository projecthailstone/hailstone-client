// The weather on what it falls on, in the modern lighting (render/weather_renderer.h WeatherLook; SceneLighting::weather
// x: wet, y: snow, z: rain falling, w: puddles): snow lying on what faces up, in drifts; the rain darkening what it
// soaks and letting it reflect the sky; water standing in the ground's low spots, ringed by the drops while it rains.
// None of it reaches under cover (sky_open, render/cover_map.h). Shared by the zone, its objects and the characters.
// After lighting.glsl.

#include "puddle_cover.glsl"

// The sky seen in direction r: the horizon's colour (the haze's), a little deeper overhead.
vec3 weather_sky(vec3 r, vec3 horizon, vec3 sky) {
    return mix(horizon, horizon * 0.7 + sky * 0.3, smoothstep(0.0, 0.6, r.z));
}

// The snow lying at world point p on a surface facing n (0..1): on what faces up, in drifts that close up as more falls.
float snow_cover(float snow, vec3 p, vec3 n, float open) {
    float s = snow * smoothstep(0.55, 0.9, n.z) * open;
    if (s <= 0.0) return 0.0;
    float drift = weather_noise(p.xy * 0.15) * 0.6 + weather_noise(p.xy * 0.6) * 0.4;
    return s * smoothstep(0.75 - snow * 0.6, 0.95 - snow * 0.5, drift + 0.3);
}

// The rings the drops make on standing water: the surface's slope (dh/dx, dh/dy) at p, time t. Two jittered grids of
// cells about a unit across; a cell has a drop once a second or so at its own moment, more of them the harder it rains,
// and each ring spreads and fades. `footprint` (a pixel's size in world units) fades them out before they shimmer.
vec2 rain_ripples(vec2 p, float t, float rain, float footprint) {
    float fade = 1.0 - smoothstep(0.03, 0.1, footprint);
    if (rain <= 0.0 || fade <= 0.0) return vec2(0.0);
    vec2 slope = vec2(0.0);
    for (int layer = 0; layer < 2; ++layer) {
        vec2 q = p * (layer == 0 ? 0.8 : 1.3) + float(layer) * 17.0;
        vec2 cell = floor(q);
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                vec2 c = cell + vec2(x, y);
                float h = weather_hash(c), h2 = weather_hash(c + 7.1);
                if (h2 > 0.35 + 0.65 * rain) continue;
                vec2 centre = c + 0.5 + (vec2(h, h2) - 0.5) * 0.8;
                float age = fract(t * (0.8 + 0.3 * h) + h * 7.0);
                vec2 d = q - centre;
                float dist = length(d);
                float ring = dist - age * 1.1;
                float wave = sin(ring * 16.0) * exp(-ring * ring * 25.0);
                float life = (1.0 - age) * (1.0 - age);
                slope += d / max(dist, 1e-3) * wave * life;
            }
    }
    return slope * 0.12 * fade;
}

// A lit surface `color` (linear) under the weather at world point p, facing n, seen along `view` (to the eye); `snow_lit`
// is white snow lit there; `open` its sky_open. `horizon` and `sky` colour the reflections (weather_sky); `sheen`
// scales the wet look (the characters' is lighter).
vec3 weather_surface(vec3 color, vec3 snow_lit, vec3 p, vec3 n, vec3 view, vec4 weather, float open, vec3 horizon,
                     vec3 sky, float sheen) {
    float snow = snow_cover(weather.y, p, n, open);
    if (snow > 0.0) color = mix(color, snow_lit, snow * 0.9);
    float wet = weather.x * mix(0.35, 1.0, smoothstep(0.55, 0.9, n.z)) * (1.0 - snow) * open * sheen;
    if (wet <= 0.0) return color;
    vec3 r = reflect(-view, n);
    float fresnel = 0.04 + 0.96 * pow(1.0 - clamp(dot(n, view), 0.0, 1.0), 5.0);
    return color * mix(1.0, 0.6, wet) +
           weather_sky(normalize(vec3(r.xy, max(r.z, 0.02))), horizon, sky) * fresnel * wet * 0.7;
}

// Standing water on the ground (puddle_cover), after weather_surface: `face` is the triangle's own normal, `t` the
// time, `footprint` a pixel's size in world units (rain_ripples), `to_light` and `sun` the sun's or moon's direction
// and shadowed light, for its glint. Shallow water at the shore shows the soaked ground through it; deeper in, the
// water's reflection takes over as a mirror's does (Schlick, water's 2 %), so it reads dark seen from above and
// holds the sky at a low angle. The drops ring it while it rains, their crests catching the light.
vec3 puddle_surface(vec3 color, vec3 p, vec3 n, vec3 face, vec3 view, vec4 weather, float open, vec3 horizon, vec3 sky,
                    float t, float footprint, vec3 to_light, vec3 sun) {
    vec3 cover = puddle_cover(weather.w, p, n, face, open);
    float keep = 1.0 - snow_cover(weather.y, p, n, open);
    float pool = cover.x * keep;
    float rim = cover.z * keep;
    if (pool <= 0.0 && rim <= 0.0) return color;
    // The damp ground round the water, darker still than the soak.
    color *= 1.0 - 0.3 * rim;
    if (pool <= 0.0) return color;
    vec2 slope = rain_ripples(p.xy, t, weather.z, footprint);
    vec3 wn = normalize(vec3(-slope, 1.0));
    float nv = clamp(dot(wn, view), 0.0, 1.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
    // A real overcast sky is many times brighter than the ground it lights; the sky colour here is not, so the
    // reflection is lifted to hold the puddle's contrast.
    fresnel = min(fresnel * 2.5, 0.75);
    vec3 r = reflect(-view, wn);
    vec3 seen = weather_sky(normalize(vec3(r.xy, max(r.z, 0.02))), horizon, sky);
    // What lies under it: the ground, darker and richer as the water deepens over it.
    vec3 under = color * mix(0.55, 0.2, cover.y);
    float mirror = mix(0.4, 1.0, cover.y);
    vec3 glint = sun * (pow(max(dot(r, to_light), 0.0), 600.0) * 30.0 + pow(max(dot(r, to_light), 0.0), 40.0) * 0.4);
    vec3 water = under * (1.0 - fresnel * mirror) + (seen * 0.9 + glint) * fresnel * mirror +
                 seen * length(slope) * 2.5;
    return mix(color, water, pool);
}
