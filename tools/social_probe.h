#pragma once

// playprobe's raid, guild tribute and parcel-money checks (included by tools/playprobe.cpp):
//   --raid-accept 1        accept the first raid invitation (2 s after it comes)
//   --raid-size N          stay until the raid has N members (and, after --raid-motd, until the MOTD is seen),
//                          printing the member count as it changes; members print the MOTD they get
//   --raid-motd TEXT       once the raid has --raid-size members, set the raid MOTD (the raid window's MOTD button)
//   --tribute-opt-out NAME the guild window's Tribute page: opt the member NAME out (guild_tribute_opt_out) 5 s after
//                          the member list has NAME, then print NAME's row as OP_GuildOptInOut updates it
//   --tribute-cycle N      the Tribute Benefit window's Opt In / Opt Out button (guild_tribute_opt with the
//                          opposite of the current state) pressed N times, 6 s apart, printing our row and state
//   --guild-watch S        stay S seconds and print the guild tribute rows whenever one changes
//   --parcel-at TEXT       walk to the NPC whose name contains TEXT and open it as a merchant; then
//   --parcel-money P|TO    on its Parcels page pick P platinum through the coin-button path (parcel_coin_button,
//                          set_parcel_money) and send that pick to TO (send_parcel_money); print the purse before,
//                          after the answer and 5 s later
//   --parcel-claim 1       print the parcels waiting there and claim them all, printing the purse before and after
// Prints every raid, guild tribute and parcel packet it sees.

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <glm/geometric.hpp>

#include "game/coords.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"
#include "net/protocol/rof2/opcode_map.h"

namespace hailstone::probe {

class SocialProbe {
public:
    using Clock = std::chrono::steady_clock;

    bool option(const char* name, const char* value) {
        if (!std::strcmp(name, "--raid-accept")) raid_accept_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--raid-size")) raid_size_ = static_cast<std::size_t>(std::atoi(value));
        else if (!std::strcmp(name, "--raid-motd")) raid_motd_ = value;
        else if (!std::strcmp(name, "--tribute-opt-out")) opt_out_ = value;
        else if (!std::strcmp(name, "--tribute-cycle")) cycle_ = std::atoi(value);
        else if (!std::strcmp(name, "--guild-watch")) guild_watch_ = std::atoi(value);
        else if (!std::strcmp(name, "--parcel-at")) parcel_at_ = value;
        else if (!std::strcmp(name, "--parcel-money")) parcel_money_ = value;
        else if (!std::strcmp(name, "--parcel-claim")) parcel_claim_ = std::atoi(value) != 0;
        else return false;
        return true;
    }

    void tap(const net::AppPacket& p) {
        net::rof2::Op op = net::rof2::from_wire(p.opcode);
        switch (op) {
        case net::rof2::Op::GuildOptInOut: case net::rof2::Op::GuildTributeFavorAndTimer: case net::rof2::Op::GuildTributeToggleReply:
        case net::rof2::Op::ShopSendParcel: case net::rof2::Op::ShopRetrieveParcel: case net::rof2::Op::ShopParcelIcon:
        case net::rof2::Op::ShopDeleteParcel: case net::rof2::Op::MoneyUpdate: case net::rof2::Op::MoveCoin:
            if (!active()) break;
            std::printf("  SOCIAL +%.1fs: got %s (%zu bytes):", secs(), net::rof2::op_name(op), p.data.size());
            for (std::size_t i = 0; i < p.data.size() && i < 40; ++i) std::printf(" %02x", std::to_integer<unsigned>(p.data[i]));
            std::printf("\n");
            break;
        default: break;
        }
    }

    void zone_in(net::GameConnection& conn) {
        start_ = since_ = Clock::now();
        if (const auto* me = conn.world().player()) pos_ = me->position;
        if (!parcel_at_.empty()) phase_ = Phase::Start;
        print_coins(conn, "at zone-in");
    }

    bool busy() const {
        if (raid_size_ && (!raid_full_seen_ || (!raid_motd_.empty() && !motd_sent_))) return true;
        if (raid_accept_ && !raid_joined_) return true;
        if (!raid_motd_.empty() && raid_size_ && !motd_seen_) return true;
        if (!opt_out_.empty() && !opt_out_done_) return true;
        if (cycle_ > 0 && cycle_step_ <= cycle_) return true;
        if (guild_watch_ > 0 && secs() < guild_watch_) return true;
        return !parcel_at_.empty() && phase_ != Phase::Done;
    }

    void step(net::GameConnection& conn) {
        auto now = Clock::now();
        raid_step(conn);
        guild_step(conn, now);
        parcel_step(conn, now);
    }

    void finish(const net::GameConnection& conn) {
        if (raid_size_ || raid_accept_)
            std::printf("  RAID final: active %d, %zu member(s), leader '%s', motd '%s'\n", conn.raid().active,
                        conn.raid().members.size(), conn.raid().leader.c_str(), conn.raid().motd.c_str());
        if (!opt_out_.empty() || cycle_ > 0 || guild_watch_ > 0) print_rows(conn, "final");
        if (!parcel_at_.empty()) print_coins(conn, "at the end");
    }

private:
    enum class Phase { Idle, Start, Walk, Arrived, WaitOpen, Open, Sent, Claim, Done };

    bool active() const {
        return raid_accept_ || raid_size_ || !opt_out_.empty() || cycle_ > 0 || guild_watch_ > 0 || !parcel_at_.empty();
    }
    double secs() const { return std::chrono::duration<double>(Clock::now() - start_).count(); }

    void raid_step(net::GameConnection& conn) {
        const auto& r = conn.raid();
        if (r.members.size() != last_raid_count_) {
            last_raid_count_ = r.members.size();
            std::printf("  RAID +%.1fs: %zu member(s), leader '%s'\n", secs(), last_raid_count_, r.leader.c_str());
        }
        if (raid_accept_ && !raid_joined_ && !r.inviter.empty()) {
            if (invite_seen_ == Clock::time_point{}) {
                invite_seen_ = Clock::now();
                std::printf("  RAID +%.1fs: invited by '%s'\n", secs(), r.inviter.c_str());
            } else if (Clock::now() - invite_seen_ > std::chrono::seconds(2)) {
                conn.answer_raid_invite(true);
                raid_joined_ = true;
                std::printf("  RAID +%.1fs: accepted\n", secs());
            }
        }
        if (r.motd != last_motd_) {
            last_motd_ = r.motd;
            std::printf("  RAID +%.1fs: motd '%s'\n", secs(), r.motd.c_str());
            if (motd_sent_ && r.motd == raid_motd_) motd_seen_ = true;
        }
        if (raid_size_ && r.members.size() >= raid_size_ && !raid_full_seen_) {
            raid_full_seen_ = true;
            std::printf("  RAID +%.1fs: reached %zu members:", secs(), r.members.size());
            for (const auto& m : r.members) std::printf(" %s(g%d)", m.name.c_str(), static_cast<int>(m.group));
            std::printf("\n");
            full_at_ = Clock::now();
        }
        if (raid_full_seen_ && !raid_motd_.empty() && !motd_sent_ && Clock::now() - full_at_ > std::chrono::seconds(3)) {
            conn.raid_set_motd(raid_motd_);
            motd_sent_ = true;
            std::printf("  RAID +%.1fs: set motd '%s'\n", secs(), raid_motd_.c_str());
        }
        // A member (no --raid-motd) waits for some MOTD after the raid is full, so it is around to receive it.
        if (raid_size_ && raid_motd_.empty() && raid_full_seen_ && !r.motd.empty()) motd_seen_ = motd_sent_ = true;
        if (raid_size_ && raid_motd_.empty() && raid_full_seen_ && !motd_sent_ &&
            Clock::now() - full_at_ > std::chrono::seconds(40))
            motd_sent_ = true; // gave up waiting
    }

    void guild_step(net::GameConnection& conn, Clock::time_point now) {
        if (opt_out_.empty() && cycle_ <= 0 && guild_watch_ <= 0) return;
        std::string rows = row_text(conn);
        if (rows != last_rows_) {
            last_rows_ = rows;
            print_rows(conn, "changed");
        }
        if (!opt_out_.empty() && !opt_out_done_) {
            bool listed = false;
            for (const auto& m : conn.guild().members) listed |= m.name == opt_out_;
            if (listed && opt_listed_at_ == Clock::time_point{}) opt_listed_at_ = now;
            if (listed && now - opt_listed_at_ > std::chrono::seconds(5) && !opt_out_sent_) {
                std::printf("  GUILD +%.1fs: Tribute page OptOut for '%s'\n", secs(), opt_out_.c_str());
                conn.guild_tribute_opt_out(opt_out_);
                opt_out_sent_ = true;
                opt_sent_at_ = now;
            }
            if (opt_out_sent_ && now - opt_sent_at_ > std::chrono::seconds(8)) opt_out_done_ = true;
            if (!listed && now - since_ > std::chrono::seconds(60)) {
                std::printf("  GUILD: '%s' never appeared in the member list\n", opt_out_.c_str());
                opt_out_done_ = true;
            }
        }
        if (cycle_ > 0 && cycle_step_ <= cycle_ && now - since_ > std::chrono::seconds(6 + 6 * cycle_step_)) {
            const auto& gt = conn.guild_tribute();
            if (cycle_step_ < cycle_) {
                bool in = !gt.opted_in.value_or(false);
                std::printf("  GUILD +%.1fs: Opt button, opted_in %s -> sends %s\n", secs(),
                            gt.opted_in ? (*gt.opted_in ? "yes" : "no") : "unknown", in ? "in" : "out");
                conn.guild_tribute_opt(in);
            } else {
                std::printf("  GUILD +%.1fs: opted_in now %s\n", secs(), gt.opted_in ? (*gt.opted_in ? "yes" : "no") : "unknown");
            }
            ++cycle_step_;
        }
    }

    static std::string row_text(const net::GameConnection& conn) {
        std::string s;
        for (const auto& m : conn.guild().members)
            s += m.name + ":" + std::to_string(m.tribute_on) + std::to_string(m.trophy_on) + std::to_string(m.tribute_flag) +
                 "/" + std::to_string(m.donations) + "/" + std::to_string(m.last_donation) + " ";
        const auto& o = conn.guild_tribute().opted_in;
        return s + "me:" + (o ? (*o ? "in" : "out") : "?");
    }

    void print_rows(const net::GameConnection& conn, const char* why) const {
        std::printf("  GUILD +%.1fs (%s): guild %u, our opted_in %s;", secs(), why, conn.guild().guild_id,
                    conn.guild_tribute().opted_in ? (*conn.guild_tribute().opted_in ? "in" : "out") : "unknown");
        for (const auto& m : conn.guild().members)
            std::printf(" [%s tribute %s trophy %s flag %d donated %u last %u]", m.name.c_str(), m.tribute_on ? "on" : "off",
                        m.trophy_on ? "on" : "off", m.tribute_flag, m.donations, m.last_donation);
        std::printf("\n");
    }

    void print_coins(const net::GameConnection& conn, const char* when) const {
        if (parcel_at_.empty()) return;
        const auto& c = conn.coins();
        std::printf("  PARCEL +%.1fs: purse %s: %upp %ugp %usp %ucp\n", secs(), when, c.platinum, c.gold, c.silver, c.copper);
    }

    void parcel_step(net::GameConnection& conn, Clock::time_point now) {
        if (parcel_at_.empty() || phase_ == Phase::Done || phase_ == Phase::Idle) return;
        float dt = std::min(0.1f, std::chrono::duration<float>(now - last_step_).count());
        last_step_ = now;
        if (now - since_ > std::chrono::seconds(180)) {
            std::printf("  PARCEL: gave up in phase %d\n", static_cast<int>(phase_));
            phase_ = Phase::Done;
            return;
        }
        const auto& w = conn.world();
        switch (phase_) {
        case Phase::Start: {
            if (now - since_ < std::chrono::seconds(3)) return;
            float best = 1e9f;
            for (const auto& [id, s] : w.spawns)
                if (s.is_npc && s.name.find(parcel_at_) != std::string::npos && glm::distance(s.position, pos_) < best) {
                    best = glm::distance(s.position, pos_);
                    target_ = id;
                }
            if (!target_) {
                std::printf("  PARCEL: nobody named \"%s\" here\n", parcel_at_.c_str());
                phase_ = Phase::Done;
                return;
            }
            std::printf("  PARCEL: walking to %s (id %u, class %u) %.0f units away\n", w.spawns.at(target_).name.c_str(),
                        target_, w.spawns.at(target_).class_id, best);
            phase_ = Phase::Walk;
            return;
        }
        case Phase::Walk: {
            auto it = w.spawns.find(target_);
            if (it == w.spawns.end()) { phase_ = Phase::Done; return; }
            glm::vec3 to = it->second.position, d = to - pos_;
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
            conn.open_merchant(target_);
            phase_ = Phase::WaitOpen;
            wait_until_ = now + std::chrono::seconds(4);
            return;
        case Phase::WaitOpen: {
            if (now < wait_until_) return;
            const auto& m = conn.merchant();
            if (!m || !m->open) {
                std::printf("  PARCEL: the merchant did not open\n");
                phase_ = Phase::Done;
                return;
            }
            std::printf("  PARCEL: %s open, tabs %d, %zu parcel(s) waiting:\n", m->name.c_str(), m->tabs, m->parcels.size());
            for (const auto& [slot, pc] : m->parcels)
                std::printf("    parcel %u: %s (id %u) x%u from '%s' note '%s'\n", slot, pc.item.name.c_str(), pc.item.id,
                            pc.item.stack_count, pc.from.c_str(), pc.note.c_str());
            print_coins(conn, "before");
            if (!parcel_money_.empty()) {
                auto bar = parcel_money_.find('|');
                std::uint32_t pp = static_cast<std::uint32_t>(std::atoi(parcel_money_.substr(0, bar).c_str()));
                std::string to = bar == std::string::npos ? "" : parcel_money_.substr(bar + 1);
                // The inventory's platinum button on the Parcels page, then the quantity dialog's answer.
                conn.parcel_coin_button(0, static_cast<std::int32_t>(conn.coins().platinum));
                std::printf("  PARCEL: coin button outlined %d\n", conn.merchant()->parcel_coin);
                conn.set_parcel_money(0, pp);
                const auto& pick = conn.merchant()->parcel_money;
                std::array<std::uint32_t, 4> coins{};
                if (pick) coins[static_cast<std::size_t>(pick->coin)] = pick->amount;
                std::string why = conn.send_parcel_money(coins, to, "hailstone live check");
                std::printf("  PARCEL: send %upp to '%s': %s\n", coins[0], to.c_str(), why.empty() ? "sent" : why.c_str());
                phase_ = Phase::Sent;
                wait_until_ = now + std::chrono::seconds(6);
                sent_at_ = now;
                return;
            }
            if (parcel_claim_) {
                conn.claim_all_parcels();
                phase_ = Phase::Claim;
                wait_until_ = now + std::chrono::seconds(8);
                return;
            }
            phase_ = Phase::Done;
            return;
        }
        case Phase::Sent:
            if (!answered_ && conn.merchant() && !conn.merchant()->parcel_sending) {
                answered_ = true;
                print_coins(conn, "after the answer");
            }
            if (now < wait_until_) return;
            print_coins(conn, "6 s after sending");
            conn.close_merchant();
            phase_ = Phase::Done;
            return;
        case Phase::Claim:
            if (now < wait_until_ && conn.merchant() && !conn.merchant()->claiming.empty()) return;
            if (now < wait_until_) return;
            std::printf("  PARCEL: %zu parcel(s) left after claiming\n", conn.merchant() ? conn.merchant()->parcels.size() : 0);
            print_coins(conn, "after claiming");
            conn.close_merchant();
            phase_ = Phase::Done;
            return;
        default: return;
        }
    }

    bool raid_accept_ = false, raid_joined_ = false, raid_full_seen_ = false, motd_sent_ = false, motd_seen_ = false;
    std::size_t raid_size_ = 0, last_raid_count_ = 0;
    std::string raid_motd_, last_motd_;
    Clock::time_point invite_seen_{}, full_at_{};

    std::string opt_out_, last_rows_;
    bool opt_out_sent_ = false, opt_out_done_ = false;
    int cycle_ = 0, cycle_step_ = 0, guild_watch_ = 0;
    Clock::time_point opt_listed_at_{}, opt_sent_at_{};

    std::string parcel_at_, parcel_money_;
    bool parcel_claim_ = false, answered_ = false;
    Phase phase_ = Phase::Idle;
    std::uint32_t target_ = 0;
    glm::vec3 pos_{0.0f};
    Clock::time_point start_{Clock::now()}, since_{Clock::now()}, last_step_{Clock::now()}, wait_until_{}, sent_at_{};
};

} // namespace hailstone::probe
