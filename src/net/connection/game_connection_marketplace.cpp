// The marketplace (CMarketplaceWnd): the catalog and the server's answers (CEverQuest::HandleWorldMessage's cases
// 0x1e9f, 0x09bf, 0x77a7, 0x4b7f, 0x3171, 0x0a37, 0x0114, 0xb07f), and the window's buttons over the model in
// game/marketplace.h. The window model for the UI packs is app/lua_ui_marketplace.cpp.

#include <cstdlib>
#include <set>

#include "core/log.h"
#include "game/item_preview.h"
#include "net/connection/game_connection.h"

namespace hailstone::net {

namespace {

// The game state the original tests (in game, not character select): this client shows the window only in game.
constexpr bool kInGame = true;

} // namespace

// ---------------------------------------------------------------- the server's packets

bool GameConnection::handle_marketplace(const messages::GameMessage& message) {
    auto& m = marketplace_;
    auto& w = m.window;
    switch (message.opcode) {
    case messages::GameOpcode::MarketplaceItems: {
        // HWM case 0x1e9f: the list replaced, PreItemPopulate; a shown window repopulates and says Ready again (its
        // rows are back in state 2 until the states answer), a hidden one repopulates at its next show.
        const auto* v=messages::message_get<std::vector<MarketplaceEntry>>(message);
        if (!v) return true;
        m.catalog = *v;
        log::info("marketplace: {} entries", m.catalog.size());
        // The old catalog's preview does not describe the replacement catalog.
        m.preview_latest_id = 0;
        w.pre_populate();
        if (m.open) {
            w.populate(m.catalog, kInGame);
            marketplace_send_preview();
            m.ready_pending = true;
            if (submit_zone_action(messages::outgoing(messages::GameOpcode::MarketplaceReady),
                                   "Refresh marketplace catalog")) m.ready_pending = false;
        } else {
            w.stale = true;
        }
        return true;
    }
    case messages::GameOpcode::MarketplaceFunding:
        // HWM case 0x09bf: kept for the funding pages, which this client does not offer.
        if (const auto* v=messages::message_get<std::vector<MarketplaceFund>>(message)) m.funding=*v;
        return true;
    case messages::GameOpcode::MarketplaceStates: {
        // HWM case 0x77a7: ignored while a catalog waits for the next show.
        const auto* v=messages::message_get<std::vector<MarketplaceRowState>>(message);
        if (w.stale) {
            log::info("marketplace: states for {} entries ignored (the window has not shown this catalog yet)",
                      v ? v->size() : 0);
            return true;
        }
        if (!v) return true;
        bool changed = false, categories = false;
        std::set<std::uint32_t> ids;
        for (const auto& e : m.catalog) ids.insert(e.id);
        std::size_t unknown = 0;
        for (const auto& s : *v) {
            if (!ids.count(s.entry_id)) ++unknown;
            auto r = w.apply_state(m.catalog, s);
            changed |= r.changed;
            categories |= r.categories;
        }
        std::size_t counted = 0;
        for (const auto& r : w.rows)
            if (r.state != game::kMarketplaceUncounted) ++counted;
        // Rows stay hidden until this feed: the log says what it reached (the first store server tried showed none).
        log::info("marketplace: states for {} entries ({} not in the catalog); {} of {} rows can be shown", v->size(),
                  unknown, counted, w.rows.size());
        if (categories) w.rebuild_tree(kInGame);
        // 0x6d2180: the selected entry's id (0 none or stale).
        if (changed || !w.selected_entry(m.catalog)) {
            w.refilter(m.catalog);
            w.sync_shelf(m.catalog, kInGame);
            marketplace_send_preview();
        }
        return true;
    }
    case messages::GameOpcode::MarketplaceWallet: {
        const auto* v=messages::message_get<MarketplaceWallet>(message);
        if (!v) return true;
        if (v->status != 0) {
            // "Current Balance for %s Type: %d is %d." to the debug log only.
            log::debug("marketplace: wallet status {} type {} balance {}", v->status, v->type, v->balance);
        } else if (v->type == 1) {
            marketplace_set_funds(v->balance);
            m.zip_on_file = v->zip_on_file;
        }
        // Type 2 is the Krono count (KronoManager), not kept by this client.
        return true;
    }
    case messages::GameOpcode::MarketplaceZipStatus:
        // HWM case messages::GameOpcode::MarketplaceZipStatus: the zip flag ORed with the byte +0x48.
        if (const auto* flag=messages::message_get<std::uint8_t>(message); flag && *flag) m.zip_on_file=true;
        return true;
    case messages::GameOpcode::MarketplacePurchaseResult:
        if (const auto* v=messages::message_get<MarketplacePurchaseResult>(message)) marketplace_result(*v,false);
        return true;
    case messages::GameOpcode::MarketplaceUnavailable:
        // 0x6d2c10: eqstr 7631 in colour 15, the window hidden.
        system(7631, {}, 15);
        if (m.open) set_marketplace_open(false);
        return true;
    case messages::GameOpcode::MarketplacePreview: {
        // 0x6d5e70 has no request id. Requests are serialized so the reply belongs
        // to the oldest admitted selection; a superseded or cancelled selection is discarded.
        if (!m.preview_inflight) return true;
        const auto* r = messages::message_get<ItemPreviewReply>(message);
        if (!r) return true;
        const auto answered_id = *m.preview_inflight;
        m.preview_inflight.reset();
        if (answered_id == m.preview_latest_id && m.open) {
            const game::ItemPreviewShow s = game::item_preview_show(r->item, r->trailing);
            m.preview.show = s.show;
            m.preview.on_player = s.on_player;
            if (s.show) m.preview.item = r->item;
            else m.preview.item.reset();
            ++m.preview.serial;
            w.preview_tab = s.show;
        }
        // Now admit the newest queued selection, not every intermediate click.
        marketplace_send_preview();
        return true;
    }
    default: return false;
    }
}

// ---------------------------------------------------------------- the window

void GameConnection::marketplace_build() {
    // The constructor's tail (0x6e18eb..0x6e1a9f), once per game UI: the hello, the catalog asked for while the list
    // is empty (else the rows made from it), the funding tiers likewise, Ready.
    auto& m = marketplace_;
    // Building the UI is not equivalent to admitting its initial requests.
    // A rejected request leaves the build retryable on the next in-zone frame.
    if (!submit_zone_action(messages::outgoing(messages::GameOpcode::MarketplaceHello),
                            "Initialize marketplace")) return;
    if (m.catalog.empty() && !submit_zone_action(
            messages::outgoing(messages::GameOpcode::MarketplaceRequestItems),
            "Request marketplace catalog")) return;
    if (m.funding.empty() && !submit_zone_action(
            messages::outgoing(messages::GameOpcode::MarketplaceRequestFunding),
            "Request marketplace funding")) return;
    if (!submit_zone_action(messages::outgoing(messages::GameOpcode::MarketplaceReady),
                            "Marketplace ready")) return;
    m.built = true;
    m.ready_pending = false;
    m.window = {};
    m.wallet = game::kMarketplaceFundsUnknown;
    m.zip_on_file = false;
    if (!m.catalog.empty()) m.window.populate(m.catalog, kInGame);
}

void GameConnection::set_marketplace_open(bool open) {
    auto& m = marketplace_;
    auto& w = m.window;
    if (open == m.open || state_ != ConnectionState::InZone) return;
    if (!open) {
        // AboutToHide @0x6d89a0: the list emptied, the details cleared, the preview off. Nothing is sent.
        m.open = false;
        m.ready_pending = false;
        m.preview.open = false;
        m.preview_latest_id = 0;
        w.preview_request.reset();
        w.clear_details();
        return;
    }
    if (!m.built) marketplace_build();
    if (!m.built || !submit_zone_action(messages::outgoing(messages::GameOpcode::MarketplaceReady),
                                       "Open marketplace")) return;
    m.ready_pending = false;
    m.open = true;
    m.preview.open = true;
    if (w.stale) {
        w.pre_populate();
        w.populate(m.catalog, kInGame);
        w.stale = false;
    }
    w.reset_view(m.catalog, kInGame);
    log::info("marketplace: opened with {} catalog entries, Ready admitted", m.catalog.size());
    m.ping_ms = 0;
    m.processing_ms = 0;
    w.last_find_ms = 0;
    m.busy = false; // UpdateButtonStates
    marketplace_send_preview();
}

void GameConnection::marketplace_open_to_entry(std::uint32_t entry_id) {
    auto& m = marketplace_;
    set_marketplace_open(true);
    if (!m.open) return;
    m.window.pending_entry = entry_id;
    m.window.navigate(m.catalog, entry_id, kInGame);
    marketplace_send_preview();
}

void GameConnection::marketplace_send_preview() {
    auto& m = marketplace_;
    if (!m.window.preview_request) return;
    const std::uint32_t id = *m.window.preview_request;
    if (id == 0) { // SetPreviewState(false, true)
        m.window.preview_request.reset();
        m.preview_latest_id = 0;
        // Keep an admitted request in flight until its response is drained.
        m.preview.show = false;
        m.preview.item.reset();
        ++m.preview.serial;
        return;
    }
    m.preview_latest_id = id;
    if (m.preview_inflight || state_ != ConnectionState::InZone) return;
    if (!submit_zone_action(messages::outgoing(messages::GameOpcode::MarketplacePreviewRequest,id),
                            "Marketplace preview")) return;
    m.window.preview_request.reset();
    m.preview_inflight = id;
}

void GameConnection::marketplace_set_funds(std::uint32_t balance) {
    // Cached: an unchanged balance does nothing, a new one runs UpdateButtonStates (which enables the window).
    auto& m = marketplace_;
    if (m.wallet == balance) return;
    m.wallet = balance;
    m.busy = false;
}

void GameConnection::marketplace_popup(std::string text) {
    Confirmation c;
    c.id = kConfirmMarketplaceNotice;
    c.text = std::move(text);
    c.ok_only = true;
    marketplace_.dialog = true;
    ask(std::move(c));
}

void GameConnection::marketplace_click_category(std::uint32_t id) {
    auto& m = marketplace_;
    if (m.busy) return;
    m.window.click_category(m.catalog, id);
    marketplace_send_preview();
}

void GameConnection::marketplace_select_row(int row) {
    auto& m = marketplace_;
    if (m.busy) return;
    m.window.show_details(m.catalog, row);
    marketplace_send_preview();
}

void GameConnection::marketplace_select_entry(std::uint32_t entry_id) {
    auto& m = marketplace_;
    if (m.busy) return;
    // 0x6dc9d0: the row of that entry (state 2 rows skipped), its details; the view stays.
    for (std::size_t i = 0; i < m.window.rows.size(); ++i) {
        const auto& r = m.window.rows[i];
        if (r.state == game::kMarketplaceUncounted || r.entry < 0) continue;
        if (m.catalog[static_cast<std::size_t>(r.entry)].id == entry_id) {
            m.window.show_details(m.catalog, static_cast<int>(i));
            break;
        }
    }
    marketplace_send_preview();
}

void GameConnection::marketplace_find() {
    auto& m = marketplace_;
    if (m.busy) return;
    m.window.find(m.catalog, m.find_text, eq_time());
    marketplace_send_preview();
}

void GameConnection::marketplace_sort(int choice) {
    auto& m = marketplace_;
    if (m.busy) return;
    m.window.sort(m.catalog, choice);
    marketplace_send_preview();
}

void GameConnection::marketplace_set_view(game::MarketplaceView v) {
    if (!marketplace_.busy) marketplace_.window.set_view(v);
}

void GameConnection::marketplace_set_quantity_text(const std::string& text) {
    auto& m = marketplace_;
    if (m.busy || !m.window.selected_entry(m.catalog)) return; // QtyEdit is disabled with no entry
    m.window.set_quantity(m.catalog, static_cast<std::uint32_t>(std::atoi(text.c_str())));
}

void GameConnection::marketplace_step_quantity(int delta) {
    auto& m = marketplace_;
    if (m.busy || m.window.page != game::MarketplacePage::Main) return;
    if (delta > 0 && !marketplace_quantity_up_enabled()) return;
    if (delta < 0 && !marketplace_quantity_down_enabled()) return;
    // atoi of the edit's text, which SetQuantity keeps equal to the quantity.
    m.window.set_quantity(m.catalog, static_cast<std::uint32_t>(static_cast<std::int64_t>(m.window.quantity) + delta));
}

bool GameConnection::marketplace_buy_enabled() const {
    const auto& m = marketplace_;
    const auto* r = m.window.selected_row();
    const auto* e = m.window.selected_entry(m.catalog);
    return !m.busy && r && e && r->state == 0 && game::marketplace_can_buy(*e, m.wallet);
}

bool GameConnection::marketplace_confirm_enabled() const {
    // OnProcessFrame: also not while the player is a corpse.
    const game::Spawn* me = world_.player();
    return marketplace_buy_enabled() && !(me && me->is_corpse);
}

bool GameConnection::marketplace_quantity_up_enabled() const {
    const auto& m = marketplace_;
    const auto* e = m.window.selected_entry(m.catalog);
    return !m.busy && e && game::marketplace_can_raise_quantity(*e, m.window.quantity, m.wallet);
}

bool GameConnection::marketplace_quantity_down_enabled() const {
    const auto& m = marketplace_;
    return !m.busy && m.window.selected_entry(m.catalog) && game::marketplace_can_lower_quantity(m.window.quantity);
}

void GameConnection::marketplace_buy() {
    auto& m = marketplace_;
    auto& w = m.window;
    const net::MarketplaceEntry* e = w.selected_entry(m.catalog);
    if (!marketplace_buy_enabled() || w.page != game::MarketplacePage::Main || !e) return;
    // Kind 3 asks PendingRewardMgr whether the quest rewards waiting have reached their cap (exe text @0x9f1368
    // then); this client keeps no pending rewards, so it goes on as kind 0 (docs/unknowns.md).
    if (e->kind == 4) return; // the funding page (the publisher's service)
    if (!m.zip_on_file) w.page = game::MarketplacePage::TaxInfo;
    else if (e->kind == 6) return; // the credit card pages (the publisher's service)
    else w.page = game::MarketplacePage::Confirm;
    w.preview_tab = false; // AdditionalDetails back to SubwindowsPage
}

void GameConnection::marketplace_tax_next() {
    auto& m = marketplace_;
    auto& w = m.window;
    if (m.busy || w.page != game::MarketplacePage::TaxInfo) return;
    const net::MarketplaceEntry* e = w.selected_entry(m.catalog);
    if (std::atoi(m.zip_text.c_str()) != 0 && m.zip_text.size() < 6) {
        if (!submit_zone_action(messages::outgoing(messages::GameOpcode::MarketplaceZip,
                MarketplacePostalCode{m.zip_text}), "Submit marketplace postal code")) return;
        if (!(e && e->kind == 6)) w.page = game::MarketplacePage::Confirm;
        return;
    }
    // The window disabled and the exe's own sentence in a popup.
    m.busy = true;
    marketplace_popup("Please enter your 5 digit zip code!");
}

void GameConnection::marketplace_confirm() {
    auto& m = marketplace_;
    auto& w = m.window;
    const net::MarketplaceEntry* e = w.selected_entry(m.catalog);
    if (!marketplace_confirm_enabled() || w.page != game::MarketplacePage::Confirm || !e) return;
    if (!submit_zone_action(messages::outgoing(messages::GameOpcode::MarketplacePurchase,
                                               MarketplacePurchaseIntent{e->id,w.quantity}),
                            "Marketplace purchase")) return;
    // BeginProcessing @0x6d7a70: everything disabled, "Processing..." (0x9ef8f4) on the results page, the clock set.
    m.busy = true;
    m.result_text = "Processing...";
    w.page = game::MarketplacePage::Results;
    m.processing_ms = now_ms();
}

void GameConnection::marketplace_cancel() {
    // 0x6e3420: back to the main page; nothing is sent or enabled.
    auto& m = marketplace_;
    if (m.busy) return;
    m.window.page = game::MarketplacePage::Main;
}

void GameConnection::marketplace_inspect(bool shift) {
    auto& m = marketplace_;
    const auto* r = m.window.selected_row();
    const auto* e = m.window.selected_entry(m.catalog);
    if (m.busy || !e) return;
    // Kind 3 goes to PendingRewardMgr (0x463c10), which this client does not have (docs/unknowns.md).
    if (e->kind == 3 || !r) return;
    request_item_link(r->item_id, e->name, 0, shift); // Send_OP_ItemLinkResponse @0x69baf0
}

void GameConnection::marketplace_preview_window() {
    auto& m = marketplace_;
    const auto* r = m.window.selected_row();
    if (m.busy || !r) return;
    preview_item_id(r->item_id); // context 0: answered on 0x7f80, into the object preview window
}

void GameConnection::marketplace_item_of_week() {
    auto& m = marketplace_;
    const int iotw = m.window.item_of_week;
    if (m.busy || iotw < 0 || iotw >= static_cast<int>(m.catalog.size())) return;
    m.window.navigate(m.catalog, m.catalog[static_cast<std::size_t>(iotw)].id, kInGame);
    marketplace_send_preview();
}

void GameConnection::marketplace_result(const MarketplacePurchaseResult& r, bool timeout) {
    auto& m = marketplace_;
    // HWM case 0x0a37: the entry by id (FindById @0x7f9000); HandlePurchaseResult disables the window's children,
    // shows it (restored) when hidden, puts the text in an OK popup and, without its <BR> tags, on the results page.
    const MarketplaceEntry* entry = nullptr;
    if (!timeout)
        for (const auto& e : m.catalog)
            if (e.id == r.entry_id) {
                entry = &e;
                break;
            }
    std::string text = game::marketplace_purchase_text(strings_, r, entry);
    m.busy = true;
    if (!m.open) set_marketplace_open(true); // AboutToShow's UpdateButtonStates enables it again
    Confirmation c;
    c.id = kConfirmMarketplaceResult;
    c.text = text;
    c.ok_only = true;
    m.dialog = true;
    ask(std::move(c));
    m.result_text = game::marketplace_strip_breaks(std::move(text));
    if (!timeout) marketplace_set_funds(r.balance);
}

void GameConnection::marketplace_frame(std::int64_t now) {
    auto& m = marketplace_;
    if (state_ == ConnectionState::CharacterSelect && m.built) {
        // The game UI is torn down: the window is made again, hidden, at the next entry (the catalog stays).
        m.built = false;
        m.ready_pending = false;
        m.open = false;
        m.preview.open = false;
    }
    if (state_ != ConnectionState::InZone) {
        m.preview_inflight.reset();
        m.preview_latest_id = 0;
        m.window.preview_request.reset();
        return;
    }
    if (!m.built) marketplace_build();
    if (!m.open) return;
    if (m.ready_pending && submit_zone_action(messages::outgoing(messages::GameOpcode::MarketplaceReady),
                                               "Refresh marketplace catalog")) m.ready_pending = false;
    if (monster_shrouded()) {
        system(7510);
        m.open = false;
        m.preview.open = false;
        return;
    }
    // The keep-alive while shown; wnd +0x320 is zeroed at every show, so the first frame sends it (against the
    // original's tick count, 0 is always 30 s past).
    if (m.ping_ms == 0 || now - m.ping_ms > 30000) {
        if (submit_zone_action(messages::outgoing(messages::GameOpcode::MarketplaceHello),
                               "Marketplace keepalive")) m.ping_ms = now;
    }
    // 60 s on the results page with no popup: HandlePurchaseResult(5) (eqstr 7626) and Results OK.
    if (m.window.page == game::MarketplacePage::Results && m.processing_ms != 0 && now - m.processing_ms > 60000 &&
        !m.dialog) {
        m.processing_ms = 0;
        MarketplacePurchaseResult r;
        r.code = 5;
        marketplace_result(r, true);
        m.window.page = game::MarketplacePage::Main;
    }
}

} // namespace hailstone::net
