#pragma once

// playprobe's bank checks (included by tools/playprobe.cpp):
//   --bank TEXT      walks (straight, at run speed, through walls) to the
//                    banker whose name contains TEXT ("any": the nearest
//                    banker), opens the bank (CBankWnd) and runs, in order:
//                    a coin deposit and withdrawal (1 of the most valuable
//                    coin held), the first plain general-slot item into the
//                    first free bank slot (with the Auto button) and back,
//                    the first general item into shared bank slot 0 and back, a split of the first
//                    stack of 2 or more (one onto the cursor, into a free
//                    general slot, then picked up and merged back), and
//                    finally walks 40 units away and checks that the window
//                    closed itself.
//   --bank-stay 1    only walks to the banker and opens the bank (camps there)
//   --bank-keep 1    leaves the coin and the item in the bank and the stack split (to check them after relogging)
//   --bank none      walks nowhere: only reports the bank at zone-in and at the end
//   --bank-file 1    reads and saves the bank as last seen in the character's bank.dat (game/bank_snapshot.h), as
//                    the client does; every report says whether the bank is known and what the copy holds

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <optional>
#include <string>

#include <glm/geometric.hpp>

#include "game/coords.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"
#include "net/protocol/rof2/opcode_map.h"

namespace hailstone::probe {

class BankProbe {
public:
    using Clock = std::chrono::steady_clock;

    bool option(const char* name, const char* value) {
        if (!std::strcmp(name, "--bank")) want_ = value;
        else if (!std::strcmp(name, "--bank-keep")) keep_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--bank-stay")) stay_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--bank-file")) file_ = std::atoi(value) != 0;
        else return false;
        return true;
    }

    void tap(const net::AppPacket& p) {
        if (want_.empty()) return;
        net::rof2::Op op = net::rof2::from_wire(p.opcode);
        if (op == net::rof2::Op::MoveItem || op == net::rof2::Op::MoneyUpdate || op == net::rof2::Op::DeleteItem || op == net::rof2::Op::MoveCoin) {
            std::printf("  BANK: got %s (%zu bytes):", net::rof2::op_name(op), p.data.size());
            for (std::size_t i = 0; i < p.data.size() && i < 32; ++i) std::printf(" %02x", std::to_integer<unsigned>(p.data[i]));
            std::printf("\n");
        } else if (op == net::rof2::Op::ItemPacket) {
            if (auto ip = net::decode_item_packet(p))
                std::printf("  BANK: item packet %#x: %s x%u at %d/%d/%d\n", ip->type, ip->item.name.c_str(), ip->item.stack_count,
                            ip->item.slot.type, ip->item.slot.slot, ip->item.slot.sub);
        }
    }

    void configure(net::ConnectionOptions& o) const { o.bank_snapshot_file = file_; }

    void zone_in(net::GameConnection& conn) {
        if (want_.empty()) return;
        if (const auto* me = conn.world().player()) pos_ = me->position;
        phase_ = want_ == "none" ? 99 : 1;
        since_ = Clock::now();
        dump(conn, "at zone-in");
    }

    bool busy() const { return !want_.empty() && phase_ != 0 && phase_ != 99; }

    void step(net::GameConnection& conn) {
        if (!busy()) return;
        auto now = Clock::now();
        float dt = std::min(0.1f, std::chrono::duration<float>(now - last_).count());
        last_ = now;
        if (now - since_ > std::chrono::seconds(70)) {
            std::printf("  BANK: gave up in phase %d\n", phase_);
            phase_ = 99;
            return;
        }
        if (now < wait_) return;
        auto pause = [&](int ms) { wait_ = now + std::chrono::milliseconds(ms); };
        switch (phase_) {
        case 1: { // find the banker
            if (now - since_ < std::chrono::seconds(3)) return;
            float best_d = 1e30f;
            for (const auto& [id, s] : conn.world().spawns) {
                if (!s.is_npc || s.is_corpse || s.class_id != 40) continue;
                if (want_ != "any" && s.name.find(want_) == std::string::npos) continue;
                float d = glm::distance(s.position, pos_);
                if (d < best_d) { best_d = d; banker_ = id; }
            }
            if (!banker_) {
                std::printf("  BANK: no banker \"%s\" here\n", want_.c_str());
                phase_ = 99;
                return;
            }
            std::printf("  BANK: walking to %s (%u), %.0f units\n", conn.world().spawns.at(banker_).name.c_str(), banker_, best_d);
            phase_ = 2;
            return;
        }
        case 2: { // walk
            auto it = conn.world().spawns.find(banker_);
            if (it == conn.world().spawns.end()) { phase_ = 99; return; }
            if (walk_to(conn, it->second.position, 8.0f, dt)) {
                conn.set_target(banker_);
                phase_ = 3;
                pause(1500);
            }
            return;
        }
        case 3:
            conn.open_bank(banker_);
            if (!conn.bank()) {
                std::printf("  BANK: the bank did not open\n");
                phase_ = 99;
                return;
            }
            dump(conn, "bank open");
            phase_ = stay_ ? 99 : 4;
            pause(500);
            return;
        case 4: { // coin: deposit 1 of the most valuable coin held
            const auto& c = conn.coins();
            coin_type_ = c.platinum ? 3 : c.gold ? 2 : c.silver ? 1 : c.copper ? 0 : -1;
            if (coin_type_ < 0) {
                std::printf("  BANK: no coin to deposit\n");
                phase_ = 6;
                return;
            }
            std::printf("  BANK: depositing 1 of coin type %d\n", coin_type_);
            conn.bank_deposit(coin_type_, 1, false);
            dump(conn, "after deposit (local)");
            phase_ = 5;
            pause(2000);
            return;
        }
        case 5:
            dump(conn, "after deposit (server)");
            if (keep_) { phase_ = 6; return; }
            conn.bank_withdraw(coin_type_, 1, false);
            phase_ = 51;
            pause(2000);
            return;
        case 51:
            dump(conn, "after withdrawal");
            phase_ = 6;
            return;
        case 6: { // an item into the bank
            for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s) {
                const net::Item* it = conn.inventory().at(net::ItemSlot::possessions(s));
                if (it && !it->is_bag() && !it->stackable()) { item_from_ = net::ItemSlot::possessions(s); break; }
            }
            for (std::int16_t s = net::kSlotGeneral10; !item_from_ && s >= net::kSlotGeneral1; --s) {
                const net::Item* it = conn.inventory().at(net::ItemSlot::possessions(s));
                if (it && !it->is_bag()) item_from_ = net::ItemSlot::possessions(s);
            }
            if (!item_from_) {
                std::printf("  BANK: no plain general-slot item\n");
                phase_ = 8;
                return;
            }
            for (std::int16_t s = 0; s < conn.bank()->slots; ++s)
                if (!conn.inventory().at({net::kInvBank, s, -1, -1, 0, 0})) { bank_slot_ = {net::kInvBank, s, -1, -1, 0, 0}; break; }
            if (!bank_slot_) { std::printf("  BANK: bank full\n"); phase_ = 8; return; }
            std::printf("  BANK: moving %s from general %d to bank %d\n", conn.inventory().at(*item_from_)->name.c_str(),
                        item_from_->slot, bank_slot_->slot);
            conn.click_slot(*item_from_, true);
            if (keep_) {
                conn.click_slot(*bank_slot_);
            } else {
                std::printf("  BANK: using the Auto button\n");
                conn.auto_bank();
            }
            dump(conn, "item in the bank");
            phase_ = keep_ ? 8 : 7;
            pause(2000);
            return;
        }
        case 7:
            conn.click_slot(*bank_slot_, true);
            conn.click_slot(*item_from_);
            dump(conn, "item back");
            phase_ = 71;
            pause(2000);
            return;
        case 71: { // the shared bank: the first general item (whole stack) into shared slot 0 and back
            shared_from_.reset();
            for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s) {
                const net::Item* it = conn.inventory().at(net::ItemSlot::possessions(s));
                if (it && !it->is_bag()) { shared_from_ = net::ItemSlot::possessions(s); break; }
            }
            net::ItemSlot shared{net::kInvSharedBank, 0, -1, -1, 0, 0};
            if (!shared_from_ || conn.inventory().at(shared)) { phase_ = 8; return; }
            std::printf("  BANK: %s into shared bank slot 0\n", conn.inventory().at(*shared_from_)->name.c_str());
            conn.click_slot(*shared_from_, true);
            conn.click_slot(shared);
            dump(conn, "item in the shared bank");
            phase_ = 72;
            pause(2000);
            return;
        }
        case 72:
            conn.click_slot({net::kInvSharedBank, 0, -1, -1, 0, 0}, true);
            conn.click_slot(*shared_from_);
            dump(conn, "item back from the shared bank");
            phase_ = 8;
            pause(2000);
            return;
        case 8: { // a stack split
            for (const auto& [key, it] : conn.inventory().items()) {
                if (key.first != net::kInvPossessions || key.second < net::kSlotGeneral1 || key.second > net::kSlotGeneral10) continue;
                if (it.stackable() && it.stack_count >= 2) { stack_ = net::ItemSlot::possessions(key.second); break; }
                for (const auto& b : it.contents)
                    if (b.item.stackable() && b.item.stack_count >= 2) {
                        stack_ = net::ItemSlot::possessions(key.second, static_cast<std::int16_t>(b.index));
                        break;
                    }
                if (stack_) break;
            }
            std::optional<net::ItemSlot> free;
            for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s)
                if (!conn.inventory().at(net::ItemSlot::possessions(s))) { free = net::ItemSlot::possessions(s); break; }
            if (!stack_ || !free) {
                std::printf("  BANK: no stack to split or no free slot\n");
                phase_ = 10;
                return;
            }
            free_ = free;
            const net::Item* st = conn.inventory().at(*stack_);
            std::printf("  BANK: splitting 1 off %s x%u at %d/%d\n", st->name.c_str(), st->stack_count, stack_->slot, stack_->sub);
            conn.click_slot(*stack_);
            if (!conn.quantity_request()) { std::printf("  BANK: no quantity dialog\n"); phase_ = 10; return; }
            std::printf("  BANK: quantity dialog, max %u\n", conn.quantity_request()->max);
            conn.finish_quantity(1);
            std::printf("  BANK: cursor %s x%u, source x%u\n", conn.inventory().cursor()->name.c_str(),
                        conn.inventory().cursor()->stack_count, conn.inventory().at(*stack_)->stack_count);
            conn.click_slot(*free_);
            dump(conn, "after split");
            phase_ = keep_ ? 10 : 9;
            pause(2000);
            return;
        }
        case 9: // merge back: ctrl-less pick up of the single item, drop on the stack
            conn.click_slot(*free_, true);
            conn.click_slot(*stack_);
            std::printf("  BANK: merged back: source x%u, free slot %s, cursor %s\n", conn.inventory().at(*stack_)->stack_count,
                        conn.inventory().at(*free_) ? "used" : "empty", conn.inventory().cursor() ? "full" : "empty");
            dump(conn, "after merge");
            phase_ = 10;
            pause(2000);
            return;
        case 10: { // walk away
            auto it = conn.world().spawns.find(banker_);
            if (it == conn.world().spawns.end()) { phase_ = 99; return; }
            glm::vec3 d = pos_ - it->second.position;
            d.z = 0;
            away_ = it->second.position + (glm::length(d) > 0.1f ? d / glm::length(d) : glm::vec3(1, 0, 0)) * 40.0f;
            away_.z = pos_.z;
            phase_ = 11;
            return;
        }
        case 11:
            if (walk_to(conn, away_, 0.5f, dt)) {
                phase_ = 12;
                pause(1500);
            }
            return;
        case 12:
            std::printf("  BANK: %.0f units from the banker, window %s\n",
                        glm::distance(pos_, conn.world().spawns.at(banker_).position), conn.bank() ? "STILL OPEN" : "closed");
            dump(conn, "at the end");
            phase_ = 99;
            return;
        default: return;
        }
    }

    void finish(const net::GameConnection& conn) {
        if (!want_.empty()) dump(conn, "final");
    }

private:
    bool walk_to(net::GameConnection& conn, const glm::vec3& to, float reach, float dt) {
        glm::vec3 d = to - pos_;
        d.z = 0;
        float len = glm::length(d);
        glm::vec3 v(0.0f);
        if (len > reach) {
            v = d / len * 30.0f;
            pos_ += len - reach > 30.0f * dt ? v * dt : d / len * (len - reach);
        }
        pos_.z = to.z; // as shop_probe: stand at the target's height
        conn.set_player_motion(pos_, game::heading_to(pos_, to), v, len > reach);
        if (len <= reach + 0.5f) {
            conn.set_player_motion(pos_, game::heading_to(pos_, to), glm::vec3(0.0f), false);
            return true;
        }
        return false;
    }

    static void dump(const net::GameConnection& conn, const char* when) {
        const auto& c = conn.coins();
        const auto& b = conn.bank_coins();
        const auto& k = conn.cursor_coins();
        std::printf("  BANK [%s]: carried %up %ug %us %uc, cursor %u/%u/%u/%u, bank %up %ug %us %uc, shared %up\n", when,
                    c.platinum, c.gold, c.silver, c.copper, k[3], k[2], k[1], k[0], b.platinum, b.gold, b.silver, b.copper,
                    b.shared_platinum);
        // the bank as last seen (game/bank_snapshot.h): listed in the bank's place while the bank is not known
        const auto& seen = conn.bank_seen();
        std::printf("    bank known: %s; as last seen: %zu top-level items, %s\n", conn.bank_live() ? "yes" : "no",
                    seen.items.size(),
                    seen.seen ? std::format("{} s ago", std::time(nullptr) - seen.seen).c_str() : "never seen");
        for (const auto& it : seen.items) {
            std::printf("      seen %d/%d: %s x%u%s\n", it.slot.type, it.slot.slot, it.name.c_str(), it.stack_count,
                        it.is_bag() ? " (bag)" : "");
            for (const auto& bi : it.contents) std::printf("        [%u] %s x%u\n", bi.index, bi.item.name.c_str(), bi.item.stack_count);
        }
        for (const auto& [key, it] : conn.inventory().items()) {
            if (key.first != net::kInvBank && key.first != net::kInvSharedBank &&
                !(key.first == net::kInvPossessions && key.second >= net::kSlotGeneral1))
                continue;
            std::printf("    %d/%d: %s x%u%s\n", key.first, key.second, it.name.c_str(), it.stack_count, it.is_bag() ? " (bag)" : "");
            for (const auto& bi : it.contents)
                if (it.is_bag()) std::printf("      [%u] %s x%u\n", bi.index, bi.item.name.c_str(), bi.item.stack_count);
        }
    }

    std::string want_;
    bool keep_ = false;
    bool stay_ = false;
    bool file_ = false;
    int phase_ = 0;
    std::uint32_t banker_ = 0;
    int coin_type_ = -1;
    glm::vec3 pos_{0.0f}, away_{0.0f};
    std::optional<net::ItemSlot> item_from_, bank_slot_, stack_, free_, shared_from_;
    Clock::time_point since_{}, last_{}, wait_{};
};

} // namespace hailstone::probe
