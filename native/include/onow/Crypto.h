// Encoding, randomness and the credential vault's cipher.
//
// The web client stored tokens in AES-256-GCM encrypted HTTP-only cookies keyed
// by SESSION_SECRET. The native client keeps the same guarantees locally: the
// vault file is AES-256-GCM encrypted under a key derived from a machine-local
// secret plus the user's passphrase (when one is set), so a copied
// `session.json` is useless on another machine.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace onow {

std::string base64_encode(const uint8_t* data, size_t len);
std::string base64_encode(const std::string& data);
bool base64_decode(const std::string& text, std::vector<uint8_t>& out);
std::string base64_decode_string(const std::string& text);

// CSPRNG bytes.
void random_bytes(uint8_t* out, size_t len);
std::string random_hex(size_t bytes);

// Lowercase hex SHA-256 (cache keys, ETag folding).
std::string sha256_hex(const std::string& data);
std::string sha256_hex(const uint8_t* data, size_t len);

// Machine-local secret. On Windows this is DPAPI-protected; elsewhere it is a
// random key file inside the app-data directory (mode 0600).
std::string machine_secret();

// AES-256-GCM. Returns false when the platform has no usable cipher (headless
// builds without OpenSSL still run — they just refuse to persist tokens and say
// so in the UI).
bool aes256_gcm_encrypt(const std::string& key, const std::string& plaintext,
                        const std::string& aad, std::vector<uint8_t>& out);
bool aes256_gcm_decrypt(const std::string& key, const std::vector<uint8_t>& blob,
                        const std::string& aad, std::string& out_plaintext);

// Key derivation for the vault: PBKDF2-HMAC-SHA256(passphrase, salt, iters).
std::string derive_key(const std::string& passphrase, const std::string& salt, int iterations);

} // namespace onow
