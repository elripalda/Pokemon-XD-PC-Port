#include <settings.h>

#include <sdk_port.h>

#include "dol_tables.h"
#include "data/xd_data_mods.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

extern "C" {
uint32_t PortXD_RealItemName(uint8_t* item);
uint32_t itemDataBiosGetWazaMachineNo(uint8_t* item);
uint32_t itemDataBiosGetWazaIDByWazaMachineNo(uint32_t machineNo);
uint8_t* itemDataBiosGetPtr(uint16_t index);
void* wazaDataBiosGetPtr(uint16_t index);
uint32_t wazaDataBiosGetName(void* waza);
uint16_t* GSmsgGetGSchar(uint32_t key);
void* GSmsgOpen(void* bank);
int32_t GSmsgClose(void* bank);
}

namespace {

constexpr uint32_t kTmTextGroup = xd::data::kAddedTextGroup + 1;
constexpr uint32_t kMaxItems = 4096;

struct Entry {
  uint32_t key = 0;
  uint32_t waza = 0;
  uint32_t nameMsg = 0;
};
std::map<const uint8_t*, Entry> sEntries;
void* sBank = nullptr;
uint32_t sGeneration = 0;

bool Enabled() {
  static bool ready = false;
  static engine::settings::BoolSetting setting;
  if (!ready) {
    ready = true;
    setting = engine::settings::Global().Bool("gameplay.tm_move_names");
  }
  return setting.Valid() && setting.Get();
}

void Put16(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  v[at] = static_cast<uint8_t>(x >> 8);
  v[at + 1] = static_cast<uint8_t>(x);
}
void Put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  Put16(v, at, x >> 16);
  Put16(v, at + 2, x & 0xFFFF);
}

bool Compose(uint32_t nameMsg, uint32_t wazaName, std::vector<uint16_t>& out) {
  const uint16_t* a = GSmsgGetGSchar(nameMsg);
  const uint16_t* b = GSmsgGetGSchar(wazaName);
  if (a == nullptr || b == nullptr) return false;
  out.clear();
  for (; *a != 0; a++) {
    if (*a == 0xFFFF) return false;
    out.push_back(*a);
  }
  out.push_back(0x20);
  for (; *b != 0; b++) {
    if (*b == 0xFFFF) return false;
    out.push_back(*b);
  }
  return out.size() > 1;
}

void Rebuild() {
  struct Made {
    const uint8_t* item;
    Entry e;
    std::vector<uint16_t> text;
    bool ok;
  };
  std::vector<Made> made;
  for (uint32_t i = 0; i < kMaxItems; i++) {
    uint8_t* p = itemDataBiosGetPtr(static_cast<uint16_t>(i));
    if (p == nullptr) continue;
    const uint32_t machine = itemDataBiosGetWazaMachineNo(p);
    if (machine == 0xFF) continue;
    Made m;
    m.item = p;
    m.e.nameMsg = PortXD_RealItemName(p);
    m.e.waza = itemDataBiosGetWazaIDByWazaMachineNo(machine);
    m.ok = m.e.nameMsg != 0 &&
           Compose(m.e.nameMsg, wazaDataBiosGetName(wazaDataBiosGetPtr(static_cast<uint16_t>(m.e.waza))), m.text);
    made.push_back(std::move(m));
  }
  size_t count = 0;
  for (const Made& m : made) count += m.ok ? 1 : 0;
  std::vector<uint8_t> bytes(0x10 + count * 8, 0);
  sGeneration++;
  Put16(bytes, 0, kTmTextGroup);
  Put16(bytes, 4, static_cast<uint32_t>(count));
  if (const uint8_t* like = PortXD_GetDolTable("common_tbl")) {
    bytes[3] = like[3];
    bytes[6] = like[6], bytes[7] = like[7];
  }
  std::map<const uint8_t*, Entry> entries;
  size_t n = 0;
  for (Made& m : made) {
    if (m.ok) {
      m.e.key = (kTmTextGroup << 20) | ((sGeneration * 0x400 + static_cast<uint32_t>(n)) & 0xFFFFF);
      const size_t at = bytes.size();
      Put32(bytes, 0x10 + n * 8, m.e.key & 0xFFFFF);
      Put32(bytes, 0x10 + n * 8 + 4, static_cast<uint32_t>(at));
      bytes.resize(at + (m.text.size() + 1) * 2, 0);
      for (size_t c = 0; c < m.text.size(); c++) Put16(bytes, at + c * 2, m.text[c]);
      n++;
    }
    entries[m.item] = m.e;
  }
  auto* bank = static_cast<uint8_t*>(PortMem_LowAlloc(bytes.size() + 0x10, 16));
  std::memcpy(bank, bytes.data(), bytes.size());
  if (sBank != nullptr) GSmsgClose(sBank);
  GSmsgOpen(bank);
  sBank = bank;
  sEntries.swap(entries);
  std::fprintf(stderr, "[tm names] message bank %u: %zu of %zu machine items\n", kTmTextGroup, count, sEntries.size());
}

}

extern "C" uint32_t itemDataBiosGetName(uint8_t* item) {
  const uint32_t real = PortXD_RealItemName(item);
  if (item == nullptr || real == 0 || !Enabled()) return real;
  const uint32_t machine = itemDataBiosGetWazaMachineNo(item);
  if (machine == 0xFF) return real;
  const uint32_t waza = itemDataBiosGetWazaIDByWazaMachineNo(machine);
  for (int attempt = 0; attempt < 2; attempt++) {
    auto it = sEntries.find(item);
    if (it != sEntries.end() && it->second.waza == waza && it->second.nameMsg == real) return it->second.key != 0 ? it->second.key : real;
    if (attempt == 0) Rebuild();
  }
  return real;
}
