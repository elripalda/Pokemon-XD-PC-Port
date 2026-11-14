#include <gen3_save.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace engine::gen3;

namespace {

void Put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void Put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

PlainMon MakeMon(uint16_t species, uint32_t exp, uint16_t item, uint32_t pid, uint32_t ot) {
  PlainMon m;
  m.Set32(0x00, pid);
  m.Set32(0x04, ot);
  for (int i = 0; i < 10; i++) m.b[0x08 + i] = (i < 4) ? static_cast<uint8_t>(0xBB + i) : 0xFF;
  m.b[0x12] = 2;
  m.b[0x13] = 2;
  for (int i = 0; i < 7; i++) m.b[0x14 + i] = (i < 4) ? static_cast<uint8_t>(0xC7 + i) : 0xFF;
  m.Set16(0x20, species);
  m.Set16(0x22, item);
  m.Set32(0x24, exp);
  m.b[0x28] = 0;
  m.b[0x29] = 70;
  const uint16_t moves[4] = {33, 45, 0, 0};
  for (int i = 0; i < 4; i++) {
    m.Set16(0x2C + i * 2, moves[i]);
    m.b[0x34 + i] = moves[i] != 0 ? 20 : 0;
  }
  m.Set32(0x44, 3u << 23);
  m.Set32(0x48, 0x0A5294A5);
  m.b[0x54] = 10;
  m.Set16(0x56, 30);
  m.Set16(0x58, 30);
  return m;
}

}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <out.sav> <species>[:<exp>[:<item>]] ...\n", argv[0]);
    return 2;
  }
  const Layout& l = *LayoutFor(Game::Emerald);
  std::array<std::array<uint8_t, kSectorDataSize>, kSectorsPerSlot> sec{};
  auto sb1 = [&](uint32_t off) { return sec[1 + off / kSectorDataSize].data() + off % kSectorDataSize; };
  const uint8_t name[8] = {0xC7, 0xBF, 0xC8, 0xC8, 0xD3, 0xFF, 0, 0};
  std::memcpy(sec[0].data(), name, 8);
  sec[0][0x08] = 1;
  Put32(sec[0].data() + 0x0A, 0x22220011u);
  Put32(sec[0].data() + 0xAC, 0x9ABCDEF0u);
  const int count = argc - 2 > 6 ? 6 : argc - 2;
  *sb1(l.partyCount) = static_cast<uint8_t>(count);
  for (int i = 0; i < count; i++) {
    unsigned species = 0, exp = 3000, item = 0;
    std::sscanf(argv[2 + i], "%u:%u:%u", &species, &exp, &item);
    Encrypt(MakeMon(static_cast<uint16_t>(species), exp, static_cast<uint16_t>(item), 0x1000u * (i + 1) + 7u * i,
                    0x22220011u),
            sb1(l.party + i * kPartyMonSize), kPartyMonSize);
  }
  std::vector<uint8_t> file(kFlashSize, 0xFF);
  const int first = 3, counter = 7;
  for (int id = 0; id < kSectorsPerSlot; id++) {
    uint8_t* s = file.data() + static_cast<size_t>(kSectorsPerSlot + (id + first) % kSectorsPerSlot) * kSectorSize;
    std::memset(s, 0, kSectorSize);
    std::memcpy(s, sec[id].data(), kSectorDataSize);
    Put16(s + 0xFF4, static_cast<uint16_t>(id));
    Put16(s + 0xFF6, SectionChecksum(s, kSectorDataSize));
    Put32(s + 0xFF8, kSectorSignature);
    Put32(s + 0xFFC, counter);
  }
  std::ofstream out(argv[1], std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  return out ? 0 : 1;
}
