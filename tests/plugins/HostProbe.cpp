// A compiled test plugin (tests/native_plugins_test.cpp), in C++ against the SDK alone: it calls the host table from
// where a plugin should not, to check that the client holds.
//   /probethread   calls host functions from a std::thread, then reports what they answered
//   /probethrow    lets an exception out (not a std::exception); so do ${Probe.Throw} and OnUpdateImGui
//   /probestate n  web_state(tab 1) with a struct whose size says n bytes, as a plugin built with a shorter one
//   /probemessage n  web_message(tab 1) into a buffer of n bytes (0: no buffer)
//   /probetcp port    tcp_open("127.0.0.1", port); /probetcpsend h  tcp_send(h, "hello") and tcp_state(h)
//   /probebuffs [n], /probepetbuffs [n], /probespawns [n]  get_buff_data_bulk / get_pet_buff_data_bulk /
//   get_spawns_data_bulk into n bytes (the whole buffer without n), read through the SDK's HS_Bulk_* structs cast
//   onto the buffer
//   /probextargets [n]  get_xtarget_data_bulk the same way, through HS_Bulk_XTargetData
//   /probecursor   cursor_origin, when the host table has it
// Built again with HAILSTONE_PROBE_THROW_INIT (HostProbeThrow): InitializePlugin registers a command and a TLO, then
// throws.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

#include "hailstone_byte_io.h"
#include "hailstone_plugin.h"

HAILSTONE_PLUGIN

namespace {

void nothing(void*, const char*) {}

void probe_thread(void*, const char*) {
    int added = -1;
    std::uint32_t caps = 99;
    void* imgui = &added;
    std::thread t([&] {
        added = host->add_command(host->host, "/fromthread", nothing, nullptr);
        caps = host->caps(host->host);
        imgui = host->imgui_context(host->host);
        host->write_chat(host->host, "[Probe] written from the thread", 0);
        host->do_command(host->host, "/echo from the thread");
    });
    t.join();
    WriteChatf("[Probe] thread: add_command %d, caps %u, imgui %s", added, static_cast<unsigned>(caps),
               imgui ? "set" : "null");
}

void probe_throw(void*, const char*) { throw 42; }

void probe_state(void*, const char* args) {
    HailstoneWebState st;
    std::memset(&st, 0x5a, sizeof st);
    st.size = static_cast<size_t>(std::strtoul(args, nullptr, 10));
    const int r = host->web_state(host->host, 1, &st);
    WriteChatf("[Probe] state %d, size %u, url %s, title %s", r, static_cast<unsigned>(st.size),
               r && st.size > offsetof(HailstoneWebState, url) ? st.url : "-",
               static_cast<unsigned char>(st.title[0]) == 0x5a ? "untouched" : st.title);
}

void probe_message(void*, const char* args) {
    char buf[64];
    const std::size_t n = std::strtoul(args, nullptr, 10);
    const int r = host->web_message(host->host, 1, n ? buf : nullptr, n < sizeof buf ? n : sizeof buf);
    WriteChatf("[Probe] message %d, %s", r, n && r >= 0 ? buf : "-");
}

void probe_tcp(void*, const char* args) {
    const auto port = static_cast<std::uint16_t>(std::strtoul(args, nullptr, 10));
    WriteChatf("[Probe] tcp open %d", static_cast<int>(host->tcp_open(host->host, "127.0.0.1", port)));
}

void probe_tcp_send(void*, const char* args) {
    const auto handle = static_cast<std::int32_t>(std::strtol(args, nullptr, 10));
    const long long sent = host->tcp_send(host->host, handle, "hello", 5);
    WriteChatf("[Probe] tcp send %lld state %d", sent, static_cast<int>(host->tcp_state(host->host, handle)));
}

// A bulk read's buffer size: the command's number, at most `whole`; `whole` without one (no argument, only spaces,
// or no number at its start).
std::int32_t size_arg(const char* args, std::size_t whole) {
    if (!args) return static_cast<std::int32_t>(whole);
    char* end = nullptr;
    const std::size_t n = std::strtoul(args, &end, 10);
    if (end == args) return static_cast<std::int32_t>(whole); // nothing read as a number
    return static_cast<std::int32_t>(n < whole ? n : whole);
}

static_assert(sizeof(HS_Bulk_BuffData) == HAILSTONE_BUFF_RECORD_SIZE);
static_assert(sizeof(HS_Bulk_PetBuffData) == HAILSTONE_PET_BUFF_RECORD_SIZE);

// The occupied slots one line each; the empty ones only counted, and only when laid out as empty.
void probe_buffs(void*, const char* args) {
    unsigned char buf[4096]; // at least 62 * HAILSTONE_BUFF_RECORD_SIZE (1798), or nothing comes back
    const std::int32_t r = host->get_buff_data_bulk(host->host, buf, size_arg(args, sizeof buf));
    const std::size_t count = r > 0 ? static_cast<std::size_t>(r) / sizeof(HS_Bulk_BuffData) : 0;
    const auto* recs = reinterpret_cast<const HS_Bulk_BuffData*>(buf);
    int empty = 0;
    for (std::size_t slot = 0; slot < count; ++slot) {
        const HS_Bulk_BuffData* rec = recs + slot;
        if (rec->ID == 0) { // an empty slot: every field 0, including the counter type
            ++empty;
            continue;
        }
        WriteChatf("[Probe] buff %d: %d %d %d %d %d %d %d %u", static_cast<int>(slot), rec->ID, rec->casterID,
            rec->duration, rec->hitcount, rec->spelltype, rec->counterType, rec->counterTotal,
            static_cast<unsigned>(rec->slotType)); // HAILSTONE_BUFF_SLOT_*
    }
    WriteChatf("[Probe] buffs %d bytes, %d empty", r, empty);
}

void probe_pet_buffs(void*, const char* args) {
    unsigned char buf[1024];
    const std::int32_t r = host->get_pet_buff_data_bulk(host->host, buf, size_arg(args, sizeof buf));
    const std::size_t count = r > 0 ? static_cast<std::size_t>(r) / sizeof(HS_Bulk_PetBuffData) : 0;
    const auto* recs = reinterpret_cast<const HS_Bulk_PetBuffData*>(buf);
    for (std::size_t i = 0; i < count; ++i) {
        const HS_Bulk_PetBuffData* rec = recs + i;
        WriteChatf("[Probe] pet buff: %d %d %d", rec->id, rec->duration, rec->spellType);
    }
    WriteChatf("[Probe] pet buffs %d bytes", r);
}

static_assert(sizeof(HS_Bulk_SpawnHeader) == HS_BULK_SPAWN_HEADER_SIZE);
static_assert(sizeof(HS_Bulk_SpawnFixedRecord) == HS_BULK_SPAWN_RECORD_SIZE);

// The header, then each spawn one line (id, level, class, race, HP percent, con colour, name), then the bytes left
// over after the records the header counts (0 when every record's strings are framed right).
void probe_spawns(void*, const char* args) {
    static unsigned char buf[1024*1024];
    const std::int32_t r = host->get_spawns_data_bulk(host->host, buf, size_arg(args, sizeof buf));
    if (r <= 0 || static_cast<std::size_t>(r) < sizeof(HS_Bulk_SpawnHeader)) {
        WriteChatf("[Probe] spawns %d bytes", static_cast<int>(r));
        return;
    }
    const auto size = static_cast<std::size_t>(r);

    const auto* header = reinterpret_cast<const HS_Bulk_SpawnHeader*>(buf);
    std::size_t at = sizeof(HS_Bulk_SpawnHeader);

    for (std::uint32_t i = 0; i < header->spawnCount; ++i) {
        if (size - at < sizeof(HS_Bulk_SpawnFixedRecord)) break; // a short buffer: stop, the "left" count shows it
        const auto* rec = reinterpret_cast<const HS_Bulk_SpawnFixedRecord*>(buf + at);
        at += sizeof(HS_Bulk_SpawnFixedRecord);

        // BodyDesc, CleanName, DisplayName, Name, RaceName, Suffix, TypeDesc: each a u16 length and its bytes.
        hailstone::ByteReader rd(buf + at, size - at);
        std::string name;
        for (int s = 0; s < 7; ++s) {
            const auto len = rd.le<std::uint16_t>();
            const auto text = rd.bytes(len);
            if (s == 3) name.assign(reinterpret_cast<const char*>(text.data()), text.size());
        }
        at += rd.position();

        WriteChatf("[Probe] spawn %d: %d %d %d %d %d %s", rec->id, rec->level, rec->classID, rec->raceID, rec->pctHPs,
            rec->conColorID, name.c_str());
    }
    WriteChatf("[Probe] spawns %u, player %.2f %.2f %.2f, %zu bytes left", header->spawnCount,
        header->playerX, header->playerY, header->playerZ, size - at);
}
static_assert(sizeof(HS_Bulk_XTargetData) == HS_BULK_XTARGET_RECORD_SIZE);

// Each extended target slot one line: slot, type (HS_BULK_XTARGET_*), spawn id, aggro percent, HP percent.
void probe_xtargets(void*, const char* args) {
    unsigned char buf[512];
    const std::int32_t r = host->get_xtarget_data_bulk(host->host, buf, size_arg(args, sizeof buf));
    const std::size_t count = r > 0 ? static_cast<std::size_t>(r) / sizeof(HS_Bulk_XTargetData) : 0;
    const auto* recs = reinterpret_cast<const HS_Bulk_XTargetData*>(buf);
    for (std::size_t slot = 0; slot < count; ++slot) {
        const HS_Bulk_XTargetData* rec = recs + slot;
        WriteChatf("[Probe] xtarget %d: %d %d %d %d", static_cast<int>(slot), rec->targetType, rec->id,
            rec->aggroPct, rec->pctHPs);
    }
    WriteChatf("[Probe] xtargets %d bytes", static_cast<int>(r));
}

void probe_cursor(void*, const char*) {
    if (!HAILSTONE_HOST_HAS(host, cursor_origin)) {
        WriteChatf("[Probe] cursor origin: not in this client");
        return;
    }
    WriteChatf("[Probe] cursor origin %u", static_cast<unsigned>(host->cursor_origin(host->host)));
}

int probe_tlo(void*, const char*, const char* member, const char*, HailstoneValue* out) {
    if (member && std::strcmp(member, "Throw") == 0) throw std::runtime_error("the TLO threw");
    out->kind = HAILSTONE_STRING;
    out->s = "probe";
    return 1;
}

} // namespace

HAILSTONE_API void InitializePlugin(void) {
    AddCommand("/probethread", probe_thread, nullptr);
    AddCommand("/probethrow", probe_throw, nullptr);
    AddCommand("/probestate", probe_state, nullptr);
    AddCommand("/probemessage", probe_message, nullptr);
    AddCommand("/probetcp", probe_tcp, nullptr);
    AddCommand("/probetcpsend", probe_tcp_send, nullptr);
    AddCommand("/probebuffs", probe_buffs, nullptr);
    AddCommand("/probepetbuffs", probe_pet_buffs, nullptr);
    AddCommand("/probespawns", probe_spawns, nullptr);
    AddCommand("/probextargets", probe_xtargets, nullptr);
    AddCommand("/probecursor", probe_cursor, nullptr);
    AddTopLevelObject("Probe", probe_tlo, nullptr);
#ifdef HAILSTONE_PROBE_THROW_INIT
    throw std::runtime_error("InitializePlugin threw");
#endif
}
HAILSTONE_API void ShutdownPlugin(void) {
    RemoveCommand("/probethread");
    RemoveCommand("/probethrow");
    RemoveCommand("/probestate");
    RemoveCommand("/probemessage");
    RemoveCommand("/probetcp");
    RemoveCommand("/probetcpsend");
    RemoveCommand("/probebuffs");
    RemoveCommand("/probepetbuffs");
    RemoveCommand("/probespawns");
    RemoveCommand("/probextargets");
    RemoveCommand("/probecursor");
    RemoveTopLevelObject("Probe");
}
HAILSTONE_API void OnUpdateImGui(void) { throw std::runtime_error("OnUpdateImGui threw"); }
