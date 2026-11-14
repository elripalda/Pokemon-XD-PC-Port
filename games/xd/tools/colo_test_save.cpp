#include <colo_save.h>
#include <gc_memcard.h>
#include <xd_colosseum.h>
#include <xd_save.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

using namespace engine::colo;

namespace {

void PutBe16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}
void PutBe32(uint8_t* p, uint32_t v) {
  PutBe16(p, static_cast<uint16_t>(v >> 16));
  PutBe16(p + 2, static_cast<uint16_t>(v));
}

std::vector<uint8_t> MakeSlot(int32_t count, const std::u16string& name, const std::vector<PokemonCore>& mons) {
  std::vector<uint8_t> s(kSlotSize, 0);
  s[0] = 1;
  s[1] = 1;
  PutBe32(s.data() + 4, static_cast<uint32_t>(count));
  for (size_t i = 0; i < 8; i++) s[0x10 + i] = static_cast<uint8_t>(0x60 + i);
  for (size_t i = 0; i < 11; i++) PutBe16(s.data() + kHeroOffset + i * 2, i < name.size() ? static_cast<uint16_t>(name[i]) : 0);
  PutBe32(s.data() + kHeroOffset + 0x2C, 0x00CD5678);
  for (size_t i = 0; i < mons.size() && i < 6; i++) RecordFromCore(mons[i], s.data() + kPartyOffset + i * kRecordSize);
  for (size_t i = 6; i < mons.size(); i++) {
    RecordFromCore(mons[i], s.data() + kPcOffset + kBoxStride * 1 + 0x14 + (i - 6) * kRecordSize);
  }
  PutBe16(s.data() + kPcOffset + kBoxStride * 1, u'B');
  for (size_t i = 0x1D000; i < 0x1D200; i++) s[i] = static_cast<uint8_t>(i * 7);
  EncryptSlot(s.data());
  return s;
}

}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <out.gci> <xd save.gci>\n", argv[0]);
    return 2;
  }
  std::vector<uint8_t> bytes;
  engine::memcard::CardFile xdCard;
  std::string error;
  if (!engine::memcard::ReadWholeFile(argv[2], bytes) || !engine::memcard::ReadGci(bytes, xdCard, error)) {
    std::fprintf(stderr, "can't read the XD save: %s\n", error.c_str());
    return 1;
  }
  xd::save::Save xd;
  xd::save::ReadReport rep;
  if (!xd::save::ReadSave(std::vector<uint8_t>(xdCard.data.begin(), xdCard.data.begin() + static_cast<std::ptrdiff_t>(xd::save::kFileSize)), xd, rep, error)) {
    std::fprintf(stderr, "can't read the XD save: %s\n", error.c_str());
    return 1;
  }
  std::vector<PokemonCore> mons;
  for (const auto& p : xd.party) {
    if (!p.IsEmpty()) mons.push_back(xd::colosseum::CoreFromXd(p.raw.data(), xd.order));
  }
  if (mons.size() < 4) {
    std::fprintf(stderr, "the XD save needs 4 or more Pokemon in its party\n");
    return 1;
  }
  for (PokemonCore& c : mons) {
    if (c.shadowId != 0) c.heartGauge = -1;
  }
  mons[1].heldItem = 520;
  mons[1].metLocation = 0x31;
  mons[2].shadowId = 5;
  mons[2].heartGauge = 3000;
  std::vector<uint8_t> file(kFileSize, 0);
  for (size_t i = 0; i < 0xE90; i++) PutBe32(file.data() + i * 4, static_cast<uint32_t>(i * 2654435761u));
  uint32_t sum = 0;
  for (size_t i = 0; i < 0xE90; i++) {
    sum += static_cast<uint32_t>(file[i * 4]) << 24 | static_cast<uint32_t>(file[i * 4 + 1]) << 16 | static_cast<uint32_t>(file[i * 4 + 2]) << 8 | file[i * 4 + 3];
  }
  PutBe32(file.data() + 0x3A40, sum);
  const auto older = MakeSlot(2, u"OLDER", {});
  const auto newer = MakeSlot(3, u"WES", mons);
  std::memcpy(file.data() + SlotOffset(0), newer.data(), kSlotSize);
  std::memcpy(file.data() + SlotOffset(1), older.data(), kSlotSize);

  engine::memcard::CardFile card;
  std::memset(card.entry.raw.data(), 0, engine::memcard::kDirEntrySize);
  std::memcpy(card.entry.raw.data(), "GC6E01", 6);
  card.entry.raw[6] = 0xFF;
  std::memcpy(card.entry.raw.data() + 8, "pokemon_colosseum", 17);
  PutBe32(card.entry.raw.data() + 0x28, 12345);
  PutBe32(card.entry.raw.data() + 0x2C, 0x40);
  PutBe16(card.entry.raw.data() + 0x36, 5);
  PutBe16(card.entry.raw.data() + 0x38, static_cast<uint16_t>(kFileSize / engine::memcard::kBlockSize));
  PutBe16(card.entry.raw.data() + 0x3A, 0xFFFF);
  card.data = file;
  const std::vector<uint8_t> gci = engine::memcard::MakeGci(card);
  std::ofstream out(argv[1], std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(gci.data()), static_cast<std::streamsize>(gci.size()));
  return out ? 0 : 1;
}
