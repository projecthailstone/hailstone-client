#pragma once
#include <optional>
#include "net/messages/tracking.h"
#include "net/wire/app_packet.h"
namespace hailstone::net { std::optional<TrackReply> decode_track_reply(const AppPacket& packet); }
