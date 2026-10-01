#include "Uuid.h"

#include <array>
#include <cstdio>
#include <mutex>
#include <random>

namespace sr {

std::string newUuid() {
    // std::random_device is backed by the OS CSPRNG on MSVC.
    static std::mutex mu;
    static std::random_device rd;
    std::array<unsigned char, 16> b{};
    {
        std::lock_guard lock(mu);
        for (std::size_t i = 0; i < b.size(); i += 4) {
            unsigned int v = rd();
            b[i] = static_cast<unsigned char>(v);
            b[i + 1] = static_cast<unsigned char>(v >> 8);
            b[i + 2] = static_cast<unsigned char>(v >> 16);
            b[i + 3] = static_cast<unsigned char>(v >> 24);
        }
    }
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);
    char out[37];
    std::snprintf(out, sizeof out, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11],
                  b[12], b[13], b[14], b[15]);
    return out;
}

bool looksLikeUuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

}  // namespace sr
