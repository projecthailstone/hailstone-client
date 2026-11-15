#include "render/object_renderer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "assets/model.h"
#include "assets/zone_fires.h"
#include "assets/texture.h"
#include "assets/texture_overrides.h"
#include "core/log.h"
#include "game/view_distance.h"
#include "render/cover_map.h"
#include "render/culling.h"
#include "render/material_state.h"
#include "render/shadows.h"
#include "render/point_shadows.h"
#include "render/vk_util.h"

namespace hailstone::render {

namespace {

struct GpuVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    std::uint32_t color;
};

// Matches the push_constant block in object.vert / object.frag.
struct PushConstants {
    glm::mat4 view_proj;
    glm::vec4 fog_color;
    glm::vec4 fog; // start, end, alpha_ref, opacity
    glm::vec4 camera_pos; // w: brightness
    std::uint32_t texture;
    std::uint32_t instance;
    std::uint32_t maps; // a texture pack's normal map slot | roughness map slot << 16 (0 = none)
};
static_assert(sizeof(PushConstants) <= 128);

VkShaderModule load_shader(VkDevice device, const std::filesystem::path& path) {
    return create_shader_module(device, path);
}

} // namespace

ObjectRenderer::ObjectRenderer(VkContext& vk, const std::filesystem::path& shader_dir) : vk_(vk) {
    VkDevice d = vk_.device();
    // Up to three slots a material with a texture pack (render/actor_renderer.cpp); 16-bit slots in the shaders.
    max_textures_ = std::max(1u, std::min(16384u, vk_.caps().max_sampled_images));

    for (std::size_t i = 0; i < instances_.size(); ++i) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sizeof(glm::mat4) * kMaxInstances * 2;
        bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo out{};
        vk_check(vmaCreateBuffer(vk_.allocator(), &bi, &ai, &instances_[i].buffer, &instances_[i].allocation, &out),
                 "create instance buffer");
        instances_[i].size = bi.size;
        instances_mapped_[i] = out.pMappedData;
    }

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod = VK_LOD_CLAMP_NONE;
    if (vk_.caps().max_anisotropy > 0.0f) {
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy = std::min(8.0f, vk_.caps().max_anisotropy);
    }
    vk_check(vkCreateSampler(d, &si, nullptr, &sampler_), "vkCreateSampler");

    // Binding 1 is the lighting (render/scene_lighting.h), 2 the textures: a variable-count array must have the
    // highest binding number.
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = max_textures_;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorBindingFlags flags[3] = {0, 0,
                                         VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                             VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
                                             vk_.texture_array_binding_flags()};
    VkDescriptorSetLayoutBindingFlagsCreateInfo bf{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    bf.bindingCount = 3;
    bf.pBindingFlags = flags;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.pNext = &bf;
    li.flags = vk_.texture_array_layout_flags();
    li.bindingCount = 3;
    li.pBindings = bindings;
    vk_check(vkCreateDescriptorSetLayout(d, &li, nullptr, &set_layout_), "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize sizes[2] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * VkContext::kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_textures_ * VkContext::kFramesInFlight},
    };
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = vk_.texture_array_pool_flags();
    pi.maxSets = VkContext::kFramesInFlight;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    vk_check(vkCreateDescriptorPool(d, &pi, nullptr, &pool_), "vkCreateDescriptorPool");
    for (auto& set : sets_) {
        VkDescriptorSetVariableDescriptorCountAllocateInfo vc{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
        vc.descriptorSetCount = 1;
        vc.pDescriptorCounts = &max_textures_;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.pNext = &vc;
        ai.descriptorPool = pool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &set_layout_;
        vk_check(vkAllocateDescriptorSets(d, &ai, &set), "vkAllocateDescriptorSets");
    }
    lighting_buffers_ = std::make_unique<FrameBuffers>(vk_, sizeof(SceneLighting), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    for (std::size_t i = 0; i < sets_.size(); ++i) {
        VkDescriptorBufferInfo infos[2] = {{instances_[i].buffer, 0, VK_WHOLE_SIZE},
                                           {lighting_buffers_->buffer(i), 0, VK_WHOLE_SIZE}};
        VkWriteDescriptorSet w[2]{};
        for (std::uint32_t k = 0; k < 2; ++k) {
            w[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[k].dstSet = sets_[i];
            w[k].dstBinding = k;
            w[k].descriptorCount = 1;
            w[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[k].pBufferInfo = &infos[k];
        }
        vkUpdateDescriptorSets(d, 2, w, 0, nullptr);
    }

    // Texture slot 0 is plain white, for anything that fails to load.
    UploadBatch upload(vk_);
    assets::Image white;
    white.width = white.height = 1;
    white.mips.push_back(std::vector<std::byte>(4, std::byte{0xff}));
    textures_.push_back(upload.texture(white));
    upload.submit();
    for (std::uint32_t slot = 0; slot < VkContext::kFramesInFlight; ++slot) rebuild_descriptors(slot); // none in flight yet
    create_pipelines(shader_dir);
    create_shadow_pipelines(shader_dir);
}

ObjectRenderer::~ObjectRenderer() {
    VkDevice d = vk_.device();
    vk_.wait_idle();
    for (VkPipeline p : pipelines_)
        if (p) vkDestroyPipeline(d, p, nullptr);
    if (shadow_pipeline_) vkDestroyPipeline(d, shadow_pipeline_, nullptr);
    if (point_shadow_pipeline_) vkDestroyPipeline(d, point_shadow_pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(d, layout_, nullptr);
    if (pool_) vkDestroyDescriptorPool(d, pool_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(d, set_layout_, nullptr);
    if (sampler_) vkDestroySampler(d, sampler_, nullptr);
    for (auto& t : textures_) t.destroy(d, vk_.allocator());
    for (auto& g : models_) {
        g.vertices.destroy(vk_.allocator());
        g.indices.destroy(vk_.allocator());
    }
    for (auto& b : instances_) b.destroy(vk_.allocator());
}

std::uint32_t ObjectRenderer::frame_texture(const Part& part) const {
    if (part.frames.empty()) return part.texture;
    return part.frames[texture_frame(time_ms_, part.frame_delay_ms, part.frames.size())];
}

void ObjectRenderer::rebuild_descriptors(std::uint32_t slot) {
    // Only the slot's own set: its last frame's fence has passed (VkContext::begin_frame), while the other slot's
    // frame may still be in flight. The other set takes the new textures when its frame comes round, so a new model
    // no longer waits for the GPU to go idle (render/actor_renderer.cpp does the same).
    auto& written = descriptor_textures_[slot];
    if (written == textures_.size()) return;
    std::vector<VkDescriptorImageInfo> infos;
    for (std::size_t i = written; i < textures_.size(); ++i)
        infos.push_back({sampler_, textures_[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = sets_[slot];
    w.dstBinding = 2;
    w.dstArrayElement = written;
    w.descriptorCount = static_cast<std::uint32_t>(infos.size());
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = infos.data();
    vkUpdateDescriptorSets(vk_.device(), 1, &w, 0, nullptr);
    written = static_cast<std::uint32_t>(textures_.size());
}

VkDescriptorSet ObjectRenderer::frame_set(const FrameInfo& frame) {
    // Textures are added by add_model, between frames; this brings the set up to date before the frame binds it.
    rebuild_descriptors(frame.frame_slot);
    return sets_[frame.frame_slot];
}

std::uint32_t ObjectRenderer::texture_slot(const std::string& name, bool masked, const FileReader& read,
                                           UploadBatch& upload, TextureUse use) {
    std::string key = name + (masked ? "#m" : "") + (use == TextureUse::OpaqueColor ? "#o" : "");
    if (auto it = texture_slots_.find(key); it != texture_slots_.end()) return it->second;
    std::uint32_t slot = 0;
    std::string error;
    auto data = read ? read(name) : std::nullopt;
    std::optional<assets::Image> image;
    // Every model drawn here is a WLD mesh (a zone's object archive, or the equipment archives' IT<n> items) or an EQG
    // one: wld_bmp_rows turns a real BMP's rows to the WLD UVs once, as for the zone and the characters. A second
    // turn for the items puts their BMPs upside down (the gequip campfire IT78's flames burning downwards).
    image = assets::decode_game_texture(
        name, data, {.palette_index0_transparent = masked, .wld_bmp_rows = true, .reads_alpha = use == TextureUse::Color},
        &error);
    if (image && textures_.size() < max_textures_) {
        slot = static_cast<std::uint32_t>(textures_.size());
        textures_.push_back(upload.texture(*image, use));
    } else {
        log::warn("object texture {}: {}", name, image ? "texture array full" : (error.empty() ? "not found" : error));
    }
    texture_slots_.emplace(std::move(key), slot);
    return slot;
}

std::uint32_t ObjectRenderer::map_slot(const std::string& name, assets::TextureMap map, const FileReader& read,
                                       UploadBatch& upload) {
    std::string key = name + (map == assets::TextureMap::Normal ? "#n" : "#r");
    if (auto it = texture_slots_.find(key); it != texture_slots_.end()) return it->second;
    std::uint32_t slot = 0;
    auto image = assets::decode_texture_map(name, read ? read(name) : std::nullopt, map, true);
    if (image && textures_.size() < max_textures_ && textures_.size() <= 0xffff) {
        slot = static_cast<std::uint32_t>(textures_.size());
        textures_.push_back(upload.texture(*image, TextureUse::Data));
    }
    texture_slots_.emplace(std::move(key), slot);
    return slot;
}

int ObjectRenderer::add_model(const assets::Mesh& mesh, const FileReader& read) {
    GpuModel g;
    g.front_ccw = mesh.front_ccw;
    UploadBatch upload(vk_);
    std::vector<GpuVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) vertices.push_back({v.position, v.normal, v.uv, v.color});
    for (const auto& p : mesh.parts) {
        if (p.material >= mesh.materials.size()) continue;
        const auto& m = mesh.materials[p.material];
        if (m.blend == assets::BlendMode::Invisible || m.frames.empty()) continue;
        Part part;
        part.first_index = p.first_index;
        part.index_count = p.index_count;
        MaterialState st = material_state(m);
        // Every frame of an animated texture, not only the first: the doors and ground objects carry the classic
        // flames (ecommons' GRATE door, gequip's IT78 campfire), whose FIRE1_SPRITE steps fire1..fire4 at 200 ms.
        for (const auto& frame : m.frames)
            part.frames.push_back(texture_slot(frame, st.masked_texture, read, upload,
                                               st.alpha_unused() ? TextureUse::OpaqueColor : TextureUse::Color));
        part.texture = part.frames.front();
        part.maps = map_slot(m.frames.front(), assets::TextureMap::Normal, read, upload) |
                    map_slot(m.frames.front(), assets::TextureMap::Roughness, read, upload) << 16;
        if (part.frames.size() > 1) part.frame_delay_ms = m.frame_delay_ms;
        part.two_sided = m.two_sided;
        part.pass = static_cast<Pass>(st.pass);
        part.alpha_ref = st.alpha_ref;
        part.opacity = st.opacity;
        part.fog = st.fog;
        part.flame = assets::flame_material_hue(m) != assets::FlameHue::None;
        g.parts.push_back(part);
    }
    std::stable_sort(g.parts.begin(), g.parts.end(), [](const Part& a, const Part& b) { return a.pass < b.pass; });
    g.casts = std::any_of(g.parts.begin(), g.parts.end(), [](const Part& p) { return p.pass == Pass::Opaque; });
    std::vector<glm::vec3> positions;
    positions.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) positions.push_back(v.position);
    g.bounds = bounding_sphere(positions);
    g.flames = assets::mesh_flames(mesh);
    if (!vertices.empty() && !mesh.indices.empty()) {
        g.vertices = upload.buffer(std::as_bytes(std::span(vertices)), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        g.indices = upload.buffer(std::as_bytes(std::span(mesh.indices)), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    } else {
        g.parts.clear();
    }
    upload.submit();
    models_.push_back(std::move(g));
    return static_cast<int>(models_.size() - 1);
}

void ObjectRenderer::draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera_pos,
                          const game::ViewDistance& view, std::span<const Instance> instances) {
    prepare(frame, instances);
    draw_prepared(frame, view_proj, camera_pos, view);
}

void ObjectRenderer::prepare(const FrameInfo& frame, std::span<const Instance> instances) {
    prepared_.clear();
    rebuild_descriptors(frame.frame_slot);
    auto* mats = static_cast<glm::mat4*>(instances_mapped_[frame.frame_slot]);
    std::size_t count = std::min<std::size_t>(instances.size(), kMaxInstances);
    // The temporal upscaler's motion: a new frame makes this frame's transforms the last frame's.
    const bool motion = vk_.scene_motion();
    if (motion && frame_transforms_ != vk_.frames_begun()) {
        frame_transforms_ = vk_.frames_begun();
        std::swap(transforms_now_, transforms_before_);
        transforms_now_.clear();
    }
    for (std::size_t i = 0; i < count; ++i) {
        mats[i] = instances[i].transform;
        if (motion) {
            glm::mat4 before = instances[i].transform;
            if (const std::uint64_t key = instances[i].motion_key) {
                if (auto it = transforms_before_.find(key); it != transforms_before_.end()) before = it->second;
                transforms_now_[key] = instances[i].transform;
            }
            mats[kMaxInstances + i] = before;
        }
        int m = instances[i].model;
        if (m < 0 || static_cast<std::size_t>(m) >= models_.size()) continue;
        const GpuModel& g = models_[static_cast<std::size_t>(m)];
        if (g.parts.empty()) continue;
        prepared_.push_back({&g, static_cast<std::uint32_t>(i), transform_sphere(instances[i].transform, g.bounds),
                             instances[i].transform});
    }
    // VMA may place the buffer in host-visible memory that is not coherent; a no-op where it is.
    if (count) {
        vmaFlushAllocation(vk_.allocator(), instances_[frame.frame_slot].allocation, 0, sizeof(glm::mat4) * count);
        if (motion)
            vmaFlushAllocation(vk_.allocator(), instances_[frame.frame_slot].allocation,
                               sizeof(glm::mat4) * kMaxInstances, sizeof(glm::mat4) * count);
    }
}

void ObjectRenderer::draw_prepared(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera_pos,
                                   const game::ViewDistance& view) {
    if (prepared_.empty()) return;
    lighting_buffers_->write(frame.frame_slot, &lighting_, sizeof(lighting_));
    VkCommandBuffer cmd = frame.cmd;
    PushConstants pc{};
    pc.view_proj = view_proj;
    pc.fog_color = glm::vec4(view.fog_color, view.fog_density); // a: the exp2 fog's density
    pc.camera_pos = glm::vec4(camera_pos, brightness_);
    VkDescriptorSet sets[2] = {frame_set(frame), vk_.shadow_set()};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);
    for (int pass = 0; pass < 3; ++pass) {
        bool bound = false;
        for (const Prepared& p : prepared_) {
            const GpuModel& g = *p.gpu;
            bool buffers = false;
            for (const auto& part : g.parts) {
                if (static_cast<int>(part.pass) != pass || (part.flame && hide_flames_)) continue;
                if (!bound) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[pass]);
                    bound = true;
                }
                if (!buffers) {
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &g.vertices.buffer, &offset);
                    vkCmdBindIndexBuffer(cmd, g.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                    buffers = true;
                }
                vkCmdSetCullMode(cmd, part.two_sided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
                vkCmdSetFrontFace(cmd, g.front_ccw ? opposite(kEqFrontFace) : kEqFrontFace);
                pc.texture = frame_texture(part);
                pc.instance = p.instance;
                pc.maps = part.maps;
                pc.fog = part.fog ? glm::vec4(view.fog_start, view.fog_end, part.alpha_ref, part.opacity)
                                  : glm::vec4(kNoFogStart, kNoFogEnd, part.alpha_ref, part.opacity);
                vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(pc), &pc);
                vkCmdDrawIndexed(cmd, part.index_count, 1, part.first_index, 0, 0);
            }
        }
    }
}

// The opaque pass's parts (alpha-tested ones discard by their texture) are the casters; the blended and additive
// ones (glass, glows) let the light through, as in the zone and the characters.
template <class Push> void ObjectRenderer::draw_casters(VkCommandBuffer cmd, const Prepared& p, Push& pc, bool solid) {
    bool bound = false;
    for (const auto& part : p.gpu->parts) {
        if (part.pass != Pass::Opaque || part.flame || (solid && part.alpha_ref > 0.0f)) continue;
        if (!bound) {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &p.gpu->vertices.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, p.gpu->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            bound = true;
        }
        pc.texture = frame_texture(part); // an alpha-tested animated texture casts the frame it shows
        pc.instance = p.instance;
        pc.fog.z = part.alpha_ref;
        vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cmd, part.index_count, 1, part.first_index, 0, 0);
    }
}

void ObjectRenderer::draw_shadow(const FrameInfo& frame, std::uint32_t cascade) {
    if (!shadow_pipeline_ || prepared_.empty() || cascade == kWaterLayer) return;
    VkCommandBuffer cmd = frame.cmd;
    VkDescriptorSet sets[2] = {frame_set(frame), vk_.shadow_set()};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);
    const bool cover = cascade == kCoverLayer;
    PushConstants pc{};
    pc.view_proj = cover ? cover_view_proj(lighting_.cover, lighting_.cover_params) : lighting_.shadow_view_proj[cascade];
    pc.fog = glm::vec4(kNoFogStart, kNoFogEnd, 0.0f, 1.0f);
    // Only the casters that can reach the layer, as the zone's chunks (ZoneRenderer::draw_shadow): the cascade's clip
    // volume, or the cover map's box.
    const Frustum frustum = cover ? Frustum{} : frustum_from(pc.view_proj);
    const Box area = cover ? cover_box(lighting_.cover, lighting_.cover_params) : Box{};
    for (const Prepared& p : prepared_) {
        if (!p.gpu->casts) continue;
        const Box b = sphere_box(p.bounds.center, p.bounds.radius);
        if (cover ? !boxes_overlap(b, area) : !box_in_frustum(frustum, b)) continue;
        draw_casters(cmd, p, pc, cover);
    }
}

std::uint64_t ObjectRenderer::cover_signature() const {
    const Box area = cover_box(lighting_.cover, lighting_.cover_params);
    std::uint64_t h = 1469598103934665603ull; // FNV-1a
    auto mix = [&](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xffu;
            h *= 1099511628211ull;
        }
    };
    for (const Prepared& p : prepared_) {
        if (!p.gpu->casts || !boxes_overlap(sphere_box(p.bounds.center, p.bounds.radius), area)) continue;
        mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p.gpu)));
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) {
                std::uint32_t bits = 0;
                std::memcpy(&bits, &p.transform[c][r], sizeof(bits));
                mix(bits);
            }
    }
    return h;
}

void ObjectRenderer::prepared_fires(std::vector<assets::ZoneFire>& out) const {
    for (const Prepared& p : prepared_)
        for (const auto& box : p.gpu->flames) out.push_back(assets::place_flame(box, p.transform));
}

bool ObjectRenderer::any_in_reach(const glm::vec3& light, float radius) const {
    return std::any_of(prepared_.begin(), prepared_.end(),
                       [&](const Prepared& p) { return p.gpu->casts && sphere_in_reach(p.bounds, light, radius); });
}

void ObjectRenderer::draw_point_shadow(const FrameInfo& frame, const glm::vec3& light, float radius,
                                       std::uint32_t face) {
    if (!point_shadow_pipeline_ || prepared_.empty()) return;
    VkCommandBuffer cmd = frame.cmd;
    VkDescriptorSet sets[2] = {frame_set(frame), vk_.shadow_set()};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, point_shadow_pipeline_);
    PushConstants pc{};
    pc.fog_color = glm::vec4(light, radius);
    pc.camera_pos = glm::vec4(kPointShadowFaceScale, kPointShadowNear, static_cast<float>(face), 0.0f);
    for (const Prepared& p : prepared_) {
        if (!p.gpu->casts || !sphere_in_reach(p.bounds, light, radius)) continue;
        glm::vec3 r(p.bounds.radius);
        if (!box_reaches_face(light, face, p.bounds.center - r, p.bounds.center + r)) continue;
        draw_casters(cmd, p, pc);
    }
}

void ObjectRenderer::create_pipelines(const std::filesystem::path& shader_dir) {
    VkDevice d = vk_.device();
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkDescriptorSetLayout set_layouts[2] = {set_layout_, vk_.shadow_set_layout()};
    li.setLayoutCount = 2;
    li.pSetLayouts = set_layouts;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pcr;
    vk_check(vkCreatePipelineLayout(d, &li, nullptr, &layout_), "vkCreatePipelineLayout");

    VkShaderModule vs = load_shader(d, shader_dir / "object.vert.spv");
    VkShaderModule fs = load_shader(d, shader_dir / "object.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr},
    };
    VkVertexInputBindingDescription vb{0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, uv)},
        {3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GpuVertex, color)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT; // dynamic (two-sided materials, EQG winding)
    rs.frontFace = kEqFrontFace;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = vk_.scene_samples();
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE,
                            VK_DYNAMIC_STATE_FRONT_FACE};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 4;
    ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri = vk_.scene_rendering();

    // Multisampled, the opaque pass turns the alpha test into coverage: alpha-to-coverage, and the fragment
    // shader's constant_id 0 makes its alpha the coverage (shaders/zone.frag, actor.frag, object.frag).
    const VkBool32 coverage = vk_.scene_samples() != VK_SAMPLE_COUNT_1_BIT ? VK_TRUE : VK_FALSE;
    VkSpecializationMapEntry spec_entry{0, 0, sizeof(VkBool32)};
    VkBool32 spec_value = VK_FALSE;
    VkSpecializationInfo spec{1, &spec_entry, sizeof(VkBool32), &spec_value};
    stages[1].pSpecializationInfo = &spec;

    for (int pass = 0; pass < 3; ++pass) {
        spec_value = pass == 0 ? coverage : VK_FALSE;
        ms.alphaToCoverageEnable = spec_value;
        VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        dss.depthTestEnable = VK_TRUE;
        dss.depthWriteEnable = pass == 0 ? VK_TRUE : VK_FALSE;
        dss.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xf;
        if (pass == 1) {
            ba.blendEnable = VK_TRUE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.alphaBlendOp = VK_BLEND_OP_ADD;
        } else if (pass == 2) {
            ba.blendEnable = VK_TRUE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.colorBlendOp = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        // The opaque pass draws the objects' own motion (a door swinging); the blended and additive ones the
        // reactive mask.
        const SceneBlends blends = vk_.scene_blends(ba, pass == 0 ? SceneOutput::Motion : SceneOutput::Reactive);
        const VkPipelineColorBlendStateCreateInfo cb = blends.info();
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &ri;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vi;
        info.pInputAssemblyState = &ia;
        info.pViewportState = &vp;
        info.pRasterizationState = &rs;
        info.pMultisampleState = &ms;
        info.pDepthStencilState = &dss;
        info.pColorBlendState = &cb;
        info.pDynamicState = &ds;
        info.layout = layout_;
        vk_check(vkCreateGraphicsPipelines(d, vk_.pipeline_cache(), 1, &info, nullptr, &pipelines_[pass]),
                 "vkCreateGraphicsPipelines (objects)");
    }
    vkDestroyShaderModule(d, vs, nullptr);
    vkDestroyShaderModule(d, fs, nullptr);
}

// Shadow casters: the model matrix with the cascade as view_proj (draw_shadow) or the light's face (draw_point_shadow),
// depth only, single-sampled, no culling (a door's back face shadows as well as its front); the cascades take the
// slope-scaled bias VkContext::begin_shadow_cascade sets, the cubes none.
void ObjectRenderer::create_shadow_pipelines(const std::filesystem::path& shader_dir) {
    VkDevice d = vk_.device();
    VkShaderModule vs = load_shader(d, shader_dir / "object_shadow.vert.spv");
    VkShaderModule fs = load_shader(d, shader_dir / "object_shadow.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr},
    };
    VkVertexInputBindingDescription vb{0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, position)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = kEqFrontFace;
    rs.depthBiasEnable = VK_TRUE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = VK_TRUE;
    dss.depthWriteEnable = VK_TRUE;
    dss.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 3;
    ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.depthAttachmentFormat = vk_.shadow_format();
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &ri;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &dss;
    info.pColorBlendState = &cb;
    info.pDynamicState = &ds;
    info.layout = layout_;
    vk_check(vkCreateGraphicsPipelines(d, vk_.pipeline_cache(), 1, &info, nullptr, &shadow_pipeline_),
             "vkCreateGraphicsPipelines (object shadows)");
    vkDestroyShaderModule(d, vs, nullptr);
    vkDestroyShaderModule(d, fs, nullptr);

    // The lights' shadow cubes: the same, writing the distance, with no bias.
    VkShaderModule pvs = load_shader(d, shader_dir / "object_point_shadow.vert.spv");
    VkShaderModule pfs = load_shader(d, shader_dir / "object_point_shadow.frag.spv");
    stages[0].module = pvs;
    stages[1].module = pfs;
    rs.depthBiasEnable = VK_FALSE;
    ds.dynamicStateCount = 2;
    vk_check(vkCreateGraphicsPipelines(d, vk_.pipeline_cache(), 1, &info, nullptr, &point_shadow_pipeline_),
             "vkCreateGraphicsPipelines (object point shadows)");
    vkDestroyShaderModule(d, pvs, nullptr);
    vkDestroyShaderModule(d, pfs, nullptr);
}

} // namespace hailstone::render
