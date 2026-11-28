#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "core/config.h"
#include "mq/chat_text_filter.h"

using hailstone::Config;
using hailstone::mq::ChatTextFilters;
using hailstone::mq::filter_name_command;

TEST_CASE("/filter name: MacroQuest's match, a line's start with case ignored or '*' anywhere with case kept") {
    ChatTextFilters f;
    CHECK(f.enabled());
    CHECK_FALSE(f.hides("Bob beams a smile at a rat."));
    CHECK(f.add("*beams a smile at"));
    CHECK(f.add("You have become better at "));
    CHECK(f.hides("Bob beams a smile at a rat."));
    CHECK_FALSE(f.hides("Bob BEAMS A SMILE AT a rat.")); // MQ's strstr keeps the case
    CHECK(f.hides("you have become better at Alcohol Tolerance! (12)")); // MQ's _strnicmp ignores it
    CHECK_FALSE(f.hides("Then you have become better at it")); // a start, not anywhere
    CHECK_FALSE(f.hides("You have become")); // shorter than the text
    // Off, nothing is hidden and the list stays.
    f.set_enabled(false);
    CHECK_FALSE(f.hides("Bob beams a smile at a rat."));
    CHECK(f.names().size() == 2);
}

TEST_CASE("/filter name: add, remove, on and off answer as MacroQuest's") {
    ChatTextFilters f;
    using Lines = std::vector<std::string>;
    CHECK(filter_name_command(f, "add *beams a smile at") == Lines{"Started filtering on: *beams a smile at"});
    CHECK(filter_name_command(f, "ADD *Beams A Smile At") == Lines{"Name '*Beams A Smile At' is already being filtered."});
    CHECK(filter_name_command(f, "add") == Lines{"Add what?"});
    CHECK(filter_name_command(f, "add   You give ") == Lines{"Started filtering on: You give "});
    CHECK(filter_name_command(f, "") ==
          Lines{"Names currently filtered:", "---------------------------", "   *beams a smile at", "   You give "});
    CHECK(filter_name_command(f, "off") == Lines{"Filtering of custom messages changed to: off"});
    CHECK_FALSE(f.enabled());
    CHECK(filter_name_command(f, "On") == Lines{"Filtering of custom messages changed to: on"});
    CHECK(filter_name_command(f, "remove *BEAMS a smile at") == Lines{"Stopped filtering on: *BEAMS a smile at"});
    CHECK(filter_name_command(f, "remove nothing like it").empty()); // MQ says nothing
    CHECK(filter_name_command(f, "remove") == Lines{"Remove what?"});
    CHECK(filter_name_command(f, "remove all") == Lines{"Cleared all name filters."});
    CHECK(f.names().empty());
    CHECK(filter_name_command(f, "list") == Lines{"Usage: /filter name on|off|add|remove <name>"});
}

TEST_CASE("/filter name: the texts are kept in the shared settings, read back in order") {
    Config c;
    int saves = 0;
    {
        ChatTextFilters f;
        f.load(c);
        f.set_on_change([&] { ++saves; });
        f.add("*beams a smile at");
        f.add("You give ");
        f.add("You receive ");
        f.remove("You give ");
        f.set_enabled(false);
    }
    CHECK(saves == 5);
    CHECK(c.get_string("chatfilter.name1") == "*beams a smile at");
    CHECK(c.get_string("chatfilter.name2") == "You receive ");
    CHECK_FALSE(c.get("chatfilter.name3")); // the third, gone with the removal
    CHECK_FALSE(c.get_bool("chatfilter.enabled", true));
    ChatTextFilters back;
    back.load(c);
    CHECK_FALSE(back.enabled());
    CHECK(back.names() == std::vector<std::string>{"*beams a smile at", "You receive "});
    // Every character's: hailstone.ini keeps them, as MacroQuest.ini does.
    CHECK(Config::is_shared_key("chatfilter.name1"));
    CHECK(Config::is_shared_key("chatfilter.enabled"));
}
