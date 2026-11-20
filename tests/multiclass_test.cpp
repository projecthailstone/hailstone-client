// Hailstone's multiclass and multi-pet support in the core (net/connection/game_connection.h): the class masks and the pet
// list a server's extension sets, on a connection with the stock extension.

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "core/game_dir.h"
#include "net/connection/game_connection.h"

using namespace hailstone;

TEST_CASE("multiclass: the class masks an extension sets") {
    GameDir game;
    net::ConnectionOptions opts;
    net::GameConnection c(game, opts);

    // Character select: by name, 0 = the character's own class only.
    CHECK(c.classes_mask("Bob") == 0);
    CHECK(c.characters_serial() == 0);
    c.set_character_classes("Bob", 0x3);
    CHECK(c.classes_mask("Bob") == 0x3);
    // A change of a mask changes the character list's serial (the character select's rows are built again), the
    // same mask again does not.
    const std::uint32_t serial = c.characters_serial();
    CHECK(serial != 0);
    c.set_character_classes("Bob", 0x3);
    CHECK(c.characters_serial() == serial);
    c.set_character_classes("Bob", 0);
    CHECK(c.classes_mask("Bob") == 0);
    CHECK(c.characters_serial() != serial);
    c.set_character_classes("Bob", 0x5);
    const std::uint32_t before_clear = c.characters_serial();
    c.clear_character_classes();
    CHECK(c.classes_mask("Bob") == 0);
    CHECK(c.characters_serial() != before_clear);

    // The player: the mask set, else the profile's class.
    net::PlayerProfile p;
    p.class_ = 2;
    c.set_profile_for_test(p);
    CHECK(c.server_class_mask() == 0);
    CHECK(c.player_class_mask() == 0x2);
    c.set_player_classes(0x1 | 0x800);
    CHECK(c.server_class_mask() == 0x801);
    CHECK(c.player_class_mask() == 0x801);
    c.set_player_classes(0);
    CHECK(c.player_class_mask() == 0x2);

    // The guild column on a stock server: the class's short name.
    CHECK(c.guild_member_class(2) == "CLR");
    CHECK(c.guild_member_class(0).empty());
}

TEST_CASE("multi-pet: the pet list keeps the focused pet while it is listed") {
    GameDir game;
    net::ConnectionOptions opts;
    net::GameConnection c(game, opts);
    CHECK_FALSE(c.pets().have_list);

    c.set_pet_list({{10, 13}, {11, 13}});
    CHECK(c.pets().have_list);
    REQUIRE(c.pets().list.size() == 2);
    CHECK(c.pets().focused == 10); // none focused: the first

    c.set_pet_list({{11, 13}, {10, 13}});
    CHECK(c.pets().focused == 10); // still listed

    c.set_pet_list({{11, 13}});
    CHECK(c.pets().focused == 11); // gone: the first

    c.set_pet_list({});
    CHECK(c.pets().have_list);
    CHECK(c.pets().focused == 0);
}
