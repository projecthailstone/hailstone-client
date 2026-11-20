// GameConnection processes semantic ability responses; no RoF2 packet inspection.
#include "net/connection/game_connection.h"

#include "game/merchant.h"

namespace hailstone::net {

void GameConnection::handle_ability(const messages::GameMessage& message) {
    const auto op = message.opcode;
    if (op == messages::GameOpcode::Begging || op == messages::GameOpcode::PickPocket) {
        if (const auto* coin = messages::message_get<CoinFind>(message)) server_coin(op, *coin);
    }
    switch (op) {
    case messages::GameOpcode::Begging:
    case messages::GameOpcode::PickPocket:
    case messages::GameOpcode::SpawnAppearance:
    case messages::GameOpcode::CancelSneakHide:
    case messages::GameOpcode::Bind_Wound:
        if (ability_events_.size() == 64) ability_events_.pop_front();
        ability_events_.push_back(message);
        break;
    default:
        break;
    }
}

void GameConnection::server_coin(messages::GameOpcode op, const CoinFind& find) {
    if (find.amount == 0) return;
    if (op == messages::GameOpcode::Begging) {
        if (find.kind == 4) game::server_add_copper(coins_, find.amount);
        else if (find.kind == 3) game::server_add_copper(coins_, static_cast<std::uint64_t>(find.amount) * 10);
    } else if (find.kind == 1) coins_.platinum += find.amount;
    else if (find.kind == 2) coins_.gold += find.amount;
    else if (find.kind == 3) coins_.silver += find.amount;
    else if (find.kind == 4) coins_.copper += find.amount;
}

std::optional<messages::GameMessage> GameConnection::poll_ability_event() {
    if (ability_events_.empty()) return std::nullopt;
    auto message = std::move(ability_events_.front());
    ability_events_.pop_front();
    return message;
}

} // namespace hailstone::net
