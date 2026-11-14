
#include "../src/online_commit.h"

#include <cstdio>

using namespace xd::online;

static int sFailed = 0, sChecks = 0;
#define CHECK(c)                                          \
  do {                                                    \
    sChecks++;                                            \
    if (!(c)) {                                           \
      sFailed++;                                          \
      std::printf("FAILED line %d: %s\n", __LINE__, #c);  \
    }                                                     \
  } while (0)

int main() {
  const std::string nonce = "000102030405060708090a0b0c0d0e0f";
  const std::string nonce2 = "ffeeddccbbaa99887766554433221100";
  const std::string choice = "A 3 1 2 0 19 337 15 0 0 1 19 89 15 0 0";

  const std::string c = Commit(nonce, choice);
  CHECK(c.size() == 64);
  CHECK(c == Commit(nonce, choice));
  CHECK(c != Commit(nonce2, choice));
  CHECK(c != Commit(nonce, choice + " "));
  CHECK(Verify(c, nonce, choice));
  CHECK(!Verify(c, nonce, "A 3 1 2 0 19 337 15 0 0 1 19 90 15 0 0"));
  CHECK(!Verify(c, nonce2, choice));
  CHECK(!Verify(c.substr(0, 63) + "0", nonce, choice));
  CHECK(!Verify("", nonce, choice));
  CHECK(Commit("00", choice).empty());
  CHECK(Commit("zz0102030405060708090a0b0c0d0e0f", choice).empty());


  CHECK(Commit("00000000000000000000000000000001", choice) != Commit("00000000000000000000000000000002", choice));

  std::string bytes;
  CHECK(FromHex("0aff", bytes) && bytes.size() == 2 && static_cast<unsigned char>(bytes[1]) == 255);
  CHECK(!FromHex("0af", bytes));
  CHECK(!FromHex("0g", bytes));
  CHECK(ToHex(reinterpret_cast<const unsigned char*>("\x01\xab"), 2) == "01ab");

  std::printf("online_commit_test: %d checks, %d failed\n", sChecks, sFailed);
  return sFailed ? 1 : 0;
}
