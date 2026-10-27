#ifndef SHA1_H
#define SHA1_H

#include <string>
#include <cstdint>

namespace crypto {

std::string sha1(const std::string& input);

} // namespace crypto

#endif // SHA1_H
