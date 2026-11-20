#include "net/protocol/extensions/hailstone/codec.h"
#include "net/messages/hailstone.h"
#include "net/messages/server_files.h"
#include "net/messages/zone.h"
#include "net/messages/social.h"
#include "net/protocol/extensions/hailstone/settings_packets.h"
#include "net/protocol/extensions/hailstone/quest_npc_packets.h"
#include "net/protocol/extensions/hailstone/zone_link_packets.h"
#include "net/protocol/extensions/hailstone/guild_packets.h"
#include "net/protocol/extensions/hailstone/server_files_packets.h"

namespace hailstone::net::hailstone_extension {
using messages::GameOpcode;
using messages::ServiceScope;
using messages::CodecResult;
using messages::CodecErrorKind;
using protocol::WirePacketBatch;
using messages::MessageBatch;

bool Codec::supports(ServiceScope scope, GameOpcode op, protocol::CodecDirection direction) const {
    if (scope != ServiceScope::Zone) return false;
    if (direction == protocol::CodecDirection::Outbound)
        return op == GameOpcode::HailstoneSettingsRequest || op == GameOpcode::HsQuestNpcsRequest ||
               op == GameOpcode::HsZoneLinksRequest || op == GameOpcode::HailstoneGuild ||
               op == GameOpcode::HsFilesRequest;
    return op == GameOpcode::HailstoneSettings || op == GameOpcode::HsQuestNpcs ||
           op == GameOpcode::HsZoneLinks || op == GameOpcode::HailstoneGuildReply ||
           op == GameOpcode::HsFiles;
}

CodecResult<WirePacketBatch> Codec::encode(ServiceScope scope, const messages::GameMessage& message) const {
    if (!supports(scope, message.opcode, protocol::CodecDirection::Outbound))
        return CodecResult<WirePacketBatch>::failure({CodecErrorKind::UnsupportedOpcode, "unknown Hailstone extension command"});
    if (message.opcode == GameOpcode::HsFilesRequest) {
        const auto* r = messages::message_get<ServerFilesRequest>(message);
        if (!r) return CodecResult<WirePacketBatch>::failure({CodecErrorKind::UnsupportedPayload, "server file request values required"});
        if (r->kind == ServerFilesRequest::Kind::List)
            return CodecResult<WirePacketBatch>::success({encode_server_files_list_request(r->first)});
        if (r->length == 0 || r->length > kServerFilesChunkMax)
            return CodecResult<WirePacketBatch>::failure({CodecErrorKind::InvalidValue, "server file chunk length out of range"});
        return CodecResult<WirePacketBatch>::success({encode_server_files_chunk_request(r->list_id, r->index, r->offset, r->length)});
    }
    if (message.opcode == GameOpcode::HailstoneGuild) {
        if (const auto* g = messages::message_get<HailstoneGuild>(message))
            return CodecResult<WirePacketBatch>::success({encode_hailstone_guild(*g)});
        return CodecResult<WirePacketBatch>::failure({CodecErrorKind::UnsupportedPayload, "guild extension requires guild values"});
    }
    if (!messages::message_is<std::monostate>(message))
        return CodecResult<WirePacketBatch>::failure({CodecErrorKind::UnsupportedPayload, "extension request requires empty payload"});
    if (message.opcode == GameOpcode::HailstoneSettingsRequest)
        return CodecResult<WirePacketBatch>::success({encode_hailstone_settings_request()});
    if (message.opcode == GameOpcode::HsQuestNpcsRequest)
        return CodecResult<WirePacketBatch>::success({encode_quest_npcs_request()});
    return CodecResult<WirePacketBatch>::success({encode_zone_links_request()});
}

CodecResult<MessageBatch> Codec::decode(ServiceScope scope, const AppPacket& packet) const {
    if (scope != ServiceScope::Zone)
        return CodecResult<MessageBatch>::failure({CodecErrorKind::UnsupportedOpcode, "not a zone extension"});
    if (packet.opcode == kOpHailstoneSettings) {
        if (auto v = decode_hailstone_settings(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::HailstoneSettings, *v}});
    } else if (packet.opcode == kOpHsQuestNpcs) {
        if (auto v = decode_quest_npcs(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::HsQuestNpcs, std::move(*v)}});
    } else if (packet.opcode == kOpHsZoneLinks) {
        if (auto v = decode_zone_links(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::HsZoneLinks, std::move(*v)}});
    } else if (packet.opcode == kOpHsFiles) {
        if (auto v = decode_server_files(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::HsFiles, std::move(*v)}});
    } else if (packet.opcode == kOpHailstoneGuildReply) {
        if (auto v = decode_hailstone_guild(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::HailstoneGuildReply, std::move(*v)}});
    } else {
        return CodecResult<MessageBatch>::failure({CodecErrorKind::UnsupportedOpcode, "not a Hailstone extension reply"});
    }
    return CodecResult<MessageBatch>::failure({CodecErrorKind::MalformedPacket, "invalid Hailstone extension reply"});
}
} // namespace hailstone::net::hailstone_extension
