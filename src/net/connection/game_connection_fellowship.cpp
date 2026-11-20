// Native fellowship messages and requests. Membership, sharing, camp requirements
// and member limits are decided by the server; a request never edits the roster.
#include "net/connection/game_connection.h"

#include <algorithm>
#include <format>

namespace hailstone::net {

void GameConnection::reset_fellowship(bool zoning) {
    ++fellowship_revision_;
    discard_questions([](const Confirmation& c) { return c.id == kConfirmFellowshipInvite; });
    fellowship_.invite.reset();
    fellowship_invite_expires_ = 0;
    fellowship_.kits.clear();
    if (!zoning) fellowship_ = {};
}

bool GameConnection::handle_fellowship(const messages::GameMessage& message) {
    if (message.opcode == messages::GameOpcode::FellowshipRoster) {
        if (auto received = messages::message_get<FellowshipState>(message)) {
            if (received->members.empty()) {
                if (fellowship_.active() && received->id != fellowship_.id) return true;
                auto vitality = fellowship_.vitality;
                reset_fellowship();
                fellowship_.vitality = vitality;
            } else if (!received->member(character_)) {
                // Native updates concern the local character. A same-fellowship
                // roster without us removes membership; another roster is stale.
                if (received->id == fellowship_.id) {
                    auto vitality = fellowship_.vitality;
                    reset_fellowship();
                    fellowship_.vitality = vitality;
                }
            } else {
                auto kits = fellowship_.id == received->id ? std::move(fellowship_.kits) : std::vector<FellowshipKit>{};
                auto vitality = fellowship_.vitality;
                reset_fellowship();
                fellowship_ = std::move(*received);
                fellowship_.kits = std::move(kits);
                fellowship_.vitality = vitality;
            }
        }
        return true;
    }
    if (message.opcode == messages::GameOpcode::FellowshipKitList) {
        if (auto kits = messages::message_get<std::vector<FellowshipKit>>(message); kits && fellowship_.active()) fellowship_.kits = std::move(*kits);
        return true;
    }
    if (message.opcode == messages::GameOpcode::FellowshipVitality) {
        if (auto vitality = messages::message_get<FellowshipVitality>(message)) {
            fellowship_.vitality = *vitality;
            if (profile_) {
                profile_->fellowship_vitality_normal = vitality->normal;
                profile_->fellowship_vitality_aa = vitality->aa;
            }
        }
        return true;
    }
    if (message.opcode == messages::GameOpcode::FellowshipInvite) {
        if (const auto* received = messages::message_get<FellowshipInvite>(message); received && !fellowship_.active() && state_ == ConnectionState::InZone) {
            auto invite = *received;
            const auto it = world_.spawns.find(invite.inviter_id);
            // Handle_Opcode_0x62ab @0x667db0 shows no dialog when GetSpawnByID
            // fails, and leaves any earlier one alone. Extension beyond RoF2:
            // only the 1080-byte form (a server built for a cross-zone invite)
            // gets a dialog without the inviter's name.
            if (it == world_.spawns.end() && !invite.extended) return true;
            reset_fellowship(true);
            if (it != world_.spawns.end()) invite.inviter_name = it->second.name;
            fellowship_.invite = std::move(invite);
            // The native invite has no lifetime field. Bound unanswered offers to
            // 100 seconds, as the native dialog; the server independently
            // validates its pending invite.
            fellowship_invite_expires_ = session_now_ms() + 100000;
            Confirmation question;
            question.id = kConfirmFellowshipInvite;
            question.text = fellowship_.invite->inviter_name.empty()
                ? "You have been invited to join a fellowship. Accept?"
                : std::format("{} has invited you to join a fellowship. Accept?", fellowship_.invite->inviter_name);
            question.timeout_ms = 100000;
            ask(std::move(question));
        }
        return true;
    }
    return false;
}

void GameConnection::fellowship_invite_tick() {
    if (fellowship_.invite && session_now_ms() >= fellowship_invite_expires_) {
        discard_questions([](const Confirmation& c) { return c.id == kConfirmFellowshipInvite; });
        fellowship_.invite.reset();
        fellowship_invite_expires_ = 0;
    }
}

void GameConnection::fellowship_create() {
    send_fellowship_request(FellowshipAction::Create);
}

void GameConnection::fellowship_set_motd(const std::string& motd) {
    send_fellowship_request(FellowshipAction::Motd, 0, motd);
}

void GameConnection::fellowship_invite_target() {
    send_fellowship_request(FellowshipAction::Invite, world_.target_id);
}

void GameConnection::fellowship_remove(std::uint32_t character_id) {
    send_fellowship_request(FellowshipAction::Remove, character_id);
}

void GameConnection::fellowship_promote(std::uint32_t character_id) {
    send_fellowship_request(FellowshipAction::Promote, character_id);
}

void GameConnection::fellowship_toggle_sharing(std::uint32_t character_id) {
    send_fellowship_request(FellowshipAction::ToggleSharing, character_id);
}

void GameConnection::fellowship_leave() {
    send_fellowship_request(FellowshipAction::Leave);
}

void GameConnection::fellowship_end() {
    send_fellowship_request(FellowshipAction::End);
}

void GameConnection::fellowship_refresh_kits(bool available_only, std::uint32_t category_mask) {
    if (state_ != ConnectionState::InZone || !fellowship_.active()) return;
    const auto revision = fellowship_revision_;
    if (fellowship_revision_ == revision)
        submit_zone_action(messages::outgoing(messages::GameOpcode::FellowshipCampRequest,
                                              FellowshipKitQuery{available_only,category_mask}),
                           "Fellowship camp catalog");
}

void GameConnection::fellowship_create_camp(std::uint32_t camp_type_id) {
    if (camp_type_id) send_fellowship_request(FellowshipAction::CreateCamp, 0, {}, camp_type_id);
}

void GameConnection::fellowship_destroy_camp() {
    send_fellowship_request(FellowshipAction::DestroyCamp);
}

bool GameConnection::fellowship_request_allowed(FellowshipAction action, std::uint32_t target) const {
    if (state_ != ConnectionState::InZone) return false;
    if (action == FellowshipAction::Create) return !fellowship_.active();
    if (!fellowship_.active()) return false;
    const bool leader = fellowship_.is_leader(character_);
    const auto* member = fellowship_.member(target);
    const auto* self = fellowship_.member(character_);
    switch (action) {
    case FellowshipAction::Motd:
    case FellowshipAction::End: return leader;
    case FellowshipAction::Invite: {
        const auto it = world_.spawns.find(target);
        return leader && target != 0 && target <= 65535 && target != world_.player_id &&
            it != world_.spawns.end() && !it->second.is_npc && !it->second.is_corpse;
    }
    case FellowshipAction::Remove:
    case FellowshipAction::Promote: return leader && member && member != self;
    case FellowshipAction::ToggleSharing: return member && (leader || member == self);
    case FellowshipAction::Leave:
    case FellowshipAction::CreateCamp:
    case FellowshipAction::DestroyCamp: return true;
    default: return false;
    }
}

void GameConnection::send_fellowship_request(FellowshipAction action, std::uint32_t target,
                                             std::string_view motd, std::uint32_t camp_type) {
    if (!fellowship_request_allowed(action, target)) return;
    const auto revision = fellowship_revision_;
    const auto current = [&] { return fellowship_revision_ == revision &&
        fellowship_request_allowed(action, target); };
    if (!current()) return;
    submit_zone_action(messages::outgoing(messages::GameOpcode::FellowshipRequest,
                                          FellowshipIntent{action,target,camp_type,std::string(motd)}),
                       "Fellowship request", current);
}

void GameConnection::answer_fellowship_invite(bool accept) {
    const auto expires = fellowship_invite_expires_;
    const auto revision = fellowship_revision_;
    const bool valid = fellowship_.invite && session_now_ms() < expires;
    if (!valid) {
        discard_questions([](const Confirmation& c) { return c.id == kConfirmFellowshipInvite; });
        fellowship_.invite.reset();
        fellowship_invite_expires_ = 0;
        ++fellowship_revision_;
        return;
    }
    if (state_ != ConnectionState::InZone || (accept && fellowship_.active())) return;
    const auto current = [&] { return fellowship_revision_ == revision && fellowship_.invite &&
        session_now_ms() < expires && state_ == ConnectionState::InZone &&
        (!accept || !fellowship_.active()); };
    if (!current() || !submit_zone_action(messages::outgoing(messages::GameOpcode::FellowshipRequest,
            FellowshipIntent{accept ? FellowshipAction::Accept : FellowshipAction::Decline}),
            "Answer fellowship invitation", current) || !current()) return;
    // Observers/plugins may synchronously replace an invitation. Retire only
    // the offer we submitted, never a newer offer installed during the send.
    if (fellowship_revision_ != revision) return;
    discard_questions([](const Confirmation& c) { return c.id == kConfirmFellowshipInvite; });
    fellowship_.invite.reset();
    fellowship_invite_expires_ = 0;
    ++fellowship_revision_;
}

} // namespace hailstone::net
