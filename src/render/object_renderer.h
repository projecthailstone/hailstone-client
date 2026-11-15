#pragma once

// Movable static meshes: doors, lifts and ground objects. Each model is
// uploaded once; every instance is drawn with its own model matrix, kept in
// a per-frame storage buffer (shaders/object.vert), so a door that swings
// open only changes its matrix. Textures use one bindless array, as in the
// actor renderer, and models can be added at any time.
//
// In the modern lighting they cast shadows as the characters do: into the sun's cascades (draw_shadow) and, since a
// door swings, as movers into the zone lights' live cubes (any_in_reach, draw_point_shadow; render/point_shadows.h).
// The frame is drawn in two halves for that: prepare (before the shadows), then draw_prepared in the scene.

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "assets/texture_overrides.h"
#include "assets/zone_fires.h"
#include "render/caster_bounds.h"
#include "render/gpu.h"
#include "render/scene_lighting.h"
#include "render/vk_context.h"

namespace hailstone::assets { struct Mesh; }
namespace hailstone::game { struct ViewDistance; }

namespace hailstone::render {

class ObjectRenderer {
public:
    // Reads a texture file by name for one model (the zone's archives, or
    // the equipment archives for item models).
    using FileReader = std::function<std::optional<std::vector<std::byte>>(std::string_view)>;

    struct Instance {
        int model = -1;
        glm::mat4 transform{1.0f};
        // Who this is from frame to frame (a door, an object, a placed item; make_motion_key), for its motion under
        // the temporal upscaler: its transform a frame ago is drawn beside this one. 0: none kept (it moves with the
        // camera only).
        std::uint64_t motion_key = 0;
    };
    // A motion key for item `id` of a kind of thing (`kind` 1..15, the caller's own numbering).
    static constexpr std::uint64_t make_motion_key(std::uint32_t kind, std::uint64_t id) {
        return (std::uint64_t{kind} * 0x9e3779b97f4a7c15ull) ^ (id + 1);
    }

    ObjectRenderer(VkContext& vk, const std::filesystem::path& shader_dir);
    ~ObjectRenderer();
    ObjectRenderer(const ObjectRenderer&) = delete;
    ObjectRenderer& operator=(const ObjectRenderer&) = delete;

    // Uploads a model; returns its handle. BMP textures get the WLD meshes' bottom-origin rows (TextureOptions::
    // wld_bmp_rows) whichever archive the model is from: zone objects and the equipment archives' items alike.
    int add_model(const assets::Mesh& mesh, const FileReader& read);

    void draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera_pos,
              const game::ViewDistance& view, std::span<const Instance> instances);
    // draw in two halves, so the shadows can be drawn between them: prepare writes this frame's model matrices and
    // each instance's bounds (before VkContext::begin_scene), draw_prepared records the scene pass.
    void prepare(const FrameInfo& frame, std::span<const Instance> instances);
    void draw_prepared(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera_pos,
                       const game::ViewDistance& view);
    // The prepared instances' opaque and alpha-tested parts into shadow cascade `cascade` (the cascades from the
    // last set_lighting), between VkContext::begin_shadow_cascade and end_shadow_cascade. With kCoverLayer, their
    // opaque parts alone into the cover map (render/cover_map.h), so their roofs keep the rain and snow off what is
    // under them; nothing into the water map.
    void draw_shadow(const FrameInfo& frame, std::uint32_t cascade);
    // What draw_shadow draws into the cover map, as a number that changes when it does: the casters in the map's box
    // (from the last set_lighting), their models and where they stand.
    std::uint64_t cover_signature() const;
    // Whether a prepared instance reaches within `radius` of `light`; and those that do into face `face` of the
    // light's shadow cube (render/point_shadows.h), between VkContext::begin_point_shadow and end_point_shadow.
    bool any_in_reach(const glm::vec3& light, float radius) const;
    void draw_point_shadow(const FrameInfo& frame, const glm::vec3& light, float radius, std::uint32_t face);

    void set_brightness(float b) { brightness_ = b; }
    // The clock the animated textures run on (the zone renderer's: seconds since the client started), so a
    // campfire's flame (FIRE1_SPRITE, four frames at 200 ms) flickers as the zone's own fires do. Set every frame,
    // before the shadows and the scene.
    void set_time(double seconds) { time_ms_ = seconds > 0.0 ? static_cast<std::uint64_t>(seconds * 1000.0) : 0u; }
    // The scene light and point lights (render/scene_lighting.h); the shaders use it in the modern mode.
    void set_lighting(const SceneLighting& l) { lighting_ = l; }
    std::size_t model_count() const { return models_.size(); }
    // Leaves the flame meshes out of the scene (the modern lighting draws its own: render/fire_renderer.h); they
    // never cast shadows.
    void set_hide_flames(bool hide) { hide_flames_ = hide; }
    // The prepared instances' flames, where they stand (a door's campfire, a dropped campfire), appended to `out`.
    void prepared_fires(std::vector<assets::ZoneFire>& out) const;

private:
    enum class Pass : std::uint8_t { Opaque, Blended, Additive };
    struct Part {
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::uint32_t texture = 0;  // the first frame's slot
        std::uint32_t maps = 0;     // a texture pack's normal map slot | roughness map slot << 16 (0 = none)
        // An animated texture's frames (assets::Material::frames, all uploaded, texture is frames[0]) and its delay;
        // one frame, or no delay, is a still texture. frame_texture picks the slot for the renderer's clock.
        std::vector<std::uint32_t> frames;
        std::uint32_t frame_delay_ms = 0;
        Pass pass = Pass::Opaque;
        float alpha_ref = 0.0f;
        float opacity = 1.0f;
        bool two_sided = false;
        bool fog = true;
        bool flame = false; // a flame mesh (assets::flame_material_hue): set_hide_flames leaves it out
    };
    struct GpuModel {
        Buffer vertices;
        Buffer indices;
        std::vector<Part> parts;
        bool front_ccw = false; // assets::Mesh::front_ccw
        bool casts = false;     // has opaque or alpha-tested parts (the shadow casters)
        BoundingSphere bounds;  // model space
        std::vector<assets::FlameBox> flames; // model space (assets::mesh_flames)
    };
    // An instance as prepare left it: its model, its slot in the frame's matrices, its world bounding sphere.
    struct Prepared {
        const GpuModel* gpu = nullptr;
        std::uint32_t instance = 0;
        BoundingSphere bounds; // world
        glm::mat4 transform{1.0f};
    };

    // A texture pack's normal or roughness map for the texture `name` (assets/texture_overrides.h), 0 = none.
    std::uint32_t map_slot(const std::string& name, assets::TextureMap map, const FileReader& read, UploadBatch& upload);
    std::uint32_t texture_slot(const std::string& name, bool masked, const FileReader& read,
                               UploadBatch& upload, TextureUse use = TextureUse::Color);
    std::uint32_t frame_texture(const Part& part) const;
    // Writes the textures added since into `slot`'s set (not the other's: its frame may be in flight).
    void rebuild_descriptors(std::uint32_t slot);
    VkDescriptorSet frame_set(const FrameInfo& frame); // the frame's set, brought up to date first
    void create_pipelines(const std::filesystem::path& shader_dir);
    void create_shadow_pipelines(const std::filesystem::path& shader_dir);
    // Binds what the shadow pipelines read and draws the opaque parts of `p` with `pc` (the instance and alpha
    // reference filled in per part); with `solid`, not the alpha-tested ones.
    template <class Push> void draw_casters(VkCommandBuffer cmd, const Prepared& p, Push& pc, bool solid = false);

    VkContext& vk_;
    SceneLighting lighting_{};
    std::unique_ptr<FrameBuffers> lighting_buffers_; // the SceneLighting per frame in flight (binding 2)
    float brightness_ = 1.0f;
    bool hide_flames_ = false;
    std::uint64_t time_ms_ = 0; // set_time
    std::vector<GpuModel> models_;
    std::vector<Texture> textures_;
    std::unordered_map<std::string, std::uint32_t> texture_slots_;
    std::uint32_t max_textures_ = 0;
    std::array<std::uint32_t, VkContext::kFramesInFlight> descriptor_textures_{}; // textures written into each set

    // Each frame's transforms, then from kMaxInstances on each one a frame ago (object.vert's motion).
    static constexpr std::uint32_t kMaxInstances = 8192;
    std::unordered_map<std::uint64_t, glm::mat4> transforms_now_, transforms_before_; // by motion key
    std::uint64_t frame_transforms_ = 0;
    std::array<Buffer, VkContext::kFramesInFlight> instances_{};
    std::array<void*, VkContext::kFramesInFlight> instances_mapped_{};

    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, VkContext::kFramesInFlight> sets_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipelines_[3]{};
    VkPipeline shadow_pipeline_ = VK_NULL_HANDLE;       // draw_shadow (object_shadow.*)
    VkPipeline point_shadow_pipeline_ = VK_NULL_HANDLE; // draw_point_shadow (object_point_shadow.*)
    std::vector<Prepared> prepared_;
};

} // namespace hailstone::render
