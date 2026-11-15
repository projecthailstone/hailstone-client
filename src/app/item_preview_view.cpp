#include "app/item_preview_view.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <imgui_impl_vulkan.h>

#include "core/log.h"
#include "game/view_distance.h"
#include "render/actor_renderer.h"
#include "render/vk_util.h"
#include "ui/imgui_vk_textures.h"

namespace hailstone::app {

namespace {

constexpr VkFormat kOutFormat = VK_FORMAT_R8G8B8A8_SRGB;      // the blit encodes the scene's linear colour
constexpr VkFormat kOutViewFormat = VK_FORMAT_R8G8B8A8_UNORM; // ImGui reads the encoded bytes as they are

VkImage make_image(VmaAllocator allocator, VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples,
                   int width, int height, VmaAllocation& alloc, VkImageCreateFlags flags = 0) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.flags = flags;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = samples;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    VkImage image = VK_NULL_HANDLE;
    render::vk_check(vmaCreateImage(allocator, &ii, &ai, &image, &alloc, nullptr), "item preview target");
    return image;
}

} // namespace

ItemPreviewView::ItemPreviewView(render::VkContext& vk, std::shared_ptr<const assets::CharacterLibrary> library,
                                 const std::filesystem::path& shader_dir)
    : vk_(vk), library_(std::move(library)) {
    // Its own targets, the colour alone (not the scene's motion and reactive mask).
    actors_ = std::make_unique<render::ActorRenderer>(vk_, library_, shader_dir, nullptr, false);
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    render::vk_check(vkCreateSampler(vk_.device(), &si, nullptr, &sampler_), "item preview sampler");
}

ItemPreviewView::~ItemPreviewView() {
    vk_.wait_idle();
    for (auto& t : targets_) {
        if (t.set) ui::remove_imgui_texture(t.set);
        destroy_images(t);
    }
    if (sampler_) vkDestroySampler(vk_.device(), sampler_, nullptr);
}

void ItemPreviewView::destroy_images(Target& t) {
    VkDevice d = vk_.device();
    VmaAllocator a = vk_.allocator();
    auto drop = [&](VkImage& image, VmaAllocation& alloc, VkImageView& view) {
        if (view) vkDestroyImageView(d, view, nullptr);
        if (image) vmaDestroyImage(a, image, alloc);
        image = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        view = VK_NULL_HANDLE;
    };
    drop(t.msaa, t.msaa_alloc, t.msaa_view);
    drop(t.resolve, t.resolve_alloc, t.resolve_view);
    drop(t.depth, t.depth_alloc, t.depth_view);
    drop(t.out, t.out_alloc, t.out_view);
    t.width = t.height = 0;
    t.drawn = false;
}

void ItemPreviewView::make_target(Target& t, int width, int height) {
    destroy_images(t);
    VmaAllocator a = vk_.allocator();
    VkDevice d = vk_.device();
    const VkSampleCountFlagBits samples = vk_.scene_samples();
    if (samples != VK_SAMPLE_COUNT_1_BIT) {
        t.msaa = make_image(a, vk_.scene_format(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, samples, width, height, t.msaa_alloc);
        t.msaa_view = render::create_image_view(d, t.msaa, vk_.scene_format(), VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
    t.resolve = make_image(a, vk_.scene_format(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           VK_SAMPLE_COUNT_1_BIT, width, height, t.resolve_alloc);
    t.resolve_view = render::create_image_view(d, t.resolve, vk_.scene_format(), VK_IMAGE_ASPECT_COLOR_BIT, 1);
    t.depth = make_image(a, vk_.depth_format(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, samples, width, height,
                         t.depth_alloc);
    t.depth_view = render::create_image_view(d, t.depth, vk_.depth_format(), VK_IMAGE_ASPECT_DEPTH_BIT, 1);
    t.out = make_image(a, kOutFormat, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT,
                       width, height, t.out_alloc, VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT);
    t.out_view = render::create_image_view(d, t.out, kOutViewFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    t.width = width;
    t.height = height;
    if (!t.set) {
        t.set = ui::add_imgui_texture(sampler_, t.out_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else {
        VkDescriptorImageInfo info{sampler_, t.out_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = t.set;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &info;
        vkUpdateDescriptorSets(d, 1, &w, 0, nullptr);
    }
}

void ItemPreviewView::reset_camera() {
    // 0x576120: the view fitted to the actor again.
    yaw_ = 0.0f;
    pitch_ = glm::radians(10.0f);
    zoom_ = 1.0f;
    pan_ = glm::vec2(0.0f);
    centre_ = glm::vec3(0.0f);
    radius_ = 1.0f;
    if (!model_) return;
    const glm::vec3 lo = model_->bounds_min * scale_, hi = model_->bounds_max * scale_;
    centre_ = (lo + hi) * 0.5f + glm::vec3(0.0f, 0.0f, z_offset_);
    radius_ = std::max(glm::length(hi - lo) * 0.5f, 0.5f);
}

void ItemPreviewView::set_subject(std::shared_ptr<const assets::CharacterModel> model,
                                  std::shared_ptr<const assets::CharacterMesh> mesh, float scale, float z_offset) {
    model_ = std::move(model);
    mesh_ = std::move(mesh);
    scale_ = scale;
    z_offset_ = z_offset;
    time_ = 0.0f;
    reset_camera();
}

bool ItemPreviewView::set_item_subject(std::uint32_t number) {
    auto held = number ? library_->item_mesh(number) : std::nullopt;
    if (!held || held->mesh.vertices.empty()) {
        clear_subject();
        return false;
    }
    // A one-bone stand-in for the item (0x577440 shows the item's actor alone): its vertices on bone 0, at rest.
    auto model = std::make_shared<assets::CharacterModel>();
    model->code = "ITM";
    model->bone_names = {"ITM_DAG"};
    model->parents = {-1};
    model->order = {0};
    model->eqg = held->eqg;
    assets::AnimationClip rest;
    rest.tracks = {{assets::BonePose{}}};
    model->clips[""] = rest;
    auto mesh = std::make_shared<assets::CharacterMesh>(std::move(held->mesh));
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (auto& v : mesh->vertices) {
        v.bones = {0, 0, 0, 0};
        v.weights = {1.0f, 0.0f, 0.0f, 0.0f};
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    model->bounds_min = lo;
    model->bounds_max = hi;
    model->bounding_radius = glm::length(hi - lo) * 0.5f;
    if (mesh->tints.size() < mesh->materials.size()) mesh->tints.resize(mesh->materials.size(), 0);
    set_subject(std::move(model), std::move(mesh), 1.0f, 0.0f);
    return true;
}

void ItemPreviewView::clear_subject() {
    model_.reset();
    mesh_.reset();
}

void* ItemPreviewView::image(int width, int height) {
    if (!model_ || width < 8 || height < 8) return nullptr;
    want_width_ = std::min(width, 2048);
    want_height_ = std::min(height, 2048);
    want_slot_ = vk_.next_frame_slot();
    Target& t = targets_[want_slot_];
    // The descriptor set must exist now; it is drawn later. When add_imgui_texture found none (out of memory for a
    // pool), nothing is drawn (null) and the set is asked for again on the next call, not the images made again.
    if (!t.set) {
        if (!t.out) make_target(t, want_width_, want_height_);
        else t.set = ui::add_imgui_texture(sampler_, t.out_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    return t.set;
}

void ItemPreviewView::control(std::string_view what, float x, float y) {
    if (what == "reset") {
        reset_camera();
    } else if (what == "rotate") {
        yaw_ += x;
        pitch_ = std::clamp(pitch_ + y, glm::radians(-85.0f), glm::radians(85.0f));
    } else if (what == "pan") {
        pan_ += glm::vec2(x, y);
    } else if (what == "zoom") {
        zoom_ = std::clamp(zoom_ * x, 0.2f, 5.0f);
    }
}

void ItemPreviewView::record(const render::FrameInfo& frame, float dt) {
    const int width = want_width_, height = want_height_;
    want_width_ = want_height_ = 0;
    if (width == 0 || !model_ || frame.frame_slot != want_slot_) return;
    Target& t = targets_[frame.frame_slot];
    if (t.width != width || t.height != height || !t.out) make_target(t, width, height); // this slot's frame is done
    time_ += dt;
    VkCommandBuffer cmd = frame.cmd;
    using render::image_barrier;
    const bool msaa = t.msaa != VK_NULL_HANDLE;
    if (msaa)
        image_barrier(cmd, t.msaa, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(cmd, t.resolve, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(cmd, t.depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    const VkExtent2D extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // the window shows through
    if (msaa) {
        color.imageView = t.msaa_view;
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView = t.resolve_view;
        color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    } else {
        color.imageView = t.resolve_view;
    }
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = t.depth_view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {0.0f, 0}; // reverse Z
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &rendering);
    VkViewport vp{0.0f, static_cast<float>(height), static_cast<float>(width), -static_cast<float>(height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // The orbit camera: the model faces +X (yaw 0), seen from in front.
    const float distance = radius_ * 2.4f * zoom_;
    const glm::vec3 dir(std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_));
    const glm::vec3 right = glm::normalize(glm::cross(-dir, glm::vec3(0, 0, 1)));
    const glm::vec3 up = glm::cross(right, -dir);
    const glm::vec3 target = centre_ + (right * pan_.x + up * pan_.y) * radius_ * 2.0f;
    const glm::vec3 eye = target + dir * distance;
    const glm::mat4 view = glm::lookAtRH(eye, target, glm::vec3(0, 0, 1));
    const float n = std::max(distance - radius_ * 4.0f, 0.05f), f = distance + radius_ * 4.0f;
    const float tan_half = std::tan(glm::radians(25.0f));
    glm::mat4 proj(0.0f); // reverse Z, as render::FlyCamera::projection
    proj[0][0] = 1.0f / (tan_half * static_cast<float>(width) / static_cast<float>(height));
    proj[1][1] = 1.0f / tan_half;
    proj[2][2] = n / (f - n);
    proj[2][3] = -1.0f;
    proj[3][2] = f * n / (f - n);

    game::ViewDistance no_fog;
    no_fog.fog_enabled = false;
    no_fog.fog_start = 1e6f;
    no_fog.fog_end = 2e6f;
    no_fog.far_plane = f;
    render::ActorDraw a;
    a.model = model_.get();
    a.mesh = mesh_.get();
    a.position = glm::vec3(0.0f, 0.0f, z_offset_);
    a.scale = scale_;
    a.clip = &model_->first_clip({"P01", "O01"}); // standing (the preview actor's idle)
    a.time = time_;
    actors_->set_lighting(render::classic_full_bright());
    actors_->draw(frame, proj * view, eye, no_fog, std::span<const render::ActorDraw>(&a, 1));
    vkCmdEndRendering(cmd);

    image_barrier(cmd, t.resolve, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    image_barrier(cmd, t.out, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {width, height, 1};
    blit.dstOffsets[1] = {width, height, 1};
    vkCmdBlitImage(cmd, t.resolve, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.out, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, VK_FILTER_NEAREST);
    image_barrier(cmd, t.out, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    t.drawn = true;
}

} // namespace hailstone::app
