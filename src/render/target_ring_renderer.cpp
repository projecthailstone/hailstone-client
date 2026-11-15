#include "render/target_ring_renderer.h"

#include <cmath>
#include <cstddef>

#include "render/vk_util.h"

namespace hailstone::render {

namespace {

constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// target_ring.vert / target_ring.frag's push block.
struct Push {
    glm::mat4 view_proj;
    glm::vec4 camera_time; // xyz the camera, w seconds
    glm::vec4 screen;      // x, y the depth terms (render::set_screen), zw the texel size
    glm::vec4 haze;
    glm::vec4 forward;     // xyz the camera's forward
};
static_assert(sizeof(Push) <= 128);

// The ring as the shaders take it, one instance.
struct GpuRing {
    glm::vec4 feet;  // xyz the feet, w the radius
    glm::vec4 color; // linear-to-be display colour, alpha
    glm::vec4 shape; // x the half width, y the spin, z the facing, w the height range round the feet
    glm::vec4 cues;  // x the pulse, y the sweep, z the x-ray, w the glow
};

} // namespace

TargetRingRenderer::TargetRingRenderer(VkContext& vk, const std::filesystem::path& shader_dir) : vk_(vk) {
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sizeof(GpuRing);
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo out{};
        vk_check(vmaCreateBuffer(vk_.allocator(), &bi, &aci, &instances_[i].buffer, &instances_[i].allocation, &out),
                 "create target ring buffer");
        instances_[i].size = bi.size;
        mapped_[i] = out.pMappedData;
    }
    create_pipeline(shader_dir);
}

TargetRingRenderer::~TargetRingRenderer() {
    VkDevice d = vk_.device();
    vk_.wait_idle();
    if (pipeline_) vkDestroyPipeline(d, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(d, layout_, nullptr);
    for (auto& b : instances_) b.destroy(vk_.allocator());
}

void TargetRingRenderer::draw(const FrameInfo& frame, const TargetRingView& view, const TargetRing& ring) {
    if (!pipeline_) return;
    // The ring lies on ground within this height of the feet: enough for a slope under a large target's ring, the
    // rest (a cliff's foot, a floor below) left to the level ring.
    const float band = 1.0f + 0.6f * ring.radius;
    *static_cast<GpuRing*>(mapped_[frame.frame_slot]) =
        GpuRing{glm::vec4(ring.feet, ring.radius), ring.color, glm::vec4(ring.width, ring.spin, ring.facing, band),
                glm::vec4(ring.pulse, ring.flash, ring.xray, ring.glow)};
    vmaFlushAllocation(vk_.allocator(), instances_[frame.frame_slot].allocation, 0, sizeof(GpuRing));
    const float range = std::max(view.far_plane - view.near_plane, 1e-3f);
    Push pc{view.view_proj,
            glm::vec4(view.camera, static_cast<float>(std::fmod(view.seconds, 4096.0))),
            glm::vec4(view.near_plane / range, view.far_plane * view.near_plane / range,
                      1.0f / static_cast<float>(std::max(frame.extent.width, 1u)),
                      1.0f / static_cast<float>(std::max(frame.extent.height, 1u))),
            view.haze, glm::vec4(view.forward, 0.0f)};
    VkCommandBuffer cmd = frame.cmd;
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkDescriptorSet set = vk_.shadow_set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
    vkCmdBindVertexBuffers(cmd, 0, 1, &instances_[frame.frame_slot].buffer, &offset);
    vkCmdPushConstants(cmd, layout_, kPushStages, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 36, 1, 0, 0); // a box round the ring (target_ring.vert)
}

void TargetRingRenderer::create_pipeline(const std::filesystem::path& shader_dir) {
    VkDevice d = vk_.device();
    VkPushConstantRange range{kPushStages, 0, sizeof(Push)};
    VkDescriptorSetLayout set_layout = vk_.shadow_set_layout();
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &set_layout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &range;
    vk_check(vkCreatePipelineLayout(d, &li, nullptr, &layout_), "vkCreatePipelineLayout (target ring)");
    VkShaderModule vs = create_shader_module(d, shader_dir / "target_ring.vert.spv");
    VkShaderModule fs = create_shader_module(d, shader_dir / "target_ring.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr},
    };
    VkVertexInputBindingDescription binding{0, sizeof(GpuRing), VK_VERTEX_INPUT_RATE_INSTANCE};
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuRing, feet)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuRing, color)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuRing, shape)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuRing, cues)},
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
    // No culling and no depth test: the fragment shader keeps the box's far faces (each pixel once, also with the
    // camera inside the box) and decides what hides the ring from the depth copy.
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = vk_.scene_samples();
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = VK_FALSE;
    dss.depthWriteEnable = VK_FALSE;
    // Premultiplied alpha: the ring tints what it lies on, its core a little past 1 so the bloom finds it.
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xf;
    ba.blendEnable = VK_TRUE;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
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
             "vkCreateGraphicsPipelines (target ring)");
    vkDestroyShaderModule(d, vs, nullptr);
    vkDestroyShaderModule(d, fs, nullptr);
}

} // namespace hailstone::render
