#include "render/fire_renderer.h"

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "render/scene_lighting.h"
#include "render/vk_util.h"

namespace hailstone::render {

namespace {

constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// fire.vert / fire.frag's push block.
struct Push {
    glm::mat4 view_proj;
    glm::vec4 camera_time; // xyz the camera, w seconds (wrapped)
    glm::vec4 screen;      // SceneLighting::screen: the depth terms, 1 / the target's size
    glm::vec4 haze;        // SceneLighting::haze, w: the glow scale
};
static_assert(sizeof(Push) <= 128);

// One flame as the vertex shader takes it, per instance.
struct GpuFire {
    glm::vec4 base_height; // xyz its foot, w its height
    glm::vec4 shape;       // x its width, y its seed, z its intensity
    glm::vec4 tint; // rgb the tint, w the recolour
};

VkShaderModule load_shader(VkDevice device, const std::filesystem::path& path) {
    return create_shader_module(device, path);
}

} // namespace

void add_fire_sources(const assets::ZoneFire& fire, std::vector<FireSource>& out) {
    FireSource s;
    s.base = fire.base;
    s.width = fire.width;
    s.height = fire.height;
    s.seed = std::fmod(std::abs(fire.base.x * 0.137f + fire.base.y * 0.311f + fire.base.z * 0.071f), 97.0f);
    switch (fire.hue) {
    case assets::FlameHue::Blue: s.tint = {0.12f, 0.4f, 1.8f}; s.recolor = 1.0f; break;
    case assets::FlameHue::Green: s.tint = {0.45f, 1.5f, 0.35f}; s.recolor = 1.0f; break;
    case assets::FlameHue::Violet: s.tint = {1.1f, 0.35f, 1.5f}; s.recolor = 1.0f; break;
    case assets::FlameHue::Red: s.tint = {1.6f, 0.25f, 0.12f}; s.recolor = 0.6f; break;
    default: break;
    }
    if (!(fire.width >= 2.0f && fire.width > fire.height * 0.5f)) {
        out.push_back(s);
        return;
    }
    // Three tongues round the middle one, turned by the seed, each its own turbulence.
    FireSource middle = s;
    middle.width = fire.width * 0.6f;
    out.push_back(middle);
    for (int i = 0; i < 3; ++i) {
        float a = s.seed * 2.4f + static_cast<float>(i) * 2.0944f;
        FireSource t = s;
        t.base += glm::vec3(std::cos(a), std::sin(a), 0.0f) * (fire.width * 0.22f);
        t.width = fire.width * 0.38f;
        t.height = fire.height * (0.55f + 0.1f * static_cast<float>(i));
        t.seed = s.seed + 11.3f * static_cast<float>(i + 1);
        t.intensity = 0.8f;
        out.push_back(t);
    }
}

FireRenderer::FireRenderer(VkContext& vk, const std::filesystem::path& shader_dir) : vk_(vk) {
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sizeof(GpuFire) * kMaxFires;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo out{};
        vk_check(vmaCreateBuffer(vk_.allocator(), &bi, &aci, &instances_[i].buffer, &instances_[i].allocation, &out),
                 "create fire instance buffer");
        instances_[i].size = bi.size;
        mapped_[i] = out.pMappedData;
    }
    create_pipeline(shader_dir);
}

FireRenderer::~FireRenderer() {
    VkDevice d = vk_.device();
    vk_.wait_idle();
    if (pipeline_) vkDestroyPipeline(d, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(d, layout_, nullptr);
    for (auto& b : instances_) b.destroy(vk_.allocator());
}

void FireRenderer::draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera, double seconds,
                        std::span<const FireSource> fires, const SceneLighting& lighting, float range, float glow) {
    if (fires.empty() || !pipeline_) return;
    auto* out = static_cast<GpuFire*>(mapped_[frame.frame_slot]);
    std::uint32_t count = 0;
    const float range2 = range * range;
    for (const FireSource& f : fires) {
        if (count == kMaxFires) break;
        glm::vec3 d = f.base - camera;
        if (glm::dot(d, d) > range2) continue;
        // Behind the camera: the quad reaches at most its height and half its width out from the foot.
        float reach = f.height * 1.8f + f.width;
        glm::vec4 c = view_proj * glm::vec4(f.base + glm::vec3(0.0f, 0.0f, f.height * 0.6f), 1.0f);
        if (c.w < -reach) continue;
        out[count++] = GpuFire{glm::vec4(f.base, f.height), glm::vec4(f.width, f.seed, f.intensity, 0.0f),
                               glm::vec4(f.tint, f.recolor)};
    }
    if (count == 0) return;
    vmaFlushAllocation(vk_.allocator(), instances_[frame.frame_slot].allocation, 0, sizeof(GpuFire) * count);
    VkCommandBuffer cmd = frame.cmd;
    Push pc{view_proj, glm::vec4(camera, static_cast<float>(std::fmod(seconds, 4096.0))), lighting.screen,
            glm::vec4(glm::vec3(lighting.haze), glow)};
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkDescriptorSet set = vk_.shadow_set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
    vkCmdBindVertexBuffers(cmd, 0, 1, &instances_[frame.frame_slot].buffer, &offset);
    vkCmdPushConstants(cmd, layout_, kPushStages, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 6, count, 0, 0);
}

void FireRenderer::create_pipeline(const std::filesystem::path& shader_dir) {
    VkDevice d = vk_.device();
    VkPushConstantRange range{kPushStages, 0, sizeof(Push)};
    VkDescriptorSetLayout set_layout = vk_.shadow_set_layout();
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &set_layout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &range;
    vk_check(vkCreatePipelineLayout(d, &li, nullptr, &layout_), "vkCreatePipelineLayout (fire)");
    VkShaderModule vs = load_shader(d, shader_dir / "fire.vert.spv");
    VkShaderModule fs = load_shader(d, shader_dir / "fire.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr},
    };
    VkVertexInputBindingDescription binding{0, sizeof(GpuFire), VK_VERTEX_INPUT_RATE_INSTANCE};
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuFire, base_height)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuFire, shape)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuFire, tint)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
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
    // Light, added: the flame gives off what it shows (premultiplied by its own cover).
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xf;
    ba.blendEnable = VK_TRUE;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
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
             "vkCreateGraphicsPipelines (fire)");
    vkDestroyShaderModule(d, vs, nullptr);
    vkDestroyShaderModule(d, fs, nullptr);
}

} // namespace hailstone::render
