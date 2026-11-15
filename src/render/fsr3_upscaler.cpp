// AMD FidelityFX FSR 3.1's upscaler, Hailstone's host side (render/fsr3_upscaler.h). The constants, the internal
// images, the clears and the dispatch order follow the SDK's sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
// (FidelityFX SDK v1.1.4, MIT: Copyright (C) 2024 Advanced Micro Devices, Inc.; third_party/fidelityfx/LICENSE.txt);
// the binding tables follow the passes' own (third_party/fidelityfx/shaders/*_pass.glsl).

#include "render/fsr3_upscaler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "render/vk_util.h"

namespace hailstone::render {

namespace {

// cbFSR3Upscaler (ffx_fsr3upscaler_callbacks_glsl.h, std140): every member lands where the C layout puts it.
struct Constants {
    std::int32_t render_size[2];
    std::int32_t previous_render_size[2];
    std::int32_t upscale_size[2];
    std::int32_t previous_upscale_size[2];
    std::int32_t max_render_size[2];
    std::int32_t max_upscale_size[2];
    float device_to_view_depth[4];
    float jitter[2];
    float previous_jitter[2];
    float motion_vector_scale[2];
    float downscale_factor[2];
    float motion_vector_jitter_cancellation[2];
    float tan_half_fov;
    float jitter_phase_count;
    float delta_time;
    float delta_pre_exposure;
    float view_space_to_meters;
    float frame_index;
    float velocity_factor;
    float reactiveness_scale;
    float shading_change_scale;
    float accumulation_added_per_frame;
    float min_disocclusion_accumulation;
};
static_assert(sizeof(Constants) == 148);

struct SpdConstants { // cbSPD
    std::uint32_t mips;
    std::uint32_t work_groups;
    std::uint32_t work_group_offset[2];
    std::uint32_t render_size[2];
};

struct RcasConstants { // cbRCAS
    std::uint32_t config[4];
};

// What a binding holds (the SDK's resource identifiers, as far as these passes use them).
enum class Res : std::uint8_t {
    InputColor, InputDepth, InputMotion, Reactive, TransparencyComposition, Exposure, DilatedMotion, DilatedDepth,
    ReconPrevDepth, FarthestDepth, FarthestDepthMip1, CurrentLuma, PreviousLuma, SpdAtomic, FrameInfo, SpdMips,
    SpdMip, ShadingChange, Accumulation, AccumulationOut, DilatedReactive, NewLocks, LumaHistory, LumaHistoryOut,
    LumaInstability, Upscaled, UpscaledOut, LanczosLut, RcasInput, Output, CbMain, CbSpd, CbRcas,
};
enum class Kind : std::uint8_t { Sampled, Storage, Uniform };
struct Binding {
    std::uint32_t binding;
    Kind kind;
    Res res;
    std::uint32_t mip = 0; // SpdMip
};

// The passes' bindings (their FSR3UPSCALER_BIND_* defines), in Pass order.
const std::vector<Binding> kPassBindings[] = {
    { // prepare_inputs
        {0, Kind::Sampled, Res::InputMotion}, {1, Kind::Sampled, Res::InputDepth}, {2, Kind::Sampled, Res::InputColor},
        {3, Kind::Storage, Res::DilatedMotion}, {4, Kind::Storage, Res::DilatedDepth},
        {5, Kind::Storage, Res::ReconPrevDepth}, {6, Kind::Storage, Res::FarthestDepth},
        {7, Kind::Storage, Res::CurrentLuma}, {8, Kind::Uniform, Res::CbMain},
    },
    { // luma_pyramid
        {0, Kind::Sampled, Res::CurrentLuma}, {1, Kind::Sampled, Res::FarthestDepth},
        {2, Kind::Storage, Res::SpdAtomic}, {3, Kind::Storage, Res::FrameInfo},
        {4, Kind::Storage, Res::SpdMip, 0}, {5, Kind::Storage, Res::SpdMip, 1}, {6, Kind::Storage, Res::SpdMip, 2},
        {7, Kind::Storage, Res::SpdMip, 3}, {8, Kind::Storage, Res::SpdMip, 4}, {9, Kind::Storage, Res::SpdMip, 5},
        {10, Kind::Storage, Res::FarthestDepthMip1}, {11, Kind::Uniform, Res::CbMain}, {12, Kind::Uniform, Res::CbSpd},
    },
    { // shading_change_pyramid
        {0, Kind::Sampled, Res::CurrentLuma}, {1, Kind::Sampled, Res::PreviousLuma},
        {2, Kind::Sampled, Res::DilatedMotion}, {3, Kind::Sampled, Res::Exposure}, {4, Kind::Storage, Res::SpdAtomic},
        {5, Kind::Storage, Res::SpdMip, 0}, {6, Kind::Storage, Res::SpdMip, 1}, {7, Kind::Storage, Res::SpdMip, 2},
        {8, Kind::Storage, Res::SpdMip, 3}, {9, Kind::Storage, Res::SpdMip, 4}, {10, Kind::Storage, Res::SpdMip, 5},
        {11, Kind::Uniform, Res::CbMain}, {12, Kind::Uniform, Res::CbSpd},
    },
    { // shading_change
        {0, Kind::Sampled, Res::SpdMips}, {1, Kind::Storage, Res::ShadingChange}, {2, Kind::Uniform, Res::CbMain},
    },
    { // prepare_reactivity
        {0, Kind::Sampled, Res::ReconPrevDepth}, {1, Kind::Sampled, Res::DilatedMotion},
        {2, Kind::Sampled, Res::DilatedDepth}, {3, Kind::Sampled, Res::Reactive},
        {4, Kind::Sampled, Res::TransparencyComposition}, {5, Kind::Sampled, Res::Accumulation},
        {6, Kind::Sampled, Res::ShadingChange}, {7, Kind::Sampled, Res::CurrentLuma}, {8, Kind::Sampled, Res::Exposure},
        {9, Kind::Storage, Res::DilatedReactive}, {10, Kind::Storage, Res::NewLocks},
        {11, Kind::Storage, Res::AccumulationOut}, {12, Kind::Uniform, Res::CbMain},
    },
    { // luma_instability
        {0, Kind::Sampled, Res::Exposure}, {1, Kind::Sampled, Res::DilatedReactive},
        {2, Kind::Sampled, Res::DilatedMotion}, {3, Kind::Sampled, Res::FrameInfo}, {4, Kind::Sampled, Res::LumaHistory},
        {5, Kind::Sampled, Res::FarthestDepthMip1}, {6, Kind::Sampled, Res::CurrentLuma},
        {7, Kind::Storage, Res::LumaHistoryOut}, {8, Kind::Storage, Res::LumaInstability},
        {9, Kind::Uniform, Res::CbMain},
    },
    { // accumulate
        {0, Kind::Sampled, Res::Exposure}, {1, Kind::Sampled, Res::DilatedReactive},
        {2, Kind::Sampled, Res::DilatedMotion}, {3, Kind::Sampled, Res::Upscaled}, {4, Kind::Sampled, Res::LanczosLut},
        {5, Kind::Sampled, Res::FarthestDepthMip1}, {6, Kind::Sampled, Res::CurrentLuma},
        {7, Kind::Sampled, Res::LumaInstability}, {8, Kind::Sampled, Res::InputColor},
        {9, Kind::Storage, Res::UpscaledOut}, {10, Kind::Storage, Res::Output}, {11, Kind::Storage, Res::NewLocks},
        {12, Kind::Uniform, Res::CbMain},
    },
    {}, // accumulate with sharpening: the same as accumulate (filled below)
    { // rcas
        {0, Kind::Sampled, Res::Exposure}, {1, Kind::Sampled, Res::RcasInput}, {2, Kind::Storage, Res::Output},
        {3, Kind::Uniform, Res::CbMain}, {4, Kind::Uniform, Res::CbRcas},
    },
};
const char* const kPassShaders[] = {
    "fsr3_prepare_inputs.spv", "fsr3_luma_pyramid.spv", "fsr3_shading_change_pyramid.spv", "fsr3_shading_change.spv",
    "fsr3_prepare_reactivity.spv", "fsr3_luma_instability.spv", "fsr3_accumulate.spv", "fsr3_accumulate_sharpen.spv",
    "fsr3_rcas.spv",
};
constexpr std::uint32_t kPointSampler = 1000, kLinearSampler = 1001; // ffx_fsr3upscaler_callbacks_glsl.h

const std::vector<Binding>& bindings_of(std::uint32_t pass) {
    return kPassBindings[pass == 7 ? 6 : pass];
}

VkDescriptorType descriptor_type(Kind k) {
    switch (k) {
    case Kind::Sampled: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case Kind::Storage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case Kind::Uniform: break;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

float lanczos2(float x) {
    constexpr float pi = 3.14159265358979f;
    if (std::abs(x) < 1e-6f) return 1.0f;
    return (std::sin(pi * x) / (pi * x)) * (std::sin(0.5f * pi * x) / (0.5f * pi * x));
}

// IEEE half from float (round to nearest), for RCAS's packed constant.
std::uint32_t to_half(float f) {
    std::uint32_t x;
    std::memcpy(&x, &f, 4);
    const std::uint32_t sign = (x >> 16) & 0x8000u;
    const std::int32_t exp = static_cast<std::int32_t>((x >> 23) & 0xff) - 127 + 15;
    std::uint32_t mant = x & 0x7fffffu;
    if (exp <= 0) return sign; // too small: zero
    if (exp >= 31) return sign | 0x7c00u; // too large: infinity
    mant += 0x1000u; // round
    if (mant & 0x800000u) return sign | (static_cast<std::uint32_t>(exp + 1) << 10);
    return sign | (static_cast<std::uint32_t>(exp) << 10) | (mant >> 13);
}

constexpr VkFormat kFormats[] = {
    VK_FORMAT_R8_UNORM, VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_UINT,
    VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT,
};

} // namespace

bool Fsr3Upscaler::supported(VkPhysicalDevice physical) {
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physical, &features);
    if (!features.shaderStorageImageExtendedFormats) return false;
    VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &subgroup;
    vkGetPhysicalDeviceProperties2(physical, &props);
    if (!(subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) ||
        !(subgroup.supportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT))
        return false;
    for (VkFormat f : kFormats) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(physical, f, &fp);
        constexpr VkFormatFeatureFlags need = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((fp.optimalTilingFeatures & need) != need) return false;
    }
    return true;
}

Fsr3Upscaler::Fsr3Upscaler(VkDevice device, VmaAllocator allocator, VkPipelineCache cache,
                           const std::filesystem::path& shader_dir)
    : device_(device), allocator_(allocator) {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = VK_LOD_CLAMP_NONE;
    vk_check(vkCreateSampler(device_, &si, nullptr, &point_), "vkCreateSampler (FSR 3 point)");
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vk_check(vkCreateSampler(device_, &si, nullptr, &linear_), "vkCreateSampler (FSR 3 linear)");

    std::uint32_t sampled = 0, storage = 0, uniform = 0;
    for (std::uint32_t pass = 0; pass < kPasses; ++pass) {
        std::vector<VkDescriptorSetLayoutBinding> b;
        for (const Binding& x : bindings_of(pass)) {
            b.push_back({x.binding, descriptor_type(x.kind), 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
            (x.kind == Kind::Sampled ? sampled : x.kind == Kind::Storage ? storage : uniform) += kFrames;
        }
        b.push_back({kPointSampler, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &point_});
        b.push_back({kLinearSampler, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &linear_});
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = static_cast<std::uint32_t>(b.size());
        li.pBindings = b.data();
        vk_check(vkCreateDescriptorSetLayout(device_, &li, nullptr, &set_layouts_[pass]),
                 "vkCreateDescriptorSetLayout (FSR 3)");
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &set_layouts_[pass];
        vk_check(vkCreatePipelineLayout(device_, &pli, nullptr, &layouts_[pass]), "vkCreatePipelineLayout (FSR 3)");
        VkShaderModule module = create_shader_module(device_, shader_dir / kPassShaders[pass]);
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module,
                    "main", nullptr};
        ci.layout = layouts_[pass];
        const VkResult r = vkCreateComputePipelines(device_, cache, 1, &ci, nullptr, &pipelines_[pass]);
        vkDestroyShaderModule(device_, module, nullptr);
        vk_check(r, "vkCreateComputePipelines (FSR 3)");
    }
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sampled},
                                          {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storage},
                                          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform},
                                          {VK_DESCRIPTOR_TYPE_SAMPLER, 2 * kPasses * kFrames}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = kPasses * kFrames;
    pi.poolSizeCount = 4;
    pi.pPoolSizes = sizes;
    vk_check(vkCreateDescriptorPool(device_, &pi, nullptr, &pool_), "vkCreateDescriptorPool (FSR 3)");
    for (auto& frame : sets_) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool_;
        ai.descriptorSetCount = kPasses;
        ai.pSetLayouts = set_layouts_.data();
        vk_check(vkAllocateDescriptorSets(device_, &ai, frame.data()), "vkAllocateDescriptorSets (FSR 3)");
    }

    // The constants, written each frame; the Lanczos table, once.
    auto host_buffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VmaAllocation& alloc) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size;
        bi.usage = usage;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        vk_check(vmaCreateBuffer(allocator_, &bi, &ai, &buffer, &alloc, &info), "vmaCreateBuffer (FSR 3)");
        return info.pMappedData;
    };
    for (std::uint32_t i = 0; i < kFrames; ++i)
        ubo_map_[i] = host_buffer(3 * kUboStride, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ubo_[i], ubo_alloc_[i]);
    std::int16_t lut[128];
    for (std::uint32_t i = 0; i < 128; ++i) {
        const float x = 2.0f * static_cast<float>(i) / 127.0f;
        lut[i] = static_cast<std::int16_t>(std::lround(lanczos2(x) * 32767.0f));
    }
    void* staging = host_buffer(sizeof(lut), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, lut_staging_, lut_staging_alloc_);
    std::memcpy(staging, lut, sizeof(lut));
    vmaFlushAllocation(allocator_, lut_staging_alloc_, 0, VK_WHOLE_SIZE);
}

Fsr3Upscaler::~Fsr3Upscaler() {
    destroy_images();
    for (std::uint32_t i = 0; i < kFrames; ++i)
        if (ubo_[i]) vmaDestroyBuffer(allocator_, ubo_[i], ubo_alloc_[i]);
    if (lut_staging_) vmaDestroyBuffer(allocator_, lut_staging_, lut_staging_alloc_);
    if (pool_) vkDestroyDescriptorPool(device_, pool_, nullptr);
    for (std::uint32_t pass = 0; pass < kPasses; ++pass) {
        if (pipelines_[pass]) vkDestroyPipeline(device_, pipelines_[pass], nullptr);
        if (layouts_[pass]) vkDestroyPipelineLayout(device_, layouts_[pass], nullptr);
        if (set_layouts_[pass]) vkDestroyDescriptorSetLayout(device_, set_layouts_[pass], nullptr);
    }
    if (point_) vkDestroySampler(device_, point_, nullptr);
    if (linear_) vkDestroySampler(device_, linear_, nullptr);
}

Fsr3Upscaler::Image Fsr3Upscaler::make_image(VkFormat format, VkExtent2D extent, std::uint32_t levels) {
    extent.width = std::max(extent.width, 1u);
    extent.height = std::max(extent.height, 1u);
    if (levels == 0) // the whole chain
        levels = 1 + static_cast<std::uint32_t>(std::floor(std::log2(static_cast<double>(std::max(extent.width, extent.height)))));
    Image out;
    out.format = format;
    out.levels = levels;
    const bool lut = format == VK_FORMAT_R16_SNORM;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {extent.width, extent.height, 1};
    ii.mipLevels = levels;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (lut ? 0u : static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_STORAGE_BIT));
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    vk_check(vmaCreateImage(allocator_, &ii, &ai, &out.image, &out.alloc, nullptr), "create FSR 3 image");
    out.view = create_image_view(device_, out.image, format, VK_IMAGE_ASPECT_COLOR_BIT, levels);
    if (!lut)
        for (std::uint32_t m = 0; m < out.mips.size(); ++m) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = out.image;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = format;
            // Past the chain, the last mip (as the SDK's backend binds it).
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, std::min(m, levels - 1), 1, 0, 1};
            vk_check(vkCreateImageView(device_, &vi, nullptr, &out.mips[m]), "vkCreateImageView (FSR 3 mip)");
        }
    return out;
}

void Fsr3Upscaler::destroy_image(Image& image) {
    for (VkImageView v : image.mips)
        if (v) vkDestroyImageView(device_, v, nullptr);
    if (image.view) vkDestroyImageView(device_, image.view, nullptr);
    if (image.image) vmaDestroyImage(allocator_, image.image, image.alloc);
    image = {};
}

void Fsr3Upscaler::destroy_images() {
    for (auto* pair : {&accumulation_, &luma_, &luma_history_, &upscaled_})
        for (Image& i : *pair) destroy_image(i);
    for (Image* i : {&intermediate_, &shading_change_, &new_locks_, &spd_mips_, &farthest_mip1_, &spd_atomic_,
                     &dilated_reactive_, &lanczos_lut_, &default_reactive_, &frame_info_, &dilated_motion_,
                     &dilated_depth_, &recon_prev_depth_})
        destroy_image(*i);
}

void Fsr3Upscaler::resize(VkExtent2D render, VkExtent2D display) {
    destroy_images();
    render_ = render;
    display_ = display;
    const VkExtent2D half{render.width / 2, render.height / 2};
    // The SDK's internal surfaces (fsr3upscalerCreate) and its shared ones (dilated depth and motion, the
    // reconstructed previous depth), at these sizes.
    for (std::uint32_t i = 0; i < 2; ++i) {
        accumulation_[i] = make_image(VK_FORMAT_R8_UNORM, render, 1);
        luma_[i] = make_image(VK_FORMAT_R16_SFLOAT, render, 1);
        luma_history_[i] = make_image(VK_FORMAT_R16G16B16A16_SFLOAT, render, 1);
        upscaled_[i] = make_image(VK_FORMAT_R16G16B16A16_SFLOAT, display, 1);
    }
    intermediate_ = make_image(VK_FORMAT_R16_SFLOAT, render, 1);
    shading_change_ = make_image(VK_FORMAT_R8_UNORM, half, 1);
    new_locks_ = make_image(VK_FORMAT_R8_UNORM, display, 1);
    spd_mips_ = make_image(VK_FORMAT_R16G16_SFLOAT, half, 0);
    farthest_mip1_ = make_image(VK_FORMAT_R16_SFLOAT, half, 1);
    spd_atomic_ = make_image(VK_FORMAT_R32_UINT, {1, 1}, 1);
    dilated_reactive_ = make_image(VK_FORMAT_R8G8B8A8_UNORM, render, 1);
    lanczos_lut_ = make_image(VK_FORMAT_R16_SNORM, {128, 1}, 1);
    default_reactive_ = make_image(VK_FORMAT_R8_UNORM, {1, 1}, 1);
    frame_info_ = make_image(VK_FORMAT_R32G32B32A32_SFLOAT, {1, 1}, 1);
    dilated_motion_ = make_image(VK_FORMAT_R16G16_SFLOAT, render, 1);
    dilated_depth_ = make_image(VK_FORMAT_R32_SFLOAT, render, 1);
    recon_prev_depth_ = make_image(VK_FORMAT_R32_UINT, render, 1);
    fresh_ = true;
    frame_index_ = 0;
    history_frames_ = 0;
    jitter_phase_count_ = 0.0f;
    previous_jitter_ = glm::vec2(0.0f);
}

namespace {

// Everything before (the scene's attachments, last frame's passes and post reads, the clears) before what follows
// (clears and compute passes).
void barrier(VkCommandBuffer cmd) {
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT |
                      VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void clear(VkCommandBuffer cmd, VkImage image, std::uint32_t levels, float a, float b = 0.0f) {
    VkClearColorValue v{};
    v.float32[0] = a;
    v.float32[1] = b;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &range);
}

void clear_uint(VkCommandBuffer cmd, VkImage image) {
    VkClearColorValue v{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &range);
}

std::uint32_t groups(std::int32_t size, std::int32_t per) {
    return static_cast<std::uint32_t>(std::max(1, (size + per - 1) / per));
}

} // namespace

void Fsr3Upscaler::dispatch(VkCommandBuffer cmd, const Inputs& in, const Params& p) {
    const std::uint32_t slot = p.frame_slot % kFrames;
    const bool first = fresh_;
    if (fresh_) {
        // New images: every one into GENERAL for good, the table copied in, the first histories cleared
        // (firstExecution).
        std::vector<VkImageMemoryBarrier2> to_general;
        auto add = [&](const Image& i) {
            VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            b.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT |
                             VK_PIPELINE_STAGE_2_COPY_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = i.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, i.levels, 0, 1};
            to_general.push_back(b);
        };
        for (auto* pair : {&accumulation_, &luma_, &luma_history_, &upscaled_})
            for (const Image& i : *pair) add(i);
        for (const Image* i : {&intermediate_, &shading_change_, &new_locks_, &spd_mips_, &farthest_mip1_, &spd_atomic_,
                               &dilated_reactive_, &lanczos_lut_, &default_reactive_, &frame_info_, &dilated_motion_,
                               &dilated_depth_, &recon_prev_depth_})
            add(*i);
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = static_cast<std::uint32_t>(to_general.size());
        dep.pImageMemoryBarriers = to_general.data();
        vkCmdPipelineBarrier2(cmd, &dep);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {128, 1, 1};
        vkCmdCopyBufferToImage(cmd, lut_staging_, lanczos_lut_.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        clear(cmd, default_reactive_.image, 1, 0.0f);
        for (const auto* pair : {&accumulation_, &luma_})
            for (const Image& i : *pair) clear(cmd, i.image, 1, 0.0f);
        // Not in the SDK, which leaves them as created: the histories read on the first frame hold zeros, not garbage.
        for (const auto* pair : {&luma_history_, &upscaled_})
            for (const Image& i : *pair) clear(cmd, i.image, 1, 0.0f);
        fresh_ = false;
    }

    // The output, written whole: what last frame's post pass read of it is done first.
    {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = in.output;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }
    barrier(cmd); // the inputs' attachments, last frame's passes

    const bool odd = (frame_index_ & 1) != 0;
    const std::uint32_t read = odd ? 1 : 0, write = odd ? 0 : 1; // the pairs: _1 read on even frames
    const bool reset = p.reset || first;

    // The constants (fsr3upscalerDispatch).
    Constants c{};
    const std::int32_t rw = static_cast<std::int32_t>(render_.width), rh = static_cast<std::int32_t>(render_.height);
    const std::int32_t dw = static_cast<std::int32_t>(display_.width), dh = static_cast<std::int32_t>(display_.height);
    c.render_size[0] = c.previous_render_size[0] = c.max_render_size[0] = rw;
    c.render_size[1] = c.previous_render_size[1] = c.max_render_size[1] = rh;
    c.upscale_size[0] = c.previous_upscale_size[0] = c.max_upscale_size[0] = dw;
    c.upscale_size[1] = c.previous_upscale_size[1] = c.max_upscale_size[1] = dh;
    c.jitter[0] = p.jitter.x;
    c.jitter[1] = p.jitter.y;
    c.previous_jitter[0] = previous_jitter_.x;
    c.previous_jitter[1] = previous_jitter_.y;
    previous_jitter_ = p.jitter;
    // The horizontal half angle's tangent, from the vertical one.
    const float aspect = static_cast<float>(rw) / static_cast<float>(std::max(rh, 1));
    const float tan_half_y = std::tan(0.5f * p.fov_y);
    c.tan_half_fov = tan_half_y * aspect;
    c.view_space_to_meters = 1.0f; // the SDK's default; no 3.1.4 shader reads it
    // Device depth to view depth (setupDeviceDepthToViewSpaceDepthParams), reverse Z, finite far plane: the
    // inverted case swaps the planes.
    {
        const float f_min = std::max(p.near_plane, p.far_plane), f_max = std::min(p.near_plane, p.far_plane);
        const float q = f_max / (f_min - f_max);
        c.device_to_view_depth[0] = -q;
        c.device_to_view_depth[1] = q * f_min;
        const float cot = 1.0f / tan_half_y;
        c.device_to_view_depth[2] = aspect / cot;
        c.device_to_view_depth[3] = 1.0f / cot;
    }
    c.downscale_factor[0] = static_cast<float>(rw) / static_cast<float>(dw);
    c.downscale_factor[1] = static_cast<float>(rh) / static_cast<float>(dh);
    c.delta_pre_exposure = 1.0f; // no pre-exposure: the scene is given as drawn
    c.motion_vector_scale[0] = c.motion_vector_scale[1] = 1.0f; // already in UV
    // The jitter's cycle: Hailstone's own length (render/temporal.h jitter_phases, FSR's count rounded up), eased one
    // step a frame toward a new length as the SDK does.
    const float phases = static_cast<float>(std::max(p.jitter_phases, 1u));
    if (reset || jitter_phase_count_ == 0.0f) jitter_phase_count_ = phases;
    else if (phases > jitter_phase_count_) jitter_phase_count_ += 1.0f;
    else if (phases < jitter_phase_count_) jitter_phase_count_ -= 1.0f;
    c.jitter_phase_count = jitter_phase_count_;
    c.delta_time = std::clamp(p.frame_seconds, 0.0f, 1.0f);
    history_frames_ = reset ? 0 : history_frames_ + 1;
    c.frame_index = static_cast<float>(history_frames_);
    c.velocity_factor = 1.0f;
    c.reactiveness_scale = 1.0f;
    c.shading_change_scale = 1.0f;
    c.accumulation_added_per_frame = 1.0f / 3.0f;
    c.min_disocclusion_accumulation = -1.0f / 3.0f;

    // SPD over the render size (ffxSpdSetup, every mip).
    SpdConstants spd{};
    const std::uint32_t tiles_x = (render_.width - 1) / 64 + 1, tiles_y = (render_.height - 1) / 64 + 1;
    spd.work_groups = tiles_x * tiles_y;
    spd.mips = static_cast<std::uint32_t>(std::min(std::floor(std::log2(static_cast<float>(std::max(render_.width, render_.height)))), 12.0f));
    spd.render_size[0] = render_.width;
    spd.render_size[1] = render_.height;
    // RCAS (FsrRcasCon on the SDK's remapped sharpness: 0 the most .. 2 stops less).
    RcasConstants rcas{};
    const float stops = -2.0f * std::clamp(p.sharpness, 0.0f, 1.0f) + 2.0f;
    const float sharp = std::exp2(-stops);
    std::memcpy(&rcas.config[0], &sharp, 4);
    rcas.config[1] = to_half(sharp) | (to_half(sharp) << 16);
    auto* base = static_cast<std::uint8_t*>(ubo_map_[slot]);
    std::memcpy(base, &c, sizeof(c));
    std::memcpy(base + kUboStride, &spd, sizeof(spd));
    std::memcpy(base + 2 * kUboStride, &rcas, sizeof(rcas));
    vmaFlushAllocation(allocator_, ubo_alloc_[slot], 0, VK_WHOLE_SIZE);

    // This frame's descriptors (this slot's sets were last used kFrames frames ago, which are done).
    struct Write { VkDescriptorImageInfo image; VkDescriptorBufferInfo buffer; };
    std::vector<Write> infos;
    std::vector<VkWriteDescriptorSet> writes;
    infos.reserve(128);
    auto image_of = [&](const Binding& b) -> VkDescriptorImageInfo {
        constexpr VkImageLayout G = VK_IMAGE_LAYOUT_GENERAL, R = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        switch (b.res) {
        case Res::InputColor: return {VK_NULL_HANDLE, in.color, R};
        case Res::InputDepth: return {VK_NULL_HANDLE, in.depth, R};
        case Res::InputMotion: return {VK_NULL_HANDLE, in.motion, R};
        case Res::Reactive: return in.reactive ? VkDescriptorImageInfo{VK_NULL_HANDLE, in.reactive, R}
                                               : VkDescriptorImageInfo{VK_NULL_HANDLE, default_reactive_.view, G};
        case Res::TransparencyComposition: return {VK_NULL_HANDLE, default_reactive_.view, G};
        case Res::Exposure: // auto exposure: FSR's own frame info
        case Res::FrameInfo: return {VK_NULL_HANDLE, frame_info_.view, G};
        case Res::DilatedMotion: return {VK_NULL_HANDLE, dilated_motion_.view, G};
        case Res::DilatedDepth: return {VK_NULL_HANDLE, dilated_depth_.view, G};
        case Res::ReconPrevDepth: return {VK_NULL_HANDLE, recon_prev_depth_.view, G};
        case Res::FarthestDepth:
        case Res::LumaInstability: return {VK_NULL_HANDLE, intermediate_.view, G};
        case Res::FarthestDepthMip1: return {VK_NULL_HANDLE, farthest_mip1_.view, G};
        case Res::CurrentLuma: return {VK_NULL_HANDLE, luma_[read].view, G};
        case Res::PreviousLuma: return {VK_NULL_HANDLE, luma_[write].view, G};
        case Res::SpdAtomic: return {VK_NULL_HANDLE, spd_atomic_.view, G};
        case Res::SpdMips: return {VK_NULL_HANDLE, spd_mips_.view, G};
        case Res::SpdMip: return {VK_NULL_HANDLE, spd_mips_.mips[b.mip], G};
        case Res::ShadingChange: return {VK_NULL_HANDLE, shading_change_.view, G};
        case Res::Accumulation: return {VK_NULL_HANDLE, accumulation_[read].view, G};
        case Res::AccumulationOut: return {VK_NULL_HANDLE, accumulation_[write].view, G};
        case Res::DilatedReactive: return {VK_NULL_HANDLE, dilated_reactive_.view, G};
        case Res::NewLocks: return {VK_NULL_HANDLE, new_locks_.view, G};
        case Res::LumaHistory: return {VK_NULL_HANDLE, luma_history_[read].view, G};
        case Res::LumaHistoryOut: return {VK_NULL_HANDLE, luma_history_[write].view, G};
        case Res::Upscaled: return {VK_NULL_HANDLE, upscaled_[read].view, G};
        case Res::UpscaledOut:
        case Res::RcasInput: return {VK_NULL_HANDLE, upscaled_[write].view, G};
        case Res::LanczosLut: return {VK_NULL_HANDLE, lanczos_lut_.view, G};
        case Res::Output: return {VK_NULL_HANDLE, in.output_view, G};
        default: break;
        }
        return {};
    };
    for (std::uint32_t pass = 0; pass < kPasses; ++pass)
        for (const Binding& b : bindings_of(pass)) {
            Write w{};
            if (b.kind == Kind::Uniform) {
                const VkDeviceSize at = b.res == Res::CbMain ? 0 : b.res == Res::CbSpd ? kUboStride : 2 * kUboStride;
                const VkDeviceSize size = b.res == Res::CbMain ? sizeof(Constants)
                                          : b.res == Res::CbSpd ? sizeof(SpdConstants) : sizeof(RcasConstants);
                w.buffer = {ubo_[slot], at, size};
            } else {
                w.image = image_of(b);
            }
            infos.push_back(w);
            VkWriteDescriptorSet d{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            d.dstSet = sets_[slot][pass];
            d.dstBinding = b.binding;
            d.descriptorCount = 1;
            d.descriptorType = descriptor_type(b.kind);
            writes.push_back(d);
        }
    for (std::size_t i = 0; i < writes.size(); ++i) {
        if (writes[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) writes[i].pBufferInfo = &infos[i].buffer;
        else writes[i].pImageInfo = &infos[i].image;
    }
    vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // The clears (fsr3upscalerDispatch): on a reset the accumulation read, the luminance pyramid and the frame info
    // (auto exposure starts over); each frame the reconstructed previous depth (to 0: reverse Z), SPD's counter and
    // its mips.
    if (reset) {
        clear(cmd, accumulation_[read].image, 1, 0.0f);
        clear(cmd, frame_info_.image, 1, -1.0f, 1.0f);
    }
    clear_uint(cmd, recon_prev_depth_.image);
    clear_uint(cmd, spd_atomic_.image);
    clear(cmd, spd_mips_.image, spd_mips_.levels, 0.0f);
    barrier(cmd);

    const bool sharpen = p.sharpness > 0.0f;
    auto run = [&](Pass pass, std::uint32_t x, std::uint32_t y) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines_[pass]);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layouts_[pass], 0, 1, &sets_[slot][pass], 0, nullptr);
        vkCmdDispatch(cmd, x, y, 1);
        barrier(cmd);
    };
    run(PrepareInputs, groups(rw, 8), groups(rh, 8));
    run(LumaPyramid, tiles_x, tiles_y);
    run(ShadingChangePyramid, tiles_x, tiles_y);
    run(ShadingChange, groups(rw / 2, 8), groups(rh / 2, 8));
    run(PrepareReactivity, groups(rw, 8), groups(rh, 8));
    run(LumaInstability, groups(rw, 8), groups(rh, 8));
    run(sharpen ? AccumulateSharpen : Accumulate, groups(dw, 8), groups(dh, 8));
    if (sharpen) run(Rcas, groups(dw, 16), groups(dh, 16));

    // The output, for the post pass's fragment shader.
    {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        b.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = in.output;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }
    frame_index_ = (frame_index_ + 1) % 16; // FSR3UPSCALER_MAX_QUEUED_FRAMES
}

} // namespace hailstone::render
