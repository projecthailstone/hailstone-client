#include "net/protocol/rof2/packets/training_packets.h"

#include <algorithm>

#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

AppPacket encode(const GMTraining& v) {
    ByteWriter w;
    w.le(v.trainer_id);
    w.le(v.player_id);
    for (auto s : v.skills) w.le(s);
    w.f32(v.greed);
    w.le(v.success);
    for (auto l : v.languages) w.le(l);
    w.zeros(kGMTrainingSize - (8 + 400 + 4 + 1 + game::kTrainLanguages)); // 3 bytes (uninitialised in the original)
    return make_packet(rof2::Op::GMTraining, std::move(w));
}

std::optional<GMTraining> decode_gm_training(const AppPacket& p) {
    if (p.data.size() < 8 + 400 + 4 + 1 + game::kTrainLanguages) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        GMTraining v;
        v.trainer_id = r.le<std::uint32_t>();
        v.player_id = r.le<std::uint32_t>();
        for (auto& s : v.skills) s = r.le<std::uint32_t>();
        v.greed = r.f32();
        v.success = r.le<std::uint8_t>();
        for (auto& l : v.languages) l = r.le<std::uint8_t>();
        return v;
    });
}

AppPacket encode_gm_train_skill(std::uint32_t trainer_id, bool language, std::uint32_t id) {
    ByteWriter w;
    w.le(trainer_id);
    w.le(std::uint32_t{language ? 1u : 0u});
    w.le(id);
    return make_packet(rof2::Op::GMTrainSkill, std::move(w));
}

AppPacket encode_gm_end_training(std::uint32_t trainer_id, std::uint32_t player_id) {
    ByteWriter w;
    w.le(trainer_id);
    w.le(player_id);
    return make_packet(rof2::Op::GMEndTraining, std::move(w));
}

std::optional<GMTrainSkillConfirm> decode_gm_train_skill_confirm(const AppPacket& p) {
    if (p.data.size() < 9) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        GMTrainSkillConfirm v;
        v.skill = r.le<std::uint32_t>();
        v.cost = r.le<std::uint32_t>();
        v.new_skill = r.le<std::uint8_t>();
        v.trainer = r.fixed_string(std::min<std::size_t>(64, r.remaining()));
        return v;
    });
}

AppPacket encode(const GMTrainSkillConfirm& v) {
    ByteWriter w;
    w.le(v.skill);
    w.le(v.cost);
    w.le(v.new_skill);
    std::string name = v.trainer.substr(0, 63);
    w.bytes({reinterpret_cast<const std::byte*>(name.data()), name.size()});
    w.zeros(64 - name.size() + 3);
    return make_packet(rof2::Op::GMTrainSkillConfirm, std::move(w));
}

} // namespace hailstone::net
