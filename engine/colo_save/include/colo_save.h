#ifndef ENGINE_COLO_SAVE_H
#define ENGINE_COLO_SAVE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::colo {

constexpr size_t kFileSize = 0x60000;
constexpr size_t kBannerSize = 0x6000;
constexpr size_t kSlotSize = 0x1E000;
constexpr int kSlotCount = 3;
constexpr size_t kRecordSize = 0x138;
constexpr int kPartySize = 6;
constexpr int kBoxCount = 3;
constexpr int kBoxSlots = 30;
constexpr size_t kBoxStride = 0x14 + kBoxSlots * kRecordSize;
constexpr size_t kHeroOffset = 0x78, kPartyOffset = 0xA8, kPcOffset = 0xB90;
constexpr size_t kBodyEnd = 0x1DFD8, kDigestOffset = 0x1DFEC;

constexpr size_t SlotOffset(int slot) { return kBannerSize + static_cast<size_t>(slot) * kSlotSize; }

std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t length);

bool DecryptSlot(uint8_t* slot, bool* headerSumOk = nullptr, bool* digestOk = nullptr);
void EncryptSlot(uint8_t* slot);

struct Move {
  uint16_t id = 0;
  uint8_t pp = 0;
  uint8_t ppUps = 0;
};
struct PokemonCore {
  uint16_t species = 0;
  uint32_t pid = 0;
  uint32_t otId = 0;
  std::array<uint8_t, 4> origin{};
  uint8_t otGender = 0;
  std::u16string otName, nickname;
  uint32_t exp = 0;
  uint8_t level = 0;
  uint16_t heldItem = 0;
  uint16_t hp = 0;
  std::array<uint16_t, 6> stats{}, evs{};
  std::array<uint8_t, 6> ivs{};
  uint16_t friendship = 0;
  std::array<Move, 4> moves{};
  uint16_t metLocation = 0;
  uint8_t metLevel = 0, ball = 0;
  std::array<uint8_t, 5> contest{};
  std::array<uint8_t, 5> contestRanks{};
  uint8_t sheen = 0;
  std::array<uint8_t, 12> ribbons{};
  uint8_t pokerus = 0;
  bool egg = false, secondAbility = false, invalid = false;
  uint8_t mark = 0;
  uint16_t shadowId = 0;
  int32_t heartGauge = -1;
  uint32_t eventFlag = 0;

  bool IsEmpty() const { return species == 0; }
  bool WasShadow() const { return shadowId != 0; }
  bool IsOpenShadow() const { return shadowId != 0 && heartGauge >= 0; }
};

PokemonCore CoreFromRecord(const uint8_t* record);
void RecordFromCore(const PokemonCore& core, uint8_t* record);

struct SlotReport {
  bool written = false;
  int32_t saveCount = 0;
  bool headerSumOk = false, digestOk = false;
};

struct Save {
  int slot = -1;
  int32_t saveCount = 0;
  std::vector<uint8_t> slotBytes;
  bool bannerSumOk = false;
  std::u16string trainerName;
  uint32_t trainerOtId = 0;
  uint8_t trainerGender = 0;
  std::array<PokemonCore, kPartySize> party{};
  std::array<std::array<PokemonCore, kBoxSlots>, kBoxCount> boxes{};
  std::array<std::u16string, kBoxCount> boxNames{};
};

bool ReadSave(const std::vector<uint8_t>& file, Save& out, std::array<SlotReport, kSlotCount>& report, std::string& error);

}

#endif
