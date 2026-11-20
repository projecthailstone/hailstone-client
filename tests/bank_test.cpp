// The bank and stack splits: OP_MoveCoin / OP_MoveItem as the bank window and
// CInvSlot::SliderComplete send them, MoveMoney's coin conversion and the
// bank's item rules (game/bank.h; sources there).

#include <doctest/doctest.h>

#include <cstdint>
#include <tuple>
#include <vector>

#include "game/bank.h"
#include "game/item_moves.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "net/protocol/rof2/packets/trade_packets.h"

using namespace hailstone;
using namespace hailstone::net;

namespace {

std::vector<std::uint8_t> bytes_of(const AppPacket& p) {
    std::vector<std::uint8_t> out;
    for (std::byte b : p.data) out.push_back(std::to_integer<std::uint8_t>(b));
    return out;
}

Item plain(std::uint32_t id) {
    Item it;
    it.id = id;
    it.name = "Thing";
    it.no_rent = 1;   // permanent
    it.no_drop = 255; // tradeable
    it.unknowna1 = -1;
    it.unknowna4 = -1;
    return it;
}

} // namespace

TEST_CASE("OP_MoveCoin for the bank: inventory to cursor to bank and the shared bank") {
    // MoveCoin_Struct: from, to, type from, type to, amount (all int32 LE).
    CHECK(bytes_of(encode(MoveCoin{kCoinCursor, kCoinBank, kCoinPlatinum, kCoinPlatinum, 12})) ==
          std::vector<std::uint8_t>{0, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 12, 0, 0, 0});
    CHECK(bytes_of(encode(MoveCoin{kCoinSharedBank, kCoinCursor, kCoinPlatinum, kCoinPlatinum, 300})) ==
          std::vector<std::uint8_t>{4, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 0x2c, 1, 0, 0});
    auto d = decode_move_coin(encode(MoveCoin{kCoinBank, kCoinCursor, kCoinSilver, kCoinGold, 25}));
    REQUIRE(d);
    CHECK(d->from == kCoinBank);
    CHECK(d->to == kCoinCursor);
    CHECK(d->type_from == kCoinSilver);
    CHECK(d->type_to == kCoinGold);
    CHECK(d->amount == 25);
}

TEST_CASE("OP_MoveItem for a stack split and a bank move") {
    // SliderComplete: {bank slot 5, cursor 33, 7}.
    MoveItem m;
    m.from = {kInvBank, 5, -1, -1, 0, 0};
    m.to = ItemSlot::possessions(kSlotCursor);
    m.count = 7;
    CHECK(bytes_of(encode(m)) == std::vector<std::uint8_t>{1, 0, 0, 0, 5, 0, 0xff, 0xff, 0xff, 0xff, 0, 0,
                                                           0, 0, 0, 0, 33, 0, 0xff, 0xff, 0xff, 0xff, 0, 0,
                                                           7, 0, 0, 0});
    // A shared bank bag slot.
    MoveItem b;
    b.from = ItemSlot::possessions(kSlotCursor);
    b.to = {kInvSharedBank, 1, 3, -1, 0, 0};
    auto d = decode_move_item(encode(b));
    REQUIRE(d);
    CHECK(d->to.same(b.to));
    CHECK(d->count == 0);
}

TEST_CASE("MoveMoney's conversion") {
    // game::move_money (checked against CEverQuest_MoveMoney.jsonl); coins 0 platinum .. 3 copper.
    auto move = [](std::int32_t have, std::int32_t dest, std::uint32_t from_coin, std::uint32_t to_coin, std::int32_t amount) {
        game::MoneyState s;
        s.coins[game::kMoneyInventory][from_coin] = have;
        s.coins[game::kMoneyBank][to_coin] = dest;
        game::MoneyEnv env;
        env.bank_window_active = true;
        auto r = game::move_money(s, env, game::kMoneyInventory, game::kMoneyBank, from_coin, to_coin, amount, 0);
        return std::tuple{r.value, s.coins[game::kMoneyInventory][from_coin], s.coins[game::kMoneyBank][to_coin]};
    };
    CHECK(move(50, 0, 0, 0, 20) == std::tuple{1u, 30, 20});          // same coin: all of it
    CHECK(move(15, 4, 1, 0, 15) == std::tuple{1u, 5, 5});            // 15 gold onto platinum: 10 go, 5 stay
    CHECK(move(2345, 0, 3, 0, 2345) == std::tuple{1u, 345, 2});      // copper to platinum
    CHECK(move(3, 0, 0, 2, 3) == std::tuple{1u, 0, 300});            // platinum to silver
    CHECK(std::get<0>(move(5, 0, 3, 3, 0)) == 0u);                   // nothing
    CHECK(std::get<0>(move(5, 0, 3, 3, 6)) == 0u);                   // more than held
    CHECK(std::get<0>(move(10, 999999995, 0, 0, 10)) == 0u);         // past the 10^9 cap
    CHECK(std::get<0>(move(10, 999999995, 0, 1, 1)) == 0u);
    CHECK(std::get<0>(move(10, 999999990, 0, 1, 1)) == 1u);
}

TEST_CASE("bank slot count: BankWnd or BigBankWnd") {
    game::Inventory inv;
    CHECK(game::bank_slot_count(0, inv) == 16);
    CHECK(game::bank_slot_count(0x400, inv) == 24);
    CHECK(game::bank_slot_count(0x3ff, inv) == 16);
    inv.put({kInvBank, 15, -1, -1, 0, 0}, plain(1));
    CHECK(game::bank_slot_count(0, inv) == 16);
    inv.put({kInvBank, 20, -1, -1, 0, 0}, plain(2));
    CHECK(game::bank_slot_count(0, inv) == 24);
}

TEST_CASE("bank and shared bank refusals") {
    game::Inventory inv;
    Item ok = plain(100);
    CHECK_FALSE(game::bank_refusal(ok));
    CHECK_FALSE(game::shared_bank_refusal(ok, inv));

    Item nostore = plain(101);
    nostore.no_transfer = 1;
    CHECK(game::bank_refusal(nostore).eqstr == 6191);
    auto r = game::shared_bank_refusal(nostore, inv);
    CHECK(r.eqstr == 3195);
    CHECK(r.arg == "No Storage");

    // A bag holding a No Storage item: 5301; an augment in a common item: 5302.
    Item bag = plain(102);
    bag.item_class = 1;
    bag.bag_slots = 4;
    bag.contents.push_back({0, nostore});
    CHECK(game::bank_refusal(bag).eqstr == 5301);
    CHECK(game::shared_bank_refusal(bag, inv).eqstr == 5303);
    Item augmented = plain(103);
    augmented.contents.push_back({0, nostore});
    CHECK(game::bank_refusal(augmented).eqstr == 5302);
    CHECK(game::shared_bank_refusal(augmented, inv).eqstr == 5304);

    // No Trade: refused by the shared bank unless Heirloom; the bank takes it.
    Item notrade = plain(104);
    notrade.no_drop = 0;
    CHECK_FALSE(game::bank_refusal(notrade));
    r = game::shared_bank_refusal(notrade, inv);
    CHECK(r.eqstr == 3195);
    CHECK(r.arg == "No Trade and non-Heirloom");
    notrade.heirloom = 1;
    CHECK_FALSE(game::shared_bank_refusal(notrade, inv));

    Item temp = plain(105);
    temp.no_rent = 0;
    r = game::shared_bank_refusal(temp, inv);
    CHECK(r.eqstr == 3195);
    CHECK(r.arg == "Temporary");

    // Lore: a lore item the shared bank already holds (top level or in a bag).
    Item lore = plain(106);
    lore.lore_group = -1;
    CHECK_FALSE(game::shared_bank_refusal(lore, inv));
    Item sbag = plain(107);
    sbag.item_class = 1;
    sbag.bag_slots = 4;
    sbag.contents.push_back({2, lore});
    inv.put({kInvSharedBank, 0, -1, -1, 0, 0}, sbag);
    CHECK(game::shared_bank_refusal(lore, inv).eqstr == 3194);
    Item grouped = plain(108);
    grouped.lore_group = 77;
    Item same_group = plain(109);
    same_group.lore_group = 77;
    CHECK(game::shares_lore(grouped, same_group));
    CHECK_FALSE(game::shares_lore(lore, plain(200)));
}

TEST_CASE("lore pickup from the shared bank") {
    game::Inventory inv;
    Item lore = plain(300);
    lore.lore_group = -1;
    CHECK_FALSE(game::lore_pickup_refusal(lore, inv));
    // In the shared bank itself: no conflict.
    inv.put({kInvSharedBank, 1, -1, -1, 0, 0}, lore);
    CHECK_FALSE(game::lore_pickup_refusal(lore, inv));
    // Also carried (in a bag): 379.
    Item bag = plain(301);
    bag.item_class = 1;
    bag.bag_slots = 8;
    inv.put(ItemSlot::possessions(kSlotGeneral1), bag);
    inv.put(ItemSlot::possessions(kSlotGeneral1, 3), lore);
    CHECK(game::lore_pickup_refusal(lore, inv).eqstr == 379);
    // A bag that holds it: 5305.
    Item holder = plain(302);
    holder.item_class = 1;
    holder.contents.push_back({0, lore});
    CHECK(game::lore_pickup_refusal(holder, inv).eqstr == 5305);
    // PcClient_OwnsLoreItemDefinition also searches alt storage, mail and real estate, not archived items.
    for (std::int16_t type : {kInvAltStorage, kInvMail, kInvRealEstate}) {
        game::Inventory other;
        other.put({type, 0, -1, -1, 0, 0}, lore);
        CHECK(game::lore_pickup_refusal(lore, other).eqstr == 379);
    }
    game::Inventory archived;
    archived.put({kInvArchived, 0, -1, -1, 0, 0}, lore);
    CHECK_FALSE(game::lore_pickup_refusal(lore, archived));
}
