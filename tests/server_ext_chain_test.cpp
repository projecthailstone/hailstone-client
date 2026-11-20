// Several server extensions as one (game/server_ext_chain.h): members in order, each while active, and how their
// answers combine.

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/game_dir.h"
#include "game/server_ext_chain.h"
#include "game/spells.h"
#include "net/connection/game_connection.h"

using namespace hailstone;

namespace {

// A member that answers what the test sets and counts its events.
struct Fake final : game::ServerExtension {
    std::string tag;
    bool take_commands = false, retry = false, blocks_song = false;
    std::optional<bool> caster;
    std::uint8_t hide_class = 0; // 0: the stock answer
    std::optional<std::int32_t> max_hp, cur_mana;
    std::vector<std::string>* log = nullptr;
    std::optional<bool> gate;

    explicit Fake(std::string t) : tag(std::move(t)) {}
    bool handler_command(net::GameConnection&, std::string_view, const std::string&) override {
        if (log) log->push_back(tag + ":command");
        return take_commands;
    }
    void after_combat_ability(net::GameConnection&, const game::SpellData&, bool) override {
        if (log) log->push_back(tag + ":ability");
    }
    bool retry_zone_entry(net::GameConnection&) override {
        if (log) log->push_back(tag + ":retry");
        return retry;
    }
    std::optional<bool> is_spellcaster(const net::GameConnection&) const override { return caster; }
    bool song_blocked(const net::GameConnection&, std::uint8_t) const override { return blocks_song; }
    std::uint8_t hide_check_class(const net::GameConnection&, std::uint8_t c) const override {
        return hide_class ? hide_class : c;
    }
    game::CharacterVitals character_vitals(const net::GameConnection&) const override {
        game::CharacterVitals v;
        v.max_hp = max_hp;
        v.cur_mana = cur_mana;
        return v;
    }
    void label_text(const net::GameConnection&, int, std::string& text) const override { text += "+" + tag; }
    const game::SkillRules* skill_rules(const net::GameConnection&) const override {
        if (!gate) return nullptr;
        rules.ctx = this;
        rules.gate = [](const void* ctx, std::int32_t, std::uint32_t, std::int32_t) {
            return static_cast<const Fake*>(ctx)->gate;
        };
        return &rules;
    }
    mutable game::SkillRules rules{};
};

} // namespace

TEST_CASE("extension chain: order, the active check and how answers combine") {
    GameDir game;
    net::ConnectionOptions opts;
    net::GameConnection c(game, opts);
    std::vector<std::string> log;

    game::ExtensionChain chain;
    CHECK(chain.empty());
    auto a = std::make_shared<Fake>("a");
    auto b = std::make_shared<Fake>("b");
    a->log = b->log = &log;
    bool a_active = true;
    int ida = chain.add(a, [&](const net::GameConnection&) { return a_active; });
    chain.add(b);
    CHECK_FALSE(chain.empty());

    // Nothing answers: the stock answers.
    CHECK_FALSE(chain.is_spellcaster(c));
    CHECK(chain.hide_check_class(c, 4) == 4);
    CHECK_FALSE(chain.song_blocked(c, 255));
    CHECK_FALSE(chain.skill_rules(c));

    // Events reach every active member, in order.
    const game::SpellData spell;
    chain.after_combat_ability(c, spell, true);
    CHECK(log == std::vector<std::string>{"a:ability", "b:ability"});

    // A command stops at the member that takes it.
    log.clear();
    a->take_commands = true;
    CHECK(chain.handler_command(c, "cmd", ""));
    CHECK(log == std::vector<std::string>{"a:command"});

    // The zone-in's retry: every member is asked, any may want it.
    log.clear();
    a->retry = true;
    CHECK(chain.retry_zone_entry(c));
    CHECK(log == std::vector<std::string>{"a:retry", "b:retry"});

    // Answers: the first member with one; switches: any.
    b->caster = true;
    CHECK(chain.is_spellcaster(c) == true);
    a->caster = false;
    CHECK(chain.is_spellcaster(c) == false);
    b->blocks_song = true;
    CHECK(chain.song_blocked(c, 255));
    b->hide_class = 9;
    CHECK(chain.hide_check_class(c, 4) == 9);

    // Vitals field by field; labels through each member.
    a->max_hp = 100;
    b->max_hp = 200;
    b->cur_mana = 50;
    auto v = chain.character_vitals(c);
    CHECK(v.max_hp == 100);
    CHECK(v.cur_mana == 50);
    std::string label = "L";
    chain.label_text(c, 1, label);
    CHECK(label == "L+a+b");

    // An inactive member is not asked.
    a_active = false;
    CHECK(chain.is_spellcaster(c) == true);
    log.clear();
    chain.after_combat_ability(c, spell, true);
    CHECK(log == std::vector<std::string>{"b:ability"});
    a_active = true;

    // The skill rules: one pointer that stays valid; with its member gone it gives the stock rule.
    a->gate = false;
    const game::SkillRules* rules = chain.skill_rules(c);
    REQUIRE(rules);
    REQUIRE(rules->gate);
    CHECK(rules->gate(rules->ctx, 10, 1, 0) == false);
    chain.remove(ida);
    CHECK_FALSE(rules->gate(rules->ctx, 10, 1, 0).has_value());
    CHECK(rules->tracking_allowed(rules->ctx, 4, 0, 5)); // the stock rule: any skill
    CHECK(chain.is_spellcaster(c) == true);              // b only now
}

TEST_CASE("extension chain: the connection asks the plugins; a member removed while it runs") {
    GameDir game;
    auto plugin = std::make_shared<Fake>("plugin");
    plugin->hide_class = 9;
    net::ConnectionOptions opts;
    opts.plugins = plugin;
    net::GameConnection c(game, opts);
    CHECK(c.server_ext().hide_check_class(c, 4) == 9);

    // A member removed while the chain runs: it stops at once and is dropped afterwards.
    game::ExtensionChain chain;
    std::vector<std::string> log;
    auto x = std::make_shared<Fake>("x");
    auto y = std::make_shared<Fake>("y");
    x->log = y->log = &log;
    int idy = 0;
    struct Remover final : game::ServerExtension {
        game::ExtensionChain* chain = nullptr;
        int* id = nullptr;
        void after_combat_ability(net::GameConnection&, const game::SpellData&, bool) override { chain->remove(*id); }
    };
    auto r = std::make_shared<Remover>();
    r->chain = &chain;
    r->id = &idy;
    chain.add(x);
    chain.add(r);
    idy = chain.add(y);
    const game::SpellData spell;
    chain.after_combat_ability(c, spell, true);
    CHECK(log == std::vector<std::string>{"x:ability"});
    log.clear();
    chain.after_combat_ability(c, spell, true);
    CHECK(log == std::vector<std::string>{"x:ability"});
}
