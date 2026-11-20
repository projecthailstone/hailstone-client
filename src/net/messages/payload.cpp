#include "net/messages/payload.h"

#include <utility>

namespace hailstone::net::messages {

Payload::~Payload() { reset(); }

void Payload::reset() noexcept {
    if (!ops_) return;
    if (ops_->inline_storage) ops_->destroy(object_);
    else ops_->destroy_heap(object_);
    ops_ = nullptr;
    object_ = nullptr;
}

Payload::Payload(const Payload& other) {
    if (!other.ops_) return;
    if (other.ops_->inline_storage) {
        other.ops_->copy_inline(&small_, other.object_);
        object_ = &small_;
    } else {
        object_ = other.ops_->copy_heap(other.object_);
    }
    ops_ = other.ops_;
}

Payload::Payload(Payload&& other) noexcept {
    if (!other.ops_) return;
    const Operations* operations = other.ops_;
    if (operations->inline_storage) {
        operations->move_inline(&small_, other.object_);
        object_ = &small_;
        other.reset();
    } else {
        object_ = std::exchange(other.object_, nullptr);
        other.ops_ = nullptr;
    }
    ops_ = operations;
}

Payload& Payload::operator=(const Payload& other) {
    if (this == &other) return *this;
    Payload temp(other);
    *this = std::move(temp);
    return *this;
}

Payload& Payload::operator=(Payload&& other) noexcept {
    if (this == &other) return *this;
    reset();
    if (!other.ops_) return *this;
    const Operations* operations = other.ops_;
    if (operations->inline_storage) {
        operations->move_inline(&small_, other.object_);
        object_ = &small_;
        other.reset();
    } else {
        object_ = std::exchange(other.object_, nullptr);
        other.ops_ = nullptr;
    }
    ops_ = operations;
    return *this;
}

void Payload::swap(Payload& other) noexcept {
    if (this == &other) return;
    Payload temp(std::move(*this));
    *this = std::move(other);
    other = std::move(temp);
}

} // namespace hailstone::net::messages
