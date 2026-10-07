#include "Sha1.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace crypto {

static uint32_t leftRotate(uint32_t value, uint32_t bits) {
    return (value << bits) | (value >> (32 - bits));
}

std::string sha1(const std::string& input) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    const uint64_t ml = static_cast<uint64_t>(input.size()) * 8ULL;
    size_t paddedLen = ((input.size() + 8) / 64 + 1) * 64;
    std::vector<uint8_t> buffer(paddedLen, 0);
    uint8_t* padded = buffer.data();
    std::memcpy(padded, input.data(), input.size());
    padded[input.size()] = 0x80;
    for (int i = 0; i < 8; ++i)
        padded[paddedLen - 1 - i] = (ml >> (i * 8)) & 0xFF;

    for (size_t chunk = 0; chunk < paddedLen; chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)padded[chunk + i * 4] << 24) |
                ((uint32_t)padded[chunk + i * 4 + 1] << 16) |
                ((uint32_t)padded[chunk + i * 4 + 2] << 8) | ((uint32_t)padded[chunk + i * 4 + 3]);

        for (int i = 16; i < 80; ++i)
            w[i] = leftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = leftRotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = leftRotate(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::string result;
    result.reserve(20);
    auto add = [&](uint32_t v) {
        result += (v >> 24) & 0xFF;
        result += (v >> 16) & 0xFF;
        result += (v >> 8) & 0xFF;
        result += v & 0xFF;
    };
    add(h0);
    add(h1);
    add(h2);
    add(h3);
    add(h4);
    return result;
}

} // namespace crypto
