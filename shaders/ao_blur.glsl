// The AO image (shaders/ao.frag) as the scene takes it: shared by post.frag and, under the temporal upscaler,
// temporal_prep.frag. `terms`: view depth = terms.y / (d + terms.x) (reverse Z, FlyCamera::projection).

float ao_view_depth(float d, vec2 terms) { return terms.y / (d + terms.x); }

// The AO over the 4 x 4 block that holds every turn of ao.frag's pattern, weighted against other depths so it does
// not bleed across edges.
float blurred_ao(sampler2D ao_map, sampler2D depth_map, ivec2 p, vec2 terms) {
    ivec2 size = textureSize(ao_map, 0);
    float z = ao_view_depth(texelFetch(depth_map, p, 0).r, terms);
    float sum = 0.0, weight = 0.0;
    for (int y = -2; y < 2; ++y)
        for (int x = -2; x < 2; ++x) {
            ivec2 q = clamp(p + ivec2(x, y), ivec2(0), size - 1);
            float d = texelFetch(depth_map, q, 0).r;
            if (d <= 0.0) continue;
            float w = exp(-abs(ao_view_depth(d, terms) - z) / (0.02 * z));
            sum += texelFetch(ao_map, q, 0).r * w;
            weight += w;
        }
    return weight > 0.0 ? sum / weight : 1.0;
}
