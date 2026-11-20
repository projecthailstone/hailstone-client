#pragma once

// Training skills and languages at a guildmaster: the training window's rows
// (CTrainWnd::SetGMData @0x7824a0), the cost (0x584a30), the checks
// before a train request (PcZoneClient_CanTrainSkillOrLanguage @0x5855a0)
// and which languages a
// character may learn (PcClient::checkLang @0x581800). Pure rules; the
// packets are handled in net/connection/game_connection_progress.cpp.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hailstone::game {

inline constexpr int kTrainSkills = 100;    // PACKET_SKILL_ARRAY_SIZE
inline constexpr int kTrainLanguages = 32;

// One row of the training window: kind < 100 is a skill, 100 + n language n.
// cap is the guildmaster's limit (the skill array's value; 100 for languages).
struct TrainingRow {
    std::int32_t kind = 0;
    std::int32_t cap = 0;
    bool language() const { return kind >= 100; }
    std::int32_t id() const { return language() ? kind - 100 : kind; }
};

// The open training window (Handle_OP_GMTraining @0x4b49c0 → SetGMData).
struct TrainingSession {
    std::uint32_t trainer_id = 0;
    std::string trainer_name; // the guildmaster spawn's name
    std::array<std::uint32_t, kTrainSkills> skill_caps{};
    std::array<std::uint8_t, kTrainLanguages> language_caps{};
    float greed = 0.0f;
    std::vector<TrainingRow> rows;
};

// What the rules need to know about the player.
struct TraineeView {
    std::uint32_t race = 0;
    std::uint8_t class_id = 0;
    std::int32_t level = 0;
    std::int32_t practice_points = 0;                    // profile +0x33dc
    std::function<std::int32_t(std::uint32_t)> skill;    // the trained value (profile +0x30a0)
    std::function<std::int32_t(std::uint32_t)> language; // CharacterBase +0x14 bytes
    // CSkillMgr::GetSkillCap(no character, level 100, class, skill).
    std::function<std::int32_t(std::uint32_t)> class_cap;
    // 0x5baa20: the level the class first gets the skill (0 never).
    std::function<std::int32_t(std::uint32_t)> min_level;
    // PcClient::AtSkillLimit @0x584e50 (the value >= pc_skill_limit); unset: not checked.
    std::function<bool(std::uint32_t)> at_skill_limit;
};

// What PcZoneClient::GetPcSkillLimit @0x584be0 asks of the character (recorded vector GetPcSkillLimit).
struct SkillLimitView {
    std::int32_t level = 0;   // the virtual base's virtual +0x68 (passed to GetSkillCap as the level)
    std::uint8_t class_id = 0; // profile +0x3374
    std::function<std::int32_t(std::uint32_t)> skill; // profile +0x30a0: the trained value
    // CSkillMgr::GetSkillCap(this character, level, class, skill, 1, 1, 1).
    std::function<std::int32_t(std::int32_t level, std::int32_t cls, std::uint32_t skill)> skill_cap;
    std::function<bool(std::uint32_t)> level_capped; // CSkillMgr_IsLevelCappedSkill
    std::function<std::int32_t()> highest_level;     // PcBase_GetHighestLevel (a byte)
    std::function<std::int32_t(std::int32_t spa)> total_effect; // TotalEffect(spa, 1, 0, 1, 1)
};

// PcZoneClient::GetPcSkillLimit @0x584be0: the skill cap; for a tradeskill (60, 61, 63, 64, 65, 68, 69) that is not
// level capped, 200 once at least 1 + SPA 263 of the other ones are above 200; for a specialization (43..47,
// 0x5ba150), the first limit (50, or 5 x highest level - 300 above level 70, or above 64 with the highest level
// above 70) when at least one (two with SPA 248) of the other specializations is above it, else the second (100,
// or 5 x highest - 250) when any is above that, else the cap.
std::int32_t pc_skill_limit(const SkillLimitView& who, std::uint32_t skill);
// PcClient::AtSkillLimit @0x584e50: the skill's value >= that limit.
bool at_skill_limit(const SkillLimitView& who, std::uint32_t skill);

// PcClient::checkLang @0x581800: whether the character may learn the language yet.
bool can_learn_language(const TraineeView& who, std::uint32_t language);

// CTrainWnd::SetGMData: skills with a guildmaster limit above 0 that the class gets at some level, except
// 72, 33 and 15; then the languages the character may learn and the guildmaster teaches (limit 100).
std::vector<TrainingRow> training_rows(const TraineeView& who, const std::array<std::uint32_t, kTrainSkills>& skill_caps,
                                       const std::array<std::uint8_t, kTrainLanguages>& language_caps);

// 0x584a30: the cost in copper to train `value` (the current skill or language value) one point:
// (value - 10)^3 / 100, 0 at 10 or below.
std::int32_t training_cost(std::int32_t value);

// A refusal: eqstr `id` built with the guildmaster's name as %1 (when `trainer`) and the skill or
// language name as %2 (when `named`). id 0: refused without a message.
struct TrainRefusal {
    std::uint32_t id = 0;
    bool trainer = true;
    bool named = false;
};
// PcZoneClient_CanTrainSkillOrLanguage: nullopt when the request may go out.
std::optional<TrainRefusal> check_train(const TraineeView& who, const TrainingRow& row);

} // namespace hailstone::game
