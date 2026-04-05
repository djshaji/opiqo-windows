#pragma once

#ifndef TOTP_HPP
#define TOTP_HPP

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <cstdint>
#include <cstring>
#include <cctype>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#define BASE32_SECRET "JBSWY3DPEHPK3PXP" // "Hello!" in Base32, for testing

namespace totp {

// Decode a Base32-encoded secret (RFC 4648, uppercase, padding optional).
inline std::vector<uint8_t> base32Decode(const char* s) {
    static const char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::vector<uint8_t> out;
    int buf = 0, bits = 0;
    for (; *s; ++s) {
        if (*s == '=') break;
        const char* p = strchr(kAlpha, toupper((unsigned char)*s));
        if (!p) continue;
        buf = (buf << 5) | static_cast<int>(p - kAlpha);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

// Compute one HOTP code for the given key and 8-byte big-endian counter.
inline uint32_t hotp(const uint8_t* key, size_t keyLen,
                     uint64_t counter, int digits = 6) {
    // Pack counter as big-endian 8 bytes.
    uint8_t msg[8];
    for (int i = 7; i >= 0; --i) {
        msg[i] = static_cast<uint8_t>(counter & 0xFF);
        counter >>= 8;
    }

    // HMAC-SHA1 via Windows BCrypt.
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr,
                                BCRYPT_ALG_HANDLE_HMAC_FLAG);

    BCRYPT_HASH_HANDLE hash = nullptr;
    BCryptCreateHash(alg, &hash, nullptr, 0,
                     const_cast<PUCHAR>(key), static_cast<ULONG>(keyLen), 0);
    BCryptHashData(hash, msg, 8, 0);

    uint8_t digest[20];
    BCryptFinishHash(hash, digest, 20, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);

    // RFC 4226 dynamic truncation.
    int offset = digest[19] & 0x0F;
    uint32_t code = ((digest[offset]     & 0x7F) << 24)
                  | ((digest[offset + 1] & 0xFF) << 16)
                  | ((digest[offset + 2] & 0xFF) <<  8)
                  |  (digest[offset + 3] & 0xFF);

    static const uint32_t kPow10[] = {
        1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000
    };
    return code % kPow10[digits];
}

// Compute a TOTP code from a Base32 secret.
// step    — time step in seconds (default 30)
// digits  — code length (default 6)
// skew    — counter offset from current step (0 = now, -1 = previous, +1 = next)
inline uint32_t compute(const char* base32Secret,
                        int step = 30, int digits = 6, int skew = 0) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t unixSec = (((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime)
                       / 10000000ULL - 11644473600ULL;
    uint64_t counter = static_cast<uint64_t>(
        static_cast<int64_t>(unixSec / step) + skew);

    auto key = base32Decode(base32Secret);
    return hotp(key.data(), key.size(), counter, digits);
}

// Verify a user-supplied code against the current step and ±1 for clock skew.
inline bool verify(const char* base32Secret, uint32_t userCode,
                   int step = 30, int digits = 6) {
    for (int skew = -1; skew <= 1; ++skew) {
        if (compute(base32Secret, step, digits, skew) == userCode)
            return true;
    }
    return false;
}

} // namespace totp

bool verifyTotp(uint32_t userCode) {
    return totp::verify(BASE32_SECRET, userCode);
}

#endif // TOTP_HPP