// GameConnection: personal tribute over EQEmu's packets (net/protocol/rof2/packets/tribute_packets.h). The tribute list is asked for
// while zoning in (OP_SendTributes, which EQEmu takes only then); the windows are app/lua_ui_progress.cpp's.

#include <algorithm>
#include <cctype>

#include "core/log.h"
#include "game/merchant.h"
#include "net/connection/game_connection.h"

namespace hailstone::net {

namespace {

constexpr std::uint8_t kClassTributeMaster = 63, kClassGuildTributeMaster = 64;
// CTributeMasterWnd::AboutToShow @0x7878b0 and OnProcessFrame @0x787ae0: PlayerClient_IsWithinInteractRange(master,
// 0, 20.0). EQEmu's USE_NPC_RANGE2 is 200 units squared.
constexpr float kTributeRange = 20.0f;

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// EQEmu removes tribute benefits without an item deletion packet (DoTributeUpdate / DoGuildTributeUpdate).
// Retire the old received item on an authoritative state change; a later item packet supplies its replacement.
// The first enabled snapshot may follow zone-in items, so its default previous selections are not a change.
template <std::size_t N>
void retire_tribute_items(game::Inventory& inventory, std::int16_t type, bool enabled, bool previous_known,
                          const std::array<std::uint32_t, N>& previous_ids,
                          const std::array<std::uint32_t, N>& previous_tiers,
                          const std::array<std::uint32_t, N>& ids, const std::array<std::uint32_t, N>& tiers) {
    for (std::size_t slot = 0; slot < N; ++slot)
        if (!enabled || ids[slot] == kTributeNone ||
            (previous_known && (ids[slot] != previous_ids[slot] || tiers[slot] != previous_tiers[slot])))
            inventory.take({type, static_cast<std::int16_t>(slot), -1, -1});
}

std::uint32_t donation_count(const Item& item) { return item.stackable() ? item.stack_count : 1u; }

// A bag must not take newly added contents with it when its delayed donation reply arrives.
bool same_donated_item(const Item& a, const Item& b) {
    if (a.id != b.id || a.serial != b.serial || a.instance_id != b.instance_id || a.stackable() != b.stackable() ||
        donation_count(a) != donation_count(b) || a.charges != b.charges || a.contents.size() != b.contents.size())
        return false;
    for (const auto& child : a.contents) {
        const auto other =
            std::find_if(b.contents.begin(), b.contents.end(), [&](const auto& c) { return c.index == child.index; });
        if (other == b.contents.end() || !same_donated_item(child.item, other->item))
            return false;
    }
    return true;
}

} // namespace

std::int64_t GameConnection::session_now_ms() const {
    if (!options_.zone_protocol.session_config.clock) return now_ms();
    return std::chrono::duration_cast<std::chrono::milliseconds>(options_.zone_protocol.session_config.now().time_since_epoch()).count();
}

void GameConnection::expire_tribute_donation() {
    if (!pending_tribute_donation_ || tribute_donation_waiting()) return;
    log::warn("no answer to the {} tribute item donation; no longer waiting for it",
              pending_tribute_donation_->guild ? "guild" : "personal");
    pending_tribute_donation_.reset();
}

bool GameConnection::begin_tribute_donation(bool guild, const Item& item, std::uint32_t master_id) {
    expire_tribute_donation();
    if (pending_tribute_donation_) {
        chat(ChatEvent::Kind::System, "Wait for the tribute master to answer your previous item donation.");
        return false;
    }
    if (item.serial.empty() || item.serial.find_first_not_of('0') == std::string::npos) {
        chat(ChatEvent::Kind::System, "This item cannot be identified safely for a tribute donation.");
        return false;
    }
    pending_tribute_donation_ = PendingTributeDonation{guild, item.slot, donation_count(item), master_id, item,
                                                       session_now_ms() + kTributeReplyLimitMs};
    return true;
}

bool GameConnection::finish_tribute_donation(bool guild, const ItemSlot& slot, std::uint32_t quantity,
                                            std::uint32_t master_id, bool consumed) {
    expire_tribute_donation();
    if (!pending_tribute_donation_ || pending_tribute_donation_->guild != guild ||
        !pending_tribute_donation_->slot.same(slot) || pending_tribute_donation_->quantity != quantity ||
        (!guild && pending_tribute_donation_->master_id != master_id)) {
        log::warn("ignoring an unmatched {} tribute item reply", guild ? "guild" : "personal");
        return false;
    }
    PendingTributeDonation pending = std::move(*pending_tribute_donation_);
    pending_tribute_donation_.reset();
    if (!consumed)
        return true;

    // A player may have moved the donated instance while waiting. Remove that unique, unchanged instance wherever
    // it now lives, never an item that has since taken its request slot. A split/merge or duplicate serial is
    // ambiguous, so leave the inventory intact for the server's next authoritative refresh.
    std::size_t instances = 0;
    std::optional<ItemSlot> found;
    auto visit = [&](auto&& self, const Item& item, const ItemSlot& at) -> void {
        if (item.id == pending.item.id && item.serial == pending.item.serial) {
            ++instances;
            if (same_donated_item(item, pending.item))
                found = at;
        }
        if (item.is_bag() && at.sub < 0)
            for (const auto& child : item.contents) {
                ItemSlot inside = at;
                inside.sub = static_cast<std::int16_t>(child.index);
                self(self, child.item, inside);
            }
    };
    for (const auto& [key, item] : inventory_.items())
        visit(visit, item, ItemSlot{key.first, key.second, -1, -1});
    if (instances == 1 && found) {
        inventory_.take(*found);
        if (found->type == kInvPossessions && found->sub < 0 && found->slot < kEquipmentSlots)
            refresh_worn_slot(found->slot);
    } else {
        log::warn("cannot reconcile {} tribute donation of item {} serial {}", guild ? "guild" : "personal",
                  pending.item.id, pending.item.serial);
        chat(ChatEvent::Kind::System,
             "Your donated item changed before the tribute reply. Other items were left alone; zone to refresh your inventory.");
    }
    return true;
}

std::uint32_t GameConnection::Tribute::active_cost() const {
    std::uint32_t cost = 0;
    for (int i = 0; i < kTributeSlots; ++i) {
        auto it = abilities.find(settings.tributes[static_cast<std::size_t>(i)]);
        std::uint32_t tier = settings.tiers[static_cast<std::size_t>(i)];
        if (it != abilities.end() && tier < kTributeTiers) cost += it->second.tiers[tier].cost;
    }
    return cost;
}

std::int64_t GameConnection::Tribute::time_left(std::int64_t now) const {
    if (!settings.active) return timer_ms;
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(timer_ms) - (now - timer_at));
}

bool GameConnection::open_tribute_master(std::uint32_t npc_id) {
    if (state_ != ConnectionState::InZone || pstatus_.dead) return false;
    auto it = world_.spawns.find(npc_id);
    const game::Spawn* me = world_.player();
    if (it == world_.spawns.end() || !me || !it->second.is_npc ||
        (it->second.class_id != kClassTributeMaster && it->second.class_id != kClassGuildTributeMaster))
        return false;
    glm::vec3 at = motion_known_ ? position_ : me->position;
    if (glm::distance(at, it->second.position) >= kTributeRange) return false;
    StartTribute st;
    st.client_id = world_.player_id;
    st.master_id = npc_id;
    if (it->second.class_id == kClassGuildTributeMaster && !guild_.in_guild()) return false;
    return submit_zone_action(messages::outgoing(it->second.class_id == kClassGuildTributeMaster
        ? messages::GameOpcode::OpenGuildTributeMaster : messages::GameOpcode::OpenTributeMaster, st),
        "Open tribute master");
}

void GameConnection::tribute_describe(std::uint32_t tribute_id) {
    if (state_ != ConnectionState::InZone || tribute_id == kTributeNone || tribute_.descriptions.contains(tribute_id)) return;
    send_zone(messages::outgoing(messages::GameOpcode::SelectTribute, TributeSelection{world_.player_id, tribute_id}));
}

void GameConnection::tribute_change(const TributeSettings& settings) {
    if (state_ != ConnectionState::InZone) return;
    TributeSettings s = settings;
    s.master_id = tribute_.master_id;
    send_zone(messages::outgoing(messages::GameOpcode::TributeUpdate, s));
}

void GameConnection::tribute_toggle(bool on) {
    if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(messages::GameOpcode::TributeToggle, on));
}

bool GameConnection::tribute_donate_cursor() {
    const Item* held = inventory_.cursor();
    if (state_ != ConnectionState::InZone || !tribute_.master_id || !held) return false;
    if (refuse_kept(held, "donate")) return false;
    if (!begin_tribute_donation(false, *held, tribute_.master_id)) return false;
    TributeItem t;
    t.slot = ItemSlot::possessions(kSlotCursor);
    t.quantity = held->stackable() ? held->stack_count : 1;
    t.master_id = tribute_.master_id;
    if (!submit_zone_action(messages::outgoing(messages::GameOpcode::TributeItem, t),
                            "Donate tribute item")) {
        pending_tribute_donation_.reset();
        return false;
    }
    return true;
}

bool GameConnection::tribute_donate_platinum(std::uint32_t platinum) {
    if (state_ != ConnectionState::InZone || !tribute_.master_id || platinum == 0) return false;
    TributeMoney t;
    t.platinum = platinum;
    t.master_id = tribute_.master_id;
    return submit_zone_action(messages::outgoing(messages::GameOpcode::TributeMoney, t),
                              "Donate tribute money");
}

std::uint32_t GameConnection::GuildTribute::active_cost() const {
    std::uint32_t cost = 0;
    for (int i = 0; i < kGuildTributeSlots; ++i) {
        auto it = abilities.find(active.tributes[static_cast<std::size_t>(i)]);
        std::uint32_t tier = active.tiers[static_cast<std::size_t>(i)];
        if (it != abilities.end() && tier < kTributeTiers) cost += it->second.tiers[tier].cost;
    }
    return cost;
}

std::int64_t GameConnection::GuildTribute::time_left(std::int64_t now) const {
    if (!active.enabled) return active.timer_ms;
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(active.timer_ms) - (now - timer_at));
}

void GameConnection::guild_tribute_describe(std::uint32_t tribute_id, std::uint32_t tier) {
    if (state_ != ConnectionState::InZone || tribute_id == kTributeNone || guild_tribute_.descriptions.contains(tribute_id)) return;
    send_zone(messages::outgoing(messages::GameOpcode::GuildSelectTribute, TributeSelection{0, tribute_id, tier}));
}

bool GameConnection::can_change_guild_tribute() const {
    return state_ == ConnectionState::InZone && guild_.in_guild() &&
           (!guild_tribute_.active_known || !guild_tribute_.active.guild_id ||
            guild_tribute_.active.guild_id == guild_.guild_id) &&
           has_guild_permission(game::kGuildPermTributeBenefits);
}

void GameConnection::guild_tribute_change(const GuildTributeActive& v) {
    if (!can_change_guild_tribute() || !guild_tribute_.master_id) return;
    // EQEmu's save stores the benefits (turned off, the timer reset) and tells the guild; the modify echo with
    // command 0 asks it for the guild's active tributes again.
    send_zone(messages::outgoing(messages::GameOpcode::GuildSaveActiveTributes, GuildTributeSave{guild_tribute_.master_id, v, false}));
    send_zone(messages::outgoing(messages::GameOpcode::GuildModifyBenefits, GuildTributeSave{guild_tribute_.master_id, v, true}));
}

void GameConnection::guild_tribute_toggle(bool on) {
    if (can_change_guild_tribute()) send_zone(messages::outgoing(messages::GameOpcode::GuildTributeToggleReq, on));
}

void GameConnection::guild_tribute_opt(bool in) {
    if (state_ == ConnectionState::InZone && guild_.in_guild()) send_zone(messages::outgoing(messages::GameOpcode::GuildOptInOut, GuildTributeMember{guild_.guild_id, character_, in}));
}

void GameConnection::guild_tribute_opt_out(const std::string& member) {
    // WndNotification on TributeOptOutButton: Send_OP_GuildOptInOut(the selected member's name, 0).
    if (state_ == ConnectionState::InZone && guild_.in_guild() && !member.empty() &&
        (iequals(member, character_) || has_guild_permission(game::kGuildPermTributeOthers)))
        send_zone(messages::outgoing(messages::GameOpcode::GuildOptInOut, GuildTributeMember{guild_.guild_id, member, false}));
}

void GameConnection::guild_trophy_opt_out(const std::string& member) {
    // TrophyTributeOptOutButton: Send_OP_GuildTrophyOptInOut(name, 0); EQEmu does not know the opcode.
    if (state_ == ConnectionState::InZone && guild_.in_guild() && !member.empty())
        send_zone(messages::outgoing(messages::GameOpcode::GuildTrophyOptInOut, GuildTributeMember{guild_.guild_id, member, false, true}));
}

bool GameConnection::guild_tribute_donate_cursor() {
    const Item* held = inventory_.cursor();
    if (state_ != ConnectionState::InZone || !guild_tribute_.master_id || !held) return false;
    if (refuse_kept(held, "donate")) return false;
    if (!begin_tribute_donation(true, *held, guild_tribute_.master_id)) return false;
    if (!submit_zone_action(messages::outgoing(messages::GameOpcode::GuildTributeDonateItem,
            GuildTributeItem{ItemSlot::possessions(kSlotCursor), held->stackable() ? held->stack_count : 1,
                             guild_tribute_.master_id, guild_.guild_id}), "Donate guild tribute item")) {
        pending_tribute_donation_.reset();
        return false;
    }
    return true;
}

bool GameConnection::guild_tribute_donate_platinum(std::uint32_t platinum) {
    if (state_ != ConnectionState::InZone || !guild_tribute_.master_id || platinum == 0) return false;
    // Send_OP_GuildTributeDonatePlat @0x483e80 sends only while carried platinum (PcProfile+0x33b8) covers the
    // amount, then sets CTributeMasterWnd+0x2d8, which greys Donate (UpdateDonateButton @0x785f40) until the reply
    // (@0x483cd0) clears it; guild_platinum_pending() is that flag, and the window greys its button by it.
    if (guild_platinum_pending_ || platinum > coins_.platinum) return false;
    const auto master = guild_tribute_.master_id;
    const auto master_serial = guild_tribute_.master_serial;
    const auto guild_id = guild_.guild_id;
    const auto context = interaction_context();
    // A hook may deliver the server reply, change masters, or retire the zone while the send is in progress.
    // Reserve the reply before the outbound packet becomes observable, and admit it only for this session.
    guild_platinum_pending_ = platinum;
    const auto current = [&] {
        return state_ == ConnectionState::InZone && !zone_change_pending_ &&
               interaction_context() == context && guild_tribute_.master_id == master &&
               guild_tribute_.master_serial == master_serial && guild_.guild_id == guild_id &&
               guild_platinum_pending_ == platinum;
    };
    if (!submit_zone_action(messages::outgoing(messages::GameOpcode::GuildTributeDonatePlat,
            GuildTributeCoins{platinum, master, guild_id}), "Donate guild tribute platinum", current)) {
        if (interaction_context() == context && guild_tribute_.master_serial == master_serial &&
            guild_platinum_pending_ == platinum)
            guild_platinum_pending_ = 0;
        return false;
    }
    return true;
}

void GameConnection::reset_guild_tribute_bonuses(std::uint32_t next_guild_id) {
    auto& g = guild_tribute_;
    retire_tribute_items(inventory_, kInvGuildTribute, false, false,
                          g.active.tributes, g.active.tiers, g.active.tributes, g.active.tiers);
    // Source can send the new guild's ranks and active benefits before our GuildID appearance. Preserve that
    // matching snapshot, but never carry the old guild's bonus items or participation across membership.
    if (!next_guild_id || next_guild_id == kTributeNone || !g.active_known || g.active.guild_id != next_guild_id) {
        g.active = {};
        g.active_known = false;
        g.timer_at = 0;
    }
    g.opted_in.reset();
    g.master_id = 0;
    guild_platinum_pending_ = 0;
    if (pending_tribute_donation_ && pending_tribute_donation_->guild) pending_tribute_donation_.reset();
}

void GameConnection::reset_tribute_bonuses() {
    auto& t = tribute_;
    retire_tribute_items(inventory_, kInvTribute, false, false,
                          t.settings.tributes, t.settings.tiers, t.settings.tributes, t.settings.tiers);
    t.settings.active = false;
    t.settings_known = false;
    t.master_id = 0;
    reset_guild_tribute_bonuses();
}

void GameConnection::handle_tribute(const messages::GameMessage& message) {
    using messages::GameOpcode;
    const auto op = message.opcode;
    auto& g = guild_tribute_;
    switch (op) {
    case GameOpcode::SendGuildTributes:
        if (auto a = messages::message_get<TributeAbility>(message)) g.abilities[a->id] = std::move(*a);
        break;
    case GameOpcode::GuildSelectTribute:
        if (auto d = messages::message_get<TributeDescription>(message)) g.descriptions[d->tribute_id] = std::move(d->text);
        break;
    case GameOpcode::GuildSendActiveTributes:
    case GameOpcode::GuildTributeToggleReply:
        if (auto v = messages::message_get<GuildTributeActive>(message)) {
            // Incoming messages are immutable; resolve the missing guild context in the stored snapshot.
            retire_tribute_items(inventory_, kInvGuildTribute, v->enabled, g.active_known,
                                  g.active.tributes, g.active.tiers, v->tributes, v->tiers);
            g.active = *v;
            if (!g.active.guild_id) g.active.guild_id = received_guild_permission_context();
            g.active_known = true;
            g.timer_at = now_ms();
        }
        break;
    case GameOpcode::GuildTributeFavorAndTimer:
        if (auto v = messages::message_get<GuildTributeFavor>(message)) {
            g.active.favor = v->favor;
            g.active.timer_ms = v->timer_ms;
            g.timer_at = now_ms();
        }
        break;
    case GameOpcode::GuildOptInOut:
        // HandleWorldMessage_GuildTribute: the named member (CGuild::FindMemberByName, _strnicmp over 64; none,
        // nothing) gets the
        // states, donations, time and flag (0x482cf0), then its tribute list row is refreshed.
        if (auto v = messages::message_get<GuildTributeOpt>(message)) {
            if (iequals(v->member, character_)) {
                g.opted_in = v->on;
                if (!v->on)
                    retire_tribute_items(inventory_, kInvGuildTribute, false, false,
                                          g.active.tributes, g.active.tiers, g.active.tributes, g.active.tiers);
            }
            for (auto& m : guild_.members) {
                if (!iequals(m.name, v->member)) continue;
                m.tribute_on = v->on;
                m.trophy_on = v->trophy_on;
                m.donations = v->donations;
                m.last_donation = v->time;
                m.tribute_flag = (v->command & 0xff) != 0;
                break;
            }
        }
        break;
    case GameOpcode::GuildTributeDonateItem:
        if (auto v = messages::message_get<GuildTributeDonation>(message)) {
            // EQEmu consumes guild offerings even when their favour is zero.
            if (!finish_tribute_donation(true, v->slot, v->quantity, 0, true)) break;
            g.last_donation = static_cast<std::int32_t>(v->favor);
            ++g.donation_answers;
        }
        break;
    case GameOpcode::GuildTributeDonatePlat:
        if (auto v = messages::message_get<GuildTributePlatinum>(message)) {
            // Tribute_HandleGuildDonationAccepted @0x483cd0 clears the pending flag on any reply, and a quantity of
            // -1 debits nothing. A client safety deviation: a reply whose quantity is not the pending request's
            // is ignored (no debit, the request stays pending), since the reply names no transaction and a stale
            // one must not debit the purse twice; a later master-open reply or a reset still clears the request.
            if (guild_platinum_pending_ && v->platinum == 0xffffffffu) {
                guild_platinum_pending_ = 0;
                break;
            }
            if (!guild_platinum_pending_ || v->platinum != guild_platinum_pending_) {
                log::warn("ignoring an unmatched guild tribute platinum reply");
                break;
            }
            g.last_donation = static_cast<std::int32_t>(v->favor);
            // EQEmu takes the platinum without a coin update (TakePlatinum(quantity, false)); TakePlatinum takes
            // nothing when the platinum is short (and answers anyway), and take_money likewise leaves the coins.
            // A deviation: the original subtracts carried platinum only (@0x483cd0, PcProfile+0x33b8), but EQEmu's
            // TakeMoneyFromPP takes copper first and makes change, and sends no coin update, so the purse follows
            // the server's debit.
            game::take_money(coins_, static_cast<std::int64_t>(v->platinum) * 1000);
            guild_platinum_pending_ = 0;
        }
        break;
    case GameOpcode::OpenGuildTributeMaster:
        if (auto st = messages::message_get<StartTribute>(message)) {
            g.master_id = st->response && distance_to(st->master_id) <= kTributeRange ? st->master_id : 0;
            g.master_serial = ++npc_session_serial_;
            // EQEmu answered any guild donation sent before this open (Handle_OP_GuildTributeDonateItem replies in
            // the handler or never, as without a guild), so no answer to it is still coming.
            if (pending_tribute_donation_ && pending_tribute_donation_->guild) pending_tribute_donation_.reset();
            guild_platinum_pending_ = 0;
        }
        break;
    case GameOpcode::GuildModifyBenefits:
        break; // the echo of the save; the benefits come with OP_GuildSendActiveTributes
    case GameOpcode::TributeInfo:
        if (auto a = messages::message_get<TributeAbility>(message)) tribute_.abilities[a->id] = std::move(*a);
        break;
    case GameOpcode::TributeUpdate:
        if (auto s = messages::message_get<TributeSettings>(message)) {
            retire_tribute_items(inventory_, kInvTribute, s->active, tribute_.settings_known,
                                  tribute_.settings.tributes, tribute_.settings.tiers, s->tributes, s->tiers);
            tribute_.settings = *s;
            tribute_.settings_known = true;
        }
        break;
    case GameOpcode::TributeTimer:
        if (auto t = messages::message_get<std::uint32_t>(message)) {
            tribute_.timer_ms = *t;
            tribute_.timer_at = now_ms();
        }
        break;
    case GameOpcode::TributePointUpdate:
        if (auto t = messages::message_get<TributePoints>(message)) {
            tribute_.points = t->points;
            tribute_.career = t->career;
        }
        break;
    case GameOpcode::SelectTribute:
        if (auto d = messages::message_get<TributeDescription>(message)) tribute_.descriptions[d->tribute_id] = std::move(d->text);
        break;
    case GameOpcode::OpenTributeMaster:
        if (auto st = messages::message_get<StartTribute>(message)) {
            tribute_.master_id = st->response && distance_to(st->master_id) <= kTributeRange ? st->master_id : 0;
            tribute_.master_serial = ++npc_session_serial_;
            // Handle_OP_TributeItem replies in the handler or returns silently (a master out of range or gone), so
            // an answer to the earlier donation came before this one or never comes.
            if (pending_tribute_donation_ && !pending_tribute_donation_->guild) pending_tribute_donation_.reset();
        }
        break;
    case GameOpcode::TributeItem:
        if (auto t = messages::message_get<TributeItem>(message)) {
            // A personal refusal (zero favour) keeps the item.
            if (!finish_tribute_donation(false, t->slot, t->quantity, t->master_id, t->points > 0)) break;
            tribute_.last_donation = t->points;
            ++tribute_.donation_answers;
        }
        break;
    case GameOpcode::TributeMoney:
        if (auto t = messages::message_get<TributeMoney>(message)) {
            tribute_.last_donation = t->points;
            // EQEmu takes the platinum without a coin update (TakeMoneyFromPP); 0 points: it could not.
            if (t->points > 0 && !game::take_money(coins_, static_cast<std::int64_t>(t->platinum) * 1000)) coins_ = {};
        }
        break;
    default:
        break;
    }
}

} // namespace hailstone::net
