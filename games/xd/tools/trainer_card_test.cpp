
#include "../src/trainer_card.h"

#include <cstdio>

using namespace xd::card;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main() {
  CHECK(NameChars().size() == 76);
  CHECK(NameCharAllowed(u'A') && NameCharAllowed(0x2642) && NameCharAllowed(0x2030) && !NameCharAllowed(u'@') && !NameCharAllowed(0x00E9));
  CHECK(SanitizeName(u"ABCDEFGHIJKLMN") == u"ABCDEFGHIJ");
  CHECK(SanitizeName(u"a@b") == u"a?b");

  Card c;
  c.title = 17;
  c.name = u"ALEX";
  c.model = {Source::Colosseum, "ken_a1"};
  const std::string line = Encode(c);
  Card back;
  std::string why;
  CHECK(Decode(line, back, &why) && back == c);
  c.name.clear();
  c.model = {Source::Xd, "9"};
  c.title = kTitleDefault;
  CHECK(Decode(Encode(c), back, &why) && back == c);


  CHECK(!Decode("K x 0 1 -", back, &why));
  CHECK(!Decode("K 1 2 1 -", back, &why));
  CHECK(!Decode("K 1 0 ../x -", back, &why));
  CHECK(!Decode("K 1 0 1 zz", back, &why));
  CHECK(!Decode("T 1 0 1 -", back, &why));
  CHECK(Decode("K 1 0 1 00400041", back, &why) && back.name == u"?A");
  std::string longName = "K 1 0 1 ";
  for (int i = 0; i < 20; i++) longName += "0041";
  CHECK(Decode(longName, back, &why) && back.name.size() == kMaxName);

  const std::string path = "trainer_card_test.tmp";
  c.name = u"SAM";
  CHECK(SaveFile(path, c) && LoadFile(path, back) && back == c);
  std::remove(path.c_str());
  std::printf(failures ? "trainer_card_test: %d FAILED\n" : "trainer_card_test: all checks passed\n", failures);
  return failures ? 1 : 0;
}
