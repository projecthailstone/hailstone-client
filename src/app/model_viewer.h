#pragma once

// Character model viewer (--model): draws one or more classic character
// models in a row, animated and dressed, optionally standing in a zone.

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "render/vk_context.h"

namespace hailstone::app {

struct ModelViewerOptions {
    std::string game_dir;
    std::string classic_dir;
    bool validation = false;
    int frames = 0;
    std::string screenshot;
    render::SceneOptions scene;
    bool modern = false; // --lighting modern: the models and zone in daylight at `hour` (render/scene_lighting.h)
    float hour = 12.0f;
    std::vector<std::string> codes; // model codes, e.g. HUM, ELF; or race ids as "r:<race>[:<gender>]"
    bool luclin = false;            // --luclin: the Luclin player models eqclient.ini enables
    std::string look;               // --dress: default appearance "key=value/..." (see --help)
    std::string anim = "P01";       // clip code ("" = rest pose)
    std::string upper_anim;         // a clip the upper body plays over `anim` (render/actor_layers.h); "" none
    std::optional<float> anim_time; // fixed clip time in seconds; default runs in real time
    float size = 6.0f;              // spawn size (EQ: 6 is a normal human)
    float heading = 0.0f;           // model heading, 0..512 (0 faces +X)
    std::string zone;               // optional: stand the models in this zone ...
    std::optional<glm::vec3> at;    // ... with the first model's origin here (spawn coordinates)
    std::optional<glm::vec3> camera_pos;
    std::optional<float> yaw_deg, pitch_deg;
    int clip_plane = 15;
    int spell_effect = 0; // --spell-effect: a spellsnew.eff effect replayed on the models (0 none)
    int spell_stage = 0;  // its stage: 0 cast, 1 projectile, 2 impact
    bool ride = false;    // --ride: the models in pairs, the second of each seated on the first (a mount)
    float frame_dt = 0.0f; // --frame-dt: > 0, the clock moves this many seconds a frame, not with the wall clock
    float orbit = 0.0f;    // --orbit: the camera circles the first model at this many degrees a second
    bool screenshot_generated = false; // --screenshot-generated (with --frame-gen): the in-between frame too
};

int run_model_viewer(const ModelViewerOptions& options);

} // namespace hailstone::app
