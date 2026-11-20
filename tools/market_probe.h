#pragma once

// playprobe's checks of duels, inspect, the player commands, the bazaar and
// mail (included by tools/playprobe.cpp):
//   --cmd LINE        runs a typed line (e.g. "/keys"); repeatable, one every 2 s
//   --near NAME       first walks (straight, through walls) to within 5 units of the player NAME, at run speed or
//                     --near-speed N units a second (below run speed others see a walk), stopping --near-stop N units
//                     away (default 5)
//   --duel NAME       challenges NAME (targets it, /duel); --duel-wait 1 accepts the first
//                     challenge that comes (targets the challenger, /duel); --duel-decline 1
//                     declines it instead
//   --inspect NAME    inspects the player NAME (walks there first) and prints the answer
//   --trader PRICE    in the bazaar: opens the trader window, prices every satchel item at
//                     PRICE copper and begins trader mode (ends it before camping)
//   --shop-trader NAME  walks to the trader NAME ("any-trader": the nearest trader), opens its shop, prints its items and buys
//                     the first one
//   --bazaar TEXT     opens the bazaar search, searches for TEXT ("" = anything), prints the
//                     results; --bazaar-buy N buys result N to the inventory (-2: the cheapest)
//   --mail-send TO|SUBJECT|BODY  sends a mail
//   --market-zone Z   waits for the zone with short name Z (e.g. bazaar: NMS moves clients that
//                     skip its client check there after a minute)
//   --market-seconds N  how long these checks keep the character in the zone (default 60)
//   --mail 1          opens the mailbox, prints the headers and the first body; --mail-delete 1
//                     then deletes (trashes) the first message

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

#include "game/coords.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/packets/market_packets.h"
#include "net/protocol/rof2/packets/player_packets.h"

namespace hailstone::probe {

class MarketProbe {
public:
    using Clock = std::chrono::steady_clock;

    bool option(const char* name, const char* value) {
        if (!std::strcmp(name, "--cmd")) cmds_.push_back(value);
        else if (!std::strcmp(name, "--near")) near_ = value;
        else if (!std::strcmp(name, "--near-speed")) near_speed_ = std::strtof(value, nullptr);
        else if (!std::strcmp(name, "--near-stop")) near_stop_ = std::strtof(value, nullptr);
        else if (!std::strcmp(name, "--duel")) duel_ = value;
        else if (!std::strcmp(name, "--duel-wait")) duel_wait_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--duel-decline")) duel_decline_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--inspect")) inspect_ = value;
        else if (!std::strcmp(name, "--trader")) trader_price_ = static_cast<std::uint32_t>(std::atoi(value));
        else if (!std::strcmp(name, "--shop-trader")) shop_trader_ = value;
        else if (!std::strcmp(name, "--bazaar")) { bazaar_ = value; bazaar_on_ = true; }
        else if (!std::strcmp(name, "--bazaar-buy")) bazaar_buy_ = std::atoi(value);
        else if (!std::strcmp(name, "--shop-force")) shop_force_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--mail-send")) mail_send_ = value;
        else if (!std::strcmp(name, "--mail")) mail_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--mail-delete")) mail_delete_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--market-seconds")) limit_ = std::atoi(value);
        else if (!std::strcmp(name, "--market-zone")) zone_ = value;
        else return false;
        return true;
    }

    void tap(const net::AppPacket& p) {
        if (!active()) return;
        net::rof2::Op op = net::rof2::from_wire(p.opcode);
        switch (op) {
        case net::rof2::Op::RequestDuel: case net::rof2::Op::DuelAccept: case net::rof2::Op::DuelDecline: case net::rof2::Op::InspectAnswer:
        case net::rof2::Op::Trader: case net::rof2::Op::TraderShop: case net::rof2::Op::BecomeTrader: case net::rof2::Op::BazaarSearch:
        case net::rof2::Op::TraderBulkSend: case net::rof2::Op::TraderDelItem: case net::rof2::Op::Barter: case net::rof2::Op::BuyerItems:
        case net::rof2::Op::KeyRing: case net::rof2::Op::Surname: case net::rof2::Op::ReadBook: case net::rof2::Op::Bandolier:
        case net::rof2::Op::ShopEnd: case net::rof2::Op::ShopEndConfirm: case net::rof2::Op::ItemPacket: case net::rof2::Op::MoneyUpdate: {
            std::printf("  MARKET: got %s (%zu bytes):", net::rof2::op_name(op), p.data.size());
            for (std::size_t i = 0; i < p.data.size() && i < 32; ++i) std::printf(" %02x", std::to_integer<unsigned>(p.data[i]));
            std::printf("\n");
            break;
        }
        default:
            if (p.opcode == net::kOpDuelRefusal) std::printf("  MARKET: got duel refusal 0x52f7 (%zu bytes)\n", p.data.size());
            break;
        }
    }

    void zone_in(net::GameConnection& conn) {
        if (!active()) return;
        if (!zone_.empty() && conn.world().zone_short_name != zone_) {
            std::printf("  MARKET: waiting for %s\n", zone_.c_str());
            phase_ = Phase::Waiting;
            return;
        }
        if (const auto* me = conn.world().player()) pos_ = me->position;
        phase_ = Phase::Start;
        since_ = last_cmd_ = Clock::now();
    }

    bool busy() const { return active() && phase_ != Phase::Done; }


    void step(net::GameConnection& conn) {
        if (!active() || phase_ == Phase::Done || phase_ == Phase::Idle || phase_ == Phase::Waiting) return;
        auto now = Clock::now();
        float dt = std::min(0.1f, std::chrono::duration<float>(now - last_step_).count());
        last_step_ = now;
        if (now - since_ > std::chrono::seconds(limit_)) {
            std::printf("  MARKET: done after %d s (phase %d)\n", limit_, static_cast<int>(phase_));
            if (trader_on_) conn.trader_end();
            phase_ = Phase::Done;
            return;
        }
        if (now - since_ < std::chrono::seconds(3)) return;
        if (!listed_) {
            listed_ = true;
            for (const auto& [id, s] : conn.world().spawns)
                if (s.trader || s.buyer)
                    std::printf("  MARKET: spawn %u %s npc %d class %u %s%s\n", id, s.name.c_str(), s.is_npc, s.class_id,
                                s.trader ? "trader " : "", s.buyer ? "buyer" : "");
        }
        // Walking toward someone.
        std::string walk_name = !near_.empty() ? near_ : !inspect_.empty() ? inspect_ : !shop_trader_.empty() ? shop_trader_ : duel_;
        if (!walk_name.empty() && !arrived_) {
            const game::Spawn* t = find(conn, walk_name);
            if (!t) return;
            glm::vec3 d = t->position - pos_;
            d.z = 0;
            float len = glm::length(d);
            glm::vec3 v(0.0f);
            if (len > near_stop_) {
                v = d / len * near_speed_;
                pos_ += len - near_stop_ > near_speed_ * dt ? v * dt : d / len * (len - near_stop_);
            }
            pos_.z = t->position.z;
            conn.set_player_motion(pos_, game::heading_to(pos_, t->position), v, len > near_stop_);
            if (len <= near_stop_ + 0.5f) {
                conn.set_player_motion(pos_, game::heading_to(pos_, t->position), glm::vec3(0.0f), false);
                conn.set_target(t->id);
                arrived_ = true;
                arrived_at_ = now;
                std::printf("  MARKET: next to %s\n", t->name.c_str());
            }
            return;
        }
        if (!walk_name.empty() && now - arrived_at_ < std::chrono::seconds(2)) return;
        // Typed lines.
        if (next_cmd_ < cmds_.size() && now - last_cmd_ > std::chrono::seconds(2)) {
            std::printf("  MARKET: %s\n", cmds_[next_cmd_].c_str());
            conn.send_command(cmds_[next_cmd_++]);
            last_cmd_ = now;
        }
        duel_step(conn, now);
        inspect_step(conn);
        trader_step(conn, now);
        shop_step(conn, now);
        bazaar_step(conn, now);
        mail_step(conn, now);
    }

    void finish(net::GameConnection& conn) {
        if (!active()) return;
        const auto& d = conn.duel_state();
        std::printf("  MARKET SUMMARY: duel challenged %u challenger %u active %d; trader %d; mail headers %zu\n",
                    d.challenged, d.challenger, d.active, conn.trader_state().trader, conn.mail().headers.size());
    }

private:
    enum class Phase { Idle, Waiting, Start, Done };

    bool active() const {
        return !cmds_.empty() || !near_.empty() || !duel_.empty() || duel_wait_ || duel_decline_ || !inspect_.empty() ||
               trader_price_ || !shop_trader_.empty() || bazaar_on_ || !mail_send_.empty() || mail_;
    }

    const game::Spawn* find(net::GameConnection& conn, const std::string& name) const {
        if (name == "any-trader") {
            // The nearest spawn with the trader flag (NMS's simulated traders turn trader ~20 s after zone-in).
            const game::Spawn* best = nullptr;
            for (const auto& [id, s] : conn.world().spawns)
                if (s.trader && id != conn.world().player_id &&
                    (!best || glm::distance(s.position, pos_) < glm::distance(best->position, pos_)))
                    best = &s;
            return best;
        }
        for (const auto& [id, s] : conn.world().spawns)
            if (s.name == name || game::display_name(s.name) == name) return &s;
        return nullptr;
    }

    void duel_step(net::GameConnection& conn, Clock::time_point now) {
        if (!duel_.empty() && !duel_sent_) {
            duel_sent_ = true;
            std::printf("  MARKET: /duel %s\n", duel_.c_str());
            conn.send_command("/duel");
        }
        if ((duel_wait_ || duel_decline_) && !duel_answered_ && conn.duel_state().challenger != 0 &&
            now - last_cmd_ > std::chrono::seconds(1)) {
            duel_answered_ = true;
            auto it = conn.world().spawns.find(conn.duel_state().challenger);
            if (duel_decline_) {
                std::printf("  MARKET: /decline\n");
                conn.send_command("/decline");
            } else if (it != conn.world().spawns.end()) {
                conn.set_target(it->first);
                std::printf("  MARKET: accepting %s's duel\n", it->second.name.c_str());
                conn.send_command("/duel");
            }
        }
    }

    void inspect_step(net::GameConnection& conn) {
        if (inspect_.empty()) return;
        if (!inspect_sent_) {
            const game::Spawn* t = find(conn, inspect_);
            if (!t) return;
            inspect_sent_ = true;
            std::printf("  MARKET: inspect(%s) -> %d\n", inspect_.c_str(), conn.inspect(t->id));
            return;
        }
        if (inspect_printed_ || !conn.inspect_view()) return;
        inspect_printed_ = true;
        const auto& v = *conn.inspect_view();
        std::printf("  MARKET: INSPECT %s (own %d): \"%s\"\n", v.player_name.c_str(), v.own, v.text.c_str());
        for (int i = 0; i < 23; ++i)
            if (!v.item_names[static_cast<std::size_t>(i)].empty())
                std::printf("    slot %d: %s (icon %u)\n", i, v.item_names[static_cast<std::size_t>(i)].c_str(),
                            v.item_icons[static_cast<std::size_t>(i)]);
    }

    void trader_step(net::GameConnection& conn, Clock::time_point now) {
        if (!trader_price_) return;
        if (!trader_on_) {
            trader_on_ = true;
            std::printf("  MARKET: open_trader_window -> %d\n", conn.open_trader_window());
            const auto& t = conn.trader_state();
            for (std::size_t i = 0; i < t.slots.size(); ++i) {
                conn.trader_select(static_cast<int>(i));
                conn.trader_enter_price(trader_price_);
                conn.trader_set_price();
            }
            std::printf("  MARKET: trader_start -> %d (%zu slots)\n", conn.trader_start(), t.slots.size());
            trader_at_ = now;
            return;
        }
        if (!trader_printed_ && now - trader_at_ > std::chrono::seconds(5)) {
            trader_printed_ = true;
            for (const auto& l : conn.trader_state().log) std::printf("    bazaar[%u]: %s\n", l.color, l.text.c_str());
            for (const auto& s : conn.trader_state().slots)
                std::printf("    slot %s %s price %u active %d\n", s.serial.c_str(), s.name.c_str(), s.price, s.active);
        }
    }

    void shop_step(net::GameConnection& conn, Clock::time_point now) {
        if (shop_trader_.empty()) return;
        if (!shop_opened_) {
            const game::Spawn* t = find(conn, shop_trader_);
            if (!t || !t->trader) return; // simulated traders raise the flag a while after zone-in
            bool ok = conn.open_trader(t->id);
            std::printf("  MARKET: %s trader flag %d; open_trader -> %d\n", t->name.c_str(), t->trader, ok);
            if (!ok && ++shop_tries_ < 6) {
                arrived_ = false; // it walked off: follow it again
                return;
            }
            shop_opened_ = true;
            shop_at_ = now;
            return;
        }
        const auto& m = conn.merchant();
        if (!shop_reported_ && now - shop_at_ > std::chrono::seconds(3)) {
            shop_reported_ = true;
            std::printf("  MARKET: merchant open %d trader %d items %zu\n", m.has_value(), m && m->trader,
                        m ? m->items.size() : 0);
        }
        if (!shop_bought_ && m && m->trader && !m->items.empty() && now - shop_at_ > std::chrono::seconds(3)) {
            shop_bought_ = true;
            for (const auto& [n, it] : m->items)
                std::printf("    trader item %u: %s serial %s price %u count %u\n", n, it.name.c_str(), it.serial.c_str(),
                            it.price, it.stack_count);
            auto cheapest = m->items.begin();
            for (auto it = m->items.begin(); it != m->items.end(); ++it)
                if (it->second.price < cheapest->second.price) cheapest = it;
            std::printf("  MARKET: buying item %u (%s for %u), coins %u/%u/%u/%u\n", cheapest->first,
                        cheapest->second.name.c_str(), cheapest->second.price, conn.coins().platinum, conn.coins().gold,
                        conn.coins().silver, conn.coins().copper);
            conn.merchant_buy(cheapest->first, 1);
            if (shop_force_) {
                // Test only: the purchase the original would refuse locally, to see the server's answer.
                net::TraderBuy b;
                b.trader_id = m->npc_id;
                b.seller_name = m->name;
                b.item_name = cheapest->second.name;
                b.serial = cheapest->second.serial;
                b.item_id = cheapest->second.id;
                b.price = cheapest->second.price;
                b.quantity = 1;
                conn.send_zone_message(net::messages::outgoing(net::messages::GameOpcode::TraderShop, b));
                std::printf("  MARKET: forced OP_TraderShop 10 for %s\n", b.item_name.c_str());
            }
            shop_at_ = now;
            return;
        }
        if (shop_bought_ && !shop_closed_ && now - shop_at_ > std::chrono::seconds(4)) {
            shop_closed_ = true;
            conn.close_merchant();
            std::printf("  MARKET: closed the trader\n");
        }
    }

    void bazaar_step(net::GameConnection& conn, Clock::time_point now) {
        if (!bazaar_on_) return;
        if (!bazaar_opened_) {
            bazaar_opened_ = true;
            conn.open_bazaar_search();
            bazaar_at_ = now;
            return;
        }
        if (!bazaar_searched_ && now - bazaar_at_ > std::chrono::seconds(2)) {
            bazaar_searched_ = true;
            std::printf("  MARKET: %zu traders known\n", conn.bazaar_search_state().traders.size());
            for (const auto& [id, t] : conn.bazaar_search_state().traders)
                std::printf("    trader %u %s spawn %u zone %u/%u\n", id, t.name.c_str(), t.entity_id, t.zone_id, t.zone_instance);
            net::BazaarQuery q;
            q.name = bazaar_;
            conn.bazaar_search(q);
            bazaar_at_ = now;
            return;
        }
        if (bazaar_searched_ && !bazaar_printed_ && now - bazaar_at_ > std::chrono::seconds(3)) {
            bazaar_printed_ = true;
            const auto& r = conn.bazaar_search_state().results;
            std::printf("  MARKET: %zu results\n", r.size());
            for (std::size_t i = 0; i < r.size() && i < 20; ++i)
                std::printf("    %zu: %s x%u for %u from %u serial %s\n", i, r[i].name.c_str(), r[i].quantity, r[i].cost,
                            r[i].trader_id, r[i].serial.c_str());
            if (bazaar_buy_ == -2 && !r.empty()) { // the cheapest
                bazaar_buy_ = 0;
                for (std::size_t i = 1; i < r.size(); ++i)
                    if (r[i].cost < r[static_cast<std::size_t>(bazaar_buy_)].cost) bazaar_buy_ = static_cast<int>(i);
            }
            if (bazaar_buy_ >= 0 && static_cast<std::size_t>(bazaar_buy_) < r.size()) {
                std::printf("  MARKET: bazaar_buy(%d), coins %u/%u/%u/%u\n", bazaar_buy_, conn.coins().platinum,
                            conn.coins().gold, conn.coins().silver, conn.coins().copper);
                conn.bazaar_buy(static_cast<std::size_t>(bazaar_buy_), 1, false);
            }
        }
    }

    void mail_step(net::GameConnection& conn, Clock::time_point now) {
        if (!mail_send_.empty() && !mail_sent_ && conn.chat_server_connected()) {
            mail_sent_ = true;
            auto a = mail_send_.find('|'), b = mail_send_.find('|', a + 1);
            std::string to = mail_send_.substr(0, a), subj = mail_send_.substr(a + 1, b - a - 1), body = mail_send_.substr(b + 1);
            std::printf("  MARKET: mail_send(%s, %s, %s) -> %d\n", to.c_str(), subj.c_str(), body.c_str(), conn.mail_send(to, subj, body));
        }
        if (!mail_) return;
        if (!mail_opened_ && conn.chat_server_connected() && now - since_ > std::chrono::seconds(5)) {
            mail_opened_ = true;
            conn.mail_open();
            mail_at_ = now;
            return;
        }
        if (mail_opened_ && !mail_listed_ && now - mail_at_ > std::chrono::seconds(3)) {
            mail_listed_ = true;
            const auto& m = conn.mail();
            std::printf("  MARKET: %zu mailboxes, current %d, %zu headers (expected %d)\n", m.mailboxes.size(), m.current,
                        m.headers.size(), m.expected);
            for (const auto& [id, h] : m.headers)
                std::printf("    mail %u from %s: \"%s\" status %d time %u\n", id, h.from.c_str(), h.subject.c_str(),
                            static_cast<int>(h.status), h.timestamp);
            auto in = m.inbox();
            if (!in.empty()) {
                first_mail_ = in.front()->id;
                conn.mail_request_body(first_mail_);
            }
            mail_at_ = now;
            return;
        }
        if (mail_listed_ && first_mail_ && !mail_body_printed_ && now - mail_at_ > std::chrono::seconds(3)) {
            mail_body_printed_ = true;
            auto it = conn.mail().headers.find(first_mail_);
            if (it != conn.mail().headers.end())
                std::printf("  MARKET: body of %u to \"%s\": \"%s\"\n", first_mail_, it->second.to.c_str(),
                            it->second.body ? it->second.body->c_str() : "(none)");
            if (mail_delete_) {
                std::printf("  MARKET: mail_delete(%u)\n", first_mail_);
                conn.mail_delete({first_mail_});
            }
        }
    }

    Phase phase_ = Phase::Idle;
    Clock::time_point since_{}, last_step_{}, last_cmd_{}, arrived_at_{}, trader_at_{}, shop_at_{}, bazaar_at_{}, mail_at_{};
    int limit_ = 60;
    glm::vec3 pos_{0.0f};
    std::vector<std::string> cmds_;
    std::size_t next_cmd_ = 0;
    float near_speed_ = 30.0f, near_stop_ = 5.0f;
    std::string zone_, near_, duel_, inspect_, shop_trader_, bazaar_, mail_send_;
    bool arrived_ = false, listed_ = false;
    bool duel_wait_ = false, duel_decline_ = false, duel_sent_ = false, duel_answered_ = false;
    bool inspect_sent_ = false, inspect_printed_ = false;
    std::uint32_t trader_price_ = 0;
    bool trader_on_ = false, trader_printed_ = false;
    bool shop_reported_ = false, shop_opened_ = false, shop_bought_ = false, shop_closed_ = false;
    int bazaar_buy_ = -1;
    bool bazaar_on_ = false, bazaar_opened_ = false, bazaar_searched_ = false, bazaar_printed_ = false;
    bool mail_ = false, mail_delete_ = false, mail_sent_ = false, mail_opened_ = false, mail_listed_ = false, mail_body_printed_ = false;
    std::uint32_t first_mail_ = 0;
    int shop_tries_ = 0;
    bool shop_force_ = false;
};

} // namespace hailstone::probe
