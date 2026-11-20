#pragma once

// playprobe's merchant, dialogue and trade checks (included by tools/playprobe.cpp).
// Each walks (straight, at run speed, through walls) to an NPC whose name
// contains the given text, then:
//   --shop TEXT        opens the merchant (TEXT "any": the nearest merchant),
//                      prints its stock and prices, sells --sell TEXT (the
//                      first inventory item whose name contains it, --sell-qty
//                      N, default 1), buys --buy TEXT (--buy-qty N), sells the
//                      bought item back with --sell-back 1, and closes
//   --list-merchants 1 prints every merchant in the zone
//   --hail TEXT        targets and hails the NPC, and clicks the first say
//                      link of its answer with --saylink 1
//   --handin TEXT      gives it the inventory item --handin-item ID (a quest
//                      hand-in: OP_TradeRequest, the item into the give window,
//                      Give) and reports what comes back
//   --trade-with NAME  player trade: walks to the player NAME, offers the
//                      first tradeable general-slot item and --trade-coin N
//                      copper, and presses Trade once the partner is ready
//   --trade-accept 1   accepts incoming trades: offers --trade-coin N copper
//                      and presses Trade once the partner is ready

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#include <glm/geometric.hpp>

#include "game/coords.h"
#include "game/item_links.h"
#include "game/merchant.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"
#include "net/protocol/rof2/opcode_map.h"

namespace hailstone::probe {

class ShopProbe {
public:
    using Clock = std::chrono::steady_clock;

    bool option(const char* name, const char* value) {
        if (!std::strcmp(name, "--shop")) shop_ = value;
        else if (!std::strcmp(name, "--list-merchants")) list_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--buy")) buy_ = value;
        else if (!std::strcmp(name, "--buy-qty")) buy_qty_ = static_cast<std::uint32_t>(std::atoi(value));
        else if (!std::strcmp(name, "--sell")) sell_ = value;
        else if (!std::strcmp(name, "--sell-qty")) sell_qty_ = static_cast<std::uint32_t>(std::atoi(value));
        else if (!std::strcmp(name, "--sell-back")) sell_back_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--hail")) hail_ = value;
        else if (!std::strcmp(name, "--saylink")) saylink_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--handin")) handin_ = value;
        else if (!std::strcmp(name, "--handin-item")) handin_item_ = static_cast<std::uint32_t>(std::atoi(value));
        else if (!std::strcmp(name, "--trade-with")) trade_with_ = value;
        else if (!std::strcmp(name, "--trade-accept")) trade_accept_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--trade-coin")) trade_coin_ = static_cast<std::uint32_t>(std::atoi(value));
        else return false;
        return true;
    }

    void tap(const net::AppPacket& p) {
        if (!active()) return;
        net::rof2::Op op = net::rof2::from_wire(p.opcode);
        switch (op) {
        case net::rof2::Op::ShopRequest: case net::rof2::Op::ShopPlayerBuy: case net::rof2::Op::ShopPlayerSell: case net::rof2::Op::ShopEnd:
        case net::rof2::Op::ShopEndConfirm: case net::rof2::Op::ShopDelItem: case net::rof2::Op::TradeRequest: case net::rof2::Op::TradeRequestAck:
        case net::rof2::Op::TradeAcceptClick: case net::rof2::Op::TradeCoins: case net::rof2::Op::TradeBusy: case net::rof2::Op::CancelTrade:
        case net::rof2::Op::FinishTrade: case net::rof2::Op::FinishWindow: case net::rof2::Op::FinishWindow2: case net::rof2::Op::MoneyUpdate:
        case net::rof2::Op::ExpUpdate: case net::rof2::Op::MoveItem: case net::rof2::Op::DeleteItem: {
            std::printf("  SHOP: got %s (%zu bytes):", net::rof2::op_name(op), p.data.size());
            for (std::size_t i = 0; i < p.data.size() && i < 32; ++i) std::printf(" %02x", std::to_integer<unsigned>(p.data[i]));
            std::printf("\n");
            break;
        }
        case net::rof2::Op::ItemPacket:
            if (auto ip = net::decode_item_packet(p); ip && ip->type != net::kItemPacketMerchant)
                std::printf("  SHOP: item packet type %#x: %s x%u at %d/%d/%d\n", ip->type, ip->item.name.c_str(),
                            ip->item.stack_count, ip->item.slot.type, ip->item.slot.slot, ip->item.slot.sub);
            break;
        default: break;
        }
    }

    void chat(const std::string& raw) {
        if (!saylink_ || saylink_clicked_ || phase_ != Phase::Hailed) return;
        for (const auto& seg : game::split_links(raw)) {
            if (!seg.link) continue;
            std::printf("  SHOP: say link \"%s\" (item %u, phrase id %u)\n", seg.link->text.c_str(), seg.link->item_id,
                        seg.link->augments[0]);
            pending_link_ = seg.link;
            return;
        }
    }

    void zone_in(net::GameConnection& conn) {
        if (list_) {
            for (const auto& [id, s] : conn.world().spawns)
                if (s.is_npc && s.class_id == 41)
                    std::printf("  SHOP: merchant %u %s L%u at %.1f %.1f %.1f\n", id, s.name.c_str(), s.level, s.position.x,
                                s.position.y, s.position.z);
        }
        if (!active()) return;
        coins_at_start_ = conn.coins();
        print_coins(conn, "at zone-in");
        if (const auto* me = conn.world().player()) pos_ = me->position;
        phase_ = Phase::Start;
        since_ = Clock::now();
    }

    bool busy() const { return active() && phase_ != Phase::Done; }

    void step(net::GameConnection& conn) {
        if (!active() || phase_ == Phase::Done || phase_ == Phase::Idle) return;
        auto now = Clock::now();
        float dt = std::min(0.1f, std::chrono::duration<float>(now - last_step_).count());
        last_step_ = now;
        if (now - since_ > std::chrono::seconds(240)) {
            std::printf("  SHOP: gave up in phase %d\n", static_cast<int>(phase_));
            phase_ = Phase::Done;
            return;
        }
        const auto& w = conn.world();
        switch (phase_) {
        case Phase::Start: {
            if (now - since_ < std::chrono::seconds(3)) return;
            if (trade_accept_ && shop_.empty() && hail_.empty() && handin_.empty() && trade_with_.empty()) {
                phase_ = Phase::WaitTradeRequest;
                std::printf("  SHOP: waiting for a trade request\n");
                return;
            }
            const std::string& want = !shop_.empty() ? shop_ : !hail_.empty() ? hail_ : !handin_.empty() ? handin_ : trade_with_;
            target_ = find(conn, want, !shop_.empty(), !trade_with_.empty());
            if (!target_) {
                std::printf("  SHOP: nobody named \"%s\" here\n", want.c_str());
                phase_ = Phase::Done;
                return;
            }
            const auto& s = w.spawns.at(target_);
            std::printf("  SHOP: walking to %s (id %u, class %u) %.0f units away\n", s.name.c_str(), target_, s.class_id,
                        glm::distance(s.position, pos_));
            phase_ = Phase::Walk;
            return;
        }
        case Phase::Walk: {
            auto it = w.spawns.find(target_);
            if (it == w.spawns.end()) { phase_ = Phase::Done; return; }
            glm::vec3 to = it->second.position;
            glm::vec3 d = to - pos_;
            d.z = 0;
            float len = glm::length(d);
            constexpr float kReach = 8.0f;
            glm::vec3 v(0.0f);
            if (len > kReach) {
                v = d / len * 30.0f;
                pos_ += len - kReach > 30.0f * dt ? v * dt : d / len * (len - kReach);
            }
            pos_.z = to.z;
            conn.set_player_motion(pos_, game::heading_to(pos_, to), v, len > kReach);
            if (len <= kReach + 0.5f) {
                conn.set_player_motion(pos_, game::heading_to(pos_, to), glm::vec3(0.0f), false);
                conn.set_target(target_);
                phase_ = Phase::Arrived;
                wait_until_ = now + std::chrono::milliseconds(1500);
            }
            return;
        }
        case Phase::Arrived:
            if (now < wait_until_) return;
            if (!shop_.empty()) {
                conn.open_merchant(target_);
                phase_ = Phase::WaitOpen;
            } else if (!hail_.empty()) {
                conn.hail();
                phase_ = Phase::Hailed;
                wait_until_ = now + std::chrono::seconds(4);
            } else if (!handin_.empty() || !trade_with_.empty()) {
                pick_up(conn, handin_.empty() ? 0 : handin_item_);
                phase_ = Phase::Give;
                wait_until_ = now + std::chrono::seconds(1);
            }
            return;
        case Phase::WaitOpen:
            if (const auto& m = conn.merchant(); m && m->open && now - since_ > std::chrono::seconds(1)) {
                if (wait_until_ < now) wait_until_ = now + std::chrono::seconds(2); // let the stock arrive
                phase_ = Phase::Stock;
            } else if (!conn.merchant()) {
                std::printf("  SHOP: the merchant did not open\n");
                phase_ = Phase::Done;
            }
            return;
        case Phase::Stock: {
            if (now < wait_until_) return;
            const auto& m = *conn.merchant();
            std::printf("  SHOP: %s is open, rate %.4f, tabs %d, %zu items:\n", m.name.c_str(), m.rate, m.tabs, m.items.size());
            for (const auto& [slot, it] : m.items)
                std::printf("    slot %u: %s (id %u) price %u (%s)%s\n", slot, it.name.c_str(), it.id, it.price,
                            game::money_words(it.price).c_str(),
                            it.merchant_slot != 1 ? (" stock " + std::to_string(static_cast<std::int32_t>(it.merchant_slot))).c_str() : "");
            phase_ = Phase::Sell;
            return;
        }
        case Phase::Sell: {
            if (sell_.empty()) { phase_ = Phase::Buy; return; }
            auto slot = find_item(conn, sell_);
            if (!slot) { std::printf("  SHOP: no \"%s\" to sell\n", sell_.c_str()); phase_ = Phase::Buy; return; }
            const net::Item& it = *conn.inventory().at(*slot);
            std::printf("  SHOP: selling %u of %s (value %u): the client shows %d copper\n", sell_qty_, it.name.c_str(), it.value,
                        game::sell_value(it, conn.merchant()->rate, static_cast<std::int32_t>(sell_qty_)));
            conn.merchant_sell(*slot, sell_qty_);
            phase_ = Phase::WaitSell;
            wait_until_ = now + std::chrono::seconds(3);
            return;
        }
        case Phase::WaitSell:
            if (now < wait_until_ || (conn.merchant() && conn.merchant()->pending > 0 && now < wait_until_ + std::chrono::seconds(5))) return;
            print_coins(conn, "after selling");
            phase_ = Phase::Buy;
            return;
        case Phase::Buy: {
            if (buy_.empty() || !conn.merchant()) { phase_ = Phase::Close; return; }
            for (const auto& [slot, it] : conn.merchant()->items) {
                if (it.name.find(buy_) == std::string::npos) continue;
                std::printf("  SHOP: buying %u of %s (slot %u) for %u copper\n", buy_qty_, it.name.c_str(), slot, it.price * buy_qty_);
                bought_ = it.name;
                conn.merchant_buy(slot, buy_qty_);
                phase_ = Phase::WaitBuy;
                wait_until_ = now + std::chrono::seconds(3);
                return;
            }
            std::printf("  SHOP: the merchant has no \"%s\"\n", buy_.c_str());
            phase_ = Phase::Close;
            return;
        }
        case Phase::WaitBuy:
            if (now < wait_until_) return;
            print_coins(conn, "after buying");
            if (auto slot = find_item(conn, bought_)) {
                const auto* it = conn.inventory().at(*slot);
                std::printf("  SHOP: %s is in slot %d/%d (x%u)\n", it->name.c_str(), slot->slot, slot->sub, it->stack_count);
            }
            phase_ = sell_back_ ? Phase::SellBack : Phase::Close;
            return;
        case Phase::SellBack: {
            auto slot = find_item(conn, bought_);
            if (!slot) { phase_ = Phase::Close; return; }
            const net::Item& it = *conn.inventory().at(*slot);
            std::printf("  SHOP: selling back %u of %s: the client shows %d copper\n", buy_qty_, it.name.c_str(),
                        game::sell_value(it, conn.merchant()->rate, static_cast<std::int32_t>(buy_qty_)));
            conn.merchant_sell(*slot, buy_qty_);
            phase_ = Phase::WaitSellBack;
            wait_until_ = now + std::chrono::seconds(3);
            return;
        }
        case Phase::WaitSellBack:
            if (now < wait_until_) return;
            print_coins(conn, "after selling back");
            phase_ = Phase::Close;
            return;
        case Phase::Close:
            conn.close_merchant();
            phase_ = Phase::End;
            wait_until_ = now + std::chrono::seconds(2);
            return;
        case Phase::Hailed:
            if (pending_link_ && !saylink_clicked_) {
                std::printf("  SHOP: clicking the say link \"%s\"\n", pending_link_->text.c_str());
                conn.request_item(*pending_link_);
                saylink_clicked_ = true;
                wait_until_ = now + std::chrono::seconds(4);
            }
            if (now >= wait_until_) phase_ = Phase::Done;
            return;
        case Phase::Give:
            if (now < wait_until_) return;
            if (!conn.inventory().cursor()) {
                std::printf("  SHOP: nothing on the cursor to give\n");
                phase_ = Phase::Done;
                return;
            }
            std::printf("  SHOP: giving %s to %s\n", conn.inventory().cursor()->name.c_str(), w.spawns.at(target_).name.c_str());
            conn.give_to(target_);
            phase_ = Phase::WaitTrade;
            wait_until_ = now + std::chrono::seconds(10);
            return;
        case Phase::WaitTrade: {
            const auto& t = conn.trade();
            if (t && t->open) {
                std::printf("  SHOP: %s window open with %s\n", t->with_npc ? "give" : "trade", t->partner_name.c_str());
                print_trade(conn);
                if (trade_coin_ && !t->with_npc) offer_coin(conn);
                phase_ = t->with_npc ? Phase::PressGive : Phase::WaitPartner;
                wait_until_ = now + std::chrono::seconds(2);
            } else if (now > wait_until_) {
                std::printf("  SHOP: no trade window\n");
                phase_ = Phase::Done;
            }
            return;
        }
        case Phase::PressGive:
            if (now < wait_until_) return;
            std::printf("  SHOP: pressing Give\n");
            conn.trade_accept();
            phase_ = Phase::AfterGive;
            wait_until_ = now + std::chrono::seconds(6);
            return;
        case Phase::AfterGive:
            if (now < wait_until_) return;
            std::printf("  SHOP: after the hand-in: exp %u/330, trade %s\n", conn.player_status().exp,
                        conn.trade() ? "still open" : "closed");
            print_inventory(conn);
            phase_ = Phase::Done;
            return;
        case Phase::WaitTradeRequest:
            if (const auto& t = conn.trade(); t && t->open) {
                std::printf("  SHOP: trade request from %s accepted\n", t->partner_name.c_str());
                if (trade_coin_) offer_coin(conn);
                phase_ = Phase::WaitPartner;
                wait_until_ = now + std::chrono::seconds(60);
            }
            return;
        case Phase::WaitPartner: {
            const auto& t = conn.trade();
            if (!t) {
                std::printf("  SHOP: the trade ended\n");
                print_coins(conn, "after the trade");
                print_inventory(conn);
                phase_ = Phase::Done;
                return;
            }
            if (t->their_ready && !t->my_ready && now - last_print_ > std::chrono::milliseconds(500)) {
                print_trade(conn);
                std::printf("  SHOP: the partner is ready; pressing Trade\n");
                conn.trade_accept();
            }
            if (!t->their_ready && !t->my_ready && !trade_with_.empty() && now - last_print_ > std::chrono::seconds(3)) {
                // The initiator presses first.
                last_print_ = now;
                print_trade(conn);
                std::printf("  SHOP: pressing Trade\n");
                conn.trade_accept();
            }
            return;
        }
        case Phase::End:
            if (now < wait_until_) return;
            print_coins(conn, "at the end");
            phase_ = Phase::Done;
            return;
        default:
            return;
        }
    }

    void finish(const net::GameConnection& conn) {
        if (!active()) return;
        print_coins(conn, "final");
    }

private:
    enum class Phase { Idle, Start, Walk, Arrived, WaitOpen, Stock, Sell, WaitSell, Buy, WaitBuy, SellBack, WaitSellBack, Close,
                       Hailed, Give, WaitTrade, PressGive, AfterGive, WaitTradeRequest, WaitPartner, End, Done };

    bool active() const {
        return !shop_.empty() || !hail_.empty() || !handin_.empty() || !trade_with_.empty() || trade_accept_;
    }

    std::uint32_t find(const net::GameConnection& conn, const std::string& want, bool merchant, bool player) const {
        std::uint32_t best = 0;
        float best_d = 1e30f;
        for (const auto& [id, s] : conn.world().spawns) {
            if (id == conn.world().player_id || s.is_corpse) continue;
            if (player ? s.is_npc : !s.is_npc) continue;
            if (merchant && s.class_id != 41) continue;
            if (!(merchant && want == "any") && s.name.find(want) == std::string::npos) continue;
            float d = glm::distance(s.position, pos_);
            if (d < best_d) { best_d = d; best = id; }
        }
        return best;
    }

    std::optional<net::ItemSlot> find_item(const net::GameConnection& conn, const std::string& name) const {
        for (const auto& [key, it] : conn.inventory().items()) {
            if (key.first != net::kInvPossessions || key.second < net::kSlotGeneral1 || key.second > net::kSlotGeneral10) continue;
            if (it.name.find(name) != std::string::npos) return net::ItemSlot::possessions(key.second);
            for (const auto& b : it.contents)
                if (b.item.name.find(name) != std::string::npos)
                    return net::ItemSlot::possessions(key.second, static_cast<std::int16_t>(b.index));
        }
        for (const auto& [key, it] : conn.inventory().items()) // then worn items
            if (key.first == net::kInvPossessions && key.second < net::kEquipmentSlots && it.name.find(name) != std::string::npos)
                return net::ItemSlot::possessions(key.second);
        return std::nullopt;
    }

    void pick_up(net::GameConnection& conn, std::uint32_t item_id) {
        for (const auto& [key, it] : conn.inventory().items()) {
            if (key.first != net::kInvPossessions || key.second < net::kSlotGeneral1 || key.second > net::kSlotGeneral10) continue;
            if (item_id ? it.id != item_id : (it.is_bag() || !game::is_droppable(it))) continue;
            std::printf("  SHOP: picking up %s from %s\n", it.name.c_str(), net::possessions_slot_name(key.second));
            conn.click_slot(net::ItemSlot::possessions(key.second));
            return;
        }
        std::printf("  SHOP: no item %u to pick up\n", item_id);
    }

    // --trade-coin N of the smallest coin type the player has N of.
    void offer_coin(net::GameConnection& conn) const {
        const auto& c = conn.coins();
        const std::uint32_t have[4] = {c.copper, c.silver, c.gold, c.platinum};
        for (int type = 0; type < 4; ++type)
            if (have[type] >= trade_coin_) {
                std::printf("  SHOP: offering %u of coin type %d\n", trade_coin_, type);
                conn.trade_coin(type, trade_coin_);
                return;
            }
    }

    void print_coins(const net::GameConnection& conn, const char* when) const {
        const auto& c = conn.coins();
        std::printf("  SHOP: coin %s: %upp %ugp %usp %ucp (%lld copper)\n", when, c.platinum, c.gold, c.silver, c.copper,
                    static_cast<long long>(game::total_copper(c)));
    }

    void print_trade(const net::GameConnection& conn) {
        last_print_ = Clock::now();
        const auto& t = conn.trade();
        if (!t) return;
        for (std::int16_t n = 0; n < t->slots(); ++n)
            if (const auto* it = conn.inventory().at({net::kInvTrade, n, -1, -1, 0, 0}))
                std::printf("    my trade slot %d: %s x%u\n", n, it->name.c_str(), it->stack_count);
        for (const auto& [n, it] : t->theirs) std::printf("    their trade slot %d: %s x%u\n", n, it.name.c_str(), it.stack_count);
        std::printf("    coin: mine %u/%u/%u/%u, theirs %u/%u/%u/%u (pp/gp/sp/cp); ready: me %d, them %d\n", t->my_coins[3], t->my_coins[2],
                    t->my_coins[1], t->my_coins[0], t->their_coins[3], t->their_coins[2], t->their_coins[1], t->their_coins[0],
                    t->my_ready, t->their_ready);
    }

    void print_inventory(const net::GameConnection& conn) const {
        for (const auto& [key, it] : conn.inventory().items())
            if (key.first == net::kInvPossessions && key.second >= net::kSlotGeneral1)
                std::printf("    [%s] %s (id %u) x%u\n", net::possessions_slot_name(key.second), it.name.c_str(), it.id, it.stack_count);
    }

    std::string shop_, buy_, sell_, hail_, handin_, trade_with_, bought_;
    std::uint32_t buy_qty_ = 1, sell_qty_ = 1, handin_item_ = 0, trade_coin_ = 0;
    bool list_ = false, sell_back_ = false, saylink_ = false, trade_accept_ = false;
    Phase phase_ = Phase::Idle;
    std::uint32_t target_ = 0;
    glm::vec3 pos_{0.0f};
    Clock::time_point since_{}, last_step_{}, wait_until_{}, last_print_{};
    net::Coins coins_at_start_;
    std::optional<game::ItemLink> pending_link_;
    bool saylink_clicked_ = false;
};

} // namespace hailstone::probe
