// game/map_files.h: where the in-game map's layers come from. The player's edits on this server, then the server's
// assets, then the global maps folder, then the game's; a zone's layers from one place.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "core/game_dir.h"
#include "game/map_files.h"
#include "game/server_file_sync.h"
#include "scratch_dir.h"

using namespace hailstone;
namespace fs = std::filesystem;

namespace {

struct Folders {
    test::ScratchDir scratch{"map_files_test"};
    fs::path base = scratch.path() / "game", overlay = scratch.path() / "server" / "assets",
             own = scratch.path() / "server" / "maps", global = scratch.path() / "maps";
    std::optional<GameDir> game;

    Folders() {
        for (const auto& d : {base, overlay, own, global}) fs::create_directories(d);
    }
    static void put(const fs::path& file) {
        fs::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << "L 0, 0, 0,  1, 1, 1,  0, 0, 0\n";
    }
    game::MapFiles find(std::string_view zone, std::string_view skin = {}) {
        game.emplace(base, overlay);
        return game::find_map_files({own, &*game, global}, zone, skin);
    }
};

} // namespace

TEST_CASE("map files: own edits, then the server's assets, then the global folder, then the game's") {
    Folders f;
    CHECK(f.find("qeynos").source == game::MapSource::None);
    Folders::put(f.base / "maps" / "qeynos.txt");
    CHECK(f.find("qeynos").source == game::MapSource::Game);
    Folders::put(f.global / "qeynos.txt");
    auto m = f.find("qeynos");
    CHECK(m.source == game::MapSource::Global);
    CHECK(m.layers[0] == f.global / "qeynos.txt");
    Folders::put(f.overlay / "maps" / "qeynos.txt");
    m = f.find("qeynos");
    CHECK(m.source == game::MapSource::Server);
    CHECK(m.layers[0] == f.overlay / "maps" / "qeynos.txt");
    Folders::put(f.own / "qeynos.txt");
    CHECK(f.find("qeynos").source == game::MapSource::Own);
    // Another zone is looked for on its own.
    Folders::put(f.global / "crushbone_1.txt");
    CHECK(f.find("crushbone").source == game::MapSource::Global);
}

TEST_CASE("map files: a zone name that is a path reads nothing outside the maps folders") {
    Folders f;
    Folders::put(f.scratch.path() / "server" / "secret.txt");
    Folders::put(f.scratch.path() / "abs.txt");
    CHECK(f.find("../secret").source == game::MapSource::None);
    CHECK(f.find((f.scratch.path() / "abs").string()).source == game::MapSource::None);
    Folders::put(f.base / "maps" / "qeynos.txt");
    CHECK(f.find("qeynos").source == game::MapSource::Game);
}

TEST_CASE("map files: a zone's four layers come from one place, never mixed") {
    Folders f;
    for (const char* n : {"qeynos.txt", "qeynos_1.txt", "qeynos_2.txt", "qeynos_3.txt"}) Folders::put(f.global / n);
    Folders::put(f.overlay / "maps" / "qeynos_1.txt"); // the server's map has only layer 1
    auto m = f.find("qeynos");
    CHECK(m.source == game::MapSource::Server);
    CHECK_FALSE(m.layers[0]);
    CHECK(m.layers[1] == f.overlay / "maps" / "qeynos_1.txt");
    CHECK_FALSE(m.layers[2]);
    CHECK_FALSE(m.layers[3]);
}

TEST_CASE("map files: names in any case; the server's left out when its overlay filter hides them") {
    Folders f;
    Folders::put(f.global / "QEYNOS.TXT");
    Folders::put(f.overlay / "Maps" / "Qeynos.txt");
    CHECK(f.find("qeynos").source == game::MapSource::Server);
    // [assets] player = off: a file the server did not send does not count; the global folder is the player's own.
    f.game.emplace(f.base, f.overlay);
    f.game->set_overlay_filter([](std::string_view) { return false; });
    auto m = game::find_map_files({f.own, &*f.game, f.global}, "qeynos", {});
    CHECK(m.source == game::MapSource::Global);
    // The same file, not the same spelling: where names ignore case (Windows, macOS) the exact name asked for is
    // found as it is, elsewhere the folder's own spelling.
    REQUIRE(m.layers[0]);
    CHECK(fs::equivalent(*m.layers[0], f.global / "QEYNOS.TXT"));
}

TEST_CASE("map files: the maps directory is looked for in every source before the plain folders") {
    Folders f;
    Folders::put(f.global / "qeynos.txt");
    Folders::put(f.base / "maps" / "Brewall" / "qeynos.txt");
    auto m = f.find("qeynos", "brewall");
    CHECK(m.source == game::MapSource::Game);
    CHECK(m.dir == "brewall");
    Folders::put(f.global / "brewall" / "qeynos.txt");
    CHECK(f.find("qeynos", "brewall").source == game::MapSource::Global);
    // A maps directory nobody has: the plain folders.
    m = f.find("qeynos", "nope");
    CHECK(m.source == game::MapSource::Global);
    CHECK(m.dir.empty());
    CHECK(m.layers[0] == f.global / "qeynos.txt");
}

TEST_CASE("map files: the maps directories of every source, once each") {
    Folders f;
    Folders::put(f.own / "Mine" / "qeynos.txt");
    Folders::put(f.global / "brewall" / "qeynos.txt");
    Folders::put(f.global / ".hidden" / "qeynos.txt");
    Folders::put(f.base / "maps" / "Brewall" / "qeynos.txt");
    Folders::put(f.overlay / "maps" / "server" / "qeynos.txt");
    f.game.emplace(f.base, f.overlay);
    auto skins = game::map_skins({f.own, &*f.game, f.global});
    REQUIRE(skins.size() == 3);
    CHECK(skins[0] == "brewall");
    CHECK(skins[1] == "Mine");
    CHECK(skins[2] == "server");
}

TEST_CASE("map files: a server's client_files/maps/ and assets/maps/ both land in its assets' maps/") {
    CHECK(net::server_file_destination("maps/qeynos.txt") == "assets/maps/qeynos.txt");
    CHECK(net::server_file_destination("assets/maps/brewall/qeynos_1.txt") == "assets/maps/brewall/qeynos_1.txt");
}
