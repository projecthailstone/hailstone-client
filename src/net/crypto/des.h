#pragma once

// DES (FIPS 46-3) for the login handshake, which encrypts credentials and the
// login reply with DES-CBC under an all-zero key and IV (EQEmu's
// eqcrypt_block). Obfuscation only, not security.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hailstone::net::des {

class Cipher {
public:
    explicit Cipher(std::uint64_t key);
    std::uint64_t encrypt(std::uint64_t block) const;
    std::uint64_t decrypt(std::uint64_t block) const;

private:
    std::uint64_t crypt(std::uint64_t block, bool decrypt) const;
    std::array<std::uint64_t, 16> subkeys_{};
};

// DES-CBC with a zero key and IV; the input is zero-padded to whole blocks.
std::vector<std::byte> zero_cbc_encrypt(std::span<const std::byte> plain);
// nullopt when the input is not a whole number of blocks.
std::optional<std::vector<std::byte>> zero_cbc_decrypt(std::span<const std::byte> cipher);

} // namespace hailstone::net::des
