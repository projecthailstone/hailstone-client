#include "render/scene_recorder.h"

#include "core/worker_pool.h"
#include "render/vk_util.h"

namespace hailstone::render {

SceneRecorder::SceneRecorder(VkContext& vk, bool scene) : vk_(vk), scene_(scene) {}

SceneRecorder::~SceneRecorder() {
    // The pools' buffers may be pending in a frame in flight.
    vk_.wait_idle();
    for (auto& slot : pools_)
        for (auto& p : slot)
            if (p.pool) vkDestroyCommandPool(vk_.device(), p.pool, nullptr);
}

bool SceneRecorder::begin_frame(const FrameInfo& frame, bool enabled) {
    reset();
    targets_.clear();
    target_ = 0;
    slot_ = frame.frame_slot;
    core::WorkerPool* pool = core::workers();
    active_ = enabled && pool && pool->workers() > 0;
    if (active_)
        for (auto& p : pools_[slot_])
            if (p.used > 0) {
                vk_check(vkResetCommandPool(vk_.device(), p.pool, 0), "vkResetCommandPool");
                p.used = 0;
            }
    if (scene_) {
        // The scene's rendering as begin_scene begins it, asked when the jobs are recorded (after begin_scene).
        add_target({[this] { return vk_.scene_inheritance(); }, [this](VkCommandBuffer cmd) { vk_.set_scene_state(cmd); }});
        vk_.set_scene_secondary(active_);
    }
    return active_;
}

std::size_t SceneRecorder::add_target(Target target) {
    targets_.push_back(std::move(target));
    target_ = targets_.size() - 1;
    return target_;
}

void SceneRecorder::add(const FrameInfo& frame, const void* lane, Job job) {
    if (!active_) {
        job(frame);
        return;
    }
    lanes_.add(lane, target_);
    jobs_.push_back({std::move(job), VK_NULL_HANDLE});
}

VkCommandBuffer SceneRecorder::take(Pool& pool) {
    if (pool.used == pool.buffers.size()) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vk_check(vkAllocateCommandBuffers(vk_.device(), &ai, &cmd), "vkAllocateCommandBuffers (scene)");
        pool.buffers.push_back(cmd);
    }
    return pool.buffers[pool.used++];
}

void SceneRecorder::record(const FrameInfo& frame) {
    if (!active_ || jobs_.empty()) return;
    auto& pools = pools_[slot_];
    while (pools.size() < lanes_.lanes()) {
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        ci.queueFamilyIndex = vk_.queue_family();
        Pool p;
        vk_check(vkCreateCommandPool(vk_.device(), &ci, nullptr, &p.pool), "vkCreateCommandPool (scene)");
        pools.push_back(std::move(p));
    }
    // The buffers are taken here, on this thread: a pool is touched by one thread at a time.
    for (std::size_t j = 0; j < jobs_.size(); ++j) jobs_[j].cmd = take(pools[lanes_.lane_of(j)]);
    // Each target's inheritance, asked once here (both lists are sized before any points into the other).
    renderings_.clear();
    for (const auto& t : targets_) renderings_.push_back(t.rendering());
    inheritances_.assign(targets_.size(), {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO});
    for (std::size_t i = 0; i < targets_.size(); ++i) inheritances_[i].pNext = &renderings_[i];

    try {
        lanes_.record([&](std::size_t j) {
            Queued& q = jobs_[j];
            const std::size_t target = lanes_.target_of(j);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            bi.pInheritanceInfo = &inheritances_[target];
            vk_check(vkBeginCommandBuffer(q.cmd, &bi), "vkBeginCommandBuffer (scene)");
            targets_[target].state(q.cmd);
            FrameInfo own = frame;
            own.cmd = q.cmd;
            q.job(own);
            vk_check(vkEndCommandBuffer(q.cmd), "vkEndCommandBuffer (scene)");
        });
    } catch (...) {
        reset(); // the buffers left recording are reset with their pool, kFramesInFlight frames on
        throw;
    }
}

void SceneRecorder::execute(const FrameInfo& frame, std::size_t target) {
    execute_.clear();
    lanes_.each_in(target, [&](std::size_t j) {
        if (jobs_[j].cmd != VK_NULL_HANDLE) execute_.push_back(jobs_[j].cmd);
    });
    if (!execute_.empty())
        vkCmdExecuteCommands(frame.cmd, static_cast<std::uint32_t>(execute_.size()), execute_.data());
}

void SceneRecorder::flush(const FrameInfo& frame) {
    if (!active_ || jobs_.empty()) return;
    record(frame);
    execute_.clear();
    for (const auto& q : jobs_) execute_.push_back(q.cmd);
    vkCmdExecuteCommands(frame.cmd, static_cast<std::uint32_t>(execute_.size()), execute_.data());
    reset();
}

void SceneRecorder::reset() {
    jobs_.clear();
    lanes_.clear();
}

} // namespace hailstone::render
