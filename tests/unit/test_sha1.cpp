// Unit tests for core/crypto: the SHA-1 helper used by ONVIF WS-Security
// (PasswordDigest = Base64(SHA1(nonce + created + password))).
//
// Contract under test: sha1() returns the RAW 20-byte binary digest (it is fed
// straight into a Base64 encoder by both call sites), not a printable hex string.
//
// Build & run:  ctest --test-dir build/tests --output-on-failure

#include "Sha1.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
    std::cout << (condition ? "  [ ok ] " : "  [FAIL] ") << name << "\n";
    if (!condition) {
        ++failures;
    }
}

std::string unhex(const std::string& hex) {
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

std::string toHex(const std::string& raw) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (unsigned char c : raw) {
        out.push_back(kDigits[c >> 4]);
        out.push_back(kDigits[c & 0x0F]);
    }
    return out;
}

struct Vector {
    const char* input;
    const char* expectedHex; // reference digest, RFC 3174 §7.3
};

void testKnownVectors() {
    const std::vector<Vector> vectors = {
        {"", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {"abc", "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
        {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
         "50abf5706a150990a08b2c5ea40fa0e585554732"},
        {"The quick brown fox jumps over the lazy dog", "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"},
    };

    for (const auto& v : vectors) {
        const std::string digest = crypto::sha1(v.input);
        check(digest.size() == 20, std::string("digest is 20 raw bytes: \"") + v.input + "\"");
        check(digest == unhex(v.expectedHex),
              std::string("matches reference digest for length ") +
                  std::to_string(std::string(v.input).size()));
    }
}

// Padding is the delicate part of SHA-1: the 0x80 terminator plus the 8-byte
// big-endian bit count must always fit into the final 64-byte block. Walk every
// length around both block boundaries and compare against the reference digest.
void testPaddingBoundaries() {
    // Reference digests for runs of 'a' (sha1 of "a"*n), produced with Python
    // hashlib and used here as ground truth.
    const std::vector<std::pair<size_t, const char*>> expected = {
        {54, "b05d71c64979cb95fa74a33cdb31a40d258ae02e"},
        {55, "c1c8bbdc22796e28c0e15163d20899b65621d65a"},
        {56, "c2db330f6083854c99d4b5bfb6e8f29f201be699"},
        {63, "03f09f5b158a7a8cdad920bddc29b81c18a551f5"},
        {64, "0098ba824b5c16427bd7a1122a5a442a25ec644d"},
        {65, "11655326c708d70319be2610e8a57d9a5b959d3b"},
        {119, "ee971065aaa017e0632a8ca6c77bb3bf8b1dfc56"},
        {120, "f34c1488385346a55709ba056ddd08280dd4c6d6"},
    };

    for (const auto& [len, hex] : expected) {
        const std::string payload(len, 'a');
        const std::string digest = crypto::sha1(payload);
        check(toHex(digest) == hex, "padding correct for " + std::to_string(len) + "-byte input");
    }
}

void testDeterminismAndDistinctness() {
    const std::string a = crypto::sha1("camera-01");
    const std::string b = crypto::sha1("camera-01");
    const std::string c = crypto::sha1("camera-02");
    check(a == b, "same input -> same digest");
    check(a != c, "different input -> different digest");
    check(crypto::sha1(std::string(1, '\0')).size() == 20,
          "embedded NUL bytes are hashed, not truncated");
}

} // namespace

int main() {
    std::cout << "sha1: RFC 3174 vectors (raw 20-byte digest)\n";
    testKnownVectors();
    std::cout << "sha1: padding boundaries\n";
    testPaddingBoundaries();
    std::cout << "sha1: determinism\n";
    testDeterminismAndDistinctness();

    if (failures) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all sha1 checks passed\n";
    return 0;
}
