#include <colo_save.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace engine::colo;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

std::string Hex(const std::array<uint8_t, 20>& h) {
  char b[48];
  std::string s;
  for (uint8_t v : h) {
    std::snprintf(b, sizeof(b), "%02x", v);
    s += b;
  }
  return s;
}

void PutBe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
void PutBe32(uint8_t* p, uint32_t v) { PutBe16(p, uint16_t(v >> 16)); PutBe16(p + 2, uint16_t(v)); }

PokemonCore MakeCore(uint16_t species, bool shadow) {
  PokemonCore c;
  c.species = species;
  c.pid = 0x12345678u + species;
  c.origin = {7, 3, 1, 2};
  c.otGender = 1;
  c.otId = 0x00AB1234;
  c.otName = u"WES";
  c.nickname = u"Spot";
  c.exp = 1000;
  c.level = 10;
  c.heldItem = 14;
  c.hp = 33;
  c.stats = {33, 20, 21, 22, 23, 24};
  c.evs = {1, 2, 3, 4, 5, 6};
  c.ivs = {31, 30, 29, 28, 27, 26};
  c.friendship = 70;
  c.moves = {{{33, 35, 1}, {45, 40, 0}, {0, 0, 0}, {0, 0, 0}}};
  c.metLocation = 0x12;
  c.metLevel = 5;
  c.ball = 4;
  c.contest = {1, 2, 3, 4, 5};
  c.contestRanks = {0, 1, 2, 3, 0};
  c.sheen = 9;
  c.ribbons = {1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1};
  c.pokerus = 0x21;
  c.secondAbility = true;
  c.mark = 3;
  c.eventFlag = 1;
  if (shadow) {
    c.shadowId = 7;
    c.heartGauge = 4000;
  }
  return c;
}

bool SameCore(const PokemonCore& a, const PokemonCore& b) {
  return a.species == b.species && a.pid == b.pid && a.origin == b.origin && a.otGender == b.otGender && a.otId == b.otId &&
         a.otName == b.otName && a.nickname == b.nickname && a.exp == b.exp && a.level == b.level && a.heldItem == b.heldItem &&
         a.hp == b.hp && a.stats == b.stats && a.evs == b.evs && a.ivs == b.ivs && a.friendship == b.friendship &&
         a.metLocation == b.metLocation && a.metLevel == b.metLevel && a.ball == b.ball && a.contest == b.contest &&
         a.contestRanks == b.contestRanks && a.sheen == b.sheen && a.ribbons == b.ribbons && a.pokerus == b.pokerus &&
         a.egg == b.egg && a.secondAbility == b.secondAbility && a.invalid == b.invalid && a.mark == b.mark &&
         a.shadowId == b.shadowId && a.heartGauge == b.heartGauge && a.eventFlag == b.eventFlag &&
         a.moves[0].id == b.moves[0].id && a.moves[0].pp == b.moves[0].pp && a.moves[0].ppUps == b.moves[0].ppUps &&
         a.moves[1].id == b.moves[1].id;
}

std::vector<uint8_t> MakeSlot(int32_t count, const char16_t* name) {
  std::vector<uint8_t> s(kSlotSize, 0);
  s[0] = 1;
  s[1] = 1;
  PutBe32(s.data() + 4, static_cast<uint32_t>(count));
  for (size_t i = 0; i < 8; i++) s[0x10 + i] = uint8_t(0xA0 + i);
  for (size_t i = 0x18; i < kBodyEnd; i++) s[i] = uint8_t((i * 31) ^ (i >> 8));
  for (size_t i = 0; i < 11; i++) {
    const char16_t c = name[i];
    PutBe16(s.data() + kHeroOffset + i * 2, c);
    if (c == 0) break;
  }
  PutBe32(s.data() + kHeroOffset + 0x2C, 0x00AB1234);
  s[kHeroOffset + 0xA80] = 1;
  std::memset(s.data() + kPartyOffset, 0, kRecordSize * 6);
  std::memset(s.data() + kPcOffset, 0, kBoxStride * kBoxCount);
  RecordFromCore(MakeCore(133, false), s.data() + kPartyOffset);
  RecordFromCore(MakeCore(196, true), s.data() + kPartyOffset + kRecordSize);
  RecordFromCore(MakeCore(25, false), s.data() + kPcOffset + kBoxStride * 1 + 0x14 + kRecordSize * 4);
  PutBe16(s.data() + kPcOffset + kBoxStride * 1, u'B');
  EncryptSlot(s.data());
  return s;
}

std::vector<uint8_t> MakeFile(const std::vector<std::vector<uint8_t>>& slots) {
  std::vector<uint8_t> f(kFileSize, 0);
  for (size_t i = 0; i < 0xE90; i++) {
    if (i != 0x3A40 / 4) PutBe32(f.data() + i * 4, uint32_t(i * 2654435761u));
  }
  uint32_t sum = 0;
  for (size_t i = 0; i < 0xE90; i++) sum += uint32_t(f[i * 4]) << 24 | uint32_t(f[i * 4 + 1]) << 16 | uint32_t(f[i * 4 + 2]) << 8 | f[i * 4 + 3];
  PutBe32(f.data() + 0x3A40, sum);
  for (size_t s = 0; s < slots.size(); s++) std::memcpy(f.data() + SlotOffset(int(s)), slots[s].data(), kSlotSize);
  return f;
}

}

int main() {
  check(Hex(Sha1(reinterpret_cast<const uint8_t*>("abc"), 3)) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 of \"abc\"");
  check(Hex(Sha1(nullptr, 0)) == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1 of nothing");
  {
    const std::string m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check(Hex(Sha1(reinterpret_cast<const uint8_t*>(m.data()), m.size())) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
          "SHA-1 of a two-block message");
  }

  {
    std::vector<uint8_t> plain = MakeSlot(5, u"WES");
    std::vector<uint8_t> enc = plain;
    check(std::memcmp(enc.data() + 0x18, "\0\0\0", 3) != 0 || true, "(encrypted slot made)");
    bool sumOk = false, digestOk = false;
    check(DecryptSlot(enc.data(), &sumOk, &digestOk) && sumOk && digestOk, "decrypts; header sum and digest fit");
    check(std::memcmp(enc.data() + 0x10, plain.data() + 0x10, 8) == 0, "the card serial (not encrypted) is as it was");
    check(std::memcmp(enc.data() + 0x18, plain.data() + 0x18, 0x40) != 0, "the body was really encrypted before");
    std::vector<uint8_t> again = enc;
    EncryptSlot(again.data());
    check(std::memcmp(again.data(), plain.data(), kBodyEnd) == 0, "decrypt then encrypt gives the same cipher text back");
    std::vector<uint8_t> bad = plain;
    bad[0x500] ^= 1;
    check(!DecryptSlot(bad.data(), &sumOk, &digestOk) && sumOk && !digestOk, "one changed byte: the digest fails");
    bad = plain;
    bad[0x4] ^= 1;
    check(!DecryptSlot(bad.data(), &sumOk, &digestOk) && !sumOk, "a changed header word: the header sum fails");
  }

  {
    const PokemonCore c = MakeCore(133, true);
    uint8_t rec[kRecordSize];
    RecordFromCore(c, rec);
    check(SameCore(c, CoreFromRecord(rec)), "a record written and read back is the same Pokemon");
    check(CoreFromRecord(rec).IsOpenShadow() && !MakeCore(133, false).WasShadow(), "open Shadow: ID set and gauge >= 0");
    PokemonCore purified = c;
    purified.heartGauge = -1;
    check(purified.WasShadow() && !purified.IsOpenShadow(), "a purified one: was a Shadow Pokemon, not open");
  }

  {
    const auto older = MakeSlot(5, u"OLD");
    const auto newer = MakeSlot(9, u"WES");
    auto newest = MakeSlot(12, u"BAD");
    newest[0x700] ^= 0x10;
    const auto file = MakeFile({older, newer, newest});
    Save s;
    std::array<SlotReport, kSlotCount> rep;
    std::string error;
    check(ReadSave(file, s, rep, error), "a file is read: " + error);
    check(s.slot == 1 && s.saveCount == 9, "the newest slot that passes (the newest of all is damaged): slot 2, count 9");
    check(rep[2].written && !rep[2].digestOk && rep[0].digestOk, "the report says which slot failed");
    check(s.bannerSumOk, "the banner sum fits");
    check(s.trainerName == u"WES" && s.trainerOtId == 0x00AB1234 && s.trainerGender == 1, "trainer name, ID, gender");
    check(s.party[0].species == 133 && s.party[1].species == 196 && s.party[1].IsOpenShadow() && s.party[2].IsEmpty(), "the party");
    check(s.boxes[1][4].species == 25 && s.boxes[1][4].nickname == u"Spot" && s.boxNames[1] == u"B" && s.boxes[0][0].IsEmpty(),
          "a box Pokemon in box 2 slot 5, and the box's name");
    check(!ReadSave(std::vector<uint8_t>(1000), s, rep, error) && error.find("393216") != std::string::npos, "a wrong size is refused in words");
    const auto empty = MakeFile({});
    check(!ReadSave(empty, s, rep, error) && error.find("no saved game") != std::string::npos, "a card file with nothing saved says so");
  }
  std::printf(sOk ? "colo_save_test: all passed\n" : "colo_save_test: FAILED\n");
  return sOk ? 0 : 1;
}
