// OpenNOW native client — hashing and Base64 implementation.

#include "opennow/core/Hash.hpp"

#include <cmath>
#include <cstring>

namespace opennow::core {
namespace {

constexpr std::uint32_t RotateLeft(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

}  // namespace

// ---- MD5 ----------------------------------------------------------------------

std::string Md5Digest::Hex() const {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        out.push_back(kDigits[(byte >> 4) & 0xF]);
        out.push_back(kDigits[byte & 0xF]);
    }
    return out;
}

Md5Digest Md5(std::string_view input) {
    // Per-round shift amounts and the sine-table constants from RFC 1321.
    static constexpr int kShifts[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    std::array<std::uint32_t, 64> k{};
    for (std::size_t i = 0; i < 64; ++i) {
        const double radians = static_cast<double>(i + 1);
        // floor(abs(sin(i + 1)) * 2^32)
        const double sine = std::sin(radians);
        const double magnitude = std::fabs(sine) * 4294967296.0;
        k[i] = static_cast<std::uint32_t>(magnitude);
    }

    std::uint32_t a0 = 0x67452301;
    std::uint32_t b0 = 0xefcdab89;
    std::uint32_t c0 = 0x98badcfe;
    std::uint32_t d0 = 0x10325476;

    const std::size_t messageLength = input.size();
    const std::size_t paddedLength = ((messageLength + 8) / 64 + 1) * 64;
    std::vector<std::uint8_t> buffer(paddedLength, 0);
    if (messageLength != 0) {
        std::memcpy(buffer.data(), input.data(), messageLength);
    }
    buffer[messageLength] = 0x80;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(messageLength) * 8ULL;
    for (int i = 0; i < 8; ++i) {
        buffer[paddedLength - 8 + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((bitLength >> (i * 8)) & 0xFF);
    }

    for (std::size_t offset = 0; offset < paddedLength; offset += 64) {
        std::array<std::uint32_t, 16> m{};
        for (std::size_t j = 0; j < 16; ++j) {
            const std::size_t base = offset + j * 4;
            m[j] = static_cast<std::uint32_t>(buffer[base]) |
                   (static_cast<std::uint32_t>(buffer[base + 1]) << 8) |
                   (static_cast<std::uint32_t>(buffer[base + 2]) << 16) |
                   (static_cast<std::uint32_t>(buffer[base + 3]) << 24);
        }

        std::uint32_t a = a0;
        std::uint32_t b = b0;
        std::uint32_t c = c0;
        std::uint32_t d = d0;

        for (int i = 0; i < 64; ++i) {
            std::uint32_t f = 0;
            std::size_t g = 0;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = static_cast<std::size_t>(i);
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * static_cast<std::size_t>(i) + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * static_cast<std::size_t>(i) + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * static_cast<std::size_t>(i)) % 16;
            }
            const std::uint32_t temp = d;
            d = c;
            c = b;
            const std::uint32_t sum = a + f + k[static_cast<std::size_t>(i)] + m[g];
            b = b + RotateLeft(sum, kShifts[i]);
            a = temp;
        }

        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }

    Md5Digest digest;
    const std::uint32_t words[4] = {a0, b0, c0, d0};
    for (std::size_t i = 0; i < 4; ++i) {
        for (int byte = 0; byte < 4; ++byte) {
            digest.bytes[i * 4 + static_cast<std::size_t>(byte)] =
                static_cast<std::uint8_t>((words[i] >> (byte * 8)) & 0xFF);
        }
    }
    return digest;
}

// ---- SHA-256 --------------------------------------------------------------------

std::string Sha256Digest::Hex() const {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        out.push_back(kDigits[(byte >> 4) & 0xF]);
        out.push_back(kDigits[byte & 0xF]);
    }
    return out;
}

std::string Sha256Digest::HexPrefix(std::size_t nibbles) const {
    return Hex().substr(0, nibbles);
}

Sha256Digest Sha256(std::string_view input) {
    static constexpr std::uint32_t kK[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    const std::size_t messageLength = input.size();
    const std::size_t paddedLength = ((messageLength + 9) / 64 + 1) * 64;
    std::vector<std::uint8_t> buffer(paddedLength, 0);
    if (messageLength != 0) {
        std::memcpy(buffer.data(), input.data(), messageLength);
    }
    buffer[messageLength] = 0x80;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(messageLength) * 8ULL;
    for (int i = 0; i < 8; ++i) {
        buffer[paddedLength - 1 - static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((bitLength >> (i * 8)) & 0xFF);
    }

    std::array<std::uint32_t, 64> w{};
    for (std::size_t offset = 0; offset < paddedLength; offset += 64) {
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t base = offset + i * 4;
            w[i] = (static_cast<std::uint32_t>(buffer[base]) << 24) |
                   (static_cast<std::uint32_t>(buffer[base + 1]) << 16) |
                   (static_cast<std::uint32_t>(buffer[base + 2]) << 8) |
                   static_cast<std::uint32_t>(buffer[base + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = (w[i - 15] >> 7 | w[i - 15] << 25) ^
                                     (w[i - 15] >> 18 | w[i - 15] << 14) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = (w[i - 2] >> 17 | w[i - 2] << 15) ^
                                     (w[i - 2] >> 19 | w[i - 2] << 13) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t S1 = (e >> 6 | e << 26) ^ (e >> 11 | e << 21) ^ (e >> 25 | e << 7);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = hh + S1 + ch + kK[i] + w[i];
            const std::uint32_t S0 = (a >> 2 | a << 30) ^ (a >> 13 | a << 19) ^ (a >> 22 | a << 10);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    Sha256Digest digest;
    for (std::size_t i = 0; i < 8; ++i) {
        digest.bytes[i * 4 + 0] = static_cast<std::uint8_t>((h[i] >> 24) & 0xFF);
        digest.bytes[i * 4 + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xFF);
        digest.bytes[i * 4 + 2] = static_cast<std::uint8_t>((h[i] >> 8) & 0xFF);
        digest.bytes[i * 4 + 3] = static_cast<std::uint8_t>(h[i] & 0xFF);
    }
    return digest;
}

// ---- Base64 -----------------------------------------------------------------------

std::string Base64Encode(const std::vector<std::uint8_t>& input) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 2 < input.size(); i += 3) {
        const std::uint32_t triplet = (static_cast<std::uint32_t>(input[i]) << 16) |
                                      (static_cast<std::uint32_t>(input[i + 1]) << 8) |
                                      static_cast<std::uint32_t>(input[i + 2]);
        out.push_back(kAlphabet[(triplet >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triplet >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triplet >> 6) & 0x3F]);
        out.push_back(kAlphabet[triplet & 0x3F]);
    }
    const std::size_t remaining = input.size() - i;
    if (remaining == 1) {
        const std::uint32_t triplet = static_cast<std::uint32_t>(input[i]) << 16;
        out.push_back(kAlphabet[(triplet >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triplet >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t triplet = (static_cast<std::uint32_t>(input[i]) << 16) |
                                      (static_cast<std::uint32_t>(input[i + 1]) << 8);
        out.push_back(kAlphabet[(triplet >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triplet >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triplet >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::string Base64Encode(std::string_view input) {
    // Build the byte view in place; symmetric with Base64Encode above.
    return Base64Encode(std::vector<std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(input.data()),
        reinterpret_cast<const std::uint8_t*>(input.data()) + input.size()));
}

std::vector<std::uint8_t> Base64Decode(std::string_view input) {
    static const auto kReverse = [] {
        std::array<signed char, 256> table{};
        table.fill(-1);
        const char* alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) {
            table[static_cast<unsigned char>(alphabet[i])] = static_cast<signed char>(i);
        }
        // Accept the URL-safe replacements too: NVIDIA mixes both in the wild.
        table[static_cast<unsigned char>('-')] = 62;
        table[static_cast<unsigned char>('_')] = 63;
        return table;
    }();

    std::vector<std::uint8_t> out;
    out.reserve((input.size() / 4) * 3);
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const char c : input) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        const signed char value = kReverse[static_cast<unsigned char>(c)];
        if (value < 0) continue;  // Skip junk rather than failing the whole token.
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xFF));
        }
    }
    return out;
}

std::string Base64UrlEncode(std::string_view input) {
    std::string encoded = Base64Encode(input);
    std::string out;
    out.reserve(encoded.size());
    for (const char c : encoded) {
        if (c == '+') out.push_back('-');
        else if (c == '/') out.push_back('_');
        else if (c == '=') continue;  // No padding in the URL-safe alphabet.
        else out.push_back(c);
    }
    return out;
}

std::vector<std::uint8_t> Base64UrlDecode(std::string_view input) {
    std::string padded(input);
    const std::size_t remainder = padded.size() % 4;
    if (remainder != 0) {
        padded.append(4 - remainder, '=');
    }
    return Base64Decode(padded);
}

std::string Base64UrlDecodeToString(std::string_view input) {
    const std::vector<std::uint8_t> bytes = Base64UrlDecode(input);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string ToHex(std::string_view input) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(input.size() * 2);
    for (const char c : input) {
        const auto byte = static_cast<unsigned char>(c);
        out.push_back(kDigits[(byte >> 4) & 0xF]);
        out.push_back(kDigits[byte & 0xF]);
    }
    return out;
}

}  // namespace opennow::core
