#include "xd_languages.h"
#include "xd_user_folder.h"

#include <sdk_port.h>
#include <settings.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

extern "C" uint8_t* PortXD_GetDolTable(const char* name);
extern "C" void* GSmsgFontOpen(void* font);
extern "C" void fn_8005C068(uint32_t value);

namespace {

struct State {
  bool read = false;
  const xd::languages::Language* active = nullptr;
  xd::languages::Pack pack;
  std::map<std::string, uint8_t*> tables;
  std::string note;
};
State sState;

uint8_t* LoadTable(const std::string& name) {
  const auto it = sState.tables.find(name);
  if (it != sState.tables.end()) return it->second;
  std::ifstream in(xd::languages::TableFile(sState.pack, name), std::ios::binary);
  const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  uint8_t* data = nullptr;
  if (!bytes.empty()) {
    data = static_cast<uint8_t*>(PortMem_LowAlloc(bytes.size() + 0x40, 32));
    if (data != nullptr) std::memcpy(data, bytes.data(), bytes.size());
  }
  sState.tables[name] = data;
  return data;
}

void Read() {
  if (sState.read) return;
  sState.read = true;
  sState.active = xd::languages::Find("us");
  if (const char* off = std::getenv("XD_MODS"); off != nullptr && std::strcmp(off, "0") == 0) {
    sState.note = "XD_MODS=0: no mods, no language pack: English (US)";
    return;
  }
  namespace es = engine::settings;
  const es::Registry& reg = es::Global();
  const size_t at = reg.Find(xd::languages::kSetting);
  if (at == es::kNoSetting) return;
  const std::string wanted = reg.ValueText(at);
  sState.pack = xd::languages::ReadPack(xd::SettingsFolder());
  if (wanted == "us") return;
  const xd::languages::Language* l = xd::languages::Find(wanted);
  if (l == nullptr) return;
  if (!sState.pack.found) {
    sState.note = "language " + wanted + " chosen, but no language pack" +
                  (sState.pack.problem.empty() ? std::string(" (Extra assets page)") : ": " + sState.pack.problem) +
                  ": English (US)";
    return;
  }
  if (!sState.pack.Has(wanted)) {
    sState.note = "language " + wanted + " chosen, but the pack does not have it: English (US)";
    return;
  }
  if (LoadTable(std::string("common_tbl_") + l->id) == nullptr || LoadTable(std::string("system_error_tbl_") + l->id) == nullptr ||
      LoadTable("resident_fnt") == nullptr || LoadTable("common_fnt") == nullptr || LoadTable("_menu_sprite") == nullptr ||
      LoadTable(std::string("table_res_") + l->id) == nullptr) {
    sState.note = "language " + wanted + ": the pack's tables could not be read: English (US)";
    return;
  }
  sState.active = l;
  sState.note = std::string("language ") + l->name + " (number " + std::to_string(l->number) + ")";
}

}

namespace xd::languages {
const Language* Active() {
  Read();
  return sState.active;
}
const Pack& LoadedPack() {
  Read();
  return sState.pack;
}
}

extern "C" const char* PortXD_LanguageText(void) {
  Read();
  static std::string text;
  text = sState.note.empty() ? std::string("language: English (US)") : "language: " + sState.note;
  return text.c_str();
}

extern "C" void* PortXD_LanguageBank(void* bank) {
  const xd::languages::Language* l = xd::languages::Active();
  if (std::strcmp(l->id, "us") == 0) return bank;
  if (bank == PortXD_GetDolTable("common_tbl")) return LoadTable(std::string("common_tbl_") + l->id);
  if (bank == PortXD_GetDolTable("system_error_tbl")) return LoadTable(std::string("system_error_tbl_") + l->id);
  {
    const uint8_t* b = static_cast<const uint8_t*>(bank);
    if (b != nullptr && b[0] == 0 && b[1] == 0 && b[4] == 0x07 && b[5] == 0x8B && std::memcmp(b + 6, "US", 2) == 0) {
      if (uint8_t* own = LoadTable(std::string("table_res_") + l->id)) return own;
      std::fprintf(stderr, "[languages] problem: the pack has no common_rel text bank for %s\n", l->id);
    }
  }
  return bank;
}

extern "C" void* PortXD_FontOpen(void* font) {
  const xd::languages::Language* l = xd::languages::Active();
  static bool first = true;
  if (first) {
    first = false;
    fn_8005C068(static_cast<uint32_t>(l->number));
  }
  if (std::strcmp(l->id, "us") != 0) {
    if (font == PortXD_GetDolTable("resident_fnt")) font = LoadTable("resident_fnt");
    else if (font == PortXD_GetDolTable("common_fnt")) font = LoadTable("common_fnt");
  }
  return GSmsgFontOpen(font);
}

extern "C" int32_t PortXD_GetLanguage(void) { return xd::languages::Active()->number; }
extern "C" const char* PortXD_GetLanguageCode(void) { return xd::languages::Active()->id; }
extern "C" int32_t PortXD_FloorLanguage(int32_t) { return xd::languages::Active()->number; }
extern "C" const char* PortXD_FsysGroupName(uint32_t, const char* name) { return name; }

extern "C" void PortXD_SetDolTableProvider(uint8_t* (*provider)(const char* name));
namespace {
uint8_t* PackTableFor(const char* name) {
  const xd::languages::Language* l = xd::languages::Active();
  if (std::strcmp(l->id, "us") == 0) return nullptr;
  const std::string n = name, suffix = std::string("_") + l->id;
  if (n.size() <= suffix.size() || n.compare(n.size() - suffix.size(), suffix.size(), suffix) != 0) return nullptr;
  const std::string base = n.substr(0, n.size() - suffix.size());
  if (base == "common_tbl" || base == "system_error_tbl") return LoadTable(n);
  if (base == "resident_fnt" || base == "common_fnt") return LoadTable(base);
  return nullptr;
}
struct Register {
  Register() { PortXD_SetDolTableProvider(PackTableFor); }
} sRegister;
}

extern "C" bool PortXD_LanguageMenuTable(void) {
  const xd::languages::Language* l = xd::languages::Active();
  if (std::strcmp(l->id, "us") == 0) return true;
  uint8_t* mem1 = PortMem_MEM1();
  const uint8_t* table = LoadTable("_menu_sprite");
  if (mem1 == nullptr || table == nullptr) return false;
  std::memcpy(mem1 + (0x803001A8u - 0x80000000u), table, 0x11E98);
  static const uint32_t kTable[4] = {0x803C8198u, 0x803123B0u, 0x803C25E0u, 0x803CD508u};
  static const uint32_t kBytes[4] = {0x4F50u, 0xFA28u, 0x32C0u, 0x1228u};
  const std::filesystem::path file = xd::languages::TableFile(xd::languages::LoadedPack(), "menu_layout");
  std::ifstream in(file, std::ios::binary);
  const std::vector<char> rec((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t used = 0;
  for (size_t at = 0; at + 20 <= rec.size(); at += 20) {
    const uint8_t* r = reinterpret_cast<const uint8_t*>(rec.data()) + at;
    const unsigned t = r[0], row = static_cast<unsigned>(r[2]) << 8 | r[3];
    if (t > 3 || row * 28u + 28u > kBytes[t]) continue;
    uint8_t* d = mem1 + (kTable[t] - 0x80000000u) + row * 28u;
    d[1] = r[4];
    std::memcpy(d + 6, r + 6, 8);
    if (r[14] != 0 || r[15] != 0 || r[16] != 0 || r[17] != 0) std::memcpy(d + 0x10, r + 14, 4);
    std::memcpy(d + 0x18, r + 18, 2);
    used++;
  }
  std::fprintf(stderr, "[languages] menu layout: %zu rows of the European game's used\n", used);
  std::ifstream pin(xd::languages::TableFile(xd::languages::LoadedPack(), "menu_images"), std::ios::binary);
  const std::vector<char> pics((std::istreambuf_iterator<char>(pin)), std::istreambuf_iterator<char>());
  size_t at = 0, changed = 0;
  for (int t = 0; t < 4 && at + 4 <= pics.size(); t++) {
    for (uint32_t row = 0; row * 28u + 28u <= kBytes[t] && at + 4 <= pics.size(); row++, at += 4) {
      uint8_t* d = mem1 + (kTable[t] - 0x80000000u) + row * 28u;
      if (std::memcmp(d + 4, pics.data() + at, 2) != 0 || std::memcmp(d + 0x0E, pics.data() + at + 2, 2) != 0) changed++;
      std::memcpy(d + 4, pics.data() + at, 2);
      std::memcpy(d + 0x0E, pics.data() + at + 2, 2);
    }
  }
  std::fprintf(stderr, "[languages] menu pictures: %zu rows renumbered as in the European game\n", changed);
  return true;
}

extern "C" void PortXD_FloorSetLanguage(uint32_t) {
  fn_8005C068(static_cast<uint32_t>(xd::languages::Active()->number));
}
