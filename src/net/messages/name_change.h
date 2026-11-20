#pragma once
#include <cstdint>
#include <string>
namespace hailstone::net::messages {
enum class NameSubject : std::uint8_t {Character, Pet, Mercenary};
struct NameChangeRequest {NameSubject subject{}; std::string requested_name, character_name;};
struct NameChangeNotice {
    NameSubject subject{};
    bool prompt = false, immediate = false, has_result = false;
    std::int32_t result = 0;
};
} // namespace hailstone::net::messages
