#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <variant> // std::monostate is the unique empty semantic payload

namespace hailstone::net::messages {

// A value-semantic, checked, type-erased payload with inline storage for small,
// nothrow-movable messages. A heavyweight message type is instantiated only
// by the translation units that construct or access that particular type.
// There is no per-message shared mutable state and no raw packet representation.
class Payload {
public:
    Payload() noexcept = default;
    Payload(std::monostate) noexcept {}
    Payload(const Payload&);
    Payload(Payload&&) noexcept;
    Payload& operator=(const Payload&);
    Payload& operator=(Payload&&) noexcept;
    ~Payload();

    template<class T>
        requires (!std::is_same_v<std::remove_cvref_t<T>, Payload> &&
                  !std::is_same_v<std::remove_cvref_t<T>, std::monostate>)
    Payload(T&& value) { emplace<std::remove_cvref_t<T>>(std::forward<T>(value)); }

    template<class T, class... Args>
    void emplace(Args&&... args) {
        using V = std::remove_cvref_t<T>;
        static_assert(!std::is_reference_v<T> && !std::is_same_v<V, Payload>);
        reset();
        if constexpr (!std::is_same_v<V, std::monostate>) {
            const auto& operations = ops<V>();
            if constexpr (fits_inline<V>) {
                ::new (static_cast<void*>(&small_)) V(std::forward<Args>(args)...);
                object_ = &small_;
            } else {
                object_ = new V(std::forward<Args>(args)...);
            }
            ops_ = &operations;
        }
    }

    template<class T> const T* get_if() const noexcept {
        using V = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<V, std::monostate>) {
            return ops_ == nullptr ? &empty_ : nullptr;
        } else {
            return ops_ && ops_->type_token == token<V>() ?
                static_cast<const V*>(object_) : nullptr;
        }
    }
    template<class T> T* get_if() noexcept {
        return const_cast<T*>(std::as_const(*this).get_if<T>());
    }
    bool empty() const noexcept { return ops_ == nullptr; }
    void reset() noexcept;
    void swap(Payload&) noexcept;

private:
    static constexpr std::size_t inline_bytes = 64;
    using InlineStorage = std::aligned_storage_t<inline_bytes, alignof(std::max_align_t)>;
    template<class T> static constexpr bool fits_inline =
        sizeof(T) <= inline_bytes && alignof(T) <= alignof(InlineStorage) &&
        std::is_nothrow_move_constructible_v<T>;

    struct Operations {
        const void* type_token;
        bool inline_storage;
        void (*copy_inline)(void*, const void*);
        void* (*copy_heap)(const void*);
        void (*move_inline)(void*, void*) noexcept;
        void (*destroy)(void*) noexcept;
        void (*destroy_heap)(void*) noexcept;
    };

    template<class T> static const void* token() noexcept {
        static const char unique_token{};
        return &unique_token;
    }
    template<class T> static const Operations& ops() {
        static const Operations instance{
            token<T>(), fits_inline<T>,
            [](void* dest, const void* src) {
                ::new (dest) T(*static_cast<const T*>(src));
            },
            [](const void* src) -> void* {
                return new T(*static_cast<const T*>(src));
            },
            [](void* dest, void* src) noexcept {
                if constexpr (std::is_nothrow_move_constructible_v<T>) {
                    ::new (dest) T(std::move(*static_cast<T*>(src)));
                }
            },
            [](void* src) noexcept { static_cast<T*>(src)->~T(); },
            [](void* src) noexcept { delete static_cast<T*>(src); }
        };
        return instance;
    }

    InlineStorage small_{};
    void* object_ = nullptr;
    const Operations* ops_ = nullptr;
    inline static std::monostate empty_{};
};

} // namespace hailstone::net::messages
