#include "render/spell_fx_renderer.h"

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "render/scene_lighting.h"
#include "render/vk_util.h"

namespace hailstone::render {

namespace {

constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// spell_fx.vert / spell_fx.frag's push block (fire.vert's).
struct Push {
    glm::mat4 view_proj;
    glm::vec4 camera_time;
    glm::vec4 screen;
    glm::vec4 haze; // w: the glow scale
};
static_assert(sizeof(Push) <= 128);

// One piece as the vertex shader takes it, per instance.
struct GpuPiece {
    glm::vec4 a;     // xyz the point, w its radius
    glm::vec4 b;     // xyz a segment's tail, w its radius
    glm::vec4 kind;  // x the kind, y the element, z the age, w the seed
    glm::vec4 extra; // x the intensity, y the phase
};

VkShaderModule load_shader(VkDevice device, const std::filesystem::path& path) {
    return create_shader_module(device, path);
}

} // namespace

SpellFxRenderer::SpellFxRenderer(VkContext& vk, const std::filesystem::path& shader_dir) : vk_(vk) {
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sizeof(GpuPiece) * kMaxPieces;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo out{};
        vk_check(vmaCreateBuffer(vk_.allocator(), &bi, &aci, &instances_[i].buffer, &instances_[i].allocation, &out),
                 "create spell fx instance buffer");
        instances_[i].size = bi.size;
        mapped_[i] = out.pMappedData;
    }
    create_pipeline(shader_dir);
}

SpellFxRenderer::~SpellFxRenderer() {
    VkDevice d = vk_.device();
    vk_.wait_idle();
    if (pipeline_) vkDestroyPipeline(d, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(d, layout_, nullptr);
    for (auto& b : instances_) b.destroy(vk_.allocator());
}

void SpellFxRenderer::draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera, double seconds,
                           std::span<const SpellFxPiece> pieces, const SceneLighting& lighting, float glow) {
    if (pieces.empty() || !pipeline_) return;
    auto* out = static_cast<GpuPiece*>(mapped_[frame.frame_slot]);
    std::uint32_t count = 0;
    for (const SpellFxPiece& p : pieces) {
        if (count == kMaxPieces) break;
        out[count++] = GpuPiece{glm::vec4(p.a, p.radius_a), glm::vec4(p.b, p.radius_b),
                                glm::vec4(static_cast<float>(p.kind), static_cast<float>(p.element), p.age, p.seed),
                                glm::vec4(p.intensity, p.phase, 0.0f, 0.0f)};
    }
    vmaFlushAllocation(vk_.allocator(), instances_[frame.frame_slot].allocation, 0, sizeof(GpuPiece) * count);
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

void SpellFxRenderer::create_pipeline(const std::filesystem::path& shader_dir) {
    VkDevice d = vk_.device();
    VkPushConstantRange range{kPushStages, 0, sizeof(Push)};
    VkDescriptorSetLayout set_layout = vk_.shadow_set_layout();
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &set_layout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &range;
    vk_check(vkCreatePipelineLayout(d, &li, nullptr, &layout_), "vkCreatePipelineLayout (spell fx)");
    VkShaderModule vs = load_shader(d, shader_dir / "spell_fx.vert.spv");
    VkShaderModule fs = load_shader(d, shader_dir / "spell_fx.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr},
    };
    VkVertexInputBindingDescription binding{0, sizeof(GpuPiece), VK_VERTEX_INPUT_RATE_INSTANCE};
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuPiece, a)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuPiece, b)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuPiece, kind)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuPiece, extra)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
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
    // Light, added.
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
             "vkCreateGraphicsPipelines (spell fx)");
    vkDestroyShaderModule(d, vs, nullptr);
    vkDestroyShaderModule(d, fs, nullptr);
}

} // namespace hailstone::render
