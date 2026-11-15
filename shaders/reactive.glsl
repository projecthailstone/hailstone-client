// The temporal upscaler's reactive mask (render/vk_context.h SceneOutput::Reactive): what a blended or additive
// draw writes as o_reactive (location 2), the share of the pixel the history gives way to.

// How much of a blended or additive draw's colour `c` (alpha in a) the reactive mask takes: the history gives way
// to it in proportion. Hailstone's own measure: the larger of its coverage and its brightness, at most 0.9 so a
// little history still smooths it.
float reactive_of(vec4 c) {
    return clamp(max(c.a, dot(c.rgb, vec3(0.2126, 0.7152, 0.0722))), 0.0, 1.0) * 0.9;
}
