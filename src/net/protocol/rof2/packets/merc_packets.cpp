#include "net/protocol/rof2/packets/merc_packets.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace hailstone::net::rof2 {
namespace {
using game::MercRecord;
using game::MercStance;
using game::kMercMaxStances;
using namespace messages;
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

// The list readers' u32 (0x56bf90 and the handlers): read when it fits, else the value stays.
bool read_u32(std::span<const std::uint8_t> d, std::size_t& pos, std::uint32_t& out) {
    if (pos + 4 > d.size()) return false;
    out = le32(d.data() + pos);
    pos += 4;
    return true;
}
template <typename T> void read_field(std::span<const std::uint8_t> d, std::size_t& pos, T& out) {
    std::uint32_t v = 0;
    if (read_u32(d, pos, v)) out = static_cast<T>(v);
}
void read_u8(std::span<const std::uint8_t> d, std::size_t& pos, std::uint8_t& out) {
    if (pos < d.size()) out = d[pos++];
}

void decode_record(std::span<const std::uint8_t> d, std::size_t& pos, MercRecord& r) {
    read_field(d, pos, r.id);
    read_field(d, pos, r.type);
    read_field(d, pos, r.subtype);
    read_field(d, pos, r.purchase_cost);
    read_field(d, pos, r.upkeep_cost);
    read_field(d, pos, r.required_membership);
    read_field(d, pos, r.alt_purchase_cost);
    read_field(d, pos, r.alt_upkeep_cost);
    read_field(d, pos, r.alt_currency);
    read_u8(d, pos, r.byte_0x20);
    read_field(d, pos, r.time_left_ms);
    read_field(d, pos, r.command);
    read_field(d, pos, r.stance);
    read_field(d, pos, r.stance_count);
    read_field(d, pos, r.active_since);
    read_u8(d, pos, r.assist_mode);
    if (pos < d.size()) {
        // The name: at most 63 bytes kept (+0x78 = 0); the position skips the whole string and its NUL.
        std::size_t end = pos;
        while (end < d.size() && d[end] != 0) ++end;
        std::size_t keep = std::min<std::size_t>(end - pos, 63);
        r.name.assign(reinterpret_cast<const char*>(d.data() + pos), keep);
        pos = end < d.size() ? end + 1 : end;
    }
    // The stance pairs, each read when it fits. Pairs beyond kMercMaxStances (which overrun the original's record)
    // are skipped.
    if (r.stances.size() > r.stance_count) r.stances.resize(r.stance_count);
    for (std::uint32_t k = 0; k < r.stance_count; ++k) {
        if (pos >= d.size()) break; // nothing left to read: every further pair keeps its value
        // Past the stored pairs a pass only moves the position, and with under 4 bytes left none can: stop rather
        // than spin through a wire count of up to 2^32.
        if (k >= kMercMaxStances && pos + 4 > d.size()) break;
        MercStance s = k < r.stances.size() ? r.stances[k] : MercStance{};
        read_field(d, pos, s.index);
        read_field(d, pos, s.stance);
        if (k < kMercMaxStances) {
            if (k < r.stances.size()) r.stances[k] = s;
            else r.stances.push_back(s);
        }
    }
}


// These are RoF2 wire values, intentionally private to the RoF2 packet codec.
enum class MercWire : std::uint16_t {
    DataUpdateRequest=0x7b89, DataUpdate=0x61a4, DataRequest=0x11c1, DataResponse=0x72ce,
    Hire=0x7169, Dismiss=0x6e83, TimerRequest=0x31e4, Timer=0x0763,
    Unknown1=0x5d26, Command=0x27f2, SuspendRequest=0x4407,
    SuspendResponse=0x6f03, UnsuspendResponse=0x27a0,
    Switch=0x1b37, Suspended=0x0e52, Died=0x038f, TimerCleared=0x2749,
    Event7=0x20b9, TimeLeft=0x2b4f, UpkeepCharged=0x5f88, Costs=0x3cbc,
    ZoneIn=0x4820, AssistMode=0x0b72, CallForAssist=0x7e6f,
    SetOwner=0x5df8, TransferAnswer=0x4333, Log=0xee80
};
std::uint32_t value(std::span<const std::uint8_t> d, std::size_t pos) {
    return pos + 4 <= d.size() ? le32(d.data()+pos) : 0;
}
void append32(std::vector<std::byte>& p, std::uint32_t n) {
    for (int j=0;j<4;++j) p.push_back(static_cast<std::byte>((n>>(j*8)) & 0xff));
}
 } // namespace

std::optional<messages::MercenaryNotice> decode_mercenary_notice(const AppPacket& packet) {
    const auto d = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(packet.data.data()), packet.data.size());
    const auto opcode=static_cast<MercWire>(packet.opcode);
    MercenaryNotice n;
    switch(opcode) {
    case MercWire::DataResponse: {
        n.kind=MercenaryNoticeKind::MerchantList;
        std::size_t pos=0; std::uint32_t count=0;
        read_u32(d,pos,count);
        // Each type consumes four bytes. Stop when the buffer ends rather than allocating from a forged count.
        for(std::uint32_t i=0;i<count && pos+4<=d.size();++i) {std::uint32_t v=0;read_u32(d,pos,v);n.types.push_back(v);}
        count=0;read_u32(d,pos,count);
        for(std::uint32_t i=0;i<count && pos<d.size();++i){MercRecord record;decode_record(d,pos,record);n.mercenaries.push_back(std::move(record));}
        break;
    }
    case MercWire::DataUpdate: {
        n.kind=MercenaryNoticeKind::OwnedList;
        std::size_t pos=0;read_field(d,pos,n.current_index);
        std::uint32_t count=0;read_u32(d,pos,count);
        for(std::uint32_t i=0;i<count && pos<d.size();++i){MercRecord record;decode_record(d,pos,record);n.mercenaries.push_back(std::move(record));}
        read_field(d,pos,n.slot_count);
        break;
    }
    case MercWire::Hire: n.kind=MercenaryNoticeKind::HireResult;n.result=value(d,0);break;
    case MercWire::Timer:
        n.kind=MercenaryNoticeKind::Timer;n.flag=static_cast<std::uint8_t>(value(d,0));
        n.timer_interval=value(d,4);n.timer_tag=value(d,8);n.state=value(d,12);n.suspend_time=value(d,16);break;
    case MercWire::SuspendResponse: n.kind=MercenaryNoticeKind::Suspended;n.suspend_time=value(d,0);break;
    case MercWire::UnsuspendResponse:n.kind=MercenaryNoticeKind::Unsuspended;break;
    case MercWire::Suspended:n.kind=MercenaryNoticeKind::SuspensionNotice;break;
    case MercWire::Died:n.kind=MercenaryNoticeKind::Died;n.suspend_time=value(d,0);break;
    case MercWire::TimerCleared:n.kind=MercenaryNoticeKind::TimerCleared;break;
    case MercWire::Event7:n.kind=MercenaryNoticeKind::Event7;break;
    case MercWire::Unknown1:n.kind=MercenaryNoticeKind::Unknown1;n.flag=d.empty()?0:d[0];break;
    case MercWire::TimeLeft:n.kind=MercenaryNoticeKind::TimeLeft;n.time_left=value(d,0);break;
    case MercWire::UpkeepCharged:n.kind=MercenaryNoticeKind::UpkeepCharged;n.flag=d.empty()?0:d[0];break;
    case MercWire::Costs:
        n.kind=MercenaryNoticeKind::Costs;n.purchase_cost=static_cast<std::int32_t>(value(d,0));
        n.alt_purchase_cost=static_cast<std::int32_t>(value(d,4));
        n.upkeep_cost=static_cast<std::int32_t>(value(d,8));
        n.alt_upkeep_cost=static_cast<std::int32_t>(value(d,12));break;
    case MercWire::Command:n.kind=MercenaryNoticeKind::Command;n.result=value(d,0);break;
    case MercWire::Switch:n.kind=MercenaryNoticeKind::SwitchResult;n.seconds=value(d,4);n.result=value(d,8);break;
    case MercWire::Dismiss:n.kind=MercenaryNoticeKind::Dismissed;break;
    default:return std::nullopt;
    }
    return n;
}

std::optional<AppPacket> encode_mercenary_request(const MercenaryRequest& request) {
    std::vector<std::byte> bytes;
    MercWire opcode;
    switch(request.action) {
    case MercenaryAction::RequestTimer:opcode=MercWire::TimerRequest;break;
    case MercenaryAction::ZoneEntered:opcode=MercWire::ZoneIn;append32(bytes,8);break;
    case MercenaryAction::RequestMerchantList:opcode=MercWire::DataRequest;append32(bytes,request.liaison_id);break;
    case MercenaryAction::RequestOwnedList:opcode=MercWire::DataUpdateRequest;break;
    case MercenaryAction::Hire:
        opcode=MercWire::Hire;append32(bytes,request.owner_id);append32(bytes,request.merc_id);
        append32(bytes,request.liaison_id);append32(bytes,0);break;
    case MercenaryAction::Suspend:opcode=MercWire::SuspendRequest;bytes.push_back(std::byte{'0'});break;
    case MercenaryAction::Dismiss:opcode=MercWire::Dismiss;break;
    case MercenaryAction::SetAssist:opcode=MercWire::AssistMode;bytes.push_back(request.enabled?std::byte{1}:std::byte{0});break;
    case MercenaryAction::CallForAssist:opcode=MercWire::CallForAssist;bytes.push_back(std::byte{'0'});break;
    case MercenaryAction::Switch:opcode=MercWire::Switch;append32(bytes,static_cast<std::uint32_t>(request.index));break;
    case MercenaryAction::SetOwner:
        opcode=MercWire::SetOwner;append32(bytes,request.owner_id);append32(bytes,request.target_id);
        bytes.resize(28,std::byte{0});break;
    case MercenaryAction::SetStance:opcode=MercWire::Command;append32(bytes,request.command);append32(bytes,request.stance);break;
    case MercenaryAction::RequestServerLog:opcode=MercWire::Log;break;
    }
    return AppPacket(static_cast<std::uint16_t>(opcode), std::move(bytes));
}
} // namespace hailstone::net::rof2
