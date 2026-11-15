// SpellUi builds each C++ spell window's view only while that window draws (app/spell_ui.h spell_windows_drawn).
#include <doctest/doctest.h>

#include <set>
#include <string>
#include <string_view>

#include "app/spell_ui.h"
#include "ui/lua/lua_host.h"

using hailstone::app::spell_windows_drawn;
namespace lua = hailstone::ui::lua;

namespace {

// Replaces `screens` while it lives; no check after it.
struct Replaced {
    explicit Replaced(std::set<std::string, std::less<>> screens) {
        lua::set_replaced_check([s = std::move(screens)](std::string_view screen) { return s.contains(screen); });
    }
    ~Replaced() { lua::set_replaced_check(nullptr); }
    Replaced(const Replaced&) = delete;
    Replaced& operator=(const Replaced&) = delete;
};

} // namespace

TEST_CASE("spell UI: with no Lua windows every C++ window draws, the book only while open") {
    Replaced none({});
    auto closed = spell_windows_drawn(false);
    CHECK(closed.bar);
    CHECK(closed.cast);
    CHECK(closed.buffs);
    CHECK(closed.songs);
    CHECK(closed.target);
    CHECK_FALSE(closed.book); // its 720 entries and tooltips are not built while it is closed
    CHECK(spell_windows_drawn(true).book);
}

TEST_CASE("spell UI: a window its Lua window replaces is not built, the open book included") {
    Replaced pack({"CastSpellWnd", "SpellBookWnd", "BuffWindow", "TargetWindow"});
    auto d = spell_windows_drawn(true);
    CHECK_FALSE(d.bar);
    CHECK_FALSE(d.book);
    CHECK_FALSE(d.buffs);
    CHECK_FALSE(d.target);
    CHECK(d.cast);  // CastingWindow not replaced
    CHECK(d.songs); // ShortDurationBuffWindow not replaced
}
