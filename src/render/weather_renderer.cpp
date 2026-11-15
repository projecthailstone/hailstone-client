#include "render/weather_renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "assets/texture.h"
#include "assets/texture_overrides.h"
#include "core/game_dir.h"
#include "core/log.h"
#include "render/scene_lighting.h"
#include "render/vk_util.h"

namespace hailstone::render {

namespace {

constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// Matches the push_constant block in precipitation.vert / .frag.
struct PushConstants {
    glm::mat4 view_proj;
    glm::vec4 camera;  // xyz, w: a pixel's size one unit in front of the camera
    glm::vec4 forward; // xyz the camera's direction, w: the light scale
    glm::vec4 style;   // x a pixel's size one unit in front, y the facing ones' least pixels, z dot_below
};
static_assert(sizeof(PushConstants) + 16 <= 128); // and the texture slot after it

float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// srgb_to_linear of a byte (byte / 255), worked out once: a drop's colour took three pow calls each frame.
const std::array<float, 256>& srgb_byte_table() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (std::size_t i = 0; i < t.size(); ++i) t[i] = srgb_to_linear(static_cast<float>(i) / 255.0f);
        return t;
    }();
    return table;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

VkShaderModule load_shader(VkDevice device, const std::filesystem::path& path) {
    return create_shader_module(device, path);
}

float approach(float value, float target, float up_per_second, float down_per_second, float dt) {
    return value < target ? std::min(target, value + up_per_second * dt) : std::max(target, value - down_per_second * dt);
}

} // namespace

glm::vec4 wind_at(double seconds, float storm) {
    // The way it blows veers slowly back and forth about the south-west wind; a storm blows harder and gustier.
    auto t = static_cast<float>(std::fmod(seconds, 36000.0));
    float a = 0.8f + 0.5f * std::sin(t * 0.011f) + 0.2f * std::sin(t * 0.037f + 1.0f);
    float s = std::clamp(storm, 0.0f, 1.0f);
    return glm::vec4(std::cos(a), std::sin(a), 0.3f + 0.7f * s, 0.6f + 0.8f * s);
}

float lightning_flash(float age) {
    if (age < 0.0f || age > 2.0f) return 0.0f;
    auto pulse = [age](float at, float strength) {
        float t = age - at;
        return t < 0.0f ? 0.0f : strength * std::exp(-t * 28.0f);
    };
    return std::min(1.0f, pulse(0.0f, 1.0f) + pulse(0.07f, 0.55f) + pulse(0.19f, 0.8f) + 0.12f * std::exp(-age * 5.0f));
}

void WeatherLook::update(const game::Weather& weather, float dt, std::uint32_t zone) {
    if (zone != 0 && zone != zone_) {
        *this = WeatherLook{};
        zone_ = zone;
    }
    // Towards a target, about 95 % of the way in `seconds`.
    auto ease = [dt](float value, float target, float seconds) {
        return value + (target - value) * (1.0f - std::exp(-std::max(dt, 0.0f) * 3.0f / seconds));
    };
    const float falling = weather.precipitation();
    precipitation = ease(precipitation, falling, kFallEase);
    if (falling <= 0.0f && precipitation < 1e-4f) precipitation = 0.0f;
    const float target = std::clamp(falling * 10.0f, 0.0f, 1.0f);
    storm = ease(storm, target, target > storm ? kOvercastIn : kOvercastOut);
    if (target <= 0.0f && storm < 0.01f) storm = 0.0f;
    const bool is_snow = weather.kind() == game::WeatherKind::Snow;
    snowing = ease(snowing, is_snow ? 1.0f : 0.0f, 4.0f);
    const float drawn = std::clamp(precipitation * 10.0f, 0.0f, 1.0f);
    bool raining = weather.kind() == game::WeatherKind::Rain && drawn > 0.01f;
    bool snowing_now = is_snow && drawn > 0.01f;
    wet = approach(wet, raining ? std::max(drawn, 0.6f) : 0.0f, 1.0f / 30.0f, 1.0f / 120.0f, dt);
    snow = approach(snow, snowing_now ? std::max(drawn, 0.5f) : 0.0f, 1.0f / 120.0f, 1.0f / 300.0f, dt);
    strike_age = weather.lightning() ? 0.0f : strike_age + dt;
    flash = lightning_flash(strike_age);
    rain = raining ? drawn : 0.0f;
    puddles = approach(puddles, raining ? drawn : 0.0f, 1.0f / 90.0f, 1.0f / 240.0f, dt);
}

void set_weather(SceneLighting& lighting, const WeatherLook& look) {
    lighting.weather = glm::vec4(look.wet, look.snow, look.rain, look.puddles);
    lighting.time.z = look.flash;
}

void apply_storm(Daylight& d, float storm) {
    if (storm <= 0.0f) return;
    d.light *= 1.0f - 0.7f * storm;
    auto grey = [](const glm::vec3& c) { return glm::vec3(glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f))); };
    d.sky = glm::mix(d.sky, grey(d.sky) * 0.8f, 0.6f * storm);
    d.ground = glm::mix(d.ground, grey(d.ground) * 0.8f, 0.6f * storm);
    d.baked *= 1.0f - 0.25f * storm;
}

WeatherRenderer::WeatherRenderer(VkContext& vk, const std::filesystem::path& shader_dir) : vk_(vk) {
    VkDevice d = vk_.device();
    {
        UploadBatch upload(vk_);
        assets::Image white;
        white.width = white.height = 1;
        white.mips.push_back(std::vector<std::byte>(4, std::byte{0xff}));
        textures_.push_back(upload.texture(white));
        upload.submit();
    }
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = VK_LOD_CLAMP_NONE;
    vk_check(vkCreateSampler(d, &si, nullptr, &sampler_), "vkCreateSampler (weather)");

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = kMaxTextures;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo bf{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    bf.bindingCount = 1;
    bf.pBindingFlags = &flags;
    VkDescriptorSetLayoutCreateInfo sli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sli.pNext = &bf;
    sli.bindingCount = 1;
    sli.pBindings = &binding;
    vk_check(vkCreateDescriptorSetLayout(d, &sli, nullptr, &set_layout_), "vkCreateDescriptorSetLayout (weather)");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    vk_check(vkCreateDescriptorPool(d, &pi, nullptr, &pool_), "vkCreateDescriptorPool (weather)");
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &set_layout_;
    vk_check(vkAllocateDescriptorSets(d, &ai, &set_), "vkAllocateDescriptorSets (weather)");
    VkDescriptorImageInfo white_info{sampler_, textures_[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set_;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &white_info;
    vkUpdateDescriptorSets(d, 1, &w, 0, nullptr);

    for (std::size_t i = 0; i < instances_.size(); ++i) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sizeof(GpuDrop) * kMaxDrops;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo out{};
        vk_check(vmaCreateBuffer(vk_.allocator(), &bi, &aci, &instances_[i].buffer, &instances_[i].allocation, &out),
                 "create precipitation instance buffer");
        instances_[i].size = bi.size;
        mapped_[i] = out.pMappedData;
    }

    VkPushConstantRange pc{kPushStages, 0, sizeof(PushConstants) + 16};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    li.setLayoutCount = 1;
    li.pSetLayouts = &set_layout_;
    vk_check(vkCreatePipelineLayout(d, &li, nullptr, &layout_), "vkCreatePipelineLayout (weather)");

    VkShaderModule vs = load_shader(d, shader_dir / "precipitation.vert.spv");
    VkShaderModule fs = load_shader(d, shader_dir / "precipitation.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkVertexInputBindingDescription vb{0, sizeof(GpuDrop), VK_VERTEX_INPUT_RATE_INSTANCE};
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuDrop, position)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuDrop, size)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuDrop, uv)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuDrop, color)},
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
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = vk_.scene_samples();
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = VK_TRUE;
    dss.depthWriteEnable = VK_FALSE;
    dss.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // reverse Z
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xf;
    ba.blendEnable = VK_TRUE;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.colorBlendOp = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.alphaBlendOp = VK_BLEND_OP_ADD;
    const SceneBlends blends = vk_.scene_blends(ba, SceneOutput::Reactive);
    const VkPipelineColorBlendStateCreateInfo cb = blends.info();
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri = vk_.scene_rendering();
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
    vk_check(vkCreateGraphicsPipelines(d, vk_.pipeline_cache(), 1, &info, nullptr, &pipeline_),
             "vkCreateGraphicsPipelines (weather)");
    vkDestroyShaderModule(d, vs, nullptr);
    vkDestroyShaderModule(d, fs, nullptr);
}

WeatherRenderer::~WeatherRenderer() {
    VkDevice d = vk_.device();
    vk_.wait_idle();
    if (pipeline_) vkDestroyPipeline(d, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(d, layout_, nullptr);
    if (pool_) vkDestroyDescriptorPool(d, pool_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(d, set_layout_, nullptr);
    if (sampler_) vkDestroySampler(d, sampler_, nullptr);
    for (auto& t : textures_) t.destroy(d, vk_.allocator());
    for (auto& b : instances_) b.destroy(vk_.allocator());
}

void WeatherRenderer::load_textures(const GameDir& game, const game::PrecipitationDefinitions& defs) {
    std::vector<std::string> wanted;
    auto want = [&](const std::string& file) {
        std::string name = lower(file);
        if (!name.empty() && !slots_.contains(name) && std::find(wanted.begin(), wanted.end(), name) == wanted.end())
            wanted.push_back(name);
    };
    for (const auto& kind : defs.kinds) {
        for (const auto& f : kind.falling) want(f.file);
        for (const auto& s : kind.splashing) want(s.file);
    }
    if (wanted.empty()) return;
    vk_.wait_idle(); // the descriptor set is rewritten
    UploadBatch upload(vk_);
    for (const auto& name : wanted) {
        std::uint32_t slot = 0;
        std::string file = name + ".dds";
        auto data = game.read("Resources/Precipitation/" + file);
        if (auto image = data ? assets::decode_game_texture(file, data) : std::nullopt;
            image && textures_.size() < kMaxTextures) {
            slot = static_cast<std::uint32_t>(textures_.size());
            textures_.push_back(upload.texture(*image, TextureUse::Color));
        } else {
            log::warn("precipitation texture {} not found", file);
        }
        slots_.emplace(name, slot);
    }
    upload.submit();
    std::vector<VkDescriptorImageInfo> infos;
    for (const auto& t : textures_) infos.push_back({sampler_, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set_;
    w.dstBinding = 0;
    w.descriptorCount = static_cast<std::uint32_t>(infos.size());
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = infos.data();
    vkUpdateDescriptorSets(vk_.device(), 1, &w, 0, nullptr);
}

std::uint32_t WeatherRenderer::texture_slot(const std::string& file) const {
    auto it = slots_.find(lower(file));
    return it == slots_.end() ? 0u : it->second;
}

void WeatherRenderer::draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera,
                           const glm::vec3& forward, float pixel, const game::PrecipitationField& field, float light,
                           const PrecipitationStyle& style) {
    const auto* defs = field.definitions().kind(field.kind());
    if (!defs || field.active().empty()) return;
    // By texture, each its own draw; within one the field's order.
    auto& groups = groups_;
    for (auto& g : groups) g.clear();
    // The definitions' texture slots, looked up once a frame (a lower-cased copy of the name and a lookup each drop).
    std::array<std::uint32_t, 8> falling_slot{}, splash_slot{};
    for (std::size_t i = 0; i < defs->falling.size() && i < falling_slot.size(); ++i)
        falling_slot[i] = texture_slot(defs->falling[i].file);
    for (std::size_t i = 0; i < defs->splashing.size() && i < splash_slot.size(); ++i)
        splash_slot[i] = texture_slot(defs->splashing[i].file);
    const auto& srgb = srgb_byte_table();
    const auto particles = field.particles();
    for (std::uint32_t index : field.active()) {
        const game::PrecipitationParticle& p = particles[index];
        float width, height, rate;
        std::int32_t frames, orientation;
        std::uint32_t slot;
        if (p.state == game::PrecipitationParticle::Falling) {
            if (p.falling < 0 || static_cast<std::size_t>(p.falling) >= defs->falling.size()) continue;
            const auto& d = defs->falling[static_cast<std::size_t>(p.falling)];
            width = d.width, height = d.height, rate = d.rate, frames = d.frames, orientation = d.orientation;
            const auto f = static_cast<std::size_t>(p.falling);
            slot = f < falling_slot.size() ? falling_slot[f] : texture_slot(d.file);
            if (orientation == 1) height = std::max(height, p.speed * style.streak_seconds);
        } else if (p.state == game::PrecipitationParticle::Splashing) {
            if (p.splash < 0 || static_cast<std::size_t>(p.splash) >= defs->splashing.size()) continue;
            const auto& d = defs->splashing[static_cast<std::size_t>(p.splash)];
            width = d.width, height = d.height, rate = d.rate, frames = d.frames, orientation = d.orientation;
            const auto f = static_cast<std::size_t>(p.splash);
            slot = f < splash_slot.size() ? splash_slot[f] : texture_slot(d.file);
        } else {
            continue;
        }
        if (glm::dot(p.position - camera, forward) <= kNearest) continue;
        // The animation frame, trunc(Rate x age) under the render's chop: of a 4 x 4 atlas (16 frames) or 2 x 2 (4).
        glm::vec4 uv(0.0f, 0.0f, 1.0f, 1.0f);
        if (frames == 16 || frames == 4) {
            const int side = frames == 16 ? 4 : 2;
            auto i = static_cast<std::int32_t>(rate * p.age) % frames;
            if (i < 0) i += frames;
            const float cell = 1.0f / static_cast<float>(side);
            uv = glm::vec4(static_cast<float>(i % side) * cell, static_cast<float>(i / side) * cell, 0.0f, 0.0f);
            uv.z = uv.x + cell;
            uv.w = uv.y + cell;
        }
        auto byte = [&](int shift) { return (p.colour >> shift) & 0xffu; };
        glm::vec4 color(srgb[byte(16)], srgb[byte(8)], srgb[byte(0)], static_cast<float>(byte(24)) / 255.0f);
        groups[slot].push_back(
            GpuDrop{glm::vec4(p.position, static_cast<float>(orientation)), glm::vec4(width, height, 0.0f, 0.0f), uv,
                    color});
    }
    auto* out = static_cast<GpuDrop*>(mapped_[frame.frame_slot]);
    std::uint32_t count = 0;
    std::array<std::pair<std::uint32_t, std::uint32_t>, kMaxTextures> ranges{};
    for (std::uint32_t t = 0; t < kMaxTextures; ++t) {
        auto n = static_cast<std::uint32_t>(std::min<std::size_t>(groups[t].size(), kMaxDrops - count));
        std::copy_n(groups[t].begin(), n, out + count);
        ranges[t] = {count, n};
        count += n;
    }
    if (count == 0) return;
    vmaFlushAllocation(vk_.allocator(), instances_[frame.frame_slot].allocation, 0, sizeof(GpuDrop) * count);
    PushConstants pc{view_proj, glm::vec4(camera, pixel * style.min_pixels), glm::vec4(forward, light),
                     glm::vec4(pixel, style.flake_pixels, style.dot_below, 0.0f)};
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0, nullptr);
    vkCmdBindVertexBuffers(frame.cmd, 0, 1, &instances_[frame.frame_slot].buffer, &offset);
    vkCmdPushConstants(frame.cmd, layout_, kPushStages, 0, sizeof(pc), &pc);
    for (std::uint32_t t = 0; t < kMaxTextures; ++t) {
        if (ranges[t].second == 0) continue;
        glm::uvec4 slot(t, 0, 0, 0);
        vkCmdPushConstants(frame.cmd, layout_, kPushStages, sizeof(pc), sizeof(slot), &slot);
        vkCmdDraw(frame.cmd, 6, ranges[t].second, 0, ranges[t].first);
    }
}

} // namespace hailstone::render
