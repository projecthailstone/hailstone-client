#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/player_commands.h"
#include "net/messages/item.h"
#include "net/messages/social.h"
#include "net/messages/reward.h"
#include "net/messages/evolving.h"
#include "net/messages/result.h"
#include "net/messages/world.h"
#include "net/protocol/rof2/packets/world_packets.h"

#include <optional>
#include <string>
#include <utility>

namespace hailstone::net::rof2::detail {
using namespace messages;
namespace {
CodecError error(CodecErrorKind kind, std::string detail) { return {kind, std::move(detail)}; }
template<class T> const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }
}

// For polymorphic command opcodes, the opcode chooses a specific typed
// operation: a variant type alone does not establish that the requested
// action is legal. Single-shape messages are checked by their serializer;
// no independent 291-entry payload table is maintained alongside it.
std::optional<CodecError> validate_command_variant(const GameMessage& m) {
    switch (m.opcode) {
    case GameOpcode::SendLoginInfo: {
        const auto* login = payload<LoginInfo>(m);
        if (!login) return error(CodecErrorKind::UnsupportedPayload,
                                 "world login requires LoginInfo");
        if (login->account_id.size() + 2 > 64)
            return error(CodecErrorKind::InvalidValue,
                         "account id is too long for the world login record");
        if (!net::login_info_fits(*login))
            return error(CodecErrorKind::InvalidValue,
                         "session key is too long for the world login record");
        if (login->custom_files_key.size() > 123)
            return error(CodecErrorKind::InvalidValue,
                         "custom files key is too long for the world login record");
        return std::nullopt;
    }
    case GameOpcode::Bandolier:
        if (payload<BandolierPacket>(m) || payload<BandolierHoldIntent>(m)) return std::nullopt;
        return error(CodecErrorKind::UnsupportedPayload, "bandolier requires set or hold values");
    case GameOpcode::EvolveItem:
        if (payload<EvolvingRequest>(m) || payload<EvolvingFinalResult>(m)) return std::nullopt;
        return error(CodecErrorKind::UnsupportedPayload, "evolving item requires a request or final response");
    case GameOpcode::LFGuild:
        if (payload<LFGuildPlayer>(m) || payload<LFGuildGuild>(m) ||
            payload<LFGuildPlayerSearch>(m) || payload<LFGuildGuildSearch>(m)) return std::nullopt;
        return error(CodecErrorKind::UnsupportedPayload, "LFGuild requires a typed posting or search");
    case GameOpcode::AchievementReward:
    case GameOpcode::RewardSelection: {
        const auto* v = payload<RewardIntent>(m);
        if (!v) return error(CodecErrorKind::UnsupportedPayload, "reward action requires RewardIntent");
        using K = RewardIntent::Kind;
        const bool pending = m.opcode == GameOpcode::AchievementReward;
        const bool valid = (v->kind == K::RequestPending && pending) ||
            (v->kind == K::PreviewTask && !pending) ||
            (v->kind == K::Claim && pending) ||
            (v->kind == K::Inspect && ((v->lane == RewardLane::Pending) == pending));
        if (!valid) return error(CodecErrorKind::InvalidValue, "reward action belongs to a different reward lane");
        return std::nullopt;
    }
    case GameOpcode::GuildLeader:
    case GameOpcode::GuildPublicNote:
    case GameOpcode::GuildManageBanker:
    case GameOpcode::GuildUpdate:
    case GameOpcode::GetGuildMOTD: {
        const auto* v = payload<GuildManagementIntent>(m);
        if (!v) return error(CodecErrorKind::UnsupportedPayload, "guild command requires GuildManagementIntent");
        using A = GuildManagementIntent::Action;
        const bool valid =
            (m.opcode == GameOpcode::GuildUpdate &&
                (v->action == A::SetUrl || v->action == A::SetChannel ||
                 v->action == A::RenameRank || v->action == A::SetRankPermission)) ||
            (m.opcode == GameOpcode::GuildLeader && v->action == A::MakeLeader) ||
            (m.opcode == GameOpcode::GuildPublicNote && v->action == A::PublicNote) ||
            (m.opcode == GameOpcode::GuildManageBanker && v->action == A::ManageBanker) ||
            (m.opcode == GameOpcode::GetGuildMOTD && v->action == A::RequestMotd);
        if (!valid) return error(CodecErrorKind::InvalidValue, "guild command action/opcode mismatch");
        return std::nullopt;
    }
    default: return std::nullopt;
    }
}


} // namespace hailstone::net::rof2::detail
