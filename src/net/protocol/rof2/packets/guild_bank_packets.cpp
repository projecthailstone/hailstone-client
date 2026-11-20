#include "net/protocol/rof2/packets/guild_bank_packets.h"

#include "core/byte_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

using detail::decode_with;

namespace {

constexpr std::size_t kName = 64, kUpdateSize = 230;
constexpr std::uint32_t kActUpdate = 0, kActPromote = 2, kActDeposit = 4, kActPermissions = 5, kActAck = 9;

ByteWriter action(std::uint32_t a, std::uint32_t banker = 0) {
    ByteWriter w;
    w.le(a);
    w.le(banker);
    w.le<std::uint32_t>(0);
    return w;
}

void name_field(ByteWriter& w, std::string_view s) { w.fixed_string(s.substr(0, kName - 1), kName); }

} // namespace

std::optional<GuildBankList> decode_guild_bank_list(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        GuildBankList v;
        for (int i = 0; i < kGuildBankDepositSlots + kGuildBankMainSlots; ++i) {
            auto& area = i < kGuildBankDepositSlots ? v.deposit : v.main;
            if (r.empty()) {
                area.emplace_back();
                continue;
            }
            if (r.le<std::uint8_t>() == 0) {
                area.emplace_back();
                continue;
            }
            GuildBankItem it;
            it.permission = static_cast<GuildBankPermission>(r.le<std::uint32_t>());
            it.who_for = r.cstring();
            it.donator = r.cstring();
            it.item_id = r.le<std::uint32_t>();
            it.icon = r.le<std::uint32_t>();
            it.quantity = r.le<std::uint32_t>();
            it.allow_merge = r.le<std::uint8_t>() != 0;
            it.usable = r.le<std::uint8_t>() != 0;
            it.name = r.cstring();
            area.push_back(std::move(it));
        }
        return v;
    });
}

AppPacket encode(const GuildBankList& v) {
    ByteWriter w;
    for (const auto* area : {&v.deposit, &v.main})
        for (const auto& slot : *area) {
            w.le<std::uint8_t>(slot ? 1 : 0);
            if (!slot) continue;
            w.le(static_cast<std::uint32_t>(slot->permission));
            detail::write_cstring(w, slot->who_for);
            detail::write_cstring(w, slot->donator);
            w.le(slot->item_id);
            w.le(slot->icon);
            w.le(slot->quantity);
            w.le<std::uint8_t>(slot->allow_merge ? 1 : 0);
            w.le<std::uint8_t>(slot->usable ? 1 : 0);
            detail::write_cstring(w, slot->name);
        }
    return make_packet(rof2::Op::GuildBankItemList, std::move(w));
}

std::optional<GuildBankMessage> decode_guild_bank(const AppPacket& p) {
    return decode_with(p, [&](ByteReader& r) {
        GuildBankMessage v;
        std::uint32_t a = r.le<std::uint32_t>();
        if (a == kActAck) {
            v.kind = GuildBankMessage::Ack;
        } else if (a == kActDeposit || a == kActPromote) {
            v.kind = a == kActDeposit ? GuildBankMessage::DepositAck : GuildBankMessage::PromoteAck;
            r.skip(8);
            v.slot = r.le<std::uint16_t>();
            v.area = static_cast<GuildBankArea>(r.le<std::uint16_t>());
        } else if (a == kActUpdate) {
            if (p.data.size() < kUpdateSize) throw std::out_of_range("guild bank update");
            v.kind = GuildBankMessage::Update;
            r.skip(8);
            v.slot = r.le<std::uint16_t>();
            v.area = static_cast<GuildBankArea>(r.le<std::uint16_t>());
            bool shown = r.le<std::uint32_t>() != 0;
            GuildBankItem it;
            it.item_id = r.le<std::uint32_t>();
            it.icon = r.le<std::uint32_t>();
            it.quantity = r.le<std::uint32_t>();
            it.permission = static_cast<GuildBankPermission>(r.le<std::uint32_t>());
            it.allow_merge = r.le<std::uint8_t>() != 0;
            it.usable = r.le<std::uint8_t>() != 0;
            it.name = r.fixed_string(kName);
            it.donator = r.fixed_string(kName);
            it.who_for = r.fixed_string(kName);
            if (shown && it.item_id) v.item = std::move(it);
        }
        return v;
    });
}

AppPacket encode(const GuildBankMessage& v) {
    ByteWriter w;
    switch (v.kind) {
    case GuildBankMessage::Ack:
        w = action(kActAck);
        break;
    case GuildBankMessage::DepositAck:
    case GuildBankMessage::PromoteAck:
        w = action(v.kind == GuildBankMessage::DepositAck ? kActDeposit : kActPromote);
        w.le(v.slot);
        w.le(static_cast<std::uint16_t>(v.area));
        break;
    default: {
        w = action(kActUpdate);
        GuildBankItem none;
        const GuildBankItem& it = v.item ? *v.item : none;
        w.le(v.slot);
        w.le(static_cast<std::uint16_t>(v.area));
        w.le<std::uint32_t>(v.item ? 1 : 0);
        w.le(it.item_id);
        w.le(it.icon);
        w.le(it.quantity);
        w.le(static_cast<std::uint32_t>(it.permission));
        w.le<std::uint8_t>(it.allow_merge ? 1 : 0);
        w.le<std::uint8_t>(it.usable ? 1 : 0);
        name_field(w, it.name);
        name_field(w, it.donator);
        name_field(w, it.who_for);
        break;
    }
    }
    return make_packet(rof2::Op::GuildBank, std::move(w));
}

AppPacket encode_guild_bank_deposit(std::uint32_t banker) {
    ByteWriter w = action(kActDeposit, banker);
    w.le<std::uint32_t>(0);
    return make_packet(rof2::Op::GuildBank, std::move(w));
}

AppPacket encode_guild_bank_item(GuildBankAction a, std::uint32_t banker, std::int32_t key, std::uint32_t item_id,
                                 std::uint32_t value) {
    ByteWriter w = action(static_cast<std::uint32_t>(a), banker);
    w.le(static_cast<std::uint32_t>(key));
    w.le(item_id);
    w.le(value);
    return make_packet(rof2::Op::GuildBank, std::move(w));
}

AppPacket encode_guild_bank_permissions(std::uint32_t banker, std::int32_t key, std::uint32_t item_id,
                                        GuildBankPermission permission, std::string_view member) {
    ByteWriter w = action(kActPermissions, banker);
    w.le(static_cast<std::uint32_t>(key));
    w.le(item_id);
    w.le(static_cast<std::uint32_t>(permission));
    name_field(w, member);
    return make_packet(rof2::Op::GuildBank, std::move(w));
}

} // namespace hailstone::net
