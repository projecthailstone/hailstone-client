#include "net/protocol/rof2/packets/player_packets.h"

#include <algorithm>
#include <bit>
#include <format>

#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

namespace {

AppPacket two_ids(rof2::Op op, std::uint32_t a, std::uint32_t b) {
    ByteWriter w;
    w.le(a);
    w.le(b);
    return make_packet(op, std::move(w));
}

void write_float_bits(ByteWriter& w, float f) { w.f32(f); }

} // namespace

// ---------------------------------------------------------------- duels

AppPacket encode(const DuelRequest& v) { return two_ids(rof2::Op::RequestDuel, v.first, v.second); }

std::optional<DuelRequest> decode_duel_request(const AppPacket& p) {
    if (p.data.size() < kDuelRequestSize) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        DuelRequest v;
        v.first = r.le<std::uint32_t>();
        v.second = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode_duel_accept(std::uint32_t challenger, std::uint32_t me) { return two_ids(rof2::Op::DuelAccept, challenger, me); }

AppPacket encode_duel_decline(std::uint32_t partner, std::uint32_t me) { return two_ids(rof2::Op::DuelDecline, partner, me); }

AppPacket encode_duel_decline_eqemu(std::uint32_t challenger, std::uint32_t me) {
    ByteWriter w;
    w.le(challenger);
    w.le(me);
    w.le(std::uint32_t{0});
    return make_packet(rof2::Op::DuelDecline, std::move(w));
}

AppPacket encode(const DuelRefusal& v) {
    ByteWriter w;
    w.le(v.challenger);
    w.le(v.refuser);
    w.le(v.kind);
    w.zeros(3); // the upper bytes of the original's kind word (uninitialised stack)
    return AppPacket(kOpDuelRefusal, w.take());
}

std::optional<DuelRefusal> decode_duel_refusal(const AppPacket& p) {
    if (p.opcode != kOpDuelRefusal || p.data.size() < 9) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        DuelRefusal v;
        v.challenger = r.le<std::uint32_t>();
        v.refuser = r.le<std::uint32_t>();
        v.kind = r.le<std::uint8_t>();
        return v;
    });
}

// ---------------------------------------------------------------- inspect

AppPacket encode_inspect_request(std::uint32_t target, std::uint32_t me) { return two_ids(rof2::Op::InspectRequest, target, me); }

std::optional<InspectRequest> decode_inspect_request(const AppPacket& p) {
    if (p.data.size() < 8) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        InspectRequest v;
        v.target = r.le<std::uint32_t>();
        v.player = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const InspectAnswer& v) {
    ByteWriter w;
    w.le(v.requester);
    w.le(v.player);
    for (const auto& n : v.names) w.fixed_string(n.substr(0, 63), 64);
    for (auto i : v.icons) w.le(i);
    w.fixed_string(v.text.substr(0, 287), 288);
    return make_packet(rof2::Op::InspectAnswer, std::move(w));
}

std::optional<InspectAnswer> decode_inspect_answer(const AppPacket& p) {
    if (p.data.size() < kInspectAnswerSize) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        InspectAnswer v;
        v.requester = r.le<std::uint32_t>();
        v.player = r.le<std::uint32_t>();
        for (auto& n : v.names) n = r.fixed_string(64);
        for (auto& i : v.icons) i = r.le<std::uint32_t>();
        v.text = r.fixed_string(288);
        return v;
    });
}

AppPacket encode_inspect_message(const std::string& text) {
    ByteWriter w;
    w.fixed_string(text.substr(0, kInspectMessageSize - 1), kInspectMessageSize);
    return make_packet(rof2::Op::InspectMessageUpdate, std::move(w));
}

// ---------------------------------------------------------------- player commands

AppPacket encode(const Surname& v) {
    ByteWriter w;
    w.fixed_string(v.name.substr(0, 63), 64);
    w.le(v.unknown64);
    w.fixed_string(v.last_name.substr(0, 31), 32);
    return make_packet(rof2::Op::Surname, std::move(w));
}

std::optional<Surname> decode_surname(const AppPacket& p) {
    if (p.data.size() < kSurnameSize) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        Surname v;
        v.name = r.fixed_string(64);
        v.unknown64 = r.le<std::uint32_t>();
        v.last_name = r.fixed_string(32);
        return v;
    });
}

AppPacket encode_corpse_command(rof2::Op op, const CorpseCommand& v) {
    ByteWriter w;
    w.fixed_string(v.corpse_name.substr(0, 63), 64);
    w.fixed_string(v.player_name.substr(0, 63), 64);
    w.le(std::uint32_t{0}); // success
    w.le(v.zone_id);
    write_float_bits(w, v.y);
    write_float_bits(w, v.x);
    write_float_bits(w, v.z);
    w.le(std::uint32_t{0});
    return make_packet(op, std::move(w));
}

AppPacket encode_corpse_drop(const std::string& target_name) {
    ByteWriter w;
    detail::write_cstring(w, target_name.substr(0, 64));
    return make_packet(rof2::Op::CorpseDrop, std::move(w));
}

AppPacket encode_shielding(std::uint32_t target) {
    ByteWriter w;
    w.le(target);
    return make_packet(rof2::Op::Shielding, std::move(w));
}

AppPacket encode_shield_group(const std::vector<std::uint32_t>& members) {
    ByteWriter w;
    std::size_t n = std::min<std::size_t>(members.size(), 6);
    for (std::size_t k = 0; k < 6; ++k) w.le(k < n ? members[k] : std::uint32_t{0});
    w.le(static_cast<std::uint32_t>(n));
    return AppPacket(kOpShieldGroup, w.take());
}

AppPacket encode_player_state(bool add, const PlayerState& v) {
    return two_ids(add ? rof2::Op::PlayerStateAdd : rof2::Op::PlayerStateRemove, v.spawn_id, v.state);
}

std::optional<PlayerState> decode_player_state(const AppPacket& p) {
    if (p.data.size() < 8) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        PlayerState v;
        v.spawn_id = r.le<std::uint32_t>();
        v.state = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode_hide_corpse(std::uint32_t action, std::uint32_t corpse_id) {
    return two_ids(rof2::Op::HideCorpse, action, corpse_id);
}

AppPacket encode_decay_corpse(std::uint32_t corpse_id, std::uint32_t player_id) {
    ByteWriter w;
    w.le(corpse_id);
    w.le(player_id);
    return AppPacket(kOpDecayCorpse, w.take());
}

AppPacket encode_set_start_city(const std::string& text) {
    ByteWriter w;
    detail::write_cstring(w, text);
    return make_packet(rof2::Op::SetStartCity, std::move(w));
}

AppPacket encode_reveal() { return AppPacket(kOpReveal); }

AppPacket encode(const FaceChange& v) {
    ByteWriter w;
    for (std::uint8_t b : {v.hair_color, v.beard_color, v.eye1, v.eye2, v.hair_style, v.beard, v.face, std::uint8_t{0}})
        w.le(b);
    w.le(v.heritage);
    w.le(v.tattoo);
    w.le(v.details);
    w.le(v.spawn_id);
    return make_packet(rof2::Op::FaceChange, std::move(w));
}

std::optional<FaceChange> decode_face_change(const AppPacket& p) {
    if (p.data.size() < kFaceChangeSize) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        FaceChange v;
        v.hair_color = r.le<std::uint8_t>();
        v.beard_color = r.le<std::uint8_t>();
        v.eye1 = r.le<std::uint8_t>();
        v.eye2 = r.le<std::uint8_t>();
        v.hair_style = r.le<std::uint8_t>();
        v.beard = r.le<std::uint8_t>();
        v.face = r.le<std::uint8_t>();
        r.le<std::uint8_t>();
        v.heritage = r.le<std::uint32_t>();
        v.tattoo = r.le<std::uint32_t>();
        v.details = r.le<std::uint32_t>();
        v.spawn_id = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode_key_ring_request() {
    ByteWriter w;
    w.le(std::uint32_t{0x0059454b}); // "KEY\0"
    return make_packet(rof2::Op::KeyRing, std::move(w));
}

AppPacket encode_rewind() { return make_empty(rof2::Op::Rewind); }

AppPacket encode_reset_aa() { return make_empty(rof2::Op::ResetAA); }

std::optional<AppPacket> encode_petition(const std::string& text) {
    if (text.size() >= 0x400) return std::nullopt;
    ByteWriter w;
    detail::write_cstring(w, text);
    return make_packet(rof2::Op::Petition, std::move(w));
}

// ---------------------------------------------------------------- reports and server requests

AppPacket encode_report(std::string text) {
    if (text.size() > 0x2004) text.resize(0x2004);
    ByteWriter w;
    detail::write_cstring(w, text);
    return make_packet(rof2::Op::Report, std::move(w));
}

AppPacket encode(const Feedback& v) {
    ByteWriter w;
    w.fixed_string(v.name.substr(0, 0x3f), 0x40);
    w.zeros(0x20); // +0x40: the zero byte, then the gap
    w.f32(v.y);
    w.f32(v.x);
    w.f32(v.z);
    w.le(v.heading);
    w.zeros(8);
    w.fixed_string(v.text.substr(0, 0x3ff), 0x400);
    w.zeros(4);
    return make_packet(rof2::Op::Feedback, std::move(w));
}

std::optional<Feedback> decode_feedback(const AppPacket& p) {
    if (p.data.size() < kFeedbackSize) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        Feedback v;
        v.name = r.fixed_string(0x40);
        r.skip(0x20);
        v.y = r.f32();
        v.x = r.f32();
        v.z = r.f32();
        v.heading = r.le<std::int32_t>();
        r.skip(8);
        v.text = r.fixed_string(0x400);
        return v;
    });
}

AppPacket encode_spell_scribe(const std::string& self, const std::string& other) {
    std::vector<std::byte> b(kSpellScribeSize);
    // strcpy of the target's name, then of ours over it: the bytes past our NUL keep the target's tail.
    auto copy = [&](std::size_t at, const std::string& name) {
        std::size_t n = std::min<std::size_t>(name.size(), 0x3f);
        for (std::size_t k = 0; k < n; ++k) b[at + k] = static_cast<std::byte>(name[k]);
        b[at + n] = std::byte{0};
    };
    copy(0, self);
    copy(0x40, other);
    copy(0, self);
    copy(0x40, self);
    b[0x80] = std::byte{4};
    return AppPacket(kOpSpellScribe, std::move(b));
}

AppPacket encode_veteran_reward_claim() { return AppPacket(kOpVeteranRewardClaim, std::vector<std::byte>(0x48)); }

AppPacket encode_achievement_compare(std::uint32_t target, std::uint32_t me) {
    ByteWriter w;
    w.le(target);
    w.le(me);
    return AppPacket(kOpAchievementCompare, w.take());
}

std::optional<Uptime> decode_uptime(const AppPacket& p) {
    if (p.opcode != kOpUptime || p.data.size() < 8) return std::nullopt;
    ByteReader r(p.data);
    Uptime v;
    v.world_seconds = r.le<std::uint32_t>();
    v.zone_seconds = r.le<std::uint32_t>();
    return v;
}

AppPacket encode(const Uptime& v) {
    ByteWriter w;
    w.le(v.world_seconds);
    w.le(v.zone_seconds);
    return AppPacket(kOpUptime, w.take());
}

std::string format_uptime(std::uint32_t s) {
    // Handle_Opcode_0x344f: unsigned divisions by 60, 60 and 24.
    return std::format("{:02}:{:02}:{:02}:{:02}", s / 86400, s / 3600 % 24, s / 60 % 60, s % 60);
}

// ---------------------------------------------------------------- bandolier

AppPacket encode_bandolier(std::uint32_t action, std::uint8_t number, const std::string& name) {
    ByteWriter w;
    w.le(action);
    w.le(number);
    // The original _strncpy's the name into 0x20, with no NUL when it is 32 long. Deliberate difference: at most 31
    // characters, so the field always ends in a NUL; stock EQEmu's Client::CreateBandolier strcpy's it into
    // Name[32] and a 32-character name would write one byte past it. The set keeps 31 too (bandolier_create).
    w.fixed_string(std::string_view(name).substr(0, 31), 32);
    w.zeros(kBandolierSize - 4 - 1 - 32);
    return make_packet(rof2::Op::Bandolier, std::move(w));
}

AppPacket encode_bandolier_hold(std::uint8_t set, std::uint8_t slot, bool shift) {
    ByteWriter w;
    w.le<std::uint32_t>(3);
    w.le(set);
    w.le(slot);
    w.le<std::uint16_t>(0);
    w.le<std::uint32_t>(shift ? 2 : 0);
    return make_packet(rof2::Op::Bandolier, std::move(w));
}

std::optional<BandolierPacket> decode_bandolier(const AppPacket& p) {
    if (p.data.size() < 5) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        BandolierPacket v;
        v.action = r.le<std::uint32_t>();
        v.number = r.le<std::uint8_t>();
        v.name = r.fixed_string(std::min<std::size_t>(32, r.remaining()));
        return v;
    });
}

// ---------------------------------------------------------------- books

AppPacket encode(const ReadBook& v) {
    ByteWriter w;
    w.le(v.window);
    w.le(v.slot);
    w.le(v.sub);
    w.le(v.aug);
    w.le(v.slot_unknown);
    w.le(v.type);
    w.le(v.target);
    w.le(v.can_cast);
    w.le(v.can_scribe);
    w.fixed_string(v.text.substr(0, 8193), 8194);
    return make_packet(rof2::Op::ReadBook, std::move(w));
}

std::optional<ReadBook> decode_read_book(const AppPacket& p) {
    if (p.data.size() < 22) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        ReadBook v;
        v.window = r.le<std::uint32_t>();
        v.slot = r.le<std::int16_t>();
        v.sub = r.le<std::int16_t>();
        v.aug = r.le<std::int16_t>();
        v.slot_unknown = r.le<std::int16_t>();
        v.type = r.le<std::uint32_t>();
        v.target = r.le<std::uint32_t>();
        v.can_cast = r.le<std::uint8_t>();
        v.can_scribe = r.le<std::uint8_t>();
        v.text = r.fixed_string(std::min<std::size_t>(8194, r.remaining()));
        return v;
    });
}

AppPacket encode_book_button(std::int16_t slot, std::int16_t sub, std::int16_t aug, std::uint32_t target) {
    ByteWriter w;
    w.le(slot);
    w.le(sub);
    w.le(aug);
    w.le(std::int16_t{0});
    w.le(target);
    w.le(std::uint32_t{0});
    return make_packet(rof2::Op::BookButton, std::move(w));
}

// ---------------------------------------------------------------- the large dialog

std::optional<LevelMessage> decode_level_message(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        // SerializeBuffer_ReadCXStr: a u32 count, then that many bytes (the text ends at its NUL).
        auto counted = [&r]() {
            auto n = r.le<std::uint32_t>();
            return r.fixed_string(std::min<std::size_t>(n, r.remaining()));
        };
        LevelMessage v;
        v.title = counted();
        v.text = counted();
        v.yes = counted();
        v.no = counted();
        // The original reads each field only while the buffer has it.
        if (r.remaining() >= 1) v.buttons = r.le<std::uint8_t>();
        if (r.remaining() >= 1) v.sound = r.le<std::uint8_t>();
        if (r.remaining() >= 4) v.duration = r.le<std::uint32_t>();
        if (r.remaining() >= 4) v.popup_id = r.le<std::uint32_t>();
        if (r.remaining() >= 4) v.negative_id = r.le<std::uint32_t>();
        if (r.remaining() >= 4) v.close_id = r.le<std::uint32_t>();
        if (r.remaining() >= 4) v.language = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode_popup_response(std::uint32_t kind, std::uint32_t id) {
    ByteWriter w;
    w.le(kind);
    w.le(id);
    return make_packet(rof2::Op::PopupResponse, std::move(w));
}

AppPacket encode_request_titles() { return make_packet(rof2::Op::RequestTitles, ByteWriter{}); }

std::optional<std::vector<TitleEntry>> decode_title_list(const AppPacket& p) {
    if (p.data.size() < 4) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        std::uint32_t n = r.le<std::uint32_t>();
        if (n > 10000) throw std::out_of_range("titles");
        std::vector<TitleEntry> v;
        for (std::uint32_t i = 0; i < n; ++i) {
            TitleEntry t;
            t.id = r.le<std::uint32_t>();
            t.prefix = r.cstring();
            t.suffix = r.cstring();
            v.push_back(std::move(t));
        }
        return v;
    });
}

AppPacket encode_title_list(const std::vector<TitleEntry>& v) {
    ByteWriter w;
    w.le(static_cast<std::uint32_t>(v.size()));
    for (const auto& t : v) {
        w.le(t.id);
        detail::write_cstring(w, t.prefix);
        detail::write_cstring(w, t.suffix);
    }
    return make_packet(rof2::Op::SendTitleList, std::move(w));
}

AppPacket encode_set_title(bool suffix, std::uint32_t title_id) {
    ByteWriter w;
    w.le<std::uint32_t>(suffix ? 1 : 0);
    w.le(title_id);
    return make_packet(rof2::Op::SetTitle, std::move(w));
}

std::optional<TitleReply> decode_title_reply(const AppPacket& p) {
    if (p.data.size() < 40) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        TitleReply v;
        v.suffix = r.le<std::uint32_t>() != 0;
        v.text = r.fixed_string(32);
        v.spawn_id = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const TitleReply& v) {
    ByteWriter w;
    w.le<std::uint32_t>(v.suffix ? 1 : 0);
    w.fixed_string(v.text.substr(0, 31), 32);
    w.le(v.spawn_id);
    return make_packet(rof2::Op::SetTitleReply, std::move(w));
}

AppPacket encode_potion_belt(bool clear, std::uint32_t slot, std::uint32_t item_id) {
    ByteWriter w;
    w.le<std::uint32_t>(clear ? 1 : 0);
    w.le(slot);
    w.le(item_id);
    return make_packet(rof2::Op::PotionBelt, std::move(w));
}

AppPacket encode_potion_belt_hold(std::uint32_t slot, bool shift) {
    ByteWriter w;
    w.le<std::uint32_t>(3);
    w.le(slot);
    w.le<std::uint32_t>(shift ? 2 : 0);
    return make_packet(rof2::Op::PotionBelt, std::move(w));
}

AppPacket encode_dye(const std::array<std::uint32_t, 9>& tints) {
    ByteWriter w;
    for (auto t : tints) w.le(t);
    return make_packet(rof2::Op::Dye, std::move(w));
}

AppPacket encode(const BugReport& v) {
    auto str = [](ByteWriter& w, std::string_view s, std::size_t n) { w.fixed_string(s.substr(0, n - 1), n); };
    ByteWriter w;
    w.le(v.category);
    str(w, bug_category_name(v.category), 64);
    str(w, v.reporter, 64);
    w.zeros(32);
    str(w, "", 128);
    w.le(std::bit_cast<std::uint32_t>(v.x));
    w.le(std::bit_cast<std::uint32_t>(v.y));
    w.le(std::bit_cast<std::uint32_t>(v.z));
    w.le(v.heading);
    w.le<std::uint32_t>(0);
    w.le(v.minutes_played);
    w.zeros(8);
    w.le(v.target_id);
    w.zeros(140);
    w.le<std::uint32_t>(0);
    str(w, v.target_name, 64);
    w.le(v.flags);
    w.zeros(2052);
    str(w, v.text, 2050);
    str(w, v.system_info, 4098);
    return make_packet(rof2::Op::Bug, std::move(w));
}

AppPacket encode_vet_claim(std::uint32_t claim_id) {
    ByteWriter w;
    w.zeros(68);
    w.le(claim_id);
    w.le<std::uint32_t>(0);
    w.le<std::uint32_t>(0);
    return make_packet(rof2::Op::VetClaimRequest, std::move(w));
}

std::optional<std::vector<VetReward>> decode_vet_rewards(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        std::uint32_t n = r.le<std::uint32_t>();
        std::vector<VetReward> v;
        for (std::uint32_t i = 0; i < n; ++i) {
            VetReward e;
            e.claim_id = r.le<std::uint32_t>();
            e.available = r.le<std::uint32_t>();
            std::uint32_t items = r.le<std::uint32_t>();
            e.enabled = r.le<std::uint8_t>() != 0;
            for (std::uint32_t k = 0; k < items; ++k) {
                VetRewardItem it;
                std::uint32_t len = r.le<std::uint32_t>();
                auto name = r.bytes(len);
                it.name.assign(reinterpret_cast<const char*>(name.data()), name.size());
                it.item_id = r.le<std::uint32_t>();
                it.charges = r.le<std::uint32_t>();
                e.items.push_back(std::move(it));
            }
            v.push_back(std::move(e));
        }
        return v;
    });
}

AppPacket encode(const std::vector<VetReward>& v) {
    ByteWriter w;
    w.le(static_cast<std::uint32_t>(v.size()));
    for (const auto& e : v) {
        w.le(e.claim_id);
        w.le(e.available);
        w.le(static_cast<std::uint32_t>(e.items.size()));
        w.le<std::uint8_t>(e.enabled ? 1 : 0);
        for (const auto& it : e.items) {
            w.le(static_cast<std::uint32_t>(it.name.size()));
            w.bytes({reinterpret_cast<const std::byte*>(it.name.data()), it.name.size()});
            w.le(it.item_id);
            w.le(it.charges);
        }
    }
    return make_packet(rof2::Op::VetRewardsAvaliable, std::move(w));
}

std::optional<VetClaimReply> decode_vet_claim_reply(const AppPacket& p) {
    if (p.data.size() < 80) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        r.skip(68);
        VetClaimReply v;
        v.claim_id = r.le<std::uint32_t>();
        r.skip(4);
        v.claimed = r.le<std::uint32_t>() == 0;
        return v;
    });
}

namespace {

PvPEntry read_pvp_entry(ByteReader& r) {
    PvPEntry e;
    e.name = r.fixed_string(64);
    e.level = r.le<std::uint32_t>();
    e.race = r.le<std::uint32_t>();
    e.class_ = r.le<std::uint32_t>();
    e.zone = r.le<std::uint32_t>();
    e.time = r.le<std::uint32_t>();
    e.points = r.le<std::uint32_t>();
    return e;
}

void write_pvp_entry(ByteWriter& w, const PvPEntry& e) {
    w.fixed_string(std::string_view(e.name).substr(0, 63), 64);
    for (auto v : {e.level, e.race, e.class_, e.zone, e.time, e.points}) w.le(v);
}

} // namespace

std::optional<PvPStats> decode_pvp_stats(const AppPacket& p) {
    if (p.data.size() < 4612) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        PvPStats v;
        for (auto* f : {&v.kills, &v.deaths, &v.points, &v.career_points, &v.best_streak, &v.worst_streak,
                        &v.current_streak, &v.infamy, &v.vitality})
            *f = r.le<std::uint32_t>();
        v.last_death = read_pvp_entry(r);
        v.last_kill = read_pvp_entry(r);
        for (int i = 0; i < 50; ++i)
            if (PvPEntry e = read_pvp_entry(r); !e.name.empty()) v.recent_kills.push_back(std::move(e));
        return v;
    });
}

AppPacket encode(const PvPStats& v) {
    ByteWriter w;
    for (auto f : {v.kills, v.deaths, v.points, v.career_points, v.best_streak, v.worst_streak, v.current_streak,
                   v.infamy, v.vitality})
        w.le(f);
    write_pvp_entry(w, v.last_death);
    write_pvp_entry(w, v.last_kill);
    for (int i = 0; i < 50; ++i)
        write_pvp_entry(w, i < static_cast<int>(v.recent_kills.size()) ? v.recent_kills[static_cast<std::size_t>(i)] : PvPEntry{});
    return make_packet(rof2::Op::PVPStats, std::move(w));
}

AppPacket encode_pvp_leaderboard_request(std::uint32_t sort) {
    ByteWriter w;
    w.le(sort);
    return make_packet(rof2::Op::PVPLeaderBoardRequest, std::move(w));
}

std::optional<PvPLeaderboard> decode_pvp_leaderboard(const AppPacket& p) {
    if (p.data.size() < 8024) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        PvPLeaderboard v;
        r.skip(4);
        v.kills = r.le<std::uint32_t>();
        v.points = r.le<std::uint32_t>();
        v.rank = r.le<std::uint32_t>();
        v.deaths = r.le<std::uint32_t>();
        v.infamy = r.le<std::uint32_t>();
        for (int i = 0; i < 100; ++i) {
            PvPLeader l;
            l.name = r.fixed_string(64);
            l.kills = r.le<std::uint32_t>();
            l.deaths = r.le<std::uint32_t>();
            l.points = r.le<std::uint32_t>();
            l.infamy = r.le<std::uint32_t>();
            if (!l.name.empty()) v.rows.push_back(std::move(l));
        }
        return v;
    });
}

std::optional<CrystalCounts> decode_crystal_counts(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        CrystalCounts v;
        v.radiant = r.le<std::uint32_t>();
        v.radiant_career = r.le<std::uint32_t>();
        v.ebon = r.le<std::uint32_t>();
        v.ebon_career = r.le<std::uint32_t>();
        return v;
    });
}

} // namespace hailstone::net
