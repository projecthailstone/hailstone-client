#pragma once

// Draws recorded on the worker threads (docs/graphics.md "Worker threads"): each draw is a job in a lane, recorded
// into a secondary command buffer of its own; the lanes are recorded at once (core::parallel_for), a lane's jobs in
// the order they came on one thread, and the buffers are executed into the frame's command buffer in the order the
// jobs came, so what is drawn, and in what order, is as it was inline.
//
// A lane is whatever the caller names by an address: one renderer, or several whose draws read or write the same state
// (a renderer whose second draw uses its first's result, a draw that asks another renderer's mutable caches). Jobs of
// different lanes must touch nothing in common but read-only state; the caller changes nothing they read between
// add and flush.
//
// Each job draws into a target: the rendering its buffer continues (the secondary buffer's inheritance) and the
// dynamic state the buffer starts with. The scene's recorder (made with `scene`) has the scene's rendering as its only
// target and says at begin_frame whether the scene takes secondary buffers (VkContext::set_scene_secondary). Another
// recorder (the shadow layers') names its targets with add_target, records them all at once with record, and executes
// each inside its own rendering with execute.
//
// Inactive (begin_frame's `enabled` false, or no worker pool), add records the job at once into the frame's own
// command buffer and flush does nothing: the draws as they were before. A recorder that is not the scene's is only
// used while active; inactive, its caller draws inline itself.

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "render/scene_lanes.h"
#include "render/vk_context.h"

namespace hailstone::render {

class SceneRecorder {
public:
    explicit SceneRecorder(VkContext& vk, bool scene = true);
    ~SceneRecorder();
    SceneRecorder(const SceneRecorder&) = delete;
    SceneRecorder& operator=(const SceneRecorder&) = delete;

    // Starts a frame (after VkContext::begin_frame_commands; the scene's before begin_scene): whether this frame's
    // jobs are recorded into secondary command buffers (the scene's recorder tells the VkContext so). The frame slot's
    // buffers from kFramesInFlight frames ago are done with (its fence was waited for) and are reset. Each recorder
    // has buffers of its own, so two recorders may both be used in one frame.
    bool begin_frame(const FrameInfo& frame, bool enabled);
    bool active() const { return active_; }

    // A rendering the jobs draw into: its attachments for the secondary buffers' inheritance (asked when they are
    // recorded), and the dynamic state each of its buffers starts with.
    struct Target {
        std::function<VkCommandBufferInheritanceRenderingInfo()> rendering;
        std::function<void(VkCommandBuffer)> state;
    };
    // Jobs added from now on draw into `target`; returns its index for execute. begin_frame forgets the targets (the
    // scene's recorder then has the scene's as its first).
    std::size_t add_target(Target target);

    using Job = std::function<void(const FrameInfo& frame)>;
    // Queues `job` in `lane`, to be recorded with a FrameInfo whose cmd is a secondary command buffer inside the
    // current target's rendering; inactive, runs it now on `frame`.
    void add(const FrameInfo& frame, const void* lane, Job job);
    // Records the queued jobs and executes them into frame.cmd, in the order they came. Call before the scene's
    // rendering ends (VkContext::begin_translucent, begin_overlay) and before anything the jobs read changes.
    void flush(const FrameInfo& frame);

    // For several renderings: record() records every queued job (on the workers), execute() puts the buffers of one
    // target into frame.cmd in the order their jobs came (inside that target's rendering, begun with secondary
    // contents), and reset() drops the queue once every target is executed.
    void record(const FrameInfo& frame);
    void execute(const FrameInfo& frame, std::size_t target);
    void reset();

private:
    struct Pool {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> buffers;
        std::size_t used = 0;
    };
    struct Queued {
        Job job;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
    };
    VkCommandBuffer take(Pool& pool);

    VkContext& vk_;
    bool scene_ = true;
    // One pool per frame slot and lane (in the order lanes first came this frame): a pool's buffers are recorded only
    // by the thread recording its lane.
    std::array<std::vector<Pool>, VkContext::kFramesInFlight> pools_;
    std::vector<Target> targets_;
    std::size_t target_ = 0; // add's
    std::vector<Queued> jobs_;
    SceneLanes lanes_; // each job's lane and target
    // record's inheritance for each target, asked once a frame; the jobs point at these while they record.
    std::vector<VkCommandBufferInheritanceRenderingInfo> renderings_;
    std::vector<VkCommandBufferInheritanceInfo> inheritances_;
    std::vector<VkCommandBuffer> execute_; // execute's buffers, in the jobs' order
    std::uint32_t slot_ = 0;
    bool active_ = false;
};

} // namespace hailstone::render
