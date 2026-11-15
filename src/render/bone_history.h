#pragma once

// The bones each spawn was drawn with a frame ago, for the temporal upscaler's motion (ActorRenderer::prepare, actor.vert):
// kept on the CPU, apart from Vulkan so the rule is tested. The bones buffer is write-only memory to the CPU, so the
// last frame's are never read back from it.
//
// Each spawn's entry keeps its vectors from frame to frame (this frame's and the last are swapped, not freed), so once
// the scene settles a frame allocates nothing. An entry not drawn in the last frame is dropped when a new one begins.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace hailstone::render {

class BoneHistory {
public:
    // A new frame (VkContext::frames_begun, 1 the first) makes this frame's bones the last frame's. The same frame
    // again changes nothing.
    void begin_frame(std::uint64_t frame) {
        if (frame == frame_) return;
        previous_ = frame_;
        frame_ = frame;
        std::erase_if(entries_, [&](const auto& e) { return e.second.now_frame < previous_; });
    }

    // Keeps `count` bones from `bones` as `key`'s this frame, and returns the ones it had the last frame, or null
    // when it had none or a different number (new, or changed shape). The pointer holds until the next keep of `key`.
    const std::vector<glm::mat4>* keep(std::uint64_t key, const glm::mat4* bones, std::size_t count) {
        Entry& e = entries_[key];
        if (e.now_frame != frame_) {
            std::swap(e.now, e.before);
            e.before_frame = e.now_frame;
            e.now_frame = frame_;
        }
        e.now.assign(bones, bones + count);
        if (previous_ == 0 || e.before_frame != previous_ || e.before.size() != count) return nullptr;
        return &e.before;
    }

    void clear() {
        entries_.clear();
        frame_ = previous_ = 0;
    }
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::vector<glm::mat4> now, before;
        std::uint64_t now_frame = 0, before_frame = 0;
    };
    std::unordered_map<std::uint64_t, Entry> entries_;
    std::uint64_t frame_ = 0, previous_ = 0;
};

} // namespace hailstone::render
