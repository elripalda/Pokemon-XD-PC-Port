#include "trainer_card_game.h"

#include "data/common_rel.h"
#include "online_battle.h"
#include "xd_user_folder.h"

#include <i18n.h>

#include <sdk_port.h>
#include <xd_save_names.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>

extern "C" {
uint8_t fightTargetIsHostSide(void* trainer, uint16_t type);
uint16_t fightFloor_GetFightType(void* floor);
uint16_t* GSmsgGetGSchar(uint32_t id);
void heroBiosSetNamePtr(void* hero, uint16_t* name);
void* fightTrainer_GetHeroPtr(void* trainer);
uintptr_t Sequence_LoadTrainer(uint32_t row);
uintptr_t fightTrainer_GetSequencePtr(void* trainer);
void fightTrainer_SetSequencePtr(void* trainer, uint32_t seq);
void Sequence_SetVisibility(uintptr_t seq, int visible);
void Sequence_Free(uintptr_t seq);
void fightTrainerCreate(void* trainer, void* src, uint16_t id, uint32_t controllerId, uint32_t seq);
uint16_t* PortXD_RealPrefixNamePtr(void* trainer);
int ModelSequence_Load(uint8_t* data, uint32_t id, uint32_t variant);
void* fightTrainerKindDataBiosGetPtr(uint16_t id);
uint32_t fightTrainerKindDataBiosGetPrefixName(void* data);
uint16_t* heroBiosGetNamePtr(void* hero);
extern uint32_t* fight_trainer_kind_data_number;
extern void* pokemon_data;
extern uint32_t* pokemon_data_number;
extern void** table_res;
extern uint32_t* table_res_number;
struct PortXD_PkxRow { uint32_t a, b; };
extern PortXD_PkxRow trainer_pkx_data[];
extern uint32_t trainer_pkx_data_number;
}

uint8_t* wazaSequenceSysLoadResource(uint32_t group, uint32_t id);

namespace xd::card {
namespace {

struct Room {
  bool active = false;
  bool host = false;
  Card mine, other;
} sRoom;

namespace fs = std::filesystem;

void Log(const char* fmt, const char* a = "", const char* b = "") {
  std::fprintf(stderr, "[trainer card] ");
  std::fprintf(stderr, fmt, a, b);
  std::fprintf(stderr, "\n");
}

constexpr uint32_t kRowEvent = 0x40, kRowSnatch = 0x42, kRowGroudon = 0x2A;

bool XdRowOk(uint32_t row) {
  return row >= 1 && row < trainer_pkx_data_number && row != kRowEvent && row != kRowSnatch && row != kRowGroudon && trainer_pkx_data[row].a != 0 &&
         trainer_pkx_data[row].b != 0;
}

bool ReadFile(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return !out.empty();
}

std::map<std::string, uint8_t*> sBlobs;
uint8_t* ColoBlob(const std::string& id) {
  auto it = sBlobs.find(id);
  if (it != sBlobs.end()) return it->second;
  uint8_t* blob = nullptr;
  std::vector<uint8_t> bytes;
  if (ReadFile(ColosseumPack() / "models" / (id + ".pkx"), bytes)) {
    blob = static_cast<uint8_t*>(PortMem_LowAlloc(bytes.size() + 64, 32));
    if (blob) {
      std::memcpy(blob, bytes.data(), bytes.size());
      if (!ModelSequence_Load(blob, 1, 0)) { Log("the file of %s is not a model the game loads", id.c_str()); blob = nullptr; }
    }
  } else {
    Log("no file for %s in the Colosseum pack", id.c_str());
  }
  sBlobs[id] = blob;
  return blob;
}

uint8_t* sForcedBlob = nullptr;

struct Drawn { uint32_t row = 1; uint8_t* blob = nullptr; };
Drawn Resolve(const Model& m) {
  Drawn d;
  if (m.source == Source::Xd) {
    const uint32_t row = static_cast<uint32_t>(std::atoi(m.id.c_str()));
    d.row = row == 0 ? 0 : (XdRowOk(row) ? row : 1);
    return d;
  }
  if (uint8_t* b = ColoBlob(m.id)) { d.blob = b; d.row = 1; return d; }
  const uint32_t row = static_cast<uint32_t>(std::atoi(StandIn(m).c_str()));
  d.row = XdRowOk(row) ? row : 1;
  return d;
}

const Card* CardOf(void* trainer) {
  if (!sRoom.active || !trainer) return nullptr;
  const bool hostSide = fightTargetIsHostSide(trainer, fightFloor_GetFightType(nullptr)) != 0;
  return hostSide == sRoom.host ? &sRoom.mine : &sRoom.other;
}

}

fs::path CardFolder() { return xd::SettingsFolder() / L"trainer_card"; }
fs::path ColosseumPack() { return xd::SettingsFolder() / L"extra_assets" / L"colosseum"; }
std::string SavedCardPath() {
  if (const char* o = std::getenv("XD_TRAINER_CARD_FILE")) if (o[0]) return o;
  return (CardFolder() / L"card.txt").string();
}
bool SaveMyCard(const Card& c) {
  std::error_code ec;
  fs::create_directories(fs::path(SavedCardPath()).parent_path(), ec);
  return SaveFile(SavedCardPath(), c);
}
Card MyCard() {
  Card c;
  if (const char* e = std::getenv("XD_TRAINER_CARD")) {
    std::string text = e;
    for (char& ch : text) if (ch == ',') ch = ' ';
    if (Decode(text, c, nullptr)) return c;
  }
  if (LoadFile(SavedCardPath(), c)) return c;
  return Card();
}

std::string StandIn(const Model& m) {
  if (m.source == Source::Xd) return m.id;
  if (m.id == "hizuki_a1") return "8";
  if (m.id.find("_f_") != std::string::npos) return "8";
  return "1";
}

fs::path IconPath(const Model& m) {
  if (m.source == Source::Xd) return CardFolder() / "icons" / ("xd_" + m.id + ".png");
  const fs::path inPack = ColosseumPack() / "icons" / (m.id + ".png");
  std::error_code ec;
  if (fs::exists(inPack, ec)) return inPack;
  return CardFolder() / "icons" / ("colo_" + m.id + ".png");
}

std::u16string DiscText(uint32_t msg) {
  static std::unique_ptr<xd::save::GameNames> names;
  static bool tried = false;
  if (!tried) {
    tried = true;
    static XDCommonTables t;
    t = XDCommonTables{};
    auto add = [](const char* name, void* value) {
      if (t.numGlobals < XD_COMMON_MAX_GLOBALS) t.globals[t.numGlobals++] = XDRelGlobalValue{name, value, value != nullptr};
    };
    add("pokemon_data", pokemon_data);
    add("pokemon_data_number", pokemon_data_number);
    add("table_res", table_res);
    add("table_res_number", table_res_number);
    t.pokemon_data = static_cast<struct PokemonData*>(pokemon_data);
    t.pokemon_data_number = pokemon_data_number ? *pokemon_data_number : 0;
    std::string err;
    names.reset(new xd::save::GameNames());
    if (!names->LoadFromRunningGame(&t, err)) { std::fprintf(stderr, "[trainer card] the disc's text is not available: %s\n", err.c_str()); names.reset(); }
  }
  std::u16string out;
  if (!names) return out;
  const std::string u = names->Message(msg);
  for (size_t i = 0; i < u.size();) {
    const unsigned char c = static_cast<unsigned char>(u[i]);
    if (c < 0x80) { out += static_cast<char16_t>(c); i += 1; }
    else if (c < 0xE0 && i + 1 < u.size()) { out += static_cast<char16_t>(((c & 0x1F) << 6) | (u[i + 1] & 0x3F)); i += 2; }
    else if (i + 2 < u.size()) { out += static_cast<char16_t>(((c & 0x0F) << 12) | ((u[i + 1] & 0x3F) << 6) | (u[i + 2] & 0x3F)); i += 3; }
    else break;
  }
  return out;
}

const std::vector<TitleEntry>& Titles() {
  static std::vector<TitleEntry> list;
  static bool done = false;
  if (done || !fight_trainer_kind_data_number) return list;
  int noText = 0;
  for (uint32_t i = 0; i < *fight_trainer_kind_data_number && i < 4096; i++) {
    const uint32_t msg = fightTrainerKindDataBiosGetPrefixName(fightTrainerKindDataBiosGetPtr(static_cast<uint16_t>(i)));
    if (!msg) continue;
    const uint16_t* t = GSmsgGetGSchar(msg);
    std::u16string text;
    bool ok = true;
    for (int k = 0; t && t[k]; k++) {
      if (k >= 40) { ok = false; break; }
      if (t[k] == 0xFFFF) { if (!t[k + 1]) break; k++; continue; }
      if (t[k] < 0x20) { ok = false; break; }
      text += static_cast<char16_t>(t[k]);
    }
    if (!ok || text.empty()) {
      text = DiscText(msg);
      ok = !text.empty() && text.size() <= 30;
      if (!ok) noText++;
    }
    if (!ok || text.empty()) continue;
    bool dup = false;
    for (const TitleEntry& e : list) if (e.text == text) dup = true;
    if (!dup) list.push_back({static_cast<int>(i), msg, text});
  }
  static bool saidEmpty = false;
  if (!list.empty() || !saidEmpty) std::fprintf(stderr, "[trainer card] titles: %zu classes read of %u rows (%d without text yet)\n", list.size(), *fight_trainer_kind_data_number, noText);
  saidEmpty = true;
  if (std::getenv("XD_TRAINER_CARD_LOG")) for (const TitleEntry& e : list) std::fprintf(stderr, "[trainer card] title %d = \"%s\"\n", e.kind, NameToUtf8(e.text).c_str());
  done = !list.empty();
  return list;
}

std::vector<ModelEntry> Models() {
  std::vector<ModelEntry> out;
  auto label = [](uint32_t row) -> std::string {
    switch (row) {
      case 1: return engine::i18n::Tr("Michael (look 1)");
      case 3: return engine::i18n::Tr("Michael (look 2)");
      case 2: return engine::i18n::Tr("Michael (look 3)");
      case 9: return engine::i18n::Tr("Hero (Ruby/Sapphire, boy)");
      case 8: return engine::i18n::Tr("Hero (Ruby/Sapphire, girl)");
      case 7: return engine::i18n::Tr("Hero (FireRed/LeafGreen, boy)");
      case 6: return engine::i18n::Tr("Hero (FireRed/LeafGreen, girl)");
      case 5: return engine::i18n::Tr("Hero (Emerald, boy)");
      case 4: return engine::i18n::Tr("Hero (Emerald, girl)");
    }
    char b[64];
    std::snprintf(b, sizeof b, engine::i18n::Tr("Trainer %u"), static_cast<unsigned>(row));
    return b;
  };
  const uint32_t first[] = {1, 3, 2, 9, 8, 7, 6, 5, 4};
  std::vector<uint32_t> rows(std::begin(first), std::end(first));
  for (uint32_t r = 1; r < trainer_pkx_data_number; r++) if (std::find(rows.begin(), rows.end(), r) == rows.end()) rows.push_back(r);
  for (uint32_t r : rows) {
    if (!XdRowOk(r)) continue;
    Model m;
    m.id = std::to_string(r);
    out.push_back({m, label(r), true, IconPath(m)});
  }
  std::string text;
  {
    std::vector<uint8_t> b;
    if (ReadFile(ColosseumPack() / "characters.json", b)) text.assign(b.begin(), b.end());
  }
  size_t at = 0;
  while ((at = text.find("{\"id\": \"", at)) != std::string::npos) {
    const size_t i0 = at + 8, i1 = text.find('"', i0);
    const size_t n0 = text.find("\"name\": \"", i1);
    if (i1 == std::string::npos || n0 == std::string::npos) break;
    const size_t n1 = text.find('"', n0 + 9);
    at = n1;
    Model m;
    m.source = Source::Colosseum;
    m.id = text.substr(i0, i1 - i0);
    char cb[160];
    std::snprintf(cb, sizeof cb, engine::i18n::Tr("Colosseum: %s"), text.substr(n0 + 9, n1 - n0 - 9).c_str());
    out.push_back({m, cb, true, IconPath(m)});
  }
  return out;
}

namespace {
std::vector<void*> sEarly;
}

void ApplyTo(void* trainer) {
  const Card* c = CardOf(trainer);
  if (!c) return;
  const Drawn d = Resolve(c->model);
  if (d.row != 0) {
    const uintptr_t old = fightTrainer_GetSequencePtr(trainer);
    if (old != 0) {
      sForcedBlob = d.blob;
      const uintptr_t made = Sequence_LoadTrainer(d.row);
      sForcedBlob = nullptr;
      if (made) {
        Sequence_SetVisibility(made, 0);
        fightTrainer_SetSequencePtr(trainer, static_cast<uint32_t>(made));
        Sequence_Free(old);
      }
    }
  }
  if (!c->name.empty()) {
    static uint16_t names[2][16];
    uint16_t* buf = names[c == &sRoom.mine ? 0 : 1];
    size_t k = 0;
    for (; k < c->name.size() && k < kMaxName; k++) buf[k] = c->name[k];
    buf[k] = 0;
    if (void* hero = fightTrainer_GetHeroPtr(trainer)) heroBiosSetNamePtr(hero, buf);
  }
  std::fprintf(stderr, "[trainer card] card applied to the %s trainer: model row %u%s, name \"%s\"\n", c == &sRoom.mine ? "my" : "friend's", d.row,
               d.blob ? " (Colosseum file)" : "", NameToUtf8(c->name).c_str());
}
void NoteEarly(void* trainer) {
  if (sEarly.size() < 4) sEarly.push_back(trainer);
}

void SetRoom(bool iAmHost, const Card& mine, const Card& friendCard) {
  sRoom.active = true;
  sRoom.host = iAmHost;
  sRoom.mine = mine;
  sRoom.other = friendCard;
  const Drawn a = Resolve(mine.model), b = Resolve(friendCard.model);
  std::fprintf(stderr, "[trainer card] mine: title %d, name \"%s\", model %s:%s (%s); the friend's: title %d, name \"%s\", model %s:%s (%s)\n", mine.title,
               NameToUtf8(mine.name).c_str(), mine.model.source == Source::Xd ? "xd" : "colosseum", mine.model.id.c_str(),
               a.blob ? "from the pack" : (mine.model.source == Source::Colosseum ? "STAND-IN" : "xd row"), friendCard.title, NameToUtf8(friendCard.name).c_str(),
               friendCard.model.source == Source::Xd ? "xd" : "colosseum", friendCard.model.id.c_str(),
               b.blob ? "from the pack" : (friendCard.model.source == Source::Colosseum ? "STAND-IN" : "xd row"));
  for (void* t : sEarly) ApplyTo(t);
  sEarly.clear();
}
void ClearRoom() { sRoom = Room(); sEarly.clear(); }
bool RoomActive() { return sRoom.active; }
const Card* HostCard() { return !sRoom.active ? nullptr : (sRoom.host ? &sRoom.mine : &sRoom.other); }
const Card* GuestCard() { return !sRoom.active ? nullptr : (sRoom.host ? &sRoom.other : &sRoom.mine); }

}

extern "C" {

void PortXD_CardTrainerCreate(void* trainer, void* src, uint16_t id, uint32_t controllerId, uint32_t seq) {
  fightTrainerCreate(trainer, src, id, controllerId, seq);
  if (xd::card::RoomActive()) xd::card::ApplyTo(trainer);
  else if (xd::online::Active()) xd::card::NoteEarly(trainer);
}

uint16_t* fightTrainerGetPrefixNamePtr(void* trainer) {
  using namespace xd::card;
  if (const Card* c = CardOf(trainer)) {
    if (c->title >= 0) {
      for (const TitleEntry& e : Titles()) {
        if (e.kind == c->title) return GSmsgGetGSchar(e.msg);
      }
      const uint32_t msg = fightTrainerKindDataBiosGetPrefixName(fightTrainerKindDataBiosGetPtr(static_cast<uint16_t>(c->title)));
      if (msg) return GSmsgGetGSchar(msg);
    }
  }
  return PortXD_RealPrefixNamePtr(trainer);
}

uint16_t* toolentryTaisenGetHeroPtr(int32_t player);
uint16_t* PortXD_VsHeroName(void* hero) {
  using namespace xd::card;
  if (RoomActive()) {
    const Card* c = nullptr;
    if (hero == static_cast<void*>(toolentryTaisenGetHeroPtr(0))) c = HostCard();
    else if (hero == static_cast<void*>(toolentryTaisenGetHeroPtr(1))) c = GuestCard();
    static int logged = 0;
    if (logged < 6 && std::getenv("XD_TRAINER_CARD_LOG")) { logged++; std::fprintf(stderr, "[trainer card] VS screen asks a hero name: %p (side 0 %p, side 1 %p) -> %s\n", hero, static_cast<void*>(toolentryTaisenGetHeroPtr(0)), static_cast<void*>(toolentryTaisenGetHeroPtr(1)), c ? "card" : "game's"); }
    if (c != nullptr && !c->name.empty()) {
      static uint16_t buf[2][16];
      static int flip = 0;
      uint16_t* o = buf[flip ^= 1];
      size_t k = 0;
      for (; k < c->name.size() && k < kMaxName; k++) o[k] = c->name[k];
      o[k] = 0;
      return o;
    }
  }
  return heroBiosGetNamePtr(hero);
}
uint32_t PortXD_VsPrefixName(void* kindData) {
  using namespace xd::card;
  if (RoomActive()) {
    const Card* c = GuestCard();
    if (c != nullptr && c->title >= 0) {
      for (const TitleEntry& e : Titles()) {
        if (e.kind == c->title) return e.msg;
      }
      return fightTrainerKindDataBiosGetPrefixName(fightTrainerKindDataBiosGetPtr(static_cast<uint16_t>(c->title)));
    }
  }
  return fightTrainerKindDataBiosGetPrefixName(kindData);
}

}

uint8_t* PortXD_CardLoadResource(uint32_t group, uint32_t id) {
  uint8_t* res = wazaSequenceSysLoadResource(group, id);
  if (xd::card::sForcedBlob) {
    uint8_t* f = xd::card::sForcedBlob;
    xd::card::sForcedBlob = nullptr;
    return f;
  }
  return res;
}
