#pragma once

// What render::ParticleRenderer puts up for one particle texture, made on its loader thread rather than in the frame
// (a new spell effect's first cast read, decoded and averaged its textures on the main thread). Kept apart from Vulkan
// so it is tested.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "assets/texture.h"

namespace hailstone { class GameDir; }
namespace hailstone::assets { struct EmitterDefinition; }

namespace hailstone::render {

struct ParticleTextureFrame {
    std::string_view texture;
    glm::uvec2 grid{1};
    std::uint32_t atlas_frame = 0;
};

// EDD frames occupy cells of one texture; WLD sprites supply a separate whole bitmap per frame.
ParticleTextureFrame particle_texture_frame(const assets::EmitterDefinition& definition, std::uint32_t frame);

struct LoadedParticleTexture {
    std::optional<assets::Image> image; // as decoded; nullopt when no folder has it or it does not decode
    glm::vec3 color{1.0f};              // ParticleRenderer::texture_color
};

// The texture `name` (lower case): `zone_bytes` (the zone's archives' copy) when given, else the first of the game
// folder's SpellEffects/, EnvEmitterEffects/ and ActorEffects/ that has the file; the first source that has it is the
// one used, decoded or not. Its colour is the mean of rgb x alpha, scaled so the brightest channel is 1 (white for
// black or none).
LoadedParticleTexture load_particle_texture(const std::string& name, std::optional<std::vector<std::byte>> zone_bytes,
                                            const GameDir& game);

// ParticleRenderer's slot for a texture that no source has or that does not decode.
inline constexpr std::uint32_t kMissingParticleTexture = 0xffffffffu;

// The slot a particle frame's texture `name` (lower case) draws with, from the slots put up (0 the white texture,
// kMissingParticleTexture for a missing one) and the names still loading; nullopt when its particles are not drawn.
// One still loading is not drawn yet. A missing one is never drawn: the RoF2 client shows nothing for it (spell effect
// 226's impact emitter 1351 names spear2.dds, which no install has, and puts no square on the caster). An empty name,
// or one put up past the texture limit, draws white.
std::optional<std::uint32_t> particle_texture_slot(const std::string& name,
                                                   const std::unordered_map<std::string, std::uint32_t>& slots,
                                                   const std::unordered_set<std::string>& loading);

} // namespace hailstone::render
