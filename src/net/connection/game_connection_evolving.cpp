// Evolving transfer previews and requests. The server owns compatibility and resulting item forms.

#include "net/connection/game_connection.h"

#include <algorithm>
#include <set>

namespace hailstone::net {
namespace {

void collect_evolving(const Item& item, const ItemSlot& slot, std::vector<Item>& out, std::set<std::uint64_t>& seen) {
    const auto id = evolving_item_id(item);
    if (id && seen.insert(id).second) {
        out.push_back(item);
        out.back().slot = slot;
    }
    // Augments cannot be selected: the server searches worn, personal and cursor items, including bag contents.
    if (!item.is_bag() || slot.sub >= 0) return;
    for (const auto& child : item.contents) {
        if (child.index >= kMaxBagContents) continue;
        auto at = slot;
        at.sub = static_cast<std::int16_t>(child.index);
        collect_evolving(child.item, at, out, seen);
    }
}

const Item* by_id(const std::vector<Item>& items, std::uint64_t id) {
    if (!id) return nullptr;
    const auto it = std::find_if(items.begin(), items.end(), [id](const Item& item) { return evolving_item_id(item) == id; });
    return it == items.end() ? nullptr : &*it;
}

// Bind a confirmation to semantic item values, not RoF2 serialization. Experience
// ticks alter only progress/activation; the server decides transfer results.
std::optional<Item> snapshot(const Item* item) {
    if (!item) return std::nullopt;
    Item bound = *item;
    bound.evolving.progress = 0.0;
    bound.evolving.activated = 0;
    return bound;
}

} // namespace

std::vector<Item> GameConnection::collect_evolving_items() const {
    std::vector<Item> items;
    std::set<std::uint64_t> seen;
    for (const auto& [key, item] : inventory_.items())
        if (key.first == kInvPossessions && key.second >= 0 && key.second <= kSlotCursor)
            collect_evolving(item, ItemSlot::possessions(key.second), items, seen);
    return items;
}

const std::vector<Item>& GameConnection::evolving_transfer_items() const {
    static const std::vector<Item> none;
    if (state_ != ConnectionState::InZone) return none;
    auto& cache = evolving_cache_;
    if (!cache.items_valid || cache.items_revision != inventory_.revision()) {
        cache.items = collect_evolving_items();
        cache.items_revision = inventory_.revision();
        cache.items_valid = true;
        ++cache.builds;
    }
    return cache.items;
}

// Whether the selected pair's live items (or their absence) are those the snapshots were taken of. `fresh` builds
// the list again and ignores the cache, for the check just before the transfer goes out.
bool GameConnection::evolving_originals_match(bool fresh) const {
    const auto& view = evolving_transfer_;
    auto compare = [&](const std::vector<Item>& items) {
        return snapshot(by_id(items, view.source_id)) == evolving_source_snapshot_ &&
               snapshot(by_id(items, view.destination_id)) == evolving_destination_snapshot_;
    };
    if (fresh) return compare(state_ == ConnectionState::InZone ? collect_evolving_items() : std::vector<Item>{});
    auto& cache = evolving_cache_;
    if (!cache.match_valid || cache.match_revision != inventory_.revision()) {
        cache.match = compare(evolving_transfer_items());
        cache.match_revision = inventory_.revision();
        cache.match_valid = true;
        ++cache.checks;
    }
    return cache.match;
}

void GameConnection::close_evolving_transfer() {
    const auto revision = evolving_transfer_.revision + 1;
    evolving_transfer_ = {};
    evolving_transfer_.revision = revision;
    evolving_source_snapshot_.reset();
    evolving_destination_snapshot_.reset();
    evolving_cache_.match_valid = false;
}

void GameConnection::open_evolving_transfer() {
    if (state_ != ConnectionState::InZone || evolving_transfer_.shown) return;
    close_evolving_transfer();
    evolving_transfer_.shown = true;
    evolving_transfer_.status = "Select source and destination evolving items.";
}

bool GameConnection::select_evolving_transfer(bool source, std::uint64_t id) {
    if (state_ != ConnectionState::InZone || !evolving_transfer_.shown) return false;
    const auto& items = evolving_transfer_items();
    if (id && !by_id(items, id)) return false;
    if (id && id == (source ? evolving_transfer_.destination_id : evolving_transfer_.source_id)) return false;
    // A refused transfer changes no items: EQEmu only prints "Transfer Failed" in chat (DoEvolveTransferXP), so the
    // window would wait for ever. RoF2 clears both selections once it has sent the transfer
    // (Send_OP_EvolveItem_006b00b0), leaving its buttons free; here a new selection ends the wait and starts over.
    evolving_transfer_.submitted = false;
    (source ? evolving_transfer_.source_id : evolving_transfer_.destination_id) = id;
    request_evolving_details();
    return true;
}

void GameConnection::request_evolving_details() {
    auto& view = evolving_transfer_;
    ++view.revision;
    view.details.reset();
    view.pending = false;
    const auto& items = evolving_transfer_items();
    const auto* source = by_id(items, view.source_id);
    const auto* destination = by_id(items, view.destination_id);
    evolving_source_snapshot_ = snapshot(source);
    evolving_destination_snapshot_ = snapshot(destination);
    evolving_cache_.match = true; // taken from these items just now
    evolving_cache_.match_revision = inventory_.revision();
    evolving_cache_.match_valid = state_ == ConnectionState::InZone;
    if (!source || !destination) {
        view.status = "Select source and destination evolving items.";
        return;
    }
    const auto revision = view.revision;
    view.pending = true;
    view.status = "Waiting for the server's transfer preview.";
    if (!send_zone(messages::outgoing(messages::GameOpcode::EvolveItem,
                                      EvolvingRequest{EvolvingAction::Details, view.source_id, view.destination_id}))) {
        if (view.revision == revision) {
            view.pending = false;
            view.status = "The transfer preview request was not sent.";
        }
    }
}

bool GameConnection::can_transfer_evolving() const {
    const auto& view = evolving_transfer_;
    if (state_ != ConnectionState::InZone || !view.shown || view.pending || view.submitted || !view.details ||
        !view.details->compatibility || !view.details->source || !view.details->destination ||
        !view.source_id || !view.destination_id || view.source_id == view.destination_id) return false;
    const auto& items = evolving_transfer_items();
    return by_id(items, view.source_id) && by_id(items, view.destination_id) && evolving_originals_match();
}

bool GameConnection::request_evolving_transfer(std::uint64_t expected_revision) {
    if (evolving_transfer_.revision != expected_revision || !can_transfer_evolving() ||
        !evolving_originals_match(true)) return false;
    auto& view = evolving_transfer_;
    const auto source = view.source_id, destination = view.destination_id;
    view.submitted = true;
    view.details.reset();
    ++view.revision;
    const auto revision = view.revision;
    view.status = "Transfer sent. The server replaces both items when it accepts; if it refuses, chat says so. "
                  "Select an item to start again.";
    if (!send_zone(messages::outgoing(messages::GameOpcode::EvolveItem,
                                      EvolvingRequest{EvolvingAction::Transfer, source, destination}))) {
        if (view.revision == revision) {
            view.submitted = false;
            view.status = "The transfer request was not sent. Select the items again to retry.";
        }
        return false;
    }
    return true;
}

void GameConnection::update_evolving_transfer() {
    if (state_ != ConnectionState::InZone) {
        if (evolving_transfer_.shown) close_evolving_transfer();
        return;
    }
    auto& view = evolving_transfer_;
    if (!view.shown) return;
    const auto& items = evolving_transfer_items();
    const auto* source = by_id(items, view.source_id);
    const auto* destination = by_id(items, view.destination_id);
    if (view.submitted && source && destination) return;
    if (evolving_originals_match()) return;
    if (!source) view.source_id = 0;
    if (!destination) view.destination_id = 0;
    if (view.submitted) {
        view.source_id = view.destination_id = 0;
        view.submitted = false;
    }
    request_evolving_details();
}

void GameConnection::handle_evolving_item(const messages::GameMessage& message) {
    // Progress updates are handled in handle_item; the codec delivers either
    // the server's open notification or typed transfer-preview values here.
    if (const auto* open = messages::message_get<EvolvingUpdate>(message)) {
        if (open->action != EvolvingAction::Open) return;
        close_evolving_transfer();
        open_evolving_transfer();
        return;
    }
    const auto* details = messages::message_get<EvolvingTransferDetails>(message);
    if (!details) return;
    auto& view = evolving_transfer_;
    // CItemExpTransferWnd_HandleEvolveItem @0x6af8e0 matches both original IDs. RoF2 has no request nonce, so an older
    // answer for the same pair cannot be distinguished. Revalidate live originals here and again on commit;
    // the server recalculates the transfer rather than accepting the projected items as a request.
    if (!view.shown || !view.pending || state_ != ConnectionState::InZone ||
        view.source_id != details->source_id || view.destination_id != details->destination_id) return;
    if (!evolving_originals_match()) {
        update_evolving_transfer();
        return;
    }
    view.details = *details;
    view.pending = false;
    ++view.revision;
    view.status = details->compatibility && details->source && details->destination ? "Transfer preview ready." :
                                                                                 "These items are not compatible for transfer.";
}

} // namespace hailstone::net
