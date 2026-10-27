#ifndef CORE_CRYPTO_MD5_H
#define CORE_CRYPTO_MD5_H

#include <string>

namespace crypto {

// Returns raw 16-byte MD5 digest
std::string md5(const std::string& input);

// Returns lowercase hex (32 chars) MD5 digest
std::string md5Hex(const std::string& input);

} // namespace crypto

#endif // CORE_CRYPTO_MD5_H
