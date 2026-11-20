#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hailstone::net {
struct StunNotice { std::uint32_t duration_ms = 0; bool lean = false; };
struct TargetabilityNotice { std::uint32_t spawn_id = 0; bool targetable = true; };
struct AssistanceNotice { std::uint32_t spawn_id = 0; };
struct ExternalLinkNotice { std::string url; };
struct LearnedDisciplines { std::vector<std::uint32_t> spell_ids; };
struct ItemReuseNotice { std::uint32_t delay_s = 0, reuse_type = 0; bool ignore_cast_requirement = false; };
} // namespace hailstone::net
