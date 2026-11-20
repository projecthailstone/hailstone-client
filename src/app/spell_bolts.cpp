#include "app/spell_bolts.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "net/connection/game_connection.h"

namespace hailstone::app {

namespace {

using game::Projectile;
using game::ProjectileForm;
using game::SpellElement;
using game::SpellProjectiles;
using Kind = render::SpellFxPiece::Kind;

// A spawn's foot and height, as drawn.
struct Body {
    glm::vec3 feet;
    float height;
};

std::optional<Body> body(const net::GameConnection& conn, std::uint32_t id) {
    const auto& spawns = conn.world().spawns;
    auto it = spawns.find(id);
    if (it == spawns.end()) return std::nullopt;
    const game::Spawn& s = it->second;
    glm::vec3 p = s.display_valid ? s.display_position : s.position;
    return Body{p - glm::vec3(0.0f, 0.0f, s.heights.avatar_height), std::max(s.heights.height, 2.0f)};
}

} // namespace

void projectile_pieces(const game::SpellProjectiles& projectiles, double now, std::vector<render::SpellFxPiece>& out) {
    for (const Projectile& p : projectiles.projectiles()) {
        auto element = static_cast<std::uint8_t>(p.style.element);
        bool bolt = p.style.form == ProjectileForm::Bolt;
        bool lightning = p.style.element == SpellElement::Lightning;
        float head_radius = (bolt ? 2.0f : 3.2f) * p.size;
        float age = static_cast<float>(now - p.phase_at);
        switch (p.phase) {
        case Projectile::Phase::Waiting: break;
        case Projectile::Phase::Flying:
        case Projectile::Phase::Holding:
        case Projectile::Phase::Fizzle: {
            render::SpellFxPiece orb;
            orb.kind = Kind::Orb;
            orb.element = element;
            orb.a = p.head;
            orb.radius_a = lightning && bolt ? head_radius * 0.8f : head_radius;
            orb.seed = p.seed;
            orb.phase = p.phase == Projectile::Phase::Flying ? 0.0f : p.phase == Projectile::Phase::Holding ? 1.0f : 2.0f;
            orb.age = p.phase == Projectile::Phase::Fizzle
                          ? std::clamp(age / static_cast<float>(SpellProjectiles::kFizzle), 0.0f, 1.0f)
                          : 0.0f;
            out.push_back(orb);
            if (p.phase == Projectile::Phase::Fizzle) break;
            if (lightning && bolt && !p.missile) {
                // The bolt is the arc from the hands to the head.
                render::SpellFxPiece beam;
                beam.kind = Kind::Beam;
                beam.element = element;
                beam.a = p.head;
                beam.b = p.origin;
                beam.radius_a = beam.radius_b = 0.6f;
                beam.seed = p.seed;
                out.push_back(beam);
            } else if (p.trail.size() >= 2) {
                render::SpellFxPiece trail;
                trail.kind = Kind::Trail;
                trail.element = element;
                trail.a = p.head;
                trail.b = p.trail.front().position;
                trail.radius_a = head_radius * 0.8f;
                trail.radius_b = head_radius * 0.2f;
                trail.seed = p.seed;
                trail.intensity = 0.9f;
                if (glm::length(trail.a - trail.b) > 0.3f) out.push_back(trail);
            }
            break;
        }
        case Projectile::Phase::Burst: {
            float span = static_cast<float>(bolt ? SpellProjectiles::kBurst : SpellProjectiles::kCometBurst);
            float t = std::clamp(age / span, 0.0f, 1.0f);
            render::SpellFxPiece b;
            b.kind = Kind::Burst;
            b.element = element;
            b.a = p.head;
            b.radius_a = (bolt ? 4.5f : 5.5f) * p.size;
            b.age = t;
            b.seed = p.seed;
            out.push_back(b);
            if (!bolt) {
                render::SpellFxPiece ring;
                ring.kind = Kind::Ring;
                ring.element = element;
                ring.a = p.aim;
                ring.radius_a = 20.0f * p.size;
                ring.age = std::min(t * 1.6f, 1.0f);
                ring.seed = p.seed;
                out.push_back(ring);
            }
            break;
        }
        }
    }
    for (const game::ProjectileSpark& s : projectiles.sparks()) {
        float t = std::clamp(static_cast<float>(now - s.born) / s.life, 0.0f, 1.0f);
        render::SpellFxPiece piece;
        piece.kind = Kind::Spark;
        piece.element = static_cast<std::uint8_t>(s.element);
        piece.a = s.position;
        piece.b = s.position - s.velocity * 0.035f;
        piece.radius_a = piece.radius_b = s.size * (1.0f - 0.6f * t);
        piece.intensity = 1.0f - t * t;
        out.push_back(piece);
    }
}

std::vector<game::LightSource> projectile_lights(const game::SpellProjectiles& projectiles, double now, std::size_t max) {
    std::vector<std::pair<float, game::LightSource>> all;
    for (const Projectile& p : projectiles.projectiles()) {
        glm::vec3 color = game::element_light(p.style.element);
        float s = std::sqrt(p.size);
        switch (p.phase) {
        case Projectile::Phase::Flying:
        case Projectile::Phase::Holding:
            all.push_back({0.7f * s, {p.head, color * 0.7f, 35.0f * s}});
            break;
        case Projectile::Phase::Burst: {
            // The flash, dying away.
            bool bolt = p.style.form == ProjectileForm::Bolt;
            double span = bolt ? SpellProjectiles::kBurst : SpellProjectiles::kCometBurst;
            float fade = 1.0f - std::clamp(static_cast<float>((now - p.phase_at) / span), 0.0f, 1.0f);
            fade *= fade;
            all.push_back({1.4f * s * fade,
                           {p.head + glm::vec3(0, 0, 2.0f), color * (1.4f * s * fade), (bolt ? 50.0f : 75.0f) * s}});
            break;
        }
        default: break;
        }
    }
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<game::LightSource> out;
    for (std::size_t i = 0; i < all.size() && i < max; ++i) out.push_back(all[i].second);
    return out;
}

SpellBolts::SpellBolts(render::VkContext& vk, const std::filesystem::path& shader_dir)
    : renderer_(std::make_unique<render::SpellFxRenderer>(vk, shader_dir)) {}

void SpellBolts::take_cues(const net::GameConnection& conn, std::span<net::SpellEffectCue> cues, double now) {
    const auto me = conn.world().player_id;
    for (net::SpellEffectCue& cue : cues) {
        const game::SpellData* sp = conn.spell_table().get(cue.spell);
        if (!sp) continue;
        game::ProjectileStyle style = game::projectile_style(*sp);
        if (style.form == ProjectileForm::None) continue;
        if (cue.stage == 0) {
            if (cue.proc) continue; // a proc's bolt leaves at its landing (SpellProjectiles::landed)
            projectiles_.cast_begun(cue.spell, style, cue.caster, now);
            if (cue.caster == me)
                projectiles_.own_cast(cue.spell, style, me, conn.spell_state().casting.target, now,
                                      cue.duration_ms / 1000.0);
        } else if (cue.stage == 2) {
            float distance = 0.0f;
            auto from = body(conn, cue.source), to = body(conn, cue.target);
            if (from && to) distance = glm::length(to->feet - from->feet);
            cue.delay = projectiles_.landed(cue.spell, style, cue.source, cue.target, now, distance);
            log::debug("spell {} landed from {} on {}: form {} element {}, impact in {:.2f} s", cue.spell, cue.source,
                       cue.target, static_cast<int>(style.form), static_cast<int>(style.element), cue.delay);
        }
    }
}

bool SpellBolts::missile(std::uint32_t serial, std::uint32_t source, std::uint32_t target, double now) {
    return projectiles_.follow_missile(serial, source, target, now);
}

void SpellBolts::update(const net::GameConnection& conn, double now,
                        const std::function<std::optional<glm::vec3>(std::uint32_t serial)>& missile_at) {
    // The player's cast ending without its spell landing: its projectile, still waiting, does not leave.
    bool casting = conn.spell_state().casting.casting();
    if (was_casting_ && !casting) projectiles_.own_cast_ended(conn.world().player_id, now);
    was_casting_ = casting;

    game::ProjectileWorld world;
    world.hands = [&](std::uint32_t id, const glm::vec3& toward) -> std::optional<glm::vec3> {
        auto b = body(conn, id);
        if (!b) return std::nullopt;
        glm::vec3 chest = b->feet + glm::vec3(0.0f, 0.0f, b->height * 0.62f);
        glm::vec3 d = toward - chest;
        d.z = 0.0f;
        float l = glm::length(d);
        return l > 1e-3f ? chest + d / l * std::min(1.8f, l * 0.5f) : chest;
    };
    world.chest = [&](std::uint32_t id) -> std::optional<glm::vec3> {
        auto b = body(conn, id);
        if (!b) return std::nullopt;
        return b->feet + glm::vec3(0.0f, 0.0f, b->height * 0.55f);
    };
    world.feet = [&](std::uint32_t id) -> std::optional<glm::vec3> {
        auto b = body(conn, id);
        if (!b) return std::nullopt;
        return b->feet;
    };
    world.missile = missile_at;
    projectiles_.update(now, world);
    now_ = now;
    pieces_.clear();
    projectile_pieces(projectiles_, now, pieces_);
}

void SpellBolts::draw(const render::FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera,
                      double seconds, const render::SceneLighting& lighting, float glow) {
    if (renderer_) renderer_->draw(frame, view_proj, camera, seconds, pieces_, lighting, glow);
}

void SpellBolts::clear() {
    projectiles_.clear();
    pieces_.clear();
    was_casting_ = false;
}

std::vector<game::LightSource> SpellBolts::lights(std::size_t max) const {
    return projectile_lights(projectiles_, now_, max);
}

} // namespace hailstone::app
