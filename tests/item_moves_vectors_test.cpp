// The original's vectors (recorded from eqgame.exe): the equip-slot rule, CanUseItem and its
// race / deity / level checks, the server OP_MoveItem apply, OP_MoveMultipleItems, MoveMoney and the inventory
// window's money buttons (game/item_moves.h, net/protocol/rof2/packets/item_packets.h).

#include <doctest/doctest.h>

#include <format>
#include <optional>
#include <string>
#include <vector>

#include "game/item_moves.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "vector_files.h"

using hailstone::test::each_vector;
using hailstone::test::Json;
namespace game = hailstone::game;
namespace net = hailstone::net;

namespace {

// The record's calls as "fn" names joined, for comparing call sequences.
std::string call_names(const Json& calls) {
    std::string s;
    for (const Json& c : calls.items) s += c["fn"].text + ",";
    return s;
}

game::EquipItem equip_item(const Json& j) {
    game::EquipItem e;
    e.item_class = j["item_class"].i32();
    e.item_type = j["item_type"].i32();
    e.range = j["range"].i32();
    e.equip_slots = j["equip_slots"].u32();
    e.weapon = j["is_weapon"].truthy();
    return e;
}

game::GlobalIndex index_of(const Json& j) {
    game::GlobalIndex g;
    g.location = j["location"].i32();
    g.slot = static_cast<std::int16_t>(j["slot"].i32());
    g.sub = static_cast<std::int16_t>(j["sub_slot"].i32());
    g.aug = static_cast<std::int16_t>(j["aug_slot"].i32());
    return g;
}

net::ItemSlot slot_of(const Json& j) {
    net::ItemSlot s;
    auto location = j["location"].u32();
    s.type = static_cast<std::int16_t>(location & 0xffffu);
    s.unknown02 = static_cast<std::int16_t>(location >> 16);
    s.slot = static_cast<std::int16_t>(j["slot"].i32());
    s.sub = static_cast<std::int16_t>(j["sub_slot"].i32());
    s.aug = static_cast<std::int16_t>(j["aug_slot"].i32());
    s.unknown01 = static_cast<std::int16_t>(j["pad"].i32());
    return s;
}

} // namespace

TEST_CASE("PcZoneClient_CanItemGoInEquipSlot vectors") {
    each_vector("PcZoneClient_CanItemGoInEquipSlot.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        std::optional<game::EquipItem> item, primary, secondary;
        if (!in["item"].is_null()) item = equip_item(in["item"]);
        if (!in["primary_item"].is_null()) primary = equip_item(in["primary_item"]);
        if (!in["secondary_item"].is_null()) secondary = equip_item(in["secondary_item"]);
        bool got = game::can_item_go_in_equip_slot(in["slot"].i32(), item ? &*item : nullptr, in["slot_available"].truthy(),
                                                   primary ? &*primary : nullptr, secondary ? &*secondary : nullptr,
                                                   in["has_dual_wield"].truthy());
        bool want = r["out"]["value"].truthy();
        return got == want ? "" : std::format("slot {}: {}, original {}", in["slot"].i32(), got, want);
    });
}

TEST_CASE("CharacterZoneClient::CanUseItem vectors") {
    each_vector("CharacterZoneClient_CanUseItem.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        std::string calls = "item_class,";
        std::uint32_t usable = in["usable_classes"].u32();
        bool tell = in["tell"].truthy();
        auto check = [&](const char* name, const char* key) {
            return [&calls, &in, name, key] {
                calls += std::string(name) + ",";
                return in[key].truthy();
            };
        };
        std::int32_t item_class = in["item_class"].i32();
        if (item_class == 0) calls += "usable_classes,";
        auto cls = static_cast<std::uint32_t>(in["class"].i32() & 0xff);
        bool got = game::can_use_item(item_class, usable, 1u << ((cls - 1u) & 31u), in["full"].truthy(),
                                      in["item_type"].i32(), check("deity_ok", "deity_ok"), check("level_ok", "level_ok"),
                                      check("race_ok", "race_ok"),
                                      [&] {
                                          calls += "prestige_ok,";
                                          return in["prestige_ok"].truthy();
                                      });
        // The race check's item-type look-up happens only after a refusal.
        std::string want_calls = call_names(r["out"]["calls"]);
        std::string want_trim;
        for (const Json& c : r["out"]["calls"].items)
            if (c["fn"].text != "item_type") want_trim += c["fn"].text + ",";
        if (got != r["out"]["value"].truthy()) return std::format("value {}", got);
        if (calls != want_trim) return "calls " + calls + " original " + want_calls;
        for (const Json& c : r["out"]["calls"].items)
            if (c.has("tell") && c["tell"].truthy() != tell) return "tell";
        return "";
    });
}

TEST_CASE("CanUseItem race / deity / level vectors") {
    auto message_of = [](const Json& r) -> std::uint32_t {
        for (const Json& c : r["out"]["calls"].items)
            if (c["fn"].text == "token_string_to_client") return c["string_id"].u32();
        return 0;
    };
    auto player = [](const Json& in) { return in["has_spawn"].truthy() && in["spawn_type"].i32() == 0; };
    each_vector("CharacterZoneClient_CanUseItemRace.jsonl", [&](const Json& r) -> std::string {
        const Json& in = r["in"];
        std::uint32_t msg = 0;
        bool got = game::can_use_item_race(in["race"].i32(), in["item_races"].u32(), in["tell"].truthy(), player(in), &msg);
        if (got != r["out"]["value"].truthy() || msg != message_of(r)) return std::format("race {}: {} {}", in["race"].i32(), got, msg);
        return "";
    });
    each_vector("CharacterZoneClient_CanUseItemDeity.jsonl", [&](const Json& r) -> std::string {
        const Json& in = r["in"];
        std::uint32_t msg = 0;
        bool got = game::can_use_item_deity(in["has_deity_object"].truthy(), in["deity"].i32(), in["item_deities"].u32(),
                                            in["tell"].truthy(), player(in), &msg);
        if (got != r["out"]["value"].truthy() || msg != message_of(r)) return std::format("deity {}: {} {}", in["deity"].i32(), got, msg);
        return "";
    });
    each_vector("CharacterZoneClient_CanUseItemLevel.jsonl", [&](const Json& r) -> std::string {
        const Json& in = r["in"];
        std::uint32_t msg = 0;
        bool got = game::can_use_item_level(in["level"].i32(), in["required_level"].i32(), in["tell"].truthy(), player(in), &msg);
        if (got != r["out"]["value"].truthy() || msg != message_of(r)) return std::format("level {}: {} {}", in["level"].i32(), got, msg);
        return "";
    });
}

TEST_CASE("Handle_OP_MoveItem_Apply vectors") {
    each_vector("Handle_OP_MoveItem_Apply.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        game::GlobalIndex from = index_of(in["from"]), to = index_of(in["to"]);
        const Json& fi = in["from_item"];
        std::string calls;
        game::MoveApply plan = game::plan_move_item_apply(
            from, to, !fi.is_null(), !in["to_item"].is_null(),
            [&] {
                calls += "can_stack_with_to,";
                return fi["stacks_with_other"].truthy();
            },
            [&] {
                calls += "is_stackable,";
                return fi["stackable"].truthy();
            });
        bool value = false;
        std::int32_t cursor_flag = in["cursor_flag"].i32();
        bool applied = false;
        switch (plan) {
        case game::MoveApply::NoItem:
        case game::MoveApply::Refused:
        case game::MoveApply::WrongLocation:
            break;
        case game::MoveApply::Delete:
            calls += "item_class,";
            if (fi["item_class"].i32() == 1) calls += "close_container,";
            if (in["has_bazaar_window"].truthy()) calls += "bazaar_forget_item,";
            calls += "remove_item_at_from,";
            if (game::move_apply_clears_cursor(from)) cursor_flag = 0;
            if (in["has_local_player"].truthy() && from.location == 0) calls += "worn_slot_of,set_held_model,";
            value = true;
            break;
        case game::MoveApply::MergeCount:
            calls += "move_item_count,";
            applied = value = in["move_item_count_ok"].truthy();
            break;
        case game::MoveApply::AdjustCount:
            calls += "adjust_item_stack_count_at_from,";
            applied = value = !in["adjust_stack_count_result"].truthy();
            break;
        case game::MoveApply::Swap:
            calls += "swap_items,";
            applied = value = true;
            break;
        }
        if (applied) {
            calls += "get_cursor_item,";
            cursor_flag = in["cursor_has_item_after"].truthy() ? 1 : 0;
        }
        const Json& out = r["out"];
        if (value != out["value"].truthy()) return std::format("value {}", value);
        if (cursor_flag != out["cursor_flag"].i32()) return std::format("cursor flag {}", cursor_flag);
        if (calls != call_names(out["calls"])) return "calls " + calls + " original " + call_names(out["calls"]);
        for (const Json& c : out["calls"].items) {
            if ((c["fn"].text == "move_item_count" || c["fn"].text == "adjust_item_stack_count_at_from") &&
                c["count"].i32() != in["count"].i32())
                return "count";
            if (c["fn"].text == "worn_slot_of" && c["slot"].i32() != from.slot) return "worn slot";
        }
        return "";
    });
}

TEST_CASE("Send_OP_MoveMultipleItems vectors") {
    each_vector("Send_OP_MoveMultipleItems.jsonl", [](const Json& r) -> std::string {
        std::vector<net::MultiMoveEntry> moves;
        for (const Json& m : r["in"]["moves"].items) {
            net::MultiMoveEntry e;
            e.from = slot_of(m["from"]);
            e.to = slot_of(m["to"]);
            e.count = m["count"].u32();
            e.flags = m["flags"].u32();
            moves.push_back(e);
        }
        std::optional<net::AppPacket> sent = net::encode_move_multiple(moves);
        const Json& want = r["out"]["packet"];
        if (want.is_null() != !sent) return "packet presence";
        if (!sent) return "";
        const net::AppPacket& p = *sent;
        if (p.opcode != want["opcode"].u32()) return "opcode";
        if (p.data.size() + 2 != want["length"].u32()) return std::format("length {}", p.data.size() + 2);
        // The expected bytes from the record.
        std::vector<std::uint8_t> bytes;
        auto put = [&](std::uint32_t v, int n) {
            for (int i = 0; i < n; ++i) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        };
        put(want["count"].u32(), 4);
        for (const Json& m : want["moves"].items) {
            for (const char* k : {"from", "to"}) {
                const Json& s = m[k];
                put(s["location"].u32(), 4);
                put(s["slot"].u32(), 2);
                put(s["sub_slot"].u32(), 2);
                put(s["aug_slot"].u32(), 2);
                put(s["pad"].u32(), 2);
            }
            put(m["count"].u32(), 4);
            put(m["flags"].u32(), 4);
            put(m["room_mode"].u32(), 4);
        }
        if (p.data.size() != bytes.size()) return "payload size";
        for (std::size_t i = 0; i < bytes.size(); ++i)
            if (static_cast<std::uint8_t>(p.data[i]) != bytes[i]) return std::format("payload byte {}", i);
        return "";
    });
}

TEST_CASE("CEverQuest::MoveMoney vectors") {
    each_vector("CEverQuest_MoveMoney.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        static const char* const places[] = {"cursor", "inventory", "bank", "trade", "shared_bank"};
        game::MoneyState s;
        for (std::size_t p = 0; p < 5; ++p)
            for (std::size_t k = 0; k < 4; ++k) s.coins[p][k] = in["coins"][places[p]][k].i32();
        s.cursor_coin_type = static_cast<std::uint8_t>(in["cursor_coin_type"].i32());
        s.attachment_type = in["attachment_type"].i32();
        s.attachment_count = in["attachment_count"].i32();
        s.inventory_changed = static_cast<std::uint8_t>(in["inventory_changed"].i32());
        game::MoneyEnv env;
        env.has_local_pc = in["has_local_pc"].truthy();
        env.bank_window_active = in["bank_window_active"].truthy();
        env.cursor_attachment_free = in["cursor_attachment_free"].truthy();
        env.has_inventory_window = in["has_inventory_window"].truthy();
        game::MoneyMoveResult m = game::move_money(s, env, in["from"].i32(), in["to"].i32(), in["from_coin"].u32(),
                                                   in["to_coin"].u32(), in["amount"].i32(),
                                                   static_cast<std::uint8_t>(in["quiet"].i32()));
        const Json& out = r["out"];
        if (m.value != out["value"].u32()) return std::format("value {}", m.value);
        for (std::size_t p = 0; p < 5; ++p)
            for (std::size_t k = 0; k < 4; ++k)
                if (s.coins[p][k] != out["coins"][places[p]][k].i32())
                    return std::format("{}[{}] {}, original {}", places[p], k, s.coins[p][k], out["coins"][places[p]][k].i32());
        if (s.cursor_coin_type != out["cursor_coin_type"].i32()) return "cursor coin type";
        if (s.attachment_count != out["attachment_count"].i32()) return "attachment count";
        if (s.inventory_changed != out["inventory_changed"].i32()) return "inventory changed";
        std::string calls;
        if (m.asked_bank_window) calls += "bank_window_is_active,";
        if (m.asked_cursor_free) calls += "cursor_attachment_is_free,";
        if (m.played_sound) calls += "wave_play,";
        if (m.cleared_coin_buttons) calls += "inventory_clear_coin_buttons,";
        if (m.send) {
            if (in["connected"].truthy()) {
                calls += "packet_opcode,send,";
                if (!in["send_ok"].truthy()) calls += "close_connection,";
            }
        }
        // Not connected, the original logs the opcode unless 0xddf654 is set, which the records do not show: the
        // log call is left out of the comparison.
        std::string want_calls;
        for (const Json& c : out["calls"].items)
            if (c["fn"].text != "void_connection_log") want_calls += c["fn"].text + ",";
        if (calls != want_calls) return "calls " + calls + " original " + want_calls;
        const Json& pk = out["packet"];
        if (pk.is_null() != !(m.send && in["connected"].truthy())) return "packet presence";
        if (!pk.is_null()) {
            if (pk["from_slot"].i32() != in["from"].i32() || pk["to_slot"].i32() != in["to"].i32() ||
                pk["cointype1"].i32() != m.wire_from_coin || pk["cointype2"].i32() != m.wire_to_coin ||
                pk["amount"].i32() != in["amount"].i32())
                return "packet";
        }
        return "";
    });
}

TEST_CASE("CInventoryWnd::ClickedMoneyButton vectors") {
    each_vector("CInventoryWnd_ClickedMoneyButton.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        game::MoneyButtonIn b;
        b.coin = in["coin"].i32();
        b.amount = in["amount"].i32();
        b.cursor_coin_type = static_cast<std::uint8_t>(in["cursor_coin_type"].i32());
        for (std::size_t k = 0; k < 4; ++k) b.cursor_coins[k] = in["cursor_coins"][k].i32();
        b.has_button = in["has_button"].truthy();
        if (b.has_button)
            for (std::size_t k = 0; k < 4; ++k) b.button_rect[k] = in["button_screen_rect"][k].i32();
        game::MoneyButtonOut o = game::clicked_money_button(b);
        const Json& out = r["out"];
        if (out["pending_crystal"].i32() != -1 || out["pending_alt_currency"].i32() != -1) return "pending requests";
        const Json& calls = out["calls"];
        std::int32_t pending = in["pending_coin"].i32();
        std::string names;
        if (o.kind == game::MoneyButtonOut::Kind::AskQuantity) {
            pending = o.pending_coin;
            names = "inventory_coin_count,";
            if (in["has_inv_slot_mgr"].truthy()) names += "merchant_coin_selected,";
            names += "quantity_open,";
            std::int32_t have = calls[0]["ret"].i32();
            const Json& q = calls[calls.items.size() - 1];
            if (calls[0]["coin"].i32() != b.coin || q["max"].i32() != have || q["start"].i32() != have ||
                q["x"].i32() != o.x || q["y"].i32() != o.y || q["use_keys"].i32() != 1)
                return std::format("quantity open at ({}, {})", o.x, o.y);
        } else {
            names = "move_money,update_money_labels,";
            const Json& m = calls[0];
            if (m["from"].i32() != o.from || m["to"].i32() != o.to || m["from_coin"].u32() != o.from_coin ||
                m["to_coin"].u32() != o.to_coin || m["amount"].i32() != o.amount || m["quiet"].i32() != 0)
                return std::format("move_money({}, {}, {}, {}, {})", o.from, o.to, o.from_coin, o.to_coin, o.amount);
        }
        if (pending != out["pending_coin"].i32()) return "pending coin";
        if (names != call_names(calls)) return "calls " + names + " original " + call_names(calls);
        return "";
    });
}
