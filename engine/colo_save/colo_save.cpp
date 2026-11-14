#include <colo_save.h>

#include <algorithm>
#include <cstring>

namespace engine::colo {

namespace {

uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t Be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}
void PutBe16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}
void PutBe32(uint8_t* p, uint32_t v) {
  PutBe16(p, static_cast<uint16_t>(v >> 16));
  PutBe16(p + 2, static_cast<uint16_t>(v));
}

std::u16string ReadText(const uint8_t* p, size_t chars) {
  std::u16string s;
  for (size_t i = 0; i < chars; i++) {
    const uint16_t c = Be16(p + i * 2);
    if (c == 0) break;
    s.push_back(static_cast<char16_t>(c));
  }
  return s;
}
void WriteText(uint8_t* p, size_t chars, const std::u16string& s) {
  for (size_t i = 0; i < chars; i++) PutBe16(p + i * 2, i < s.size() && i + 1 < chars ? static_cast<uint16_t>(s[i]) : 0);
}

std::array<uint8_t, 20> NotOf(const std::array<uint8_t, 20>& h) {
  std::array<uint8_t, 20> k{};
  for (size_t i = 0; i < 20; i++) k[i] = static_cast<uint8_t>(~h[i]);
  return k;
}

std::array<uint8_t, 20> SlotDigest(const uint8_t* slot) {
  std::vector<uint8_t> copy(slot, slot + kBodyEnd);
  PutBe32(copy.data() + 0x0C, 0);
  return Sha1(copy.data(), copy.size());
}

uint32_t HeaderSum(const uint8_t* slot) {
  uint32_t s = 0;
  for (int i = 0; i < 8; i++) s += Be32(slot + i * 4);
  return s;
}

}

std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t length) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::vector<uint8_t> msg(data, data + length);
  msg.push_back(0x80);
  while (msg.size() % 64 != 56) msg.push_back(0);
  const uint64_t bits = static_cast<uint64_t>(length) * 8;
  for (int i = 7; i >= 0; i--) msg.push_back(static_cast<uint8_t>(bits >> (i * 8)));
  for (size_t off = 0; off < msg.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = Be32(msg.data() + off + static_cast<size_t>(i) * 4);
    for (int i = 16; i < 80; i++) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
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
      const uint32_t t = Rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = Rol(b, 30);
      b = a;
      a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  std::array<uint8_t, 20> out{};
  for (int i = 0; i < 5; i++) PutBe32(out.data() + i * 4, h[i]);
  return out;
}

bool DecryptSlot(uint8_t* slot, bool* headerSumOk, bool* digestOk) {
  const bool sumOk = HeaderSum(slot) == 0;
  if (headerSumOk != nullptr) *headerSumOk = sumOk;
  if (digestOk != nullptr) *digestOk = false;
  if (!sumOk) return false;
  std::vector<uint8_t> work(slot, slot + kSlotSize);
  std::array<uint8_t, 20> stored{};
  std::memcpy(stored.data(), work.data() + kDigestOffset, 20);
  std::array<uint8_t, 20> key = NotOf(stored);
  for (size_t off = 0x18; off < kBodyEnd; off += 20) {
    const std::array<uint8_t, 20> next = Sha1(work.data() + off, 20);
    for (size_t i = 0; i < 20; i++) work[off + i] ^= key[i];
    key = next;
  }
  const bool ok = SlotDigest(work.data()) == stored;
  if (digestOk != nullptr) *digestOk = ok;
  if (!ok) return false;
  std::memcpy(slot, work.data(), kSlotSize);
  return true;
}

void EncryptSlot(uint8_t* slot) {
  const std::array<uint8_t, 20> h = SlotDigest(slot);
  PutBe32(slot + 0x0C, 0);
  std::memcpy(slot + kDigestOffset, h.data(), 20);
  std::array<uint8_t, 20> key = NotOf(h);
  for (size_t off = 0x18; off < kBodyEnd; off += 20) {
    for (size_t i = 0; i < 20; i++) slot[off + i] ^= key[i];
    key = Sha1(slot + off, 20);
  }
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    if (i != 3) sum += Be32(slot + i * 4);
  }
  PutBe32(slot + 0x0C, 0u - sum);
}

PokemonCore CoreFromRecord(const uint8_t* r) {
  PokemonCore c;
  c.species = Be16(r + 0x00);
  c.pid = Be32(r + 0x04);
  std::memcpy(c.origin.data(), r + 0x08, 4);
  c.metLocation = Be16(r + 0x0C);
  c.metLevel = r[0x0E];
  c.ball = r[0x0F];
  c.otGender = r[0x10];
  c.otId = Be32(r + 0x14);
  c.otName = ReadText(r + 0x18, 11);
  c.nickname = ReadText(r + 0x2E, 11);
  c.exp = Be32(r + 0x5C);
  c.level = r[0x60];
  for (int i = 0; i < 4; i++) {
    const uint8_t* m = r + 0x78 + i * 4;
    c.moves[static_cast<size_t>(i)] = {Be16(m), m[2], m[3]};
  }
  c.heldItem = Be16(r + 0x88);
  c.hp = Be16(r + 0x8A);
  for (int i = 0; i < 6; i++) {
    c.stats[static_cast<size_t>(i)] = Be16(r + 0x8C + i * 2);
    c.evs[static_cast<size_t>(i)] = Be16(r + 0x98 + i * 2);
    c.ivs[static_cast<size_t>(i)] = static_cast<uint8_t>(std::min<uint16_t>(Be16(r + 0xA4 + i * 2), 255));
  }
  c.friendship = Be16(r + 0xB0);
  for (int i = 0; i < 5; i++) {
    c.contest[static_cast<size_t>(i)] = r[0xB2 + i];
    c.contestRanks[static_cast<size_t>(i)] = r[0xB7 + i];
  }
  c.sheen = r[0xBC];
  for (int i = 0; i < 12; i++) c.ribbons[static_cast<size_t>(i)] = r[0xBD + i] != 0 ? 1 : 0;
  c.pokerus = r[0xCA];
  c.egg = r[0xCB] != 0;
  c.secondAbility = r[0xCC] != 0;
  c.invalid = r[0xCD] != 0;
  c.mark = r[0xCF];
  c.shadowId = Be16(r + 0xD8);
  c.heartGauge = static_cast<int32_t>(Be32(r + 0xDC));
  c.eventFlag = Be32(r + 0xF8);
  return c;
}

void RecordFromCore(const PokemonCore& c, uint8_t* r) {
  std::memset(r, 0, kRecordSize);
  PutBe16(r + 0x00, c.species);
  PutBe32(r + 0x04, c.pid);
  std::memcpy(r + 0x08, c.origin.data(), 4);
  PutBe16(r + 0x0C, c.metLocation);
  r[0x0E] = c.metLevel;
  r[0x0F] = c.ball;
  r[0x10] = c.otGender;
  PutBe32(r + 0x14, c.otId);
  WriteText(r + 0x18, 11, c.otName);
  WriteText(r + 0x2E, 11, c.nickname);
  WriteText(r + 0x44, 12, c.nickname);
  PutBe32(r + 0x5C, c.exp);
  r[0x60] = c.level;
  for (int i = 0; i < 4; i++) {
    uint8_t* m = r + 0x78 + i * 4;
    const Move& mv = c.moves[static_cast<size_t>(i)];
    PutBe16(m, mv.id);
    m[2] = mv.pp;
    m[3] = mv.ppUps;
  }
  PutBe16(r + 0x88, c.heldItem);
  PutBe16(r + 0x8A, c.hp);
  for (int i = 0; i < 6; i++) {
    PutBe16(r + 0x8C + i * 2, c.stats[static_cast<size_t>(i)]);
    PutBe16(r + 0x98 + i * 2, c.evs[static_cast<size_t>(i)]);
    PutBe16(r + 0xA4 + i * 2, c.ivs[static_cast<size_t>(i)]);
  }
  PutBe16(r + 0xB0, c.friendship);
  for (int i = 0; i < 5; i++) {
    r[0xB2 + i] = c.contest[static_cast<size_t>(i)];
    r[0xB7 + i] = c.contestRanks[static_cast<size_t>(i)];
  }
  r[0xBC] = c.sheen;
  for (int i = 0; i < 12; i++) r[0xBD + i] = c.ribbons[static_cast<size_t>(i)];
  r[0xCA] = c.pokerus;
  r[0xCB] = c.egg ? 1 : 0;
  r[0xCC] = c.secondAbility ? 1 : 0;
  r[0xCD] = c.invalid ? 1 : 0;
  r[0xCF] = c.mark;
  PutBe16(r + 0xD8, c.shadowId);
  PutBe32(r + 0xDC, static_cast<uint32_t>(c.heartGauge));
  PutBe32(r + 0xF8, c.eventFlag);
}

bool ReadSave(const std::vector<uint8_t>& file, Save& out, std::array<SlotReport, kSlotCount>& report, std::string& error) {
  out = Save{};
  report = {};
  if (file.size() != kFileSize) {
    error = "A Pokemon Colosseum save file is 393216 bytes (0x60000); this one has " + std::to_string(file.size()) + ".";
    return false;
  }
  {
    uint32_t sum = 0;
    for (size_t i = 0; i < 0xE90; i++) sum += Be32(file.data() + i * 4);
    out.bannerSumOk = sum == Be32(file.data() + 0x3A40);
  }
  int best = -1;
  std::vector<uint8_t> bestBytes;
  for (int s = 0; s < kSlotCount; s++) {
    std::vector<uint8_t> slot(file.begin() + static_cast<std::ptrdiff_t>(SlotOffset(s)),
                              file.begin() + static_cast<std::ptrdiff_t>(SlotOffset(s) + kSlotSize));
    SlotReport& r = report[static_cast<size_t>(s)];
    r.written = slot[0] == 1 && slot[1] == 1;
    r.saveCount = static_cast<int32_t>(Be32(slot.data() + 4));
    if (!r.written) continue;
    if (DecryptSlot(slot.data(), &r.headerSumOk, &r.digestOk) && (best < 0 || r.saveCount > report[static_cast<size_t>(best)].saveCount)) {
      best = s;
      bestBytes = std::move(slot);
    }
  }
  if (best < 0) {
    bool any = false;
    for (const SlotReport& r : report) any |= r.written;
    error = any ? "None of this save's slots passes its checks (a damaged file, or not a Pokemon Colosseum save)."
                : "This memory card file has no saved game in it yet.";
    return false;
  }
  out.slot = best;
  out.saveCount = report[static_cast<size_t>(best)].saveCount;
  out.slotBytes = std::move(bestBytes);
  const uint8_t* s = out.slotBytes.data();
  out.trainerName = ReadText(s + kHeroOffset, 11);
  out.trainerOtId = Be32(s + kHeroOffset + 0x2C);
  out.trainerGender = s[kHeroOffset + 0xA80];
  for (int i = 0; i < kPartySize; i++) out.party[static_cast<size_t>(i)] = CoreFromRecord(s + kPartyOffset + static_cast<size_t>(i) * kRecordSize);
  for (int b = 0; b < kBoxCount; b++) {
    const uint8_t* box = s + kPcOffset + static_cast<size_t>(b) * kBoxStride;
    out.boxNames[static_cast<size_t>(b)] = ReadText(box, 10);
    for (int i = 0; i < kBoxSlots; i++) {
      out.boxes[static_cast<size_t>(b)][static_cast<size_t>(i)] = CoreFromRecord(box + 0x14 + static_cast<size_t>(i) * kRecordSize);
    }
  }
  return true;
}

}
