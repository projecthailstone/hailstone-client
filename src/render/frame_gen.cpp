#include "render/frame_gen.h"

#include <stdexcept>
#include <vector>

#include "render/spirv_file.h"
#include "render/vk_util.h"

namespace hailstone::render {

namespace {

// shaders/fg_scatter.comp's and fg_fill.comp's push constants.
struct ComputePush {
    std::int32_t width = 0, height = 0;
    float t = 0.5f;
    std::uint32_t pass = 0;
};

// The set's bindings (shaders/fg_*.comp, fg_synth.frag): 0 the depth, 1 the motion, 2 the keys, 3 the winners'
// motion, 4 the in-between motion as the fill writes it, 5 the last picture, 6 this one, 7 the reactive mask, 8 the
// in-between motion as the synthesis reads it, 9 the keys at the last frame's places.
constexpr std::uint32_t kBindings = 10;

void memory_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src, VkAccessFlags2 src_access,
                    VkPipelineStageFlags2 dst, VkAccessFlags2 dst_access) {
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = src;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst;
    b.dstAccessMask = dst_access;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

VkShaderModule load(VkDevice device, const std::filesystem::path& path) {
    const std::vector<std::uint32_t> code = read_spirv_file(path);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size() * sizeof(std::uint32_t);
    info.pCode = code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    vk_check(vkCreateShaderModule(device, &info, nullptr, &m), "vkCreateShaderModule (frame generation)");
    return m;
}

} // namespace

FrameGen::FrameGen(VkDevice device, VmaAllocator allocator, VkPipelineCache cache,
                   const std::filesystem::path& shader_dir)
    : device_(device), allocator_(allocator) {
    VkDescriptorSetLayoutBinding b[kBindings];
    for (std::uint32_t i = 0; i < kBindings; ++i) {
        const bool storage = (i >= 2 && i <= 4) || i == 9;
        b[i] = {i, storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = kBindings;
    li.pBindings = b;
    vk_check(vkCreateDescriptorSetLayout(device_, &li, nullptr, &set_layout_), "vkCreateDescriptorSetLayout (framegen)");
    // The composite: 0 the picture, 1 the UI's image.
    VkDescriptorSetLayoutBinding cbind[2] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    li.bindingCount = 2;
    li.pBindings = cbind;
    vk_check(vkCreateDescriptorSetLayout(device_, &li, nullptr, &composite_set_layout_),
             "vkCreateDescriptorSetLayout (framegen composite)");

    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * 6 + 3 * 2},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * 4}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 5;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    vk_check(vkCreateDescriptorPool(device_, &pi, nullptr, &pool_), "vkCreateDescriptorPool (framegen)");
    VkDescriptorSetLayout layouts[5] = {set_layout_, set_layout_, composite_set_layout_, composite_set_layout_,
                                        composite_set_layout_};
    VkDescriptorSet sets[5];
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 5;
    ai.pSetLayouts = layouts;
    vk_check(vkAllocateDescriptorSets(device_, &ai, sets), "vkAllocateDescriptorSets (framegen)");
    synth_sets_ = {sets[0], sets[1]};
    composite_sets_ = {sets[2], sets[3], sets[4]};

    VkPushConstantRange cpc{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePush)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &cpc;
    vk_check(vkCreatePipelineLayout(device_, &pli, nullptr, &compute_layout_), "vkCreatePipelineLayout (framegen)");
    VkPushConstantRange spc{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SynthPush)};
    pli.pPushConstantRanges = &spc;
    vk_check(vkCreatePipelineLayout(device_, &pli, nullptr, &synth_layout_), "vkCreatePipelineLayout (framegen synth)");
    VkPushConstantRange xpc{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompositePush)};
    pli.pSetLayouts = &composite_set_layout_;
    pli.pPushConstantRanges = &xpc;
    vk_check(vkCreatePipelineLayout(device_, &pli, nullptr, &composite_layout_),
             "vkCreatePipelineLayout (framegen composite)");

    for (auto [name, out] : {std::pair{"fg_scatter.comp.spv", &scatter_pipeline_},
                             std::pair{"fg_fill.comp.spv", &fill_pipeline_}}) {
        VkShaderModule m = load(device_, shader_dir / name);
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m,
                    "main", nullptr};
        ci.layout = compute_layout_;
        const VkResult r = vkCreateComputePipelines(device_, cache, 1, &ci, nullptr, out);
        vkDestroyShaderModule(device_, m, nullptr);
        vk_check(r, "vkCreateComputePipelines (framegen)");
    }
}

FrameGen::~FrameGen() {
    free_image(key_);
    free_image(prev_key_);
    free_image(field_);
    free_image(motion_t_);
    for (VkPipeline p : {scatter_pipeline_, fill_pipeline_})
        if (p) vkDestroyPipeline(device_, p, nullptr);
    for (VkPipelineLayout l : {compute_layout_, synth_layout_, composite_layout_})
        if (l) vkDestroyPipelineLayout(device_, l, nullptr);
    if (pool_) vkDestroyDescriptorPool(device_, pool_, nullptr);
    for (VkDescriptorSetLayout l : {set_layout_, composite_set_layout_})
        if (l) vkDestroyDescriptorSetLayout(device_, l, nullptr);
}

FrameGen::Image FrameGen::make_image(VkFormat format, VkImageUsageFlags usage) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {scene_.width, scene_.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    Image out;
    vk_check(vmaCreateImage(allocator_, &ii, &ai, &out.image, &out.alloc, nullptr), "create frame generation image");
    out.view = create_image_view(device_, out.image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    return out;
}

void FrameGen::free_image(Image& image) {
    if (image.view) vkDestroyImageView(device_, image.view, nullptr);
    if (image.image) vmaDestroyImage(allocator_, image.image, image.alloc);
    image = {};
}

void FrameGen::resize(VkExtent2D scene) {
    if (scene.width == scene_.width && scene.height == scene_.height && key_.image) return;
    free_image(key_);
    free_image(prev_key_);
    free_image(field_);
    free_image(motion_t_);
    scene_ = scene;
    // R32UI and RGBA16F storage need no optional device feature (shaderStorageImageExtendedFormats).
    key_ = make_image(VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    prev_key_ = make_image(VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    field_ = make_image(VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT);
    motion_t_ = make_image(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    fresh_ = true;
}

void FrameGen::bind(const Views& v) {
    constexpr VkImageLayout kRead = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    constexpr VkImageLayout kGeneral = VK_IMAGE_LAYOUT_GENERAL;
    const VkImageView reactive = v.reactive ? v.reactive : v.placeholder;
    std::vector<VkDescriptorImageInfo> infos;
    std::vector<VkWriteDescriptorSet> writes;
    infos.reserve(2 * kBindings + 6);
    auto add = [&](VkDescriptorSet set, std::uint32_t binding, VkDescriptorType type, VkDescriptorImageInfo info) {
        infos.push_back(info);
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = type;
        writes.push_back(w);
    };
    constexpr VkDescriptorType kSampled = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    constexpr VkDescriptorType kStorage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    for (std::uint32_t i = 0; i < 2; ++i) {
        const VkDescriptorSet s = synth_sets_[i];
        add(s, 0, kSampled, {v.nearest, v.depth, kRead});
        add(s, 1, kSampled, {v.nearest, v.motion, kRead});
        add(s, 2, kStorage, {VK_NULL_HANDLE, key_.view, kGeneral});
        add(s, 3, kStorage, {VK_NULL_HANDLE, field_.view, kGeneral});
        add(s, 4, kStorage, {VK_NULL_HANDLE, motion_t_.view, kGeneral});
        add(s, 5, kSampled, {v.linear, v.pictures[i ^ 1u], kRead});
        add(s, 6, kSampled, {v.linear, v.pictures[i], kRead});
        add(s, 7, kSampled, {v.linear, reactive, kRead});
        add(s, 8, kSampled, {v.nearest, motion_t_.view, kGeneral});
        add(s, 9, kStorage, {VK_NULL_HANDLE, prev_key_.view, kGeneral});
    }
    for (std::uint32_t i = 0; i < 3; ++i) {
        add(composite_sets_[i], 0, kSampled, {v.nearest, i < 2 ? v.pictures[i] : v.generated, kRead});
        add(composite_sets_[i], 1, kSampled, {v.nearest, v.overlay, kRead});
    }
    for (std::size_t i = 0; i < writes.size(); ++i) writes[i].pImageInfo = &infos[i];
    vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void FrameGen::record_motion(VkCommandBuffer cmd, float t) {
    if (fresh_) {
        for (VkImage image : {key_.image, prev_key_.image, field_.image, motion_t_.image})
            image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                          VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                          VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        fresh_ = false;
    }
    // This frame's motion and depth (written by the scene and temporal_prep.frag, already readable by fragment
    // shaders) for the compute passes; the last frame's synthesis done with the in-between motion before it changes.
    memory_barrier(cmd,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    const VkClearColorValue zero{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, key_.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
    vkCmdClearColorImage(cmd, prev_key_.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
    memory_barrier(cmd, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout_, 0, 1, &synth_sets_[0], 0, nullptr);
    ComputePush push;
    push.width = static_cast<std::int32_t>(scene_.width);
    push.height = static_cast<std::int32_t>(scene_.height);
    push.t = t;
    const std::uint32_t gx = (scene_.width + 7) / 8, gy = (scene_.height + 7) / 8;
    auto step = [&](VkPipeline pipeline, std::uint32_t pass) {
        push.pass = pass;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdPushConstants(cmd, compute_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, gx, gy, 1);
        memory_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    };
    step(scatter_pipeline_, 0); // the nearest depth at each place, then and a frame ago
    step(scatter_pipeline_, 1); // the winners' motion
    step(fill_pipeline_, 0);    // every place's motion, the holes the background's
}

} // namespace hailstone::render
