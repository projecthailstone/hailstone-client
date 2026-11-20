#include "game/item_links.h"

#include "game/mechanics/item_stats.h"
#include "net/messages/achievement.h"

#include <algorithm>
#include <cstdio>

namespace hailstone::game {

namespace {

constexpr char kTag = '\x12';

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool all_hex(std::string_view s) {
    for (char c : s)
        if (hex_digit(c) < 0) return false;
    return true;
}

std::uint32_t hex(std::string_view s) {
    std::uint32_t v = 0;
    for (char c : s) v = (v << 4) | static_cast<std::uint32_t>(hex_digit(c));
    return v;
}

} // namespace

net::ItemLinkClick ItemLink::click_request() const {
    net::ItemLinkClick c;
    c.item_id = item_id;
    c.augments = augments;
    c.hash = hash;
    c.flags = 4; // ParseItemLinkBody's caller ORs in 4 (plus 2/8/0x10 for shift/ctrl/alt)
    c.is_evolving = is_evolving;
    c.evolve_group = evolve_group;
    c.evolve_level = evolve_level;
    c.ornament_icon = ornament_icon;
    return c;
}

std::optional<ItemLink> parse_item_link(std::string_view tag) {
    if (tag.empty() || tag[0] != '0') return std::nullopt;
    std::string_view body = tag.substr(1);
    // Field widths: 7 ids, then 1 + 4 + 2 + 5 + 8 = 20 characters.
    for (std::size_t id_width : {std::size_t{8}, std::size_t{5}}) {
        std::size_t len = id_width * 7 + 20;
        if (body.size() < len || !all_hex(body.substr(0, len))) continue;
        ItemLink l;
        l.wide = id_width == 8;
        std::size_t at = 0;
        auto take = [&](std::size_t n) {
            std::uint32_t v = hex(body.substr(at, n));
            at += n;
            return v;
        };
        l.item_id = take(id_width);
        for (auto& a : l.augments) a = take(id_width);
        l.is_evolving = take(1);
        l.evolve_group = take(4);
        l.evolve_level = take(2);
        l.ornament_icon = take(5);
        l.hash = take(8);
        l.text = std::string(body.substr(len));
        return l;
    }
    return std::nullopt;
}

std::string stml_link_data(std::string_view tag) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out = "x";
    out.reserve(1 + tag.size() * 2);
    for (const char ch : tag) {
        const auto byte = static_cast<unsigned char>(ch);
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 15]);
    }
    return out;
}

std::string stml_link_tag(std::string_view data) {
    if (data.empty() || data.front() != 'x' || data.size() % 2 == 0) return std::string(data);
    std::string_view body = data.substr(1);
    if (!all_hex(body)) return std::string(data);
    std::string out;
    out.reserve(body.size() / 2);
    for (std::size_t i = 0; i < body.size(); i += 2)
        out.push_back(static_cast<char>((hex_digit(body[i]) << 4) | hex_digit(body[i + 1])));
    return out;
}

std::string escape_stml(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '<') out += "&lt;";
        else if (ch == '>') out += "&gt;";
        else if (ch == '&') out += "&amp;";
        else out += ch;
    }
    return out;
}

std::optional<AchievementChatLink> parse_achievement_chat_link(std::string_view tag) {
    if (tag.empty() || tag.front() != '3' || tag.size() > net::kMaxAchievementLinkBytes + 4098)
        return std::nullopt;
    const auto separator = tag.find('\'', 1);
    if (separator == std::string_view::npos || separator < 2 || tag[separator - 1] != '^' ||
        separator - 1 > net::kMaxAchievementLinkBytes || tag.size() - separator - 1 > 4096)
        return std::nullopt;
    // __CleanItemTags' achievement case selects the suffix after the apostrophe.
    // Control characters cannot become hidden tag delimiters or extra chat lines.
    for (const unsigned char ch : tag)
        if (ch < 0x20 || ch == 0x7f) return std::nullopt;
    return AchievementChatLink{std::string(tag.substr(1, separator - 1)), std::string(tag.substr(separator + 1))};
}

std::vector<ChatSegment> split_links(std::string_view text) {
    std::vector<ChatSegment> out;
    std::string plain;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t open = text.find(kTag, i);
        std::size_t close = open == std::string_view::npos ? open : text.find(kTag, open + 1);
        if (close == std::string_view::npos) {
            plain.append(text.substr(i));
            break;
        }
        plain.append(text.substr(i, open - i));
        std::string_view tag = text.substr(open + 1, close - open - 1);
        if (auto link = parse_item_link(tag)) {
            if (!plain.empty()) out.push_back({std::move(plain), std::nullopt});
            plain.clear();
            std::string shown = link->text;
            out.push_back({std::move(shown), std::move(link)});
        } else if (auto achievement = parse_achievement_chat_link(tag)) {
            if (!plain.empty()) out.push_back({std::move(plain), std::nullopt});
            plain.clear();
            std::string shown = achievement->text;
            out.push_back({std::move(shown), std::nullopt, std::move(achievement)});
        } else if (!tag.empty() && tag.front() == '3') {
            plain.append("Achievement"); // malformed metadata is not visible chat text
        } else if (!tag.empty()) {
            plain.append(tag.substr(1)); // other tag codes: their text
        }
        i = close + 1;
    }
    if (!plain.empty()) out.push_back({std::move(plain), std::nullopt});
    return out;
}

std::string strip_links(std::string_view text) {
    std::string s;
    for (const auto& seg : split_links(text)) s += seg.text;
    return s;
}

// ---------------------------------------------------------------- the link hash

namespace {

// Utf8CharLength @0x808f40: 1 for ASCII, else the length table at 0xb61d08 by
// bits 2..5 of the lead byte.
int utf8_length(unsigned char lead) {
    static constexpr int kByLead[16] = {2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 6};
    if (lead < 0x80) return 1;
    return kByLead[(lead >> 2) & 0xf];
}

// A byte sign-extended to 16 bits, as Utf8DecodeChar @0x8092b0 reads it.
std::uint16_t sx(unsigned char c) {
    return static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<std::int8_t>(c)));
}

// MSVC's towupper in the C locale: a..z only.
std::uint16_t towupper_c(std::uint16_t c) { return c >= 'a' && c <= 'z' ? static_cast<std::uint16_t>(c - 32) : c; }

template <typename... A> std::string format_0x800(const char* fmt, A... args) {
    char buf[0x800];
    std::snprintf(buf, sizeof buf, fmt, args...); // SafeSnprintf: _vsnprintf, the last byte always 0
    return buf;
}

std::string itoa10(std::int32_t v) { return std::to_string(v); } // _itoa(v, s, 10): '-' for negatives

} // namespace

std::uint32_t utf8_hash_no_case(std::string_view text) {
    std::uint32_t hash = 0;
    std::uint16_t code = 0; // the original's argument slot (4- to 6-byte sequences leave it)
    std::size_t i = 0;
    while (i < text.size() && text[i] != 0) {
        auto b = [&](std::size_t k) {
            return i + k < text.size() ? static_cast<unsigned char>(text[i + k]) : static_cast<unsigned char>(0);
        };
        auto low = [](unsigned char c, unsigned mask) { return static_cast<unsigned char>(c & mask); };
        int length = utf8_length(b(0));
        if (length == 1) {
            code = sx(b(0));
        } else if (length == 2) {
            std::uint16_t high = sx(low(b(0), 0x1f));
            code = static_cast<std::uint16_t>((high << 6) + sx(low(b(1), 0x3f)));
        } else if (length == 3) {
            auto high = static_cast<std::uint16_t>(sx(low(b(1), 0x3f)) + static_cast<std::uint16_t>(sx(b(0)) << 6));
            code = static_cast<std::uint16_t>((high << 6) + sx(low(b(2), 0x3f)));
        }
        std::uint16_t upper = towupper_c(code);
        hash = hash * 31 + upper;
        code = upper;
        i += static_cast<std::size_t>(length);
    }
    return hash;
}

std::string item_link_hash_text(const ItemLinkHashInput& in) {
    // Both parts are 0x800-byte buffers; steve_strcat(first, second, 0x800) ends the text at 0x7ff.
    std::string text = format_0x800("%d%s", in.item_number, in.name.c_str());
    std::string line;
    auto value = [&] {
        std::uint32_t v = static_cast<std::uint32_t>(in.cost);
        if (in.type == 0) v += static_cast<std::uint32_t>(in.contents_value);
        return static_cast<std::int32_t>(v);
    };
    auto u = [](std::int32_t v) { return static_cast<unsigned>(v); };
    if (in.type == 1) {
        line = format_0x800("%x%d%09X%d", static_cast<unsigned>(in.slots), static_cast<int>(in.weight_reduction),
                            u(value()), in.weight);
    } else if (in.type == 2) {
        line = format_0x800("%d%d%09X", in.weight, static_cast<int>(in.book_lang), u(value()));
    } else if (in.type == 0) {
        bool charm = in.charm_file_id != 0;
        for (const auto& aug : in.sockets) {
            if (!aug) continue;
            if (!charm) charm = aug->charm_file_id != 0;
            text += itoa10(aug->item_number);
        }
        if (charm)
            line = format_0x800("%d %d %d %d %d %d %d %d", static_cast<int>(in.light), in.icon, value(), in.size,
                                static_cast<int>(in.type), static_cast<int>(in.item_class), in.favor, in.guild_favor);
        else
            line = format_0x800("%d %d %d %d %d %d %d %d %d %d %d %d %d", in.mana, in.hp, in.favor,
                                static_cast<int>(in.light), in.icon, value(), in.weight, in.required_level, in.size,
                                static_cast<int>(in.type), static_cast<int>(in.item_class), in.ac, in.guild_favor);
    }
    if (text.size() > 0x7ff) text.resize(0x7ff);
    text += line;
    if (text.size() > 0x7ff) text.resize(0x7ff);
    return text;
}

std::uint32_t item_link_hash(const ItemLinkHashInput& in) { return utf8_hash_no_case(item_link_hash_text(in)); }

ItemLinkHashInput item_link_hash_input(const net::Item& item) {
    ItemLinkHashInput in;
    in.name = item.name;
    in.item_number = static_cast<std::int32_t>(item.id);
    in.type = item.item_class;
    in.cost = static_cast<std::int32_t>(item.value);
    in.icon = static_cast<std::int32_t>(item.icon);
    in.light = item.light;
    in.item_class = item.item_type;
    in.charm_file_id = static_cast<std::int32_t>(item.charm_file_id);
    in.slots = item.bag_slots;
    in.weight_reduction = item.weight_reduction;
    in.book_lang = static_cast<std::int8_t>(item.book_type);
    in.favor = static_cast<std::int32_t>(item.favor);
    in.guild_favor = static_cast<std::int32_t>(item.guild_favor);
    // The getters CalcItemLinkHash calls: SumIntStatRecursive(Weight) without contents, GetMaxIntFieldRecursive(
    // RequiredLevel, 1), GetSize(1), GetAC / GetHP / GetMana(1, 0, 1), and Cost plus the contents' GetItemValue.
    in.weight = mechanics::item_weight(item);
    in.required_level = mechanics::item_required_level(item);
    in.size = static_cast<std::int32_t>(mechanics::item_size(item));
    in.ac = mechanics::item_ac(item);
    in.hp = mechanics::item_stat(item, [](const net::Item& i) { return i.hp; });
    in.mana = mechanics::item_stat(item, [](const net::Item& i) { return i.mana; });
    in.contents_value = static_cast<std::int32_t>(static_cast<std::uint32_t>(mechanics::item_value(item)) -
                                                  item.value);
    if (item.item_class == 0)
        for (const auto& c : item.contents) {
            if (c.index >= 6) continue;
            in.sockets[c.index] = ItemLinkHashInput::Augment{static_cast<std::int32_t>(c.item.id),
                                                             static_cast<std::int32_t>(c.item.charm_file_id)};
        }
    return in;
}

std::int32_t benefit_flag_max(const net::Item& item) {
    if (item.item_class != 0) return 0;
    std::int32_t m = std::max(0, static_cast<std::int32_t>(item.benefit_flag));
    for (const auto& c : item.contents) m = std::max(m, benefit_flag_max(c.item));
    return m;
}

std::string make_item_link(const net::Item& item, bool wide) {
    std::array<std::uint32_t, 6> augs{};
    if (item.item_class == 0)
        for (const auto& c : item.contents)
            if (c.index < 6) augs[c.index] = c.item.id;
    std::uint32_t hash = item_link_hash(item_link_hash_input(item));
    char body[128];
    if (wide)
        std::snprintf(body, sizeof body, "0%08X%08X%08X%08X%08X%08X%08X%1X%04X%02X%05X%08X", item.id, augs[0], augs[1],
                      augs[2], augs[3], augs[4], augs[5], 0u, 0u, 0u, item.ornament_icon & 0xfffffu, hash);
    else
        std::snprintf(body, sizeof body, "0%05X%05X%05X%05X%05X%05X%05X%1X%04X%02X%05X%08X", item.id & 0xfffffu,
                      augs[0] & 0xfffffu, augs[1] & 0xfffffu, augs[2] & 0xfffffu, augs[3] & 0xfffffu,
                      augs[4] & 0xfffffu, augs[5] & 0xfffffu, 0u, 0u, 0u, item.ornament_icon & 0xfffffu, hash);
    std::string s(1, kTag);
    s += body;
    s += item.name;
    s += kTag;
    return s;
}

std::string make_item_link(std::uint32_t item_id, std::string_view name, bool wide) {
    char body[128];
    if (wide)
        std::snprintf(body, sizeof body, "0%08X%08X%08X%08X%08X%08X%08X%1X%04X%02X%05X%08X", item_id, 0u, 0u, 0u, 0u,
                      0u, 0u, 0u, 0u, 0u, 0u, 0u);
    else
        std::snprintf(body, sizeof body, "0%05X%05X%05X%05X%05X%05X%05X%1X%04X%02X%05X%08X", item_id & 0xfffffu, 0u,
                      0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
    std::string s(1, kTag);
    s += body;
    s += name;
    s += kTag;
    return s;
}

LinkAllChat link_all_into_chat(const std::string& typed, const std::vector<std::string>& tags, std::size_t max_chars,
                               int max_tags) {
    LinkAllChat r;
    r.text = typed;
    auto fits = [&](std::string_view add) { return r.text.size() + add.size() <= max_chars; };
    auto tag_count = [&] {
        int n = 0;
        for (const auto& seg : split_links(r.text))
            if (seg.link || seg.achievement) ++n;
        return n;
    };
    auto add_tag = [&](const std::string& tag) { // CEditWnd::AddItemTag: refused at 10 tags, then ReplaceSelection
        if (tag_count() >= max_tags || !fits(tag)) return false;
        r.text += tag;
        return true;
    };
    auto enter = [&] { // ParentWndNotification(6): the line goes; SetWindowText(the typed text)
        r.sent.push_back(r.text);
        r.text = typed;
    };
    bool added = false; // bl: the last link went in
    for (const std::string& tag : tags) {
        if (added) {
            if (fits(", ")) r.text += ", ";
            else {
                enter();
                added = false;
            }
        }
        if (add_tag(tag)) added = true;
        else if (added) {
            enter();
            added = add_tag(tag);
        }
    }
    return r;
}

} // namespace hailstone::game
