// OpenNOW native client — hashing and Base64 primitives.
//
// Pure C++20 implementations so the same code compiles on the MSVC/Windows
// target and on any Linux CI box that runs the Core unit tests. No BCrypt,
// no OpenSSL, no CNG dependency in the portable layer.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace opennow::core {

// ---- MD5 ------------------------------------------------------------------
// Only used to build Gravatar URLs for accounts without a picture claim, which
// is exactly what the web client does. Never used for secrets.

struct Md5Digest {
    std::array<std::uint8_t, 16> bytes{};
    std::string Hex() const;
};

Md5Digest Md5(std::string_view input);

// ---- SHA-256 ---------------------------------------------------------------
// Used for the stable device hash and for the network-test session cache key
// (mirrors the web server's sha256 token hash).

struct Sha256Digest {
    std::array<std::uint8_t, 32> bytes{};
    std::string Hex() const;
    std::string HexPrefix(std::size_t nibbles) const;
};

Sha256Digest Sha256(std::string_view input);

// ---- Base64 / Base64url ------------------------------------------------------

std::string Base64Encode(std::string_view input);
std::string Base64Encode(const std::vector<std::uint8_t>& input);
std::vector<std::uint8_t> Base64Decode(std::string_view input);

// RFC 4648 §5, no padding. JWT segments and the device-id payload use this.
std::string Base64UrlEncode(std::string_view input);
std::string Base64UrlDecodeToString(std::string_view input);
std::vector<std::uint8_t> Base64UrlDecode(std::string_view input);

// ---- Hex ---------------------------------------------------------------------

std::string ToHex(std::string_view input);

}  // namespace opennow::core
