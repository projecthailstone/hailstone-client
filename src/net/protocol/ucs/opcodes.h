#pragma once
#include <cstdint>
namespace hailstone::net {
namespace ucs {
inline constexpr std::uint16_t kMailLogin = 0x01, kMail = 0x02, kChannelMessage = 0x03, kAnnounceJoin = 0x04,
                               kAnnounceLeave = 0x05, kBuddy = 0x06, kMailHeaderCount = 0x0d, kMailHeader = 0x0e,
                               kMailSendBody = 0x0f, kMailNew = 0x10, kMailDeliveryStatus = 0x12,
                               kMailboxChange = 0x14, kIgnore = 0x15;
} // namespace ucs

} // namespace hailstone::net
