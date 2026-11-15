#pragma once

// AMD FidelityFX FSR 3.1's upscaler (docs/graphics.md "AMD FSR 3.1"): the SDK's GLSL passes (third_party/fidelityfx,
// v1.1.4, MIT) run by a host side of Hailstone's own, ported from the SDK's ffx_fsr3upscaler.cpp: the internal
// images, the per-frame constants, the dispatch order and the clears. Not the SDK's runtime or Vulkan backend.
//
// One permutation: HDR colour, reverse depth, motion vectors at the render size without jitter, auto exposure
// (FSR's own, in its frame info), 32-bit maths, the reference Lanczos; sharpening (RCAS) optional per frame.

#include <array>
#include <cstdint>
#include <filesystem>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

namespace hailstone::render {

class Fsr3Upscaler {
public:
    // What the device must offer (DeviceCaps::fsr3): storage images in FSR's formats (R8, R16F, RG16F ...: the
    // shaderStorageImageExtendedFormats feature, enabled by VkContext when present) and quad subgroup operations in
    // compute shaders (the luminance pyramid's SPD).
    static bool supported(VkPhysicalDevice physical);

    static constexpr std::uint32_t kFrames = 2; // descriptor sets and constants per frame in flight

    Fsr3Upscaler(VkDevice device, VmaAllocator allocator, VkPipelineCache cache, const std::filesystem::path& shader_dir);
    ~Fsr3Upscaler();
    Fsr3Upscaler(const Fsr3Upscaler&) = delete;
    Fsr3Upscaler& operator=(const Fsr3Upscaler&) = delete;

    // Makes the internal images for a `render` (input) and `display` (output) size; the device must be idle (no
    // frame in flight uses the old ones). The next dispatch starts a new history.
    void resize(VkExtent2D render, VkExtent2D display);

    struct Inputs {
        // At the render size, in SHADER_READ_ONLY_OPTIMAL, last written as colour or depth attachments.
        VkImageView color = VK_NULL_HANDLE;  // linear HDR, before the exposure and the tonemap
        VkImageView depth = VK_NULL_HANDLE;  // reverse Z, the depth aspect
        VkImageView motion = VK_NULL_HANDLE; // RG16F: UV to last frame's place, without the jitter
        // R8: 0..1 how much this frame's colour should replace the history (particles, spells ...); none: FSR's
        // default (0 everywhere).
        VkImageView reactive = VK_NULL_HANDLE;
        // At the display size, RGBA16F, STORAGE and SAMPLED: written whole; left in SHADER_READ_ONLY_OPTIMAL for
        // fragment shaders.
        VkImage output = VK_NULL_HANDLE;
        VkImageView output_view = VK_NULL_HANDLE;
    };
    struct Params {
        std::uint32_t frame_slot = 0;  // 0..kFrames-1
        glm::vec2 jitter{0.0f};        // in render pixels, x right, y down (render/temporal.h jitter_offset)
        std::uint32_t jitter_phases = 8;
        float near_plane = 1.0f;       // the camera's (reverse Z, finite far plane)
        float far_plane = 1000.0f;
        float fov_y = 1.0f;            // vertical field of view, radians
        float frame_seconds = 1.0f / 60.0f;
        bool reset = false;            // drop the history (a cut)
        float sharpness = 0.0f;        // RCAS: 0 none .. 1 the most
    };
    // Records the passes into `cmd` (outside rendering).
    void dispatch(VkCommandBuffer cmd, const Inputs& in, const Params& p);

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;          // every mip, for sampling
        std::array<VkImageView, 6> mips{};          // storage views of mips 0..5 (the last one past the chain)
        VkFormat format = VK_FORMAT_UNDEFINED;
        std::uint32_t levels = 1;
    };
    Image make_image(VkFormat format, VkExtent2D extent, std::uint32_t levels);
    void destroy_image(Image& image);
    void destroy_images();

    VkDevice device_;
    VmaAllocator allocator_;
    VkSampler point_ = VK_NULL_HANDLE, linear_ = VK_NULL_HANDLE;

    // The passes, in dispatch order (Pass), each with its set layout, pipeline and a set per frame in flight.
    enum Pass : std::uint32_t {
        PrepareInputs, LumaPyramid, ShadingChangePyramid, ShadingChange, PrepareReactivity, LumaInstability,
        Accumulate, AccumulateSharpen, Rcas, kPasses
    };
    std::array<VkDescriptorSetLayout, kPasses> set_layouts_{};
    std::array<VkPipelineLayout, kPasses> layouts_{};
    std::array<VkPipeline, kPasses> pipelines_{};
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::array<std::array<VkDescriptorSet, kPasses>, kFrames> sets_{};
    // The constant buffers per frame in flight: FSR's main block, SPD's and RCAS's, at kUboStride apart.
    static constexpr VkDeviceSize kUboStride = 256;
    std::array<VkBuffer, kFrames> ubo_{};
    std::array<VmaAllocation, kFrames> ubo_alloc_{};
    std::array<void*, kFrames> ubo_map_{};
    // The Lanczos table's upload (128 x R16_SNORM), copied in at the first dispatch.
    VkBuffer lut_staging_ = VK_NULL_HANDLE;
    VmaAllocation lut_staging_alloc_ = VK_NULL_HANDLE;

    VkExtent2D render_{}, display_{};
    // The internal images (the SDK's FFX_FSR3UPSCALER_RESOURCE_IDENTIFIER_*); the pairs alternate by frame.
    std::array<Image, 2> accumulation_, luma_, luma_history_, upscaled_;
    Image intermediate_;      // R16F: the farthest depth, then the luma instability
    Image shading_change_, new_locks_, spd_mips_, farthest_mip1_, spd_atomic_, dilated_reactive_, lanczos_lut_,
        default_reactive_, frame_info_, dilated_motion_, dilated_depth_, recon_prev_depth_;
    bool fresh_ = true;       // the images were just made: the first dispatch sets them up
    std::uint32_t frame_index_ = 0; // the SDK's resourceFrameIndex: the pairs' turn
    std::uint32_t history_frames_ = 0; // frames since the history started (FSR's frameIndex)
    glm::vec2 previous_jitter_{0.0f};
    float jitter_phase_count_ = 0.0f;
};

} // namespace hailstone::render
