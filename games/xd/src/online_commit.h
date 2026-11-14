#ifndef XD_ONLINE_COMMIT_H
#define XD_ONLINE_COMMIT_H

#include <string>

namespace xd::online {

constexpr size_t kNonceBytes = 16;

std::string ToHex(const unsigned char* data, size_t size);
bool FromHex(const std::string& hex, std::string& bytes);

std::string Commit(const std::string& nonceHex, const std::string& record);
bool Verify(const std::string& commitHex, const std::string& nonceHex, const std::string& record);

}

#endif
