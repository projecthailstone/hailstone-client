// The context tips the connection's events activate (GameConnection::take_context_tips): the experience, group and
// guild invitation, consent and world container tips, from the handlers that call CTipWnd::Activate in the original.
#include <doctest/doctest.h>

#include <algorithm>

#include "mock_server/harness.h"
#include "net/messages/door.h"
#include "net/messages/message.h"
#include "net/messages/notice.h"
#include "net/messages/social.h"
#include "net/messages/zone.h"
#include "net/protocol/rof2/packets/door_packets.h"
#include "net/protocol/rof2/packets/group_packets.h"
#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/rof2/packets/social_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;
namespace messages = hailstone::net::messages;

namespace {

bool has(const std::vector<int>& tips, int tip) { return std::find(tips.begin(), tips.end(), tip) != tips.end(); }

std::vector<std::string> drain_chat(GameConnection& c) {
    std::vector<std::string> lines;
    while (auto e = c.poll_chat()) lines.push_back(e->text);
    return lines;
}

} // namespace

TEST_CASE("context tips: the events' tips (experience, invitations, consent, world container)") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    conn.take_context_tips();
    const std::string me = conn.character_name();

    // Handle_OP_ExpUpdate: the second dword 2 and experience above 0 (EQEmu sends 0 there)
    conn.receive_zone_message_for_test({messages::GameOpcode::ExpUpdate, ExpUpdate{5, 0}});
    CHECK_FALSE(has(conn.take_context_tips(), 9));
    conn.receive_zone_message_for_test({messages::GameOpcode::ExpUpdate, ExpUpdate{0, 2}});
    CHECK_FALSE(has(conn.take_context_tips(), 9));
    conn.receive_zone_message_for_test({messages::GameOpcode::ExpUpdate, ExpUpdate{5, 2}});
    CHECK(has(conn.take_context_tips(), 9));

    // Handle_OP_GroupInvite2 -> PlayerClient::SetInvited(1): the group invitation tip
    GroupInvite g;
    g.invitee = me;
    g.inviter = "Someone";
    conn.receive_zone_message_for_test({messages::GameOpcode::GroupInvite, g});
    CHECK(has(conn.take_context_tips(), 0xc));

    // Handle_OP_GuildInvite: a packet naming us (any case)
    GuildCommand gc;
    gc.other_name = me;
    gc.my_name = "Leader";
    gc.guild_id = 7;
    conn.receive_zone_message_for_test({messages::GameOpcode::GuildInvite, gc});
    CHECK(has(conn.take_context_tips(), 0x19));

    // Handle_OP_DenyResponse: the owner's lines, then someone else's corpse with permission: 2080 and the tip
    drain_chat(conn);
    ConsentResponse c;
    c.grant_name = "Friend";
    c.owner_name = me;
    c.permission = 1;
    c.zone = "qeynos2";
    conn.receive_zone_message_for_test({messages::GameOpcode::ConsentResponse, c});
    auto lines = drain_chat(conn);
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.back().find("1427") != std::string::npos);
    CHECK_FALSE(has(conn.take_context_tips(), 0x1c));
    c.grant_name = me;
    c.owner_name = "Fallen";
    conn.receive_zone_message_for_test({messages::GameOpcode::DenyResponse, c}); // OP_DenyResponse goes to the same handler
    lines = drain_chat(conn);
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.back().find("2080") != std::string::npos);
    CHECK(has(conn.take_context_tips(), 0x1c));
    c.permission = 0;
    conn.receive_zone_message_for_test({messages::GameOpcode::ConsentResponse, c});
    lines = drain_chat(conn);
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.back().find("2103") != std::string::npos);
    CHECK_FALSE(has(conn.take_context_tips(), 0x1c));

    // CContainerMgr::OpenWorldContainer
    ClickObjectAction a;
    a.drop_id = 12;
    a.type = 16;
    a.name = "Forge";
    GroundObject station;
    station.drop_id = a.drop_id;
    station.object_type = 16;
    const auto at = conn.world().player()->position;
    station.x = at.y;
    station.y = at.x;
    station.z = at.z;
    conn.receive_zone_message_for_test({messages::GameOpcode::GroundSpawn, station});
    REQUIRE(conn.click_object(a.drop_id));
    conn.receive_zone_message_for_test({messages::GameOpcode::ClickObjectAction, a});
    CHECK(has(conn.take_context_tips(), 0x1b));
}

TEST_CASE("OP_Weblink: each address queued for the app, oldest first, taken once") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    conn.take_web_links();
    conn.receive_zone_message_for_test({messages::GameOpcode::Weblink,
                                        ExternalLinkNotice{"https://example.org/1"}});
    // Empty wire URLs are rejected at the codec boundary, not injected into gameplay.
    CHECK_FALSE(decode_weblink(AppPacket(rof2::to_wire(rof2::Op::Weblink), std::vector<std::byte>(1))));
    conn.receive_zone_message_for_test({messages::GameOpcode::Weblink,
                                        ExternalLinkNotice{"http://example.org/2"}});
    CHECK(conn.take_web_links() == std::vector<std::string>{"https://example.org/1", "http://example.org/2"});
    CHECK(conn.take_web_links().empty());

    // Cmd_survey @0x478b70: the survey page, by the same way (SurveyWebWnd in the original).
    conn.send_command("/survey");
    auto survey = conn.take_web_links();
    REQUIRE(survey.size() == 1);
    CHECK(survey[0].rfind("http://www.questionpro.com/akira/TakeSurvey?id=1069785&custom1=", 0) == 0);
    CHECK(survey[0].size() == std::string("http://www.questionpro.com/akira/TakeSurvey?id=1069785&custom1=").size() + 40);
}
