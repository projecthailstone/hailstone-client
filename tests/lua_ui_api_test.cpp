// The Lua UI's ui table (src/ui/lua/lua_imgui.cpp): the key names, ui.CaptureKeyboard, ui.GetIO().typing and
// ui.DrawText's font size, which the server windows of the Triune plugin use.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <lua.hpp>

#include "ui/imgui_layer.h"
#include "ui/lua/lua_host.h"
#include "ui/texture_ids.h"
#include "scratch_dir.h"

namespace fs = std::filesystem;

TEST_CASE("Lua UI: the key names, keyboard capture, typing and DrawText's size") {
    hailstone::test::ScratchDir scratch("lua_ui_api_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir);
    std::ofstream(dir / "keys.lua") << "ok = ui.Key.A ~= nil and ui.Key.Z ~= nil and ui.Key['0'] ~= nil and ui.Key['9'] ~= nil\n"
                                       "  and ui.Key.UpArrow ~= nil and ui.Key.Space ~= nil and ui.Key.Escape ~= nil\n"
                                       "ui.window('Keys', function()\n"
                                       "  ui.Begin('Keys')\n"
                                       "  ui.CaptureKeyboard()\n"
                                       "  typing = ui.GetIO().typing\n"
                                       "  local x, y = ui.GetCursorScreenPos()\n"
                                       "  ui.DrawText(x, y, 1, 1, 1, 1, 'big', 30)\n"
                                       "  ui.DrawText(x, y, 1, 1, 1, 1, 'plain')\n"
                                       "  ui.End()\n"
                                       "end)\n";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        for (int i = 0; i < 2; ++i) {
            ImGui::GetIO().DisplaySize = ImVec2(800, 600);
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            lua.frame();
            ImGui::Render();
        }
        lua_getglobal(L, "ok");
        CHECK(lua_toboolean(L, -1));
        lua_getglobal(L, "typing");
        CHECK(lua_isboolean(L, -1));
        lua_pop(L, 2);
        // CaptureKeyboard asked for the next frame: the keyboard is the UI's.
        ImGui::NewFrame();
        CHECK(ImGui::GetIO().WantCaptureKeyboard);
        ImGui::Render();
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
}

// ui.FindWindowRect: a window's place and size by name, before its Begin (lib/snap.lua snaps a dialog's drag from
// it); nothing for a window never drawn, and a "###" name finds the window whatever its title.
TEST_CASE("Lua UI: FindWindowRect finds a drawn window by name") {
    hailstone::test::ScratchDir scratch("lua_ui_api_rect_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir);
    std::ofstream(dir / "rect.lua") << "ui.window('Rect', function()\n"
                                       "  before = { ui.FindWindowRect('###probe') }\n"
                                       "  missing = ui.FindWindowRect('never drawn') == nil\n"
                                       "  ui.SetNextWindowPos(40, 50, ui.Cond.Always)\n"
                                       "  ui.SetNextWindowSize(120, 90, ui.Cond.Always)\n"
                                       "  ui.Begin('Title ' .. ui.GetFrameCount() .. '###probe')\n"
                                       "  ui.End()\n"
                                       "end)\n";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        for (int i = 0; i < 2; ++i) {
            ImGui::GetIO().DisplaySize = ImVec2(800, 600);
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            lua.frame();
            ImGui::Render();
        }
        lua_getglobal(L, "missing");
        CHECK(lua_toboolean(L, -1));
        lua_getglobal(L, "before");
        REQUIRE(lua_istable(L, -1));
        CHECK(lua_rawlen(L, -1) == 4);
        const double want[] = {40, 50, 120, 90};
        for (int i = 0; i < 4; ++i) {
            lua_rawgeti(L, -1, i + 1);
            CHECK(lua_tonumber(L, -1) == want[i]);
            lua_pop(L, 1);
        }
        lua_pop(L, 2);
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
}

// LuaUi::setting / set_setting: the client's own menus read and change the UI's layout settings (the hot bar's
// Lock layout), the same values ui.GetSetting / ui.SetSetting use.
TEST_CASE("Lua UI: the client reads and writes a layout setting") {
    hailstone::test::ScratchDir scratch("lua_ui_setting_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        CHECK(lua.setting("hud.lock.HotButtonWnd") == "");
        CHECK(lua.set_setting("hud.lock.HotButtonWnd", "1"));
        CHECK(lua.setting("hud.lock.HotButtonWnd") == "1");
        CHECK(lua.set_setting("hud.lock.HotButtonWnd", "0"));
        CHECK(lua.setting("hud.lock.HotButtonWnd") == "0");
    }
}

// ui.GetIO: one table a frame (the HUD asks for it from every window), with the frame's values; the next frame's is
// another, with that frame's.
TEST_CASE("Lua UI: GetIO gives one table a frame") {
    hailstone::test::ScratchDir scratch("lua_ui_api_io_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir);
    std::ofstream(dir / "io.lua") << "frames = {}\n"
                                     "ui.window('IO', function()\n"
                                     "  local a, b = ui.GetIO(), ui.GetIO()\n"
                                     "  frames[#frames + 1] = { same = rawequal(a, b), ctrl = a.ctrl, width = b.width, t = a }\n"
                                     "end)\n";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    hailstone::ui::configure_keys(); // as the client: no macOS Ctrl/Cmd swap, so the Ctrl event below reads as Ctrl
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        for (int i = 0; i < 2; ++i) {
            ImGui::GetIO().DisplaySize = ImVec2(i == 0 ? 800.0f : 1024.0f, 600);
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, i == 1);
            ImGui::NewFrame();
            lua.frame();
            ImGui::Render();
        }
        REQUIRE(luaL_dostring(L, "return frames[1].same and frames[2].same, frames[1].ctrl, frames[2].ctrl, "
                                 "frames[1].width, frames[2].width, not rawequal(frames[1].t, frames[2].t)") == LUA_OK);
        CHECK(lua_toboolean(L, -6));
        CHECK_FALSE(lua_toboolean(L, -5));
        CHECK(lua_toboolean(L, -4));
        CHECK(lua_tonumber(L, -3) == 800);
        CHECK(lua_tonumber(L, -2) == 1024);
        CHECK(lua_toboolean(L, -1));
        lua_settop(L, 0);
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
}

// ui.PushStyleColors / ui.PushStyleVars: many pushes in one call from flat lists (lib/style.lua's look for every
// window), skipping an index out of range and returning how many were pushed; the values are the ones in force after.
namespace {
ImVec4 g_text{};
float g_rounding = -1, g_pad_x = -1, g_pad_y = -1;
int probe_style(lua_State*) {
    g_text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    g_rounding = ImGui::GetStyle().WindowRounding;
    g_pad_x = ImGui::GetStyle().WindowPadding.x;
    g_pad_y = ImGui::GetStyle().WindowPadding.y;
    return 0;
}
} // namespace

TEST_CASE("Lua UI: PushStyleColors and PushStyleVars push a list in one call") {
    hailstone::test::ScratchDir scratch("lua_ui_api_style_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir);
    std::ofstream(dir / "style.lua")
        << "ui.window('Style', function()\n"
           "  nc = ui.PushStyleColors({ ui.Col.Text, 0.25, 0.5, 0.75, 1, 9999, 1, 1, 1, 1, ui.Col.Button, 0, 1, 0, 1 })\n"
           "  nv = ui.PushStyleVars({ ui.StyleVar.WindowRounding, 3, false, -1, 0, 0, ui.StyleVar.WindowPadding, 4, 6 })\n"
           "  probe()\n"
           "  ui.PopStyleVar(nv)\n"
           "  ui.PopStyleColor(nc)\n"
           "end)\n";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        lua_register(L, "probe", &probe_style);
        ImGui::GetIO().DisplaySize = ImVec2(800, 600);
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        const float rounding = ImGui::GetStyle().WindowRounding;
        ImGui::NewFrame();
        lua.frame();
        ImGui::Render();
        lua_getglobal(L, "nc");
        CHECK(lua_tointeger(L, -1) == 2);
        lua_getglobal(L, "nv");
        CHECK(lua_tointeger(L, -1) == 2);
        lua_pop(L, 2);
        CHECK(g_text.x == doctest::Approx(0.25f));
        CHECK(g_text.y == doctest::Approx(0.5f));
        CHECK(g_text.z == doctest::Approx(0.75f));
        CHECK(g_rounding == doctest::Approx(3.0f));
        CHECK(g_pad_x == doctest::Approx(4.0f));
        CHECK(g_pad_y == doctest::Approx(6.0f));
        CHECK(ImGui::GetStyle().WindowRounding == rounding); // popped again
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
}

// The modern pack's shared pieces (ui/modern/lib) over a stand-in eq table: W.List keeps a copy of a sorted or
// filtered list and sorts it again when a row changes (in view or not), W.ButtonRow leaves the caller's opts alone,
// and the testing hooks read their settings once.
TEST_CASE("Modern UI libs: W.List's sorted copy, ButtonRow's opts, the test hooks read once") {
    hailstone::test::ScratchDir scratch("lua_ui_modern_libs_test");
    const fs::path dir = scratch.path() / "pack";
    const fs::path lib = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern" / "lib";
    REQUIRE(fs::exists(lib / "widgets.lua"));
    fs::create_directories(dir);
    fs::copy(lib, dir / "lib", fs::copy_options::recursive);
    std::ofstream(scratch.path() / "ui.txt") << "test.open.Later=1\ntest.open.LaterToo=1\n";
    std::ofstream(dir / "libs.lua") << R"lua(
LIST = {}
for i = 1, 200 do LIST[i] = { tostring(1000 - i) } end -- the list's order is descending
reads, listCalls, shown, opened = 0, 0, {}, nil
eq = {
  ListInfo = function() return #LIST, 1, -1, { "Value" }, { 0 } end,
  ListRow = function(_, _, r)
    reads = reads + 1
    local t = LIST[r + 1]
    if not t then return nil end
    return { t[1] }, 1, 1, 1
  end,
  WindowList = function() listCalls = listCalls + 1; return {} end,
  ChildShown = function() return true end,
  ChildState = function() return true, nil end,
  WindowText = function() return "" end,
  ClickChild = function() end,
  ListClick = function() end,
  InGame = function() return true end,
  ShowWindow = function(w) shown[#shown + 1] = w end,
  HoldingButton = function() return false end,
}
local asked = {}
local getSetting = ui.GetSetting
ui.GetSetting = function(k, d) asked[k] = (asked[k] or 0) + 1; return getSetting(k, d) end
function askedFor(k) return asked[k] or 0 end

local W = dofile("lib/widgets.lua")
local P = dofile("lib/progress.lua")
local M = dofile("lib/markets.lua")
FILTER, FILTER_KEY = nil, nil
local held = { tooltip = "kept" }
local ROW = { { "B1", "One", held } }
frames = {}
ui.window("Libs", function()
  ui.SetNextWindowPos(0, 0, ui.Cond.Always)
  ui.SetNextWindowSize(500, 560, ui.Cond.Always)
  ui.Begin("Libs")
  local f = { drawn = {} }
  reads = 0
  W.List("Wnd", "L", 0, 300, { sortable = true, filter = FILTER, filterKey = FILTER_KEY,
    onRow = function(_, texts) f.drawn[#f.drawn + 1] = texts[1]; return true end })
  f.reads = reads
  W.ButtonRow("Wnd", ROW, 40)
  local _, y0, _, y1 = ui.GetItemRect()
  f.tall = y1 - y0
  W.ButtonRow("Wnd", ROW)
  _, y0, _, y1 = ui.GetItemRect()
  f.plain = y1 - y0
  f.heldUntouched = held.width == nil and held.height == nil and held.tooltip == "kept"
  P.TestOpen("Never", function() error("not set: never runs") end)
  P.TestOpen("Later", function(a, b) opened = a .. b end, "x", "y")
  P.TestOpen("LaterToo")
  M.TestOpen("NeverM")
  f.forced = M.Forced("NeverD")
  P.Rows("Wnd", "P", 0, 100, { rows = { { "r1" }, { "r2" } } })
  ui.End()
  frames[#frames + 1] = f
end)
)lua";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        auto frames = [&](int n, float dt = 1.0f / 60.0f) {
            for (int i = 0; i < n; ++i) {
                ImGui::GetIO().DisplaySize = ImVec2(800, 600);
                ImGui::GetIO().DeltaTime = dt;
                ImGui::NewFrame();
                lua.frame();
                ImGui::Render();
            }
        };
        // Evaluates `expr` (Lua) on the last frame drawn.
        auto last = [&](const char* expr) {
            std::string code = std::string("local f = frames[#frames]; return ") + expr;
            REQUIRE(luaL_dostring(L, code.c_str()) == LUA_OK);
            std::string out = lua_isstring(L, -1) ? lua_tostring(L, -1) : (lua_toboolean(L, -1) ? "true" : "false");
            lua_settop(L, 0);
            return out;
        };
        frames(3);
        REQUIRE_FALSE(ImGui::FindWindowByName("UI errors"));
        // sorted ascending on the first column (a sortable table's first sort): numbers as numbers
        CHECK(last("f.drawn[1] .. ',' .. f.drawn[2]") == "800,801");
        // a long list is not read whole each frame: the rows in view and a few more
        CHECK(std::stoi(last("f.reads")) < 60);
        // a row out of view changes (the count, the first and the last row as they were): sorted in within frames
        REQUIRE(luaL_dostring(L, "LIST[100] = { '5' }") == LUA_OK);
        frames(40);
        CHECK(last("f.drawn[1] .. ',' .. f.drawn[2]") == "5,800");
        // numbers as numbers, words without case
        REQUIRE(luaL_dostring(L, "LIST = { { 'b' }, { '10' }, { 'A' }, { '9' } }") == LUA_OK);
        frames(2);
        CHECK(last("table.concat(f.drawn, ',')") == "9,10,A,b");
        // the filter, kept by its key
        REQUIRE(luaL_dostring(L, "FILTER = function(_, t) return t[1]:find('1') ~= nil or t[1] == 'b' end; "
                                 "FILTER_KEY = '1'") == LUA_OK);
        frames(1);
        CHECK(last("table.concat(f.drawn, ',')") == "10,b");
        // a row changes under a kept filter: filtered and sorted again
        REQUIRE(luaL_dostring(L, "LIST[1] = { '1' }") == LUA_OK);
        frames(1);
        CHECK(last("table.concat(f.drawn, ',')") == "1,10");
        REQUIRE(luaL_dostring(L, "FILTER, FILTER_KEY = nil, nil") == LUA_OK);
        frames(1);
        CHECK(last("table.concat(f.drawn, ',')") == "1,9,10,A");

        // ButtonRow: the row's height, and the caller's opts unchanged so a later row without one is the default
        CHECK(last("f.heldUntouched") == "true");
        CHECK(std::stof(last("f.tall")) == doctest::Approx(40));
        CHECK(std::stof(last("f.plain")) < 30);

        // the test hooks: a missing setting is asked for once; a set one runs once, with its arguments
        CHECK(last("askedFor('test.open.Never')") == "1");
        CHECK(last("askedFor('test.open.NeverM')") == "1");
        CHECK(last("askedFor('test.draw.NeverD')") == "1");
        CHECK(last("askedFor('test.select.Wnd.P')") == "1");
        CHECK(last("f.forced == nil") == "true");
        CHECK(last("listCalls") == "0"); // P.Rows used the rows it was given
        CHECK(last("opened == nil and #shown == 0") == "true"); // 6 s after zoning in
        frames(3, 3.0f);
        CHECK(last("opened") == "xy");
        CHECK(last("table.concat(shown, ',')") == "LaterToo");
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
}

// The STML libraries' kept layouts (ui/modern/lib/stml.lua, ui/gamepad/lib/stml.lua): a line's wrap is worked out
// once and kept until the width or the font change, and is the one the per-frame wrap gave; the chat output's lines
// drawn from their summed heights land where the line-by-line walk put them; the gamepad's block cache keeps the
// last few texts.
TEST_CASE("Lua UI: STML wraps are kept until the width changes, and match the per-frame wrap") {
    hailstone::test::ScratchDir scratch("lua_ui_api_stml_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir / "lib");
    const fs::path ui_src = fs::path(__FILE__).parent_path().parent_path() / "ui";
    if (!fs::exists(ui_src / "modern" / "lib" / "stml.lua")) return; // an installed test binary run away from the sources
    fs::copy_file(ui_src / "modern" / "lib" / "stml.lua", dir / "lib" / "stml.lua");
    fs::copy_file(ui_src / "gamepad" / "lib" / "stml.lua", dir / "lib" / "gpstml.lua");
    // (both libraries' theme, the colours they read)
    std::ofstream(dir / "lib" / "theme.lua")
        << "local c = { link = { 0, 0, 1, 1 }, text = { 1, 1, 1, 1 } }\n"
           "return { color = c, C = c, text = function(x, y, t) return ui.CalcTextSize(t) end }\n";
    std::ofstream(dir / "wrap.lua") << R"lua(
local stml = dofile("lib/stml.lua")
local G = dofile("lib/gpstml.lua")
out = {}
local frame = 0
-- the wrap as stml.DrawLine worked it out every frame before it was kept: each piece and whether it starts a row
local function reference(segments, width)
  local r, x, first = {}, 0, true
  for _, seg in ipairs(segments) do
    for word, space in seg[1]:gmatch("([^ ]*)( *)") do
      if word ~= "" or space ~= "" then
        local piece = word .. space
        local w = ui.CalcTextSize(piece)
        local row = false
        if not first then
          if x + ui.CalcTextSize(word) > width and x > 0 then x = 0; row = true end
        end
        r[#r + 1] = piece .. (row and "|row" or "")
        x = x + w
        first = false
      end
    end
  end
  return table.concat(r, ",")
end
local function kept(segments)
  local r, p = {}, segments.wrap
  for i = 1, #p, 4 do r[#r + 1] = p[i] .. (p[i + 3] and "|row" or "") end
  return table.concat(r, ",")
end
local line = stml.Parse('<c "#ff0000">one two three  four five six seven eight nine ten</c> <a WndNotify="27,x">a link name</a> end')
local view = { }
refHeights = {}
ui.window("Wrap", function()
  frame = frame + 1
  ui.SetNextWindowSize(700, 400, ui.Cond.Always)
  ui.Begin("Wrap")
  if frame == 1 then
    local real, calls = ui.CalcTextSize, 0
    stml.DrawLine(line, 2000, nil, "k")
    local wide = line.wrap
    out.wideSame = kept(line) == reference(line, 2000)
    ui.CalcTextSize = function(...) calls = calls + 1; return real(...) end
    stml.DrawLine(line, 2000, nil, "k")
    ui.CalcTextSize = real
    out.keptCalls = calls
    out.keptTable = rawequal(line.wrap, wide)
    stml.DrawLine(line, 60, nil, "k")
    out.narrowSame = kept(line) == reference(line, 60)
    out.narrowRows = select(2, kept(line):gsub("|row", ""))
    stml.DrawLine(line, 61, nil) -- (no key: the font measured)
    out.measuredSame = kept(line) == reference(line, 61)
    -- gamepad: rows kept on the line until the width changes; blocks by text
    local seg = G.parse('<c "#ff0000">one two three four five</c> six seven')
    local a = G.rows(seg, 2000)
    out.gpSame = rawequal(G.rows(seg, 2000), a)
    local b = G.rows(seg, 40)
    out.gpNew = not rawequal(a, b) and #b > #a and #a == 1
    local first = G.blockRows("first<br>text", 300)
    out.blockSame = rawequal(G.blockRows("first<br>text", 300), first) and #first == 2
    out.blockWidth = not rawequal(G.blockRows("first<br>text", 299), first)
    local again = G.blockRows("first<br>text", 299)
    for i = 1, 8 do G.blockRows("other " .. i, 300) end
    out.blockEvicted = not rawequal(G.blockRows("first<br>text", 299), again)
    -- the chat output: 300 lines, some wrapping
    local raws = {}
    for i = 1, 300 do raws[i] = (i % 3 == 0) and ('<c "#00ff00">line ' .. i .. ' with words enough to wrap at this width, more and more words</c>') or ('<c "#ffffff">line ' .. i .. '</c> <a WndNotify="27,x">link</a>') end
    stml.UpdateLines(view, raws)
  end
  ui.BeginChild("output", 300, 200, false)
  if frame == 3 then ui.SetScrollY(1500) end
  stml.DrawLines(view, 380, 200, "k380")
  local _, y = ui.GetCursorPos()
  out["end" .. frame] = y
  out["fast" .. frame] = view.cumKey == "k380"
  ui.EndChild()
  -- the same lines drawn by the line-by-line walk DrawLines did before it kept their places
  ui.SameLine() -- (beside it: a child out of the window's sight draws nothing)
  ui.BeginChild("reference", 300, 200, false)
  if frame == 3 then ui.SetScrollY(1500) end
  local top = ui.GetScrollY()
  local x0, y = ui.GetCursorPos()
  local skipped = false
  for i, l in ipairs(view.lines) do
    local h = refHeights[i]
    if h and (y + h < top - 40 or y > top + 200 + 40) then
      y = y + h
      skipped = true
    else
      if skipped then ui.SetCursorPos(x0, y); skipped = false end
      stml.DrawLine(l, 380)
      local _, ny = ui.GetCursorPos()
      refHeights[i] = ny - y
      y = ny
    end
  end
  if skipped then ui.SetCursorPos(x0, y); ui.Dummy(0, 0) end
  out["ref" .. frame] = select(2, ui.GetCursorPos())
  ui.EndChild()
  if frame == 4 then -- (last: the rows drawn would push the outputs above out of the window's sight)
    -- a chat line holding new lines (a server's MOTD): each starts a row, an empty one is a blank row, one at the
    -- end adds nothing
    local motd = stml.Parse('<c "#808080">[06:53:16] </c>Be Free! \n\nDo Not Pay\n')
    stml.DrawLine(motd, 2000, nil, "k")
    out.motd = kept(motd)
    local gp = G.layout(G.parse('<c "#808080">[06:53:16] </c>Be Free! \n\nDo Not Pay\n'), 2000)
    local gr = {}
    for _, r in ipairs(gp) do
      local t = {}
      for _, p in ipairs(r) do t[#t + 1] = p[1] end
      gr[#gr + 1] = table.concat(t)
    end
    out.gpMotd = table.concat(gr, "|")
  end
  ui.End()
end)
)lua";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        for (int i = 0; i < 4; ++i) {
            ImGui::GetIO().DisplaySize = ImVec2(800, 600);
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            lua.frame();
            ImGui::Render();
        }
        auto get = [&](const char* key) {
            lua_getglobal(L, "out");
            lua_getfield(L, -1, key);
            std::string v = lua_isnil(L, -1) ? "nil" : luaL_tolstring(L, -1, nullptr);
            lua_settop(L, 0);
            return v;
        };
        CHECK(get("wideSame") == "true");
        CHECK(get("keptCalls") == "0");
        CHECK(get("keptTable") == "true");
        CHECK(get("narrowSame") == "true");
        CHECK(get("narrowRows") != "0");
        CHECK(get("measuredSame") == "true");
        CHECK(get("motd") == "[06:53:16] ,Be ,Free! ,|row,Do |row,Not ,Pay");
        CHECK(get("gpMotd") == "[06:53:16] Be Free! ||Do Not Pay");
        CHECK(get("gpSame") == "true");
        CHECK(get("gpNew") == "true");
        CHECK(get("blockSame") == "true");
        CHECK(get("blockWidth") == "true");
        CHECK(get("blockEvicted") == "true");
        // frame 1 walks every line (no heights yet); then the summed heights, ending where the walk ends
        CHECK(get("fast1") == "false");
        CHECK(get("fast2") == "true");
        for (const char* f : {"1", "2", "3", "4"}) CHECK(get((std::string("end") + f).c_str()) == get((std::string("ref") + f).c_str()));
        CHECK(get("fast4") == "true");
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
}

// The modern pack's STML links (ui/modern/lib/stml.lua): an href's target, also when its quote is never closed (the
// storyline pages some servers ship: <a href="file:///storyline/Epic/BardEpic.txt>Bard</a>), a WndNotify link's
// message and data, and an <a> with neither only coloured.
TEST_CASE("Lua UI: STML href links, an unclosed quote ending at the tag's end") {
    hailstone::test::ScratchDir scratch("lua_ui_api_stml_href_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir / "lib");
    const fs::path ui_src = fs::path(__FILE__).parent_path().parent_path() / "ui";
    if (!fs::exists(ui_src / "modern" / "lib" / "stml.lua")) return; // an installed test binary run away from the sources
    fs::copy_file(ui_src / "modern" / "lib" / "stml.lua", dir / "lib" / "stml.lua");
    std::ofstream(dir / "lib" / "theme.lua")
        << "local c = { link = { 0, 0, 1, 1 }, text = { 1, 1, 1, 1 } }\n"
           "return { color = c, C = c, text = function(x, y, t) return ui.CalcTextSize(t) end }\n";
    std::ofstream(dir / "links.lua") << R"lua(
local stml = dofile("lib/stml.lua")
local function link(line)
  for _, seg in ipairs(stml.Parse(line)) do
    if seg[3] then return type(seg[3]) == "table" and (seg[1] .. "=" .. seg[3][1] .. "," .. seg[3][2]) or (seg[1] .. "=plain") end
  end
  return "none"
end
out = {
  closed = link('<a href="file:///storyline/storyEpicsGuide.txt">Epics</a>'),
  unclosed = link('&nbsp;<a href="file:///storyline/Epic/BardEpic.txt>Bard</a><br>'),
  notify = link('<a WndNotify="27,x">item</a>'),
  plain = link('<a name="top">here</a>'),
}
)lua";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        ImGui::GetIO().DisplaySize = ImVec2(800, 600);
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        lua.frame(); // (the pack's scripts run)
        ImGui::Render();
        auto get = [&](const char* key) {
            lua_getglobal(L, "out");
            if (!lua_istable(L, -1)) {
                lua_settop(L, 0);
                return std::string("no out");
            }
            lua_getfield(L, -1, key);
            std::string v = lua_isnil(L, -1) ? "nil" : luaL_tolstring(L, -1, nullptr);
            lua_settop(L, 0);
            return v;
        };
        CHECK(get("closed") == "Epics=-1,file:///storyline/storyEpicsGuide.txt");
        CHECK(get("unclosed") == "Bard=-1,file:///storyline/Epic/BardEpic.txt");
        CHECK(get("notify") == "item=27,x");
        CHECK(get("plain") == "here=plain");
    }
    ImGui::DestroyContext(ctx);
}

// The table calls ImGui does not guard: TableNextRow and the header row with no current table read through a null
// table, and a column or freeze set up after the first row asserts. The bindings refuse them; inside a table they
// work as before.
TEST_CASE("Lua UI: table calls outside a table, or set up after the first row, are refused") {
    hailstone::test::ScratchDir scratch("lua_ui_api_table_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir);
    std::ofstream(dir / "t.lua") << "ui.window('T', function()\n"
                                    "  ui.Begin('T')\n"
                                    "  ui.TableNextRow()\n"
                                    "  outside = ui.TableHeadersRowClicked()\n"
                                    "  ui.TableSetupColumn('x', 0, 0)\n"
                                    "  ui.TableSetupScrollFreeze(1, 1)\n"
                                    "  if ui.BeginTable('tab', 2, ui.TableFlags.ScrollY, 200, 100) then\n"
                                    "    ui.TableSetupColumn('A')\n"
                                    "    ui.TableSetupColumn('B')\n"
                                    "    ui.TableSetupScrollFreeze(-5, 100000)\n"
                                    "    inside = ui.TableHeadersRowClicked()\n"
                                    "    ui.TableSetupColumn('late')\n"
                                    "    ui.TableSetupScrollFreeze(1, 1)\n"
                                    "    ui.TableNextRow()\n"
                                    "    ui.TableNextColumn() ui.Text('a')\n"
                                    "    ui.EndTable()\n"
                                    "  end\n"
                                    "  ui.TableNextRow()\n"
                                    "  done = true\n"
                                    "  ui.End()\n"
                                    "end)\n";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        for (int i = 0; i < 2; ++i) {
            ImGui::GetIO().DisplaySize = ImVec2(800, 600);
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            lua.frame();
            ImGui::Render();
        }
        lua_getglobal(L, "done");
        CHECK(lua_toboolean(L, -1));
        lua_getglobal(L, "outside");
        CHECK(lua_tointeger(L, -1) == -1);
        lua_getglobal(L, "inside");
        CHECK(lua_tointeger(L, -1) == -1);
        lua_pop(L, 3);
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
}

// ui.Image, ui.ImageButton and ui.DrawImage draw only a texture the client handed out (ui/texture_ids.h): other
// userdata (a full userdata such as an mq.TLO value, a light one never made a texture, one since freed) is none, as
// nil is, and never reaches the draw list, which the Vulkan backend would bind as a descriptor set.
TEST_CASE("Lua UI: images draw only live textures the client handed out") {
    hailstone::test::ScratchDir scratch("lua_ui_api_image_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir);
    std::ofstream(dir / "i.lua") << "ui.window('I', function()\n"
                                    "  ui.Begin('I')\n"
                                    "  for _, t in ipairs({ FULL, STRANGER, FREED, LIVE }) do\n"
                                    "    ui.Image(t, 8, 8)\n"
                                    "    ui.ImageButton('b' .. tostring(_), t, 8, 8)\n"
                                    "    local x, y = ui.GetCursorScreenPos()\n"
                                    "    ui.DrawImage(t, x, y, x + 8, y + 8)\n"
                                    "  end\n"
                                    "  ui.End()\n"
                                    "end)\n";
    static int live = 0, stranger = 0, freed = 0;
    hailstone::ui::texture_id_added(&live);
    hailstone::ui::texture_id_added(&freed);
    hailstone::ui::texture_id_removed(&freed);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        lua_newuserdatauv(L, 16, 0);
        lua_setglobal(L, "FULL");
        lua_pushlightuserdata(L, &stranger);
        lua_setglobal(L, "STRANGER");
        lua_pushlightuserdata(L, &freed);
        lua_setglobal(L, "FREED");
        lua_pushlightuserdata(L, &live);
        lua_setglobal(L, "LIVE");
        int live_draws = 0, other_draws = 0;
        for (int i = 0; i < 2; ++i) {
            ImGui::GetIO().DisplaySize = ImVec2(800, 600);
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            lua.frame();
            ImGui::Render();
            live_draws = other_draws = 0;
            const ImTextureID font = ImGui::GetIO().Fonts->TexID;
            const ImDrawData* data = ImGui::GetDrawData();
            for (const ImDrawList* list : data->CmdLists)
                for (const ImDrawCmd& cmd : list->CmdBuffer) {
                    if (cmd.ElemCount == 0) continue;
                    const ImTextureID id = cmd.GetTexID();
                    if (id == reinterpret_cast<ImTextureID>(&live)) ++live_draws;
                    else if (id != font) ++other_draws;
                }
        }
        CHECK(live_draws > 0);
        CHECK(other_draws == 0);
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    hailstone::ui::texture_id_removed(&live);
    ImGui::DestroyContext(ctx);
}

// ui.Image's optional r, g, b, a tint the image (ImageWithBg: Dear ImGui's Image takes no tint); without them it is
// drawn untinted.
TEST_CASE("Lua UI: ui.Image takes a tint") {
    hailstone::test::ScratchDir scratch("lua_ui_api_image_tint_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir);
    std::ofstream(dir / "i.lua") << "ui.window('I', function()\n"
                                    "  ui.Begin('I')\n"
                                    "  ui.Image(TINTED, 8, 8, 0, 0, 1, 1, 1, 0, 0, 1)\n"
                                    "  ui.Image(PLAIN, 8, 8)\n"
                                    "  ui.End()\n"
                                    "end)\n";
    static int tinted = 0, plain = 0;
    hailstone::ui::texture_id_added(&tinted);
    hailstone::ui::texture_id_added(&plain);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        lua_pushlightuserdata(L, &tinted);
        lua_setglobal(L, "TINTED");
        lua_pushlightuserdata(L, &plain);
        lua_setglobal(L, "PLAIN");
        std::vector<ImU32> tinted_cols, plain_cols;
        for (int i = 0; i < 2; ++i) {
            ImGui::GetIO().DisplaySize = ImVec2(800, 600);
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            lua.frame();
            ImGui::Render();
            tinted_cols.clear();
            plain_cols.clear();
            for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists)
                for (const ImDrawCmd& cmd : list->CmdBuffer) {
                    auto* out = cmd.GetTexID() == reinterpret_cast<ImTextureID>(&tinted) ? &tinted_cols
                                : cmd.GetTexID() == reinterpret_cast<ImTextureID>(&plain) ? &plain_cols
                                                                                          : nullptr;
                    for (unsigned e = 0; out && e < cmd.ElemCount; ++e)
                        out->push_back(list->VtxBuffer[static_cast<int>(cmd.VtxOffset + list->IdxBuffer[static_cast<int>(cmd.IdxOffset + e)])].col);
                }
        }
        REQUIRE_FALSE(tinted_cols.empty());
        REQUIRE_FALSE(plain_cols.empty());
        for (ImU32 c : tinted_cols) CHECK(c == IM_COL32(255, 0, 0, 255));
        for (ImU32 c : plain_cols) CHECK(c == IM_COL32(255, 255, 255, 255));
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    hailstone::ui::texture_id_removed(&tinted);
    hailstone::ui::texture_id_removed(&plain);
    ImGui::DestroyContext(ctx);
}

namespace {
// Two frames of a pack with one script; the global `done` after them.
bool run_pack_frames(const std::string& name, const std::string& script, int frames = 2,
                     const std::function<void(int)>& before_frame = {}) {
    hailstone::test::ScratchDir scratch(name);
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir);
    std::ofstream(dir / "s.lua") << script;
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    bool done = false;
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        for (int i = 0; i < frames; ++i) {
            ImGui::GetIO().DisplaySize = ImVec2(800, 600);
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            if (before_frame) before_frame(i);
            ImGui::NewFrame();
            lua.frame();
            ImGui::Render();
        }
        lua_getglobal(L, "done");
        done = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
    return done;
}
} // namespace

TEST_CASE("Lua UI: ImGui's internal window flags are not a script's, and unbalanced ends are refused") {
    CHECK(run_pack_frames("lua_ui_api_flags_test", "ui.window('F', function()\n"
                                                   "  ui.Begin('F', nil, 1 << 26 | 1 << 27 | 1 << 28)\n"
                                                   "  if ui.BeginChild('c', 50, 50, false, 1 << 26 | 1 << 24) then ui.Text('x') end\n"
                                                   "  ui.EndChild()\n"
                                                   "  ui.EndMenu() ui.EndPopup() ui.EndCombo() ui.EndTooltip()\n"
                                                   "  ui.Text('still F')\n"
                                                   "  ui.End()\n"
                                                   "  done = true\n"
                                                   "end)\n"));
}

TEST_CASE("Lua UI: style lists with NaN or huge indices and values push nothing undefined") {
    CHECK(run_pack_frames("lua_ui_api_style_nan_test",
                          "ui.window('S', function()\n"
                          "  local nc = ui.PushStyleColors({ 0/0, 1, 1, 1, 1, 1e300, 1, 1, 1, 1, -math.huge, 1, 1, 1, 1,\n"
                          "                                  ui.Col.Text, 1e300, 0/0, 1, 1 })\n"
                          "  local nv = ui.PushStyleVars({ 0/0, 1, false, math.huge, 1, 1, ui.StyleVar.Alpha, 1e300, false })\n"
                          "  ui.PopStyleVar(nv) ui.PopStyleColor(nc)\n"
                          "  done = nc == 1 and nv == 1\n"
                          "end)\n"));
}

TEST_CASE("Lua UI: a completion list whose __len raises is read raw, not left to unwind the editor") {
    CHECK(run_pack_frames("lua_ui_api_complete_len_test",
                          "local frame = 0\n"
                          "ui.window('E', function()\n"
                          "  frame = frame + 1\n"
                          "  ui.SetNextWindowPos(0, 0, ui.Cond.Always)\n"
                          "  ui.SetNextWindowSize(300, 200, ui.Cond.Always)\n"
                          "  ui.Begin('E')\n"
                          "  local opts = { complete = function(before)\n"
                          "    asked = true\n"
                          "    return 1, setmetatable({ 'abc' }, { __len = function() error('len') end })\n"
                          "  end }\n"
                          "  local text, changed, cursor, active = ui.CodeEditor('code', 'a', 200, 100, opts)\n"
                          "  ui.End()\n"
                          "  if frame >= 4 then done = asked == true end\n"
                          "end)\n",
                          5, [](int i) { // a click in the box makes it active
                              ImGui::GetIO().AddMousePosEvent(60, 70);
                              if (i == 1) ImGui::GetIO().AddMouseButtonEvent(0, true);
                              if (i == 2) ImGui::GetIO().AddMouseButtonEvent(0, false);
                          }));
}

TEST_CASE("Lua UI: code editors made from changing ids are forgotten, not kept for the session") {
    std::size_t kept = 0;
    int oldest_at_10 = -1, oldest = -1;
    CHECK(run_pack_frames("lua_ui_api_editors_test",
                          "local frame = 0\n"
                          "ui.window('C', function()\n"
                          "  frame = frame + 1\n"
                          "  ui.Begin('C')\n"
                          "  ui.CodeEditor('kept', 'x', 100, 50)\n"
                          "  ui.CodeEditor('id ' .. frame, 'x', 100, 50)\n"
                          "  ui.End()\n"
                          "  done = true\n"
                          "end)\n",
                          400, [&](int i) {
                              if (i == 10) oldest_at_10 = hailstone::ui::lua::code_editors_oldest();
                              if (i == 399) {
                                  kept = hailstone::ui::lua::code_editors_kept();
                                  oldest = hailstone::ui::lua::code_editors_oldest();
                              }
                          }));
    CHECK(kept > 0);
    CHECK(kept <= 258); // past 256, those not drawn last frame go
    REQUIRE(oldest_at_10 >= 0);
    CHECK(oldest == oldest_at_10); // the one drawn every frame was never forgotten
}

TEST_CASE("Lua UI: the code editor places text as CalcTextSize from the line's start does") {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    ImGui::GetIO().DisplaySize = ImVec2(800, 600);
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    const float line_h = ImGui::GetFontSize();
    auto expected = [&](const std::string& text, std::size_t to) {
        to = std::min(to, text.size());
        const std::size_t nl = to == 0 ? std::string::npos : text.rfind('\n', to - 1);
        const std::size_t start = nl == std::string::npos ? 0 : nl + 1;
        const auto line = static_cast<float>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(start), '\n'));
        return std::pair<float, float>(ImGui::CalcTextSize(text.data() + start, text.data() + to).x, line * line_h);
    };
    const std::vector<std::string> texts = {
        "local x = 1 -- a comment\nprint(x)\n",
        "--[[ a long\ncomment over\nthree lines ]] local s = 'abc'\n\tif x then\n\t\treturn \"t\\tab\" end",
        "caf\xc3\xa9 = '\xe2\x82\xac\xe2\x82\xac' -- multibyte\nna\xc3\xafve = 1\n\n\nlast",
        "",
    };
    for (const auto& text : texts) {
        CAPTURE(text);
        // Increasing offsets one byte at a time (the spans' walk), then a caret before the last (a restart), past
        // the end, and a few in no order.
        std::vector<std::size_t> at;
        for (std::size_t i = 0; i <= text.size(); ++i) at.push_back(i);
        at.push_back(text.size() / 2);
        at.push_back(text.size() + 5);
        at.push_back(1);
        at.push_back(text.size() / 3);
        at.push_back(0);
        const auto got = hailstone::ui::lua::code_editor_places(text, at);
        REQUIRE(got.size() == at.size());
        for (std::size_t i = 0; i < at.size(); ++i) {
            CAPTURE(at[i]);
            CHECK(got[i] == expected(text, at[i]));
        }
    }
    ImGui::EndFrame();
    ImGui::DestroyContext(ctx);
}

// The modern pack's "Close with Esc" (ui/modern/lib/escape.lua): off until ticked; one press closes one ticked window,
// the one opened or clicked last.
TEST_CASE("Modern UI libs: Close with Esc closes the frontmost ticked window, one a press") {
    hailstone::test::ScratchDir scratch("lua_ui_modern_escape_test");
    const fs::path dir = scratch.path() / "pack";
    const fs::path lib = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern" / "lib";
    REQUIRE(fs::exists(lib / "escape.lua"));
    fs::create_directories(dir);
    fs::copy(lib, dir / "lib", fs::copy_options::recursive);
    std::ofstream(dir / "esc.lua") << R"lua(
PRESS = false
eq = { TakeCloseTopWindow = function() local p = PRESS; PRESS = false; return p end }
local esc = dofile("lib/escape.lua")
OPEN = { A = true, B = false, C = false }
local place = { A = 0, B = 200, C = 400 }
for _, name in ipairs({ "A", "B", "C" }) do
  ui.window(name, function()
    if not OPEN[name] then return end
    ui.SetNextWindowPos(place[name], 0, ui.Cond.Always)
    ui.SetNextWindowSize(150, 100, ui.Cond.Always)
    ui.Begin(name)
    if esc.Frame(name) then OPEN[name] = false end
    ui.End()
  end)
end
function tick(name) ui.SetSetting("hud.esc." .. name, "1") end
function open() return (OPEN.A and "A" or "") .. (OPEN.B and "B" or "") .. (OPEN.C and "C" or "") end
)lua";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        auto frames = [&](int n) {
            for (int i = 0; i < n; ++i) {
                ImGui::GetIO().DisplaySize = ImVec2(800, 600);
                ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
                ImGui::NewFrame();
                lua.frame();
                ImGui::Render();
            }
        };
        auto lua_do = [&](const char* code) {
            REQUIRE(luaL_dostring(L, code) == LUA_OK);
            std::string out = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
            lua_settop(L, 0);
            return out;
        };
        auto press = [&] {
            lua_do("PRESS = true");
            frames(3);
        };
        frames(2);
        lua_do("OPEN.B = true");
        frames(2);
        lua_do("OPEN.C = true");
        frames(2);
        REQUIRE(lua_do("return open()") == "ABC");
        // nothing ticked: the press closes nothing, and is not kept for later
        press();
        CHECK(lua_do("return open()") == "ABC");
        lua_do("tick('A') tick('B')");
        frames(2);
        CHECK(lua_do("return open()") == "ABC");
        // C opened last but is not ticked: B, the ticked one opened last, goes; then A
        press();
        CHECK(lua_do("return open()") == "AC");
        press();
        CHECK(lua_do("return open()") == "C");
        press();
        CHECK(lua_do("return open()") == "C");
        // a click brings a window to the front: B opened after A, A clicked after that
        lua_do("OPEN.A, OPEN.B = true, true");
        frames(2);
        ImGui::GetIO().AddMousePosEvent(50, 50);
        frames(1);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frames(1);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frames(1);
        press();
        CHECK(lua_do("return open()") == "BC");
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
}

// Some windows (the inventory, the books, the bags, the map...) have Close with Esc ticked until the player unticks it.
TEST_CASE("Modern UI libs: Close with Esc is on by default for the put-away windows, until unticked") {
    hailstone::test::ScratchDir scratch("lua_ui_modern_escape_default_test");
    const fs::path dir = scratch.path() / "pack";
    const fs::path lib = fs::path(__FILE__).parent_path().parent_path() / "ui" / "modern" / "lib";
    REQUIRE(fs::exists(lib / "escape.lua"));
    fs::create_directories(dir);
    fs::copy(lib, dir / "lib", fs::copy_options::recursive);
    std::ofstream(dir / "esc.lua") << R"lua(
PRESS = false
eq = { TakeCloseTopWindow = function() local p = PRESS; PRESS = false; return p end }
local esc = dofile("lib/escape.lua")
NAMES = { "Other", "InventoryWindow", "Bag7", "ItemDisplayWindow#3" }
OPEN = { Other = true, InventoryWindow = true, Bag7 = true, ["ItemDisplayWindow#3"] = true }
for i, name in ipairs(NAMES) do
  ui.window(name, function()
    if not OPEN[name] then return end
    ui.SetNextWindowPos((i - 1) * 200, 0, ui.Cond.Always)
    ui.SetNextWindowSize(150, 100, ui.Cond.Always)
    ui.Begin(name)
    if esc.Frame(name) then OPEN[name] = false end
    ui.End()
  end)
end
function open()
  local s = ""
  for _, name in ipairs(NAMES) do if OPEN[name] then s = s .. name .. " " end end
  return s
end
)lua";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        auto frames = [&](int n) {
            for (int i = 0; i < n; ++i) {
                ImGui::GetIO().DisplaySize = ImVec2(800, 600);
                ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
                ImGui::NewFrame();
                lua.frame();
                ImGui::Render();
            }
        };
        auto lua_do = [&](const char* code) {
            REQUIRE(luaL_dostring(L, code) == LUA_OK);
            std::string out = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
            lua_settop(L, 0);
            return out;
        };
        auto press = [&] {
            lua_do("PRESS = true");
            frames(3);
        };
        frames(2);
        // nothing saved: the inventory, the bag and the item display close, the other window does not
        press();
        press();
        press();
        CHECK(lua_do("return open()") == "Other ");
        press();
        CHECK(lua_do("return open()") == "Other ");
        // unticked ("0" saved), the inventory stays open; the bag still closes
        lua_do("ui.SetSetting('hud.esc.InventoryWindow', '0') OPEN.InventoryWindow, OPEN.Bag7 = true, true");
        frames(2);
        press();
        press();
        CHECK(lua_do("return open()") == "Other InventoryWindow ");
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
}

// ui.Begin's collapseRight puts the collapse button beside the close box for that window alone (the style's
// WindowMenuButtonPosition is put back); ui.SetNextWindowCollapsed / ui.IsWindowCollapsed roll it up and say so.
TEST_CASE("Lua UI: a window rolls up, its collapse button at the right") {
    hailstone::test::ScratchDir scratch("lua_ui_api_collapse_test");
    const fs::path dir = scratch.path() / "pack";
    fs::create_directories(dir);
    std::ofstream(dir / "roll.lua") << "ui.window('Roll', function()\n"
                                       "  ui.SetNextWindowPos(40, 50, ui.Cond.Always)\n"
                                       "  ui.SetNextWindowSize(200, 150, ui.Cond.Always)\n"
                                       "  if ROLL ~= nil then ui.SetNextWindowCollapsed(ROLL, 3) end\n"
                                       "  ui.Begin('Roll###roll', true, 0, true)\n"
                                       "  rolled = ui.IsWindowCollapsed()\n"
                                       "  ui.End()\n"
                                       "end)\n";
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    {
        hailstone::ui::lua::LuaUi lua(dir, scratch.path() / "ui.txt");
        lua_State* L = lua.state();
        auto frames = [&](int n) {
            for (int i = 0; i < n; ++i) {
                ImGui::GetIO().DisplaySize = ImVec2(800, 600);
                ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
                ImGui::NewFrame();
                lua.frame();
                ImGui::Render();
            }
        };
        auto rolled = [&] {
            lua_getglobal(L, "rolled");
            const bool v = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            return v;
        };
        frames(2);
        CHECK(ImGui::GetStyle().WindowMenuButtonPosition == ImGuiDir_Left);
        CHECK_FALSE(rolled());
        // (a cond of more than one flag, which ImGui asserts on, is taken as Always)
        REQUIRE(luaL_dostring(L, "ROLL = true") == LUA_OK);
        frames(2);
        CHECK(rolled());
        ImGuiWindow* win = ImGui::FindWindowByName("###roll");
        REQUIRE(win);
        CHECK(win->Collapsed);
        // the collapse button left of the close box, at the title bar's right
        const ImGuiStyle& st = ImGui::GetStyle();
        const float fs = win->TitleBarHeight - st.FramePadding.y * 2;
        const ImVec2 at(win->Pos.x + win->Size.x - st.FramePadding.x - fs - st.ItemInnerSpacing.x - fs / 2,
                        win->Pos.y + st.FramePadding.y + fs / 2);
        REQUIRE(luaL_dostring(L, "ROLL = nil") == LUA_OK);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(at.x, at.y);
        frames(1);
        io.AddMouseButtonEvent(0, true);
        frames(1);
        io.AddMouseButtonEvent(0, false);
        frames(2);
        CHECK_FALSE(rolled());
        CHECK(ImGui::GetStyle().WindowMenuButtonPosition == ImGuiDir_Left);
        CHECK_FALSE(ImGui::FindWindowByName("UI errors"));
    }
    ImGui::DestroyContext(ctx);
}
