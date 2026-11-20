#pragma once

// playprobe's item checks (included by tools/playprobe.cpp):
//   --items 1          print the inventory after zone-in (and at the end)
//   --dump-inventory F write the raw OP_CharInventory payload to file F
//   --move-test 1      pick up the first general-slot item, put it into a
//                      free general slot, equip it if it fits an empty worn
//                      slot, and put it back; reports any server correction
//   --link-test 1      say a link to an inventory item, click the echoed link
//                      (OP_ItemLinkClick) and print the item info that comes back
//   --loot 1           with --fight-nearest: loot the corpse (first item to the
//                      cursor, then auto inventory, the rest with loot all)
//   --destroy-test 1   with --loot: destroy the first looted item again

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>

#include "game/item_links.h"
#include "net/connection/game_connection.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "net/protocol/rof2/opcode_map.h"
#include "ui/item_windows.h"

namespace hailstone::probe {

class ItemProbe {
public:
    using Clock = std::chrono::steady_clock;

    bool option(const char* name, const char* value) {
        if (!std::strcmp(name, "--items")) show_items_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--dump-inventory")) dump_file_ = value;
        else if (!std::strcmp(name, "--move-test")) move_test_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--link-test")) link_test_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--loot")) loot_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--destroy-test")) destroy_test_ = std::atoi(value) != 0;
        else return false;
        return true;
    }

    // Every zone packet (ConnectionOptions::zone_packet_tap).
    void tap(const net::AppPacket& p) {
        net::rof2::Op op = net::rof2::from_wire(p.opcode);
        if (op == net::rof2::Op::CharInventory && !dump_file_.empty() && !dumped_) {
            std::ofstream f(dump_file_, std::ios::binary);
            f.write(reinterpret_cast<const char*>(p.data.data()), static_cast<std::streamsize>(p.data.size()));
            std::printf("  ITEMS: wrote OP_CharInventory (%zu bytes) to %s\n", p.data.size(), dump_file_.c_str());
            dumped_ = true;
        }
        bool watching = phase_ != Phase::Idle;
        if (watching && (op == net::rof2::Op::ItemPacket || op == net::rof2::Op::DeleteItem || op == net::rof2::Op::MoveItem ||
                         op == net::rof2::Op::DeleteCharge)) {
            // A correction touches a slot the test uses (other item packets,
            // such as personal-loot deliveries, are not).
            auto mine = [&](const net::ItemSlot& s) {
                return s.type == net::kInvPossessions &&
                       (s.slot == net::kSlotCursor || s.slot == net::kSlotPrimary || (from_ && s.slot == *from_) ||
                        (to_ && s.slot == *to_) || (equipped_ && s.slot == *equipped_) ||
                        (destroy_slot_ && s.slot == *destroy_slot_));
            };
            bool touches = true;
            if (op == net::rof2::Op::ItemPacket) {
                if (auto ip = net::decode_item_packet(p)) touches = mine(ip->item.slot);
            } else if (auto m = net::decode_move_item(p)) {
                touches = mine(m->from) || mine(m->to);
            }
            if (touches) ++corrections_;
            std::printf("  ITEMS: server %s (%zu bytes)%s", net::rof2::op_name(op), p.data.size(), touches ? "" : " (not a test slot)");
            if (auto ip = op == net::rof2::Op::ItemPacket ? net::decode_item_packet(p) : std::nullopt)
                std::printf(": type %#x %s at %d/%d/%d", ip->type, ip->item.name.c_str(), ip->item.slot.type,
                            ip->item.slot.slot, ip->item.slot.sub);
            else if (auto m = net::decode_move_item(p))
                std::printf(": from %d/%d/%d to %d/%d/%d count %d", m->from.type, m->from.slot, m->from.sub, m->to.type,
                            m->to.slot, m->to.sub, static_cast<int>(m->count));
            std::printf("\n");
        }
        if (op == net::rof2::Op::LootItem || op == net::rof2::Op::MoneyOnCorpse || op == net::rof2::Op::LootComplete ||
            op == net::rof2::Op::LootRequest || op == net::rof2::Op::ItemLinkResponse)
            std::printf("  ITEMS: got %s (%zu bytes)\n", net::rof2::op_name(op), p.data.size());
    }

    // A chat line (raw, with links): prints it readable and remembers links.
    void chat(const std::string& raw) {
        bool has_link = false;
        for (const auto& seg : game::split_links(raw)) {
            if (!seg.link) continue;
            has_link = true;
            last_link_ = seg.link;
            std::printf("  ITEMS: link in chat: \"%s\" item %u (%s ids, hash %08X)\n", seg.link->text.c_str(),
                        seg.link->item_id, seg.link->wide ? "8-digit" : "5-digit", seg.link->hash);
        }
        (void)has_link;
    }

    void print_inventory(const net::GameConnection& conn, const char* when) {
        const auto& inv = conn.inventory();
        std::printf("  ITEMS: inventory %s: %zu top-level items, coin %upp %ugp %usp %ucp\n", when, inv.items().size(),
                    conn.coins().platinum, conn.coins().gold, conn.coins().silver, conn.coins().copper);
        for (const auto& [key, it] : inv.items()) {
            const char* where = key.first == net::kInvPossessions ? net::possessions_slot_name(key.second)
                                : key.first == net::kInvBank      ? "bank"
                                                                  : "other";
            std::printf("    [%d/%2d %-12s] %s (id %u, icon %u%s%s)\n", key.first, key.second, where, it.name.c_str(),
                        it.id, it.icon, it.stackable() ? (", x" + std::to_string(it.stack_count)).c_str() : "",
                        it.is_bag() ? (", bag " + std::to_string(it.bag_slots) + " slots").c_str() : "");
            for (const auto& b : it.contents)
                std::printf("        bag slot %u: %s (id %u%s)\n", b.index, b.item.name.c_str(), b.item.id,
                            b.item.stackable() ? (", x" + std::to_string(b.item.stack_count)).c_str() : "");
        }
    }

    void zone_in(net::GameConnection& conn) {
        if (show_items_ || move_test_ || link_test_ || loot_) print_inventory(conn, "at zone-in");
        if (const auto* held = conn.inventory().cursor(); held && (show_items_ || move_test_ || loot_)) {
            std::printf("  ITEMS: the cursor holds %s from last time; auto inventory\n", held->name.c_str());
            cursor_cleanup_ = true;
        }
        zone_in_at_ = Clock::now();
        if (move_test_) phase_ = Phase::MovePick;
        else if (link_test_) phase_ = Phase::LinkSay;
        else if (destroy_test_ && !loot_) phase_ = Phase::DestroyPick;
        next_at_ = Clock::now() + std::chrono::seconds(4);
    }

    // The fight finished: loot `corpse` (0 = none).
    void fight_over(std::uint32_t corpse) {
        if (!loot_ || !corpse) return;
        corpse_ = corpse;
        loot_pending_ = true;
    }

    bool busy() const { return phase_ != Phase::Idle || loot_pending_ || loot_phase_ != LootPhase::Idle; }

    void step(net::GameConnection& conn) {
        auto now = Clock::now();
        if (cursor_cleanup_ && conn.state() == net::ConnectionState::InZone && now > zone_in_at_ + std::chrono::seconds(2)) {
            cursor_cleanup_ = false;
            conn.auto_inventory();
        }
        while (auto it = conn.take_item_display()) {
            std::printf("  ITEMS: item info for %s (id %u):\n", it->name.c_str(), it->id);
            for (const auto& line : ui::item_info_lines(*it)) std::printf("      %s\n", line.c_str());
            if (phase_ == Phase::LinkWait) phase_ = Phase::Idle;
        }
        step_moves(conn, now);
        step_loot(conn, now);
    }

    void finish(const net::GameConnection& conn) {
        if (show_items_ || move_test_ || loot_) print_inventory(conn, "at the end");
        if (move_test_) std::printf("  ITEMS: move test %s (%d server corrections)\n", move_ok_ ? "OK" : "INCOMPLETE", corrections_);
    }

private:
    enum class Phase { Idle, MovePick, MovePut, MoveEquip, MoveBack, MovePutBack, MoveCheck, WornOff, WornOn, WornCheck, LinkSay, LinkWait, DestroyPick, DestroyCheck };
    enum class LootPhase { Idle, Requested, Looting, Closing };

    static net::ItemSlot gen(std::int16_t s) { return net::ItemSlot::possessions(s); }

    void step_moves(net::GameConnection& conn, Clock::time_point now) {
        if (phase_ == Phase::Idle || now < next_at_) return;
        const auto& inv = conn.inventory();
        auto later = [&](int ms) { next_at_ = now + std::chrono::milliseconds(ms); };
        switch (phase_) {
        case Phase::MovePick: {
            // The first general-slot item that is not a bag.
            for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10 && !from_; ++s)
                if (const auto* it = inv.at(gen(s)); it && !it->is_bag()) from_ = s;
            for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10 && !to_; ++s)
                if (!inv.at(gen(s))) to_ = s;
            if (!from_ || !to_) {
                std::printf("  ITEMS: move test: no loose item or no free general slot\n");
                phase_ = link_test_ ? Phase::LinkSay : Phase::Idle;
                break;
            }
            moved_name_ = inv.at(gen(*from_))->name;
            std::printf("  ITEMS: move test: pick up %s from %s\n", moved_name_.c_str(), net::possessions_slot_name(*from_));
            conn.click_slot(gen(*from_));
            std::printf("  ITEMS: cursor now holds %s\n", inv.cursor() ? inv.cursor()->name.c_str() : "nothing");
            phase_ = Phase::MovePut;
            later(1500);
            break;
        }
        case Phase::MovePut:
            std::printf("  ITEMS: move test: put it down in %s\n", net::possessions_slot_name(*to_));
            conn.click_slot(gen(*to_));
            phase_ = Phase::MoveEquip;
            later(2000);
            break;
        case Phase::MoveEquip: {
            const auto* it = inv.at(gen(*to_));
            equipped_.reset();
            if (it) {
                for (std::int16_t s = 0; s < net::kEquipmentSlots && !equipped_; ++s)
                    if (!inv.at(gen(s)) && (it->equip_slots & (1u << s)) && it->item_class == 0) equipped_ = s;
            }
            if (equipped_) {
                std::printf("  ITEMS: move test: equip %s into %s\n", it->name.c_str(), net::possessions_slot_name(*equipped_));
                conn.click_slot(gen(*to_));
                conn.click_slot(gen(*equipped_));
                std::printf("  ITEMS: %s now holds %s\n", net::possessions_slot_name(*equipped_),
                            inv.at(gen(*equipped_)) ? inv.at(gen(*equipped_))->name.c_str() : "nothing");
            } else {
                std::printf("  ITEMS: move test: the item has no free worn slot to try\n");
            }
            phase_ = Phase::MoveBack;
            later(2500);
            break;
        }
        case Phase::MoveBack: {
            net::ItemSlot at = equipped_ ? gen(*equipped_) : gen(*to_);
            std::printf("  ITEMS: move test: back to %s\n", net::possessions_slot_name(*from_));
            conn.click_slot(at);
            phase_ = Phase::MovePutBack;
            later(1000);
            break;
        }
        case Phase::MovePutBack:
            conn.click_slot(gen(*from_));
            phase_ = Phase::MoveCheck;
            later(2500);
            break;
        case Phase::MoveCheck: {
            const auto* it = inv.at(gen(*from_));
            move_ok_ = it && it->name == moved_name_ && !inv.cursor() && corrections_ == 0;
            std::printf("  ITEMS: move test: %s is back in %s: %s; cursor %s; %d server corrections\n",
                        moved_name_.c_str(), net::possessions_slot_name(*from_), it ? "yes" : "NO",
                        inv.cursor() ? "NOT empty" : "empty", corrections_);
            phase_ = Phase::WornOff;
            later(500);
            break;
        }
        case Phase::WornOff: {
            // Unequip the primary weapon into a free general slot, then equip it again.
            const auto* weapon = inv.at(gen(net::kSlotPrimary));
            if (!weapon || !to_ || inv.at(gen(*to_))) {
                phase_ = link_test_ ? Phase::LinkSay : Phase::Idle;
                break;
            }
            worn_name_ = weapon->name;
            std::printf("  ITEMS: move test: unequip %s into %s\n", worn_name_.c_str(), net::possessions_slot_name(*to_));
            conn.click_slot(gen(net::kSlotPrimary));
            conn.click_slot(gen(*to_));
            phase_ = Phase::WornOn;
            later(2500);
            break;
        }
        case Phase::WornOn:
            std::printf("  ITEMS: move test: %s holds %s; equip it again\n", net::possessions_slot_name(*to_),
                        inv.at(gen(*to_)) ? inv.at(gen(*to_))->name.c_str() : "nothing");
            conn.click_slot(gen(*to_));
            conn.click_slot(gen(net::kSlotPrimary));
            phase_ = Phase::WornCheck;
            later(2500);
            break;
        case Phase::WornCheck: {
            const auto* it = inv.at(gen(net::kSlotPrimary));
            bool ok = it && it->name == worn_name_ && !inv.cursor() && corrections_ == 0;
            move_ok_ = move_ok_ && ok;
            std::printf("  ITEMS: move test: %s back in Primary: %s; %d server corrections\n", worn_name_.c_str(),
                        ok ? "yes" : "NO", corrections_);
            phase_ = link_test_ ? Phase::LinkSay : destroy_test_ && !loot_ ? Phase::DestroyPick : Phase::Idle;
            later(500);
            break;
        }
        case Phase::LinkSay: {
            const net::Item* item = nullptr;
            for (const auto& [key, it] : inv.items())
                if (key.first == net::kInvPossessions) { item = &it; break; }
            if (!item) {
                std::printf("  ITEMS: link test: no item to link\n");
                phase_ = Phase::Idle;
                break;
            }
            std::string link = game::make_item_link(item->id, item->name, true);
            std::printf("  ITEMS: link test: saying a link to %s (id %u)\n", item->name.c_str(), item->id);
            conn.say("link test " + link);
            phase_ = Phase::LinkWait;
            link_asked_ = false;
            later(1500);
            break;
        }
        case Phase::LinkWait:
            if (!link_asked_ && last_link_) {
                std::printf("  ITEMS: clicking link to %s (item %u)\n", last_link_->text.c_str(), last_link_->item_id);
                conn.request_item(*last_link_);
                link_asked_ = true;
                later(4000);
            } else {
                std::printf("  ITEMS: link test: %s\n", link_asked_ ? "no OP_ItemLinkResponse" : "no link came back");
                phase_ = Phase::Idle;
            }
            break;
        case Phase::DestroyPick: {
            // The last loose general-slot item: onto the cursor, then destroyed.
            destroy_slot_.reset();
            for (std::int16_t s = net::kSlotGeneral10; s >= net::kSlotGeneral1 && !destroy_slot_; --s)
                if (const auto* it = inv.at(gen(s)); it && !it->is_bag()) destroy_slot_ = s;
            if (!destroy_slot_) {
                phase_ = Phase::Idle;
                break;
            }
            destroy_name_ = inv.at(gen(*destroy_slot_))->name;
            std::printf("  ITEMS: destroy test: pick up %s from %s and destroy it\n", destroy_name_.c_str(),
                        net::possessions_slot_name(*destroy_slot_));
            conn.click_slot(gen(*destroy_slot_));
            conn.destroy_cursor();
            phase_ = Phase::DestroyCheck;
            later(3000);
            break;
        }
        case Phase::DestroyCheck:
            std::printf("  ITEMS: destroy test: %s slot %s, cursor %s, %d server corrections\n", destroy_name_.c_str(),
                        inv.at(gen(*destroy_slot_)) ? "NOT empty" : "empty", inv.cursor() ? "NOT empty" : "empty",
                        corrections_);
            phase_ = Phase::Idle;
            break;
        case Phase::Idle:
            break;
        }
    }

    void step_loot(net::GameConnection& conn, Clock::time_point now) {
        if (loot_pending_ && loot_phase_ == LootPhase::Idle) {
            loot_pending_ = false;
            std::printf("  ITEMS: /loot corpse %u\n", corpse_);
            conn.loot(corpse_);
            loot_phase_ = LootPhase::Requested;
            loot_at_ = now + std::chrono::seconds(3);
        }
        const auto& s = conn.loot_session();
        switch (loot_phase_) {
        case LootPhase::Requested:
            if (!s && now > loot_at_) {
                std::printf("  ITEMS: the loot request was refused or closed\n");
                loot_phase_ = LootPhase::Idle;
            } else if (s && (s->complete || now > loot_at_)) {
                std::printf("  ITEMS: loot window: %s, %zu item(s), money %s\n", s->corpse_name.c_str(), s->items.size(),
                            game::coin_text(s->money.platinum, s->money.gold, s->money.silver, s->money.copper).c_str());
                for (const auto& [slot, it] : s->items)
                    std::printf("      corpse slot %d: %s (id %u%s)\n", slot, it.name.c_str(), it.id,
                                it.stackable() ? (", x" + std::to_string(it.stack_count)).c_str() : "");
                if (s->items.empty()) {
                    conn.end_loot();
                    loot_phase_ = LootPhase::Closing;
                } else {
                    // First item onto the cursor, then auto-inventory it.
                    auto first = s->items.begin();
                    first_loot_ = first->second.name;
                    std::printf("  ITEMS: loot %s to the cursor\n", first_loot_.c_str());
                    conn.loot_item(first->first, false);
                    loot_phase_ = LootPhase::Looting;
                }
                loot_at_ = now + std::chrono::seconds(2);
            }
            break;
        case LootPhase::Looting:
            if (now < loot_at_) break;
            if (conn.inventory().cursor() && !auto_inv_done_) {
                std::printf("  ITEMS: cursor holds %s; auto inventory\n", conn.inventory().cursor()->name.c_str());
                if (destroy_test_) {
                    std::printf("  ITEMS: destroy test: destroying %s\n", conn.inventory().cursor()->name.c_str());
                    conn.destroy_cursor();
                } else {
                    conn.auto_inventory();
                }
                auto_inv_done_ = true;
                loot_at_ = now + std::chrono::seconds(1);
                break;
            }
            if (s && !s->items.empty() && !loot_all_done_) {
                std::printf("  ITEMS: loot all (%zu item(s))\n", s->items.size());
                conn.loot_all();
                loot_all_done_ = true;
                loot_at_ = now + std::chrono::seconds(3);
                break;
            }
            std::printf("  ITEMS: done looting\n");
            conn.end_loot();
            loot_phase_ = LootPhase::Closing;
            loot_at_ = now + std::chrono::seconds(3);
            break;
        case LootPhase::Closing:
            if (!s || now > loot_at_) {
                std::printf("  ITEMS: loot window %s\n", s ? "still open" : "closed (OP_LootComplete)");
                loot_phase_ = LootPhase::Idle;
            }
            break;
        case LootPhase::Idle:
            break;
        }
    }

    bool show_items_ = false, move_test_ = false, link_test_ = false, loot_ = false, destroy_test_ = false;
    std::string dump_file_;
    bool dumped_ = false;
    bool cursor_cleanup_ = false;
    Phase phase_ = Phase::Idle;
    Clock::time_point zone_in_at_{}, next_at_{};
    std::optional<std::int16_t> from_, to_, equipped_;
    std::string moved_name_, worn_name_, destroy_name_;
    std::optional<std::int16_t> destroy_slot_;
    int corrections_ = 0;
    bool move_ok_ = false;
    std::optional<game::ItemLink> last_link_;
    bool link_asked_ = false;
    std::uint32_t corpse_ = 0;
    bool loot_pending_ = false;
    LootPhase loot_phase_ = LootPhase::Idle;
    Clock::time_point loot_at_{};
    std::string first_loot_;
    bool auto_inv_done_ = false, loot_all_done_ = false;
};

} // namespace hailstone::probe
