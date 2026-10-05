#include "onow/Crypto.h"

#include "onow/Fs.h"
#include "onow/Log.h"
#include "onow/Str.h"
#include "onow/Time.h"

#include <cstring>
#include <fstream>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace onow {

namespace {

const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

} // namespace

std::string base64_encode(const uint8_t* data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= len) {
    const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                            (static_cast<uint32_t>(data[i + 1]) << 8) |
                            static_cast<uint32_t>(data[i + 2]);
    out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
    out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
    out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
    out.push_back(kBase64Alphabet[triple & 0x3F]);
    i += 3;
  }
  const size_t remaining = len - i;
  if (remaining == 1) {
    const uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
    out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
    out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
    out += "==";
  } else if (remaining == 2) {
    const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                            (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
    out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
    out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
    out.push_back('=');
  }
  return out;
}

std::string base64_encode(const std::string& data) {
  return base64_encode(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

bool base64_decode(const std::string& text, std::vector<uint8_t>& out) {
  out.clear();
  std::string compact;
  compact.reserve(text.size());
  for (char c : text) {
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
    compact.push_back(c);
  }
  if (compact.size() % 4 != 0) return false;
  out.reserve((compact.size() / 4) * 3);
  for (size_t i = 0; i < compact.size(); i += 4) {
    int v[4] = {0, 0, 0, 0};
    int padding = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = compact[i + static_cast<size_t>(k)];
      if (c == '=') {
        ++padding;
        v[k] = 0;
      } else {
        if (padding > 0) return false;
        v[k] = base64_value(c);
        if (v[k] < 0) return false;
      }
    }
    const uint32_t triple = (static_cast<uint32_t>(v[0]) << 18) | (static_cast<uint32_t>(v[1]) << 12) |
                            (static_cast<uint32_t>(v[2]) << 6) | static_cast<uint32_t>(v[3]);
    out.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
    if (padding < 2) out.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
    if (padding < 1) out.push_back(static_cast<uint8_t>(triple & 0xFF));
  }
  return true;
}

std::string base64_decode_string(const std::string& text) {
  std::vector<uint8_t> bytes;
  if (!base64_decode(text, bytes)) return std::string();
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void random_bytes(uint8_t* out, size_t len) {
  if (!out || len == 0) return;
#if defined(_WIN32)
  // BCryptGenRandom with the system-preferred RNG.
  using Fn = long (*)(void*, unsigned char*, unsigned long);
  HMODULE bcrypt = ::LoadLibraryA("bcrypt.dll");
  if (bcrypt) {
    Fn gen = reinterpret_cast<Fn>(::GetProcAddress(bcrypt, "BCryptGenRandom"));
    if (gen && gen(nullptr, out, static_cast<unsigned long>(len), 0x00000002 /*BCRYPT_USE_SYSTEM_PREFERRED_RNG*/) == 0) {
      ::FreeLibrary(bcrypt);
      return;
    }
    ::FreeLibrary(bcrypt);
  }
  // Fallback: QueryPerformanceCounter + pid + address entropy. Weak, but only
  // reached on a broken Windows install.
  LARGE_INTEGER counter{};
  ::QueryPerformanceCounter(&counter);
  const uint64_t seed = static_cast<uint64_t>(counter.QuadPart) ^
                        (static_cast<uint64_t>(::GetCurrentProcessId()) << 32) ^
                        reinterpret_cast<uint64_t>(out);
  uint64_t state = seed * 6364136223846793005ULL + 1442695040888963407ULL;
  for (size_t i = 0; i < len; ++i) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    out[i] = static_cast<uint8_t>(state >> 33);
  }
#else
  std::ifstream stream("/dev/urandom", std::ios::binary);
  if (stream) {
    stream.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(len));
    if (stream.gcount() == static_cast<std::streamsize>(len)) return;
  }
  uint64_t state = reinterpret_cast<uint64_t>(out) ^ static_cast<uint64_t>(::getpid()) ^
                    static_cast<uint64_t>(now_ms());
  for (size_t i = 0; i < len; ++i) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    out[i] = static_cast<uint8_t>(state >> 33);
  }
#endif
}

std::string random_hex(size_t bytes) {
  std::vector<uint8_t> buffer(bytes);
  random_bytes(buffer.data(), buffer.size());
  return bytes_to_hex(buffer.data(), buffer.size());
}

// ---------------------------------------------------------------- SHA-256 ---
namespace {

const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void sha256_compress(uint32_t state[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
    const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

} // namespace

std::string sha256_hex(const uint8_t* data, size_t len) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const uint64_t bit_len = static_cast<uint64_t>(len) * 8;
  size_t i = 0;
  for (; i + 64 <= len; i += 64) sha256_compress(state, data + i);
  uint8_t tail[128] = {0};
  const size_t remaining = len - i;
  std::memcpy(tail, data + i, remaining);
  tail[remaining] = 0x80;
  size_t tail_len = remaining + 1;
  if (tail_len > 56) {
    sha256_compress(state, tail);
    std::memset(tail, 0, sizeof(tail));
    tail_len = 0;
  }
  for (int b = 0; b < 8; ++b) tail[56 + b] = static_cast<uint8_t>((bit_len >> (56 - b * 8)) & 0xFF);
  sha256_compress(state, tail);
  uint8_t digest[32];
  for (int w = 0; w < 8; ++w) {
    digest[w * 4] = static_cast<uint8_t>((state[w] >> 24) & 0xFF);
    digest[w * 4 + 1] = static_cast<uint8_t>((state[w] >> 16) & 0xFF);
    digest[w * 4 + 2] = static_cast<uint8_t>((state[w] >> 8) & 0xFF);
    digest[w * 4 + 3] = static_cast<uint8_t>(state[w] & 0xFF);
  }
  return bytes_to_hex(digest, sizeof(digest));
}

std::string sha256_hex(const std::string& data) {
  return sha256_hex(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string machine_secret() {
  const std::string path = fs_join(fs_app_data_dir(), "machine.key");
  std::string existing;
  if (fs_read_text(path, existing) && existing.size() >= 32) {
    return str_trim(existing);
  }
  const std::string created = random_hex(32);
  fs_ensure_dir(fs_app_data_dir());
  if (fs_write_text(path, created)) {
#if !defined(_WIN32)
    ::chmod(path.c_str(), 0600);
#endif
    return created;
  }
  // Read-only app data: fall back to a per-process key (tokens then live only
  // for this session, which the UI reports).
  return created;
}

std::string derive_key(const std::string& passphrase, const std::string& salt, int iterations) {
  // PBKDF2-HMAC-SHA256 over the pure-SHA256 implementation above.
  std::string key;
  key.reserve(32);
  for (uint32_t block = 1; block <= 1 && key.size() < 32; ++block) {
    // U1 = HMAC(passphrase, salt || INT(block))
    std::string message = salt;
    message.push_back(static_cast<char>((block >> 24) & 0xFF));
    message.push_back(static_cast<char>((block >> 16) & 0xFF));
    message.push_back(static_cast<char>((block >> 8) & 0xFF));
    message.push_back(static_cast<char>(block & 0xFF));

    auto hmac = [](const std::string& key_material, const std::string& msg) -> std::vector<uint8_t> {
      const size_t kBlockSize = 64;
      std::vector<uint8_t> k(kBlockSize, 0);
      if (key_material.size() > kBlockSize) {
        const std::string digest = sha256_hex(key_material);
        std::memcpy(k.data(), digest.data(), digest.size());
      } else {
        std::memcpy(k.data(), key_material.data(), key_material.size());
      }
      std::vector<uint8_t> inner(k);
      std::vector<uint8_t> outer(k);
      for (size_t i = 0; i < kBlockSize; ++i) {
        inner[i] ^= 0x36;
        outer[i] ^= 0x5C;
      }
      inner.insert(inner.end(), msg.begin(), msg.end());
      const std::string inner_digest = sha256_hex(inner.data(), inner.size());
      outer.insert(outer.end(), inner_digest.begin(), inner_digest.end());
      const std::string outer_digest = sha256_hex(outer.data(), outer.size());
      std::vector<uint8_t> out(outer_digest.begin(), outer_digest.end());
      // Convert hex back to raw bytes.
      out.clear();
      for (size_t i = 0; i + 1 < outer_digest.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::strtol(outer_digest.substr(i, 2).c_str(), nullptr, 16)));
      }
      return out;
    };

    std::vector<uint8_t> u = hmac(passphrase, message);
    std::vector<uint8_t> t = u;
    for (int iter = 1; iter < iterations; ++iter) {
      u = hmac(passphrase, std::string(reinterpret_cast<const char*>(u.data()), u.size()));
      for (size_t j = 0; j < t.size(); ++j) t[j] ^= u[j];
    }
    for (size_t j = 0; j < t.size() && key.size() < 32; ++j) key.push_back(static_cast<char>(t[j]));
  }
  return key;
}

bool aes256_gcm_encrypt(const std::string& key, const std::string& plaintext,
                        const std::string& aad, std::vector<uint8_t>& out) {
  // GCM needs a real block cipher. Without OpenSSL / BCrypt we cannot honour
  // the "encrypted at rest" contract, and silently storing plaintext would be
  // worse than refusing — so report the failure and let the caller decide.
  (void)key;
  (void)plaintext;
  (void)aad;
  (void)out;
  ONOW_WARN("Crypto", "AES-256-GCM unavailable in this build; refusing to store credentials in the clear");
  return false;
}

bool aes256_gcm_decrypt(const std::string& key, const std::vector<uint8_t>& blob,
                        const std::string& aad, std::string& out_plaintext) {
  (void)key;
  (void)blob;
  (void)aad;
  (void)out_plaintext;
  return false;
}

} // namespace onow
