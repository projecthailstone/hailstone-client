#include "render/spawn_renderer.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

#include "game/coords.h"
#include "game/view_distance.h"
#include "game/world_state.h"
#include "render/vk_util.h"

namespace hailstone::render {

namespace {

struct BoxVertex {
    glm::vec3 position;
    glm::vec3 normal;
};

// Matches the push_constant block in spawn.vert / spawn.frag.
struct PushConstants {
    glm::mat4 view_proj;
    glm::vec4 fog_color;
    glm::vec4 fog; // start, end
    glm::vec4 camera_pos;
};

// Unit box: x and y in -0.5..0.5, z in 0..1 (feet at the origin).
std::vector<BoxVertex> make_box() {
    std::vector<BoxVertex> v;
    auto face = [&](glm::vec3 n, glm::vec3 u, glm::vec3 w) {
        glm::vec3 c = n * 0.5f + glm::vec3(0, 0, 0.5f);
        glm::vec3 p[4] = {c - u * 0.5f - w * 0.5f, c + u * 0.5f - w * 0.5f, c + u * 0.5f + w * 0.5f,
                          c - u * 0.5f + w * 0.5f};
        for (int i : {0, 1, 2, 0, 2, 3}) v.push_back({p[i], n});
    };
    face({1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    face({-1, 0, 0}, {0, -1, 0}, {0, 0, 1});
    face({0, 1, 0}, {-1, 0, 0}, {0, 0, 1});
    face({0, -1, 0}, {1, 0, 0}, {0, 0, 1});
    face({0, 0, 1}, {1, 0, 0}, {0, 1, 0});
    face({0, 0, -1}, {1, 0, 0}, {0, -1, 0});
    return v;
}

VkShaderModule load_shader(VkDevice device, const std::filesystem::path& path) {
    return create_shader_module(device, path);
}

glm::vec4 spawn_color(const game::Spawn& s) {
    if (s.is_corpse) return {0.35f, 0.35f, 0.35f, 1.0f};
    if (!s.is_npc) return {0.25f, 0.55f, 1.0f, 1.0f};
    return {0.95f, 0.75f, 0.2f, 1.0f};
}

} // namespace

SpawnRenderer::SpawnRenderer(VkContext& vk, const std::filesystem::path& shader_dir) : vk_(vk) {
    auto box = make_box();
    UploadBatch upload(vk_);
    box_ = upload.buffer(std::as_bytes(std::span(box)), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    upload.submit();

    for (std::size_t i = 0; i < instances_.size(); ++i) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sizeof(Instance) * kMaxInstances;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo out{};
        vk_check(vmaCreateBuffer(vk_.allocator(), &bi, &ai, &instances_[i].buffer, &instances_[i].allocation, &out),
                 "create spawn instance buffer");
        instances_[i].size = bi.size;
        mapped_[i] = out.pMappedData;
    }

    VkDevice d = vk_.device();
    VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    vk_check(vkCreatePipelineLayout(d, &li, nullptr, &layout_), "vkCreatePipelineLayout");

    VkShaderModule vs = load_shader(d, shader_dir / "spawn.vert.spv");
    VkShaderModule fs = load_shader(d, shader_dir / "spawn.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr},
    };
    VkVertexInputBindingDescription bindings[] = {
        {0, sizeof(BoxVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE},
    };
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(BoxVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(BoxVertex, normal)},
        {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
        {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
        {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32},
        {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48},
        {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, color)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = 7;
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
    dss.depthWriteEnable = VK_TRUE;
    dss.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xf;
    const SceneBlends blends = vk_.scene_blends(ba, SceneOutput::Nothing);
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
    VkResult r = vkCreateGraphicsPipelines(d, vk_.pipeline_cache(), 1, &info, nullptr, &pipeline_);
    vkDestroyShaderModule(d, vs, nullptr);
    vkDestroyShaderModule(d, fs, nullptr);
    vk_check(r, "vkCreateGraphicsPipelines (spawns)");
}

SpawnRenderer::~SpawnRenderer() {
    vk_.wait_idle();
    VkDevice d = vk_.device();
    if (pipeline_) vkDestroyPipeline(d, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(d, layout_, nullptr);
    for (auto& b : instances_) b.destroy(vk_.allocator());
    box_.destroy(vk_.allocator());
}

void SpawnRenderer::draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera_pos,
                         const game::ViewDistance& view, const game::WorldState& world, std::uint32_t hide_id,
                         const std::unordered_set<std::uint32_t>* skip) {
    auto* out = static_cast<Instance*>(mapped_[frame.frame_slot]);
    std::uint32_t count = 0;
    for (const auto& [id, s] : world.spawns) {
        if (id == hide_id || count == kMaxInstances || (skip && skip->contains(id))) continue;
        float height = std::clamp(s.size > 0.0f ? s.size : 6.0f, 1.0f, 60.0f);
        float width = height * 0.4f;
        float yaw = game::heading_to_yaw(s.display_valid ? s.display_heading : s.heading);
        glm::vec3 at = s.display_valid ? s.display_position : s.position;
        // From the feet, AvatarHeight below the spawn's z (game/spawn_heights.h).
        glm::mat4 m = glm::translate(glm::mat4(1.0f), at - glm::vec3(0, 0, s.heights.avatar_height));
        m = glm::rotate(m, yaw, glm::vec3(0, 0, 1));
        m = glm::scale(m, glm::vec3(width * 0.6f, width, height));
        out[count++] = {m, spawn_color(s)};
    }
    if (count == 0) return;
    vmaFlushAllocation(vk_.allocator(), instances_[frame.frame_slot].allocation, 0, sizeof(Instance) * count);

    VkCommandBuffer cmd = frame.cmd;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkBuffer buffers[] = {box_.buffer, instances_[frame.frame_slot].buffer};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
    PushConstants pc{view_proj, glm::vec4(view.fog_color, 1.0f), glm::vec4(view.fog_start, view.fog_end, view.fog_density, 0),
                     glm::vec4(camera_pos, 1.0f)};
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 36, count, 0, 0);
}

} // namespace hailstone::render
