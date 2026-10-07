#ifndef SHA1_H
#define SHA1_H

#include <cstdint>
#include <string>

namespace crypto {

/**
 * Computes the SHA-1 digest of `input`.
 *
 * The return value is the RAW 20-byte binary digest — it is *not* a printable
 * hex string. Both call sites feed it directly into a Base64 encoder, which is
 * what the ONVIF WS-Security UsernameToken profile requires:
 *
 *     PasswordDigest = Base64(SHA1(Nonce + Created + Password))
 *
 * Encode to hex if a printable form is ever needed.
 *
 * SHA-1 is used here only because the (legacy) ONVIF specification mandates it
 * for camera digest authentication. It must not be used for password storage or
 * for any new integrity/signature purpose — use SHA-256 or HMAC-SHA-256 there.
 */
std::string sha1(const std::string& input);

} // namespace crypto

#endif // SHA1_H
