#include "online_draft_game.h"

#include "online_draft.h"
#include "i18n.h"
#include "xd_ui.h"

#include <gx_texture_decode.h>
#include <port_menu.h>

#include <aurora/imgui.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <dolphin/types.h>
#include <game/bitfield.h>
#include <game/faceData.h>
#include <game/gstexture.h>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/relglobal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <random>

extern "C" {
void fightMainWaitFrame(uint32_t frames);
void* GSresGetResourceAllGroup(u32 id);
s32 menuFaceLoadFromID(u16 id, void* fn, void* arg);
s8 menuFaceSearchEnable(u16 id);
void pokemonCreate(void* pokemon, u16 species, u8 level, void* attest);
uintptr_t gamedataGetStatus(void* gamedata, u32 kind);
void PortXD_PokemonSetLevelStats(void* poke, uint8_t level);
extern uint32_t* __hsd_rand_seed_hold;
}
extern "C" u16* GSmsgGetGSchar(u32 msg);

namespace xd::online {
namespace {

namespace dr = xd::online::draft;
using engine::i18n::Tr;

template <class... A> std::string Fmt(const char* f, A... a) {
  char b[512];
  std::snprintf(b, sizeof b, f, a...);
  return b;
}

constexpr int kMaxId = 0x2000;

int GenOfNationalDex(int dex) { return dex <= 151 ? 1 : dex <= 251 ? 2 : dex <= 386 ? 3 : 4; }

std::vector<dr::Species> BuildTable() {
  std::vector<dr::Species> out;
  if (pokemon_data == nullptr || pokemon_data_number == nullptr) return out;
  const uint32_t n = std::min<uint32_t>(*pokemon_data_number, kMaxId);
  std::vector<bool> hasPre(n, false), legend(n, false);
  if (legendPokemonData != nullptr && legendPokemonData_number != nullptr) {
    for (uint32_t i = 0; i < *legendPokemonData_number && i < 256; i++) {
      const uint16_t id = legendPokemonData[i];
      if (id != 0 && id < n) legend[id] = true;
    }
  }
  for (uint32_t id = 1; id < n; id++) {
    for (int k = 0; k < 5; k++) {
      const Sinka& s = pokemon_data[id].sinka[k];
      if (s.kind != 0 && s.pokemonDataId != 0 && s.pokemonDataId < n) hasPre[s.pokemonDataId] = true;
    }
  }
  for (uint32_t id = 1; id < n; id++) {
    const PokemonData& d = pokemon_data[id];
    dr::Species s;
    s.id = static_cast<uint16_t>(id);
    s.valid = d.name != 0 && d.numPokemon >= 1 && d.numPokemon <= 493;
    s.gen = GenOfNationalDex(d.numPokemon);
    s.fullyEvolved = true;
    for (int k = 0; k < 5; k++) {
      if (d.sinka[k].kind != 0) s.fullyEvolved = false;
    }
    s.hasPreEvo = hasPre[id];
    s.legend = legend[id];
    out.push_back(s);
  }
  return out;
}

std::string Utf8(const u16* chars) {
  std::string out;
  for (int i = 0; chars != nullptr && i < 40 && chars[i] != 0; i++) {
    const uint32_t c = chars[i];
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    else { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
  }
  return out;
}

std::string NameOf(uint16_t id) {
  if (pokemon_data == nullptr || id >= *pokemon_data_number) return "?";
  const std::string n = Utf8(GSmsgGetGSchar(pokemon_data[id].name));
  return n.empty() ? "#" + std::to_string(id) : n;
}

struct Face {
  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
  bool tried = false, ok = false;
  ImTextureID tex{};
  bool uploaded = false;
};
std::map<uint16_t, Face> sFaces;
bool sFacesBroken = false;

uint16_t FaceIdOf(uint16_t species) {
  if (pokemon_data == nullptr || species >= *pokemon_data_number) return 0;
  return pokemon_data[species].cgData[0].spriteId;
}

void LoadFace(uint16_t species) {
  Face& f = sFaces[species];
  if (f.tried) return;
  f.tried = true;
  if (sFacesBroken || poke_face == nullptr || poke_face_number == nullptr) return;
  const uint16_t faceId = FaceIdOf(species);
  if (faceId == 0 || faceId >= *poke_face_number) return;
  const u32 texID = static_cast<const MenuFaceData*>(poke_face)[faceId].texID;
  if (texID == 0) return;
  menuFaceLoadFromID(faceId, nullptr, nullptr);
  int waited = 0;
  while (menuFaceSearchEnable(faceId) != 1 && waited < 400) {
    fightMainWaitFrame(1);
    waited++;
  }
  if (menuFaceSearchEnable(faceId) != 1) {
    std::fprintf(stderr, "[online draft] the face of species %u did not load in %d frames: number tiles from now on\n", species, waited);
    sFacesBroken = true;
    return;
  }
  const auto* tex = static_cast<const GStexture*>(GSresGetResourceAllGroup(texID));
  if (tex == nullptr) return;
  const void* data = GXGetTexObjData(&tex->texObj);
  const u16 w = GXGetTexObjWidth(&tex->texObj), h = GXGetTexObjHeight(&tex->texObj);
  const u32 fmt = GXGetTexObjFmt(&tex->texObj);
  const u32 tlutFmt = tex->tlutFormat == 2 ? GX_TL_RGB565 : tex->tlutFormat == 3 ? GX_TL_RGB5A3 : GX_TL_IA8;
  f.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
  if (data != nullptr && w > 0 && h > 0 && PortGX_DecodeTexture(data, w, h, fmt, tex->tlutData, tlutFmt, f.rgba.data())) {
    f.w = w, f.h = h, f.ok = true;
  } else {
    f.rgba.clear();
  }
}

struct SeedKeep {
  uint32_t* p;
  uint32_t v;
  SeedKeep() : p(__hsd_rand_seed_hold), v(p ? *p : 0) {}
  ~SeedKeep() { if (p) *p = v; }
};

int LevelOf(const RoomRules& r) {
  if (r.level == "asis") return 50;
  return std::max(1, std::min(100, std::atoi(r.level.c_str() + 4)));
}

bool BuildRecord(uint16_t species, int level, xd::save::Pokemon& out) {
  alignas(16) uint8_t buf[0x100];
  std::memset(buf, 0, sizeof buf);
  SeedKeep keep;
  void* attest = reinterpret_cast<void*>(gamedataGetStatus(nullptr, 1));
  if (attest == nullptr) return false;
  pokemonCreate(buf, species, static_cast<uint8_t>(level), attest);
  const size_t S = xd::save::kPokemonSize;
  auto rd16 = [&](size_t o) { return static_cast<uint16_t>(buf[o] | buf[o + 1] << 8); };
  if (rd16(0) != species) return false;
  uint32_t pid = static_cast<uint32_t>(buf[0x28] | buf[0x29] << 8 | buf[0x2A] << 16 | static_cast<uint32_t>(buf[0x2B]) << 24);
  pid -= pid % 25;
  for (int guard = 0; guard < 64 && (((pid >> 16) ^ (pid & 0xFFFF)) >> 3) == 0; guard++) pid += 25;
  for (int i = 0; i < 4; i++) buf[0x28 + i] = static_cast<uint8_t>(pid >> (8 * i));
  buf[0x1D] = static_cast<uint8_t>(buf[0x1D] & ~0x40);
  for (int i = 0; i < 6; i++) buf[0xA8 + i] = 31;
  std::memset(buf + 0x9C, 0, 12);
  buf[0x02] = buf[0x03] = 0;
  PortXD_PokemonSetLevelStats(buf, static_cast<uint8_t>(level));
  buf[0x04] = buf[0x90];
  buf[0x05] = buf[0x91];
  buf[0x16] = 0;
  std::array<uint8_t, xd::save::kPokemonSize> raw{};
  std::memcpy(raw.data(), buf, S);
  out = xd::save::ParsePokemon(raw.data(), xd::save::ByteOrder::PC);
  return out.species == species;
}

struct Page {
  std::unique_ptr<dr::Session> sess;
  std::vector<dr::Species> table;
  std::string friendName = "Your friend";
  int cursor = 0;
  int scroll = 0;
  int armed = -1;
  std::vector<uint16_t> banPick;
  int autoCount = 0;
  int autoDelay = 0;
  int endCount = 0;
  bool open = false;
  std::atomic<bool> finished{false};
  bool needFaces = false;
  std::vector<uint16_t> shown;
  int refreshes = 0;
};
Page* sPage = nullptr;

constexpr int kCols = 8;
constexpr float kCell = 50.f, kTile = 44.f;
constexpr float kGridX = 32.f, kGridY = 108.f;
constexpr int kRowsVisible = 5;

void UploadFace(Face& f) {
  if (f.ok && !f.uploaded) {
    f.tex = aurora_imgui_add_texture(static_cast<uint32_t>(f.w), static_cast<uint32_t>(f.h), f.rgba.data());
    f.uploaded = true;
  }
}

void DrawFace(ImDrawList* dl, uint16_t species, float x, float y, float size, float s) {
  Face& f = sFaces[species];
  UploadFace(f);
  const ImVec2 a(x, y), b(x + size, y + size);
  if (f.ok && f.uploaded) {
    const float u1 = std::min(1.f, 42.f / static_cast<float>(f.w)), v1 = std::min(1.f, 42.f / static_cast<float>(f.h));
    dl->AddImage(f.tex, a, b, ImVec2(0, 0), ImVec2(u1, v1));
  } else {
    dl->AddRectFilled(a, b, IM_COL32(40, 60, 100, 255));
    char t[16];
    std::snprintf(t, sizeof t, "%u", species);
    dl->AddText(nullptr, 12.f * s, ImVec2(x + 4 * s, y + size / 2 - 6 * s), IM_COL32(220, 230, 255, 255), t);
  }
}

std::vector<uint16_t> ShownNow(const dr::Session& s) {
  if (!s.Ready()) return {};
  if (s.draft().settings().mode == "snake") return s.draft().pool();
  return s.draft().CurrentOffer(s.me());
}

bool FaceMissing(uint16_t id) { return !sFaces[id].tried && !sFacesBroken; }

bool PageFrame(void*, const PortMenuInput* in) {
  Page& P = *sPage;
  dr::Session& S = *P.sess;
  P.refreshes++;
  S.Poll();
  float x0, y0, s;
  PortMenu_PictureRect(&x0, &y0, &s);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  auto X = [&](float v) { return x0 + v * s; };
  auto Y = [&](float v) { return y0 + v * s; };
  namespace ui = xd::ui;
  ui::Backdrop(dl);

  const bool failed = S.state() == dr::Session::State::Failed;
  if (failed || S.Finished()) {
    if (P.endCount++ > (failed ? 240 : 150)) {
      P.finished = true;
      return false;
    }
  }
  if (!S.Ready()) {
    ui::Header(dl, Tr("DRAFT"));
    ui::Window(dl, 90.f, 180.f, 550.f, 280.f, failed);
    if (!failed) ui::Ring(dl, 140.f, 230.f, 22.f);
    ui::Para(dl, failed ? 116.f : 180.f, 206.f, failed ? 410.f : 350.f, failed ? S.error() : std::string(Tr("Waiting for the host's pool...")),
             failed ? ui::kBad : ui::kText, 0.9f, 24.f, 3);
    return true;
  }
  const dr::Draft& D = S.draft();
  const dr::Settings& cfg = D.settings();
  P.shown = ShownNow(S);
  for (uint16_t id : P.shown) {
    if (FaceMissing(id)) { P.needFaces = true; return false; }
  }
  const int n = static_cast<int>(P.shown.size());
  P.cursor = n == 0 ? 0 : std::max(0, std::min(P.cursor, n - 1));

  const bool banning = S.state() == dr::Session::State::Banning && !D.BansSubmitted(S.me());
  bool pressA = (in->pressed & PAD_BUTTON_A) != 0, pressB = (in->pressed & PAD_BUTTON_B) != 0, pressStart = (in->pressed & PAD_BUTTON_START) != 0;
  uint16_t rep = in->repeat;
  if (P.autoCount > 0 && !failed && !S.Finished()) {
    const bool chance = banning || S.MyTurn();
    if (chance && ++P.autoDelay >= P.autoCount) {
      P.autoDelay = 0;
      if (banning) {
        for (int i = 0; i < n && static_cast<int>(P.banPick.size()) < cfg.bans; i++) {
          if (std::find(P.banPick.begin(), P.banPick.end(), P.shown[static_cast<size_t>(i)]) == P.banPick.end() && (i % 2 == (S.me() == 0 ? 1 : 0))) {
            P.banPick.push_back(P.shown[static_cast<size_t>(i)]);
          }
        }
        for (int i = 0; i < n && static_cast<int>(P.banPick.size()) < cfg.bans; i++) {
          if (std::find(P.banPick.begin(), P.banPick.end(), P.shown[static_cast<size_t>(i)]) == P.banPick.end()) P.banPick.push_back(P.shown[static_cast<size_t>(i)]);
        }
        pressStart = true;
      } else {
        for (int i = 0; i < n; i++) {
          if (cfg.mode == "snake" ? D.MarkOf(P.shown[static_cast<size_t>(i)]) == dr::Mark::Free : true) { P.cursor = i; break; }
        }
        P.armed = P.cursor;
        pressA = true;
      }
    }
  }
  const int cols = cfg.mode == "snake" ? kCols : std::max(1, n);
  if (rep & PAD_BUTTON_LEFT) P.cursor = std::max(0, P.cursor - 1);
  if (rep & PAD_BUTTON_RIGHT) P.cursor = std::min(n - 1, P.cursor + 1);
  if (cfg.mode == "snake") {
    if (rep & PAD_BUTTON_UP) P.cursor = std::max(0, P.cursor - cols);
    if (rep & PAD_BUTTON_DOWN) P.cursor = std::min(n - 1, P.cursor + cols);
  }
  if (P.cursor != P.armed && !(pressA)) P.armed = -1;
  if (n > 0) {
    const int row = P.cursor / cols;
    if (row < P.scroll) P.scroll = row;
    if (row >= P.scroll + kRowsVisible) P.scroll = row - kRowsVisible + 1;
  }
  const uint16_t cur = n > 0 ? P.shown[static_cast<size_t>(P.cursor)] : 0;
  if (banning) {
    if (pressA && cur != 0) {
      auto it = std::find(P.banPick.begin(), P.banPick.end(), cur);
      if (it != P.banPick.end()) P.banPick.erase(it);
      else if (static_cast<int>(P.banPick.size()) < cfg.bans) P.banPick.push_back(cur);
    }
    if (pressStart && static_cast<int>(P.banPick.size()) == cfg.bans) {
      std::string why;
      S.ChooseBans(P.banPick, &why);
    }
  } else if (S.MyTurn() && pressA && cur != 0) {
    if (P.armed == P.cursor) {
      std::string why;
      if (S.Choose(cur, &why)) { P.armed = -1; P.cursor = 0; P.scroll = 0; P.autoDelay = 0; }
    } else {
      P.armed = P.cursor;
    }
  }
  if (pressB) P.armed = -1;

  std::string head;
  if (failed) head = S.error();
  else if (S.Finished()) head = Tr("The draft is complete!");
  else if (banning) head = Fmt(Tr("Ban %d Pokemon (%d marked). A marks, START locks in."), cfg.bans, static_cast<int>(P.banPick.size()));
  else if (S.state() == dr::Session::State::Banning) head = Tr("Waiting for your friend's bans...");
  else if (S.MyTurn()) head = cfg.mode == "snake" ? Tr("YOUR PICK  (A twice to take it)") : Tr("Choose one of the three  (A twice)");
  else if (S.state() == dr::Session::State::Picking) head = cfg.mode == "snake" ? Tr("Waiting for your friend to pick...") : Tr("Waiting for your friend...");
  else head = Tr("Checking the teams...");
  const double tl = S.TimeLeft();
  std::string clock;
  if (tl >= 0 && !S.Finished()) {
    char t[24];
    std::snprintf(t, sizeof t, "%d:%02d", static_cast<int>(tl) / 60, static_cast<int>(tl) % 60);
    clock = t;
  }
  ui::Header(dl, head, clock);
  if (failed) ui::Text(dl, 82.f, 40.f, head, ui::kBad, 1.f, 480.f, 1);
  if (!clock.empty() && tl < 10) ui::Text(dl, 588.f - ui::Width(clock, 0.8f), 41.f, clock, ui::kBad, 0.8f);

  ui::TitledWindow(dl, 8.f, 64.f, 446.f, 398.f, dr::SettingsDescribe(cfg));
  const int me = S.me();
  for (int i = 0; i < n; i++) {
    const int row = i / cols, col = i % cols;
    if (row < P.scroll || row >= P.scroll + kRowsVisible) continue;
    const float gx = cfg.mode == "snake" ? kGridX + col * kCell : 40.f + col * 130.f;
    const float gy = cfg.mode == "snake" ? kGridY + (row - P.scroll) * kCell : 130.f;
    const float tile = cfg.mode == "snake" ? kTile : 96.f;
    const uint16_t id = P.shown[static_cast<size_t>(i)];
    DrawFace(dl, id, X(gx), Y(gy), tile * s, s);
    const dr::Mark m = D.MarkOf(id);
    const bool marked = std::find(P.banPick.begin(), P.banPick.end(), id) != P.banPick.end();
    const ImVec2 a(X(gx), Y(gy)), b(X(gx) + tile * s, Y(gy) + tile * s);
    if (m == dr::Mark::Banned || marked) {
      dl->AddRectFilled(a, b, IM_COL32(120, 0, 0, 150));
      dl->AddLine(a, b, IM_COL32(255, 80, 80, 255), 3.f);
      dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), IM_COL32(255, 80, 80, 255), 3.f);
    } else if (m == dr::Mark::PickedByHost || m == dr::Mark::PickedByGuest) {
      const bool mine = (m == dr::Mark::PickedByHost) == (me == 0);
      dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 150));
      dl->AddRect(a, b, mine ? IM_COL32(90, 190, 255, 255) : IM_COL32(255, 170, 60, 255), 4.f * s, 0, 3.f);
      const uint8_t you[4] = {150, 220, 255, 255}, them[4] = {255, 200, 120, 255};
      ui::Text(dl, gx + 3.f, gy + tile - 16.f, mine ? Tr("YOU") : Tr("FRIEND"), mine ? you : them, 0.5f, tile - 6.f, 1);
    }
    if (i == P.cursor) {
      const ImU32 c = P.armed == i ? IM_COL32(255, 70, 70, 255) : IM_COL32(0xFF, 0xD8, 0x68, ui::Pulse());
      dl->AddRect(ImVec2(a.x - 3 * s, a.y - 3 * s), ImVec2(b.x + 3 * s, b.y + 3 * s), c, 6.f * s, 0, 3.f * s);
      ui::Cursor(dl, gx - 2.f, gy + 8.f);
    }
    if (cfg.mode == "offer") ui::TextC(dl, gx - 10.f, gy + tile + 8.f, tile + 20.f, NameOf(id), ui::kText, 0.75f, 1);
  }
  if (cfg.mode == "snake" && (n + cols - 1) / cols > kRowsVisible) {
    if (P.scroll > 0) ui::Text(dl, 404.f, 74.f, "^", ui::kSoft, 0.8f);
    if (P.scroll + kRowsVisible < (n + cols - 1) / cols) ui::Text(dl, 410.f, 376.f, "v", ui::kSoft, 0.8f);
  }

  for (int side = 0; side < 2; side++) {
    const int player = side == 0 ? me : 1 - me;
    const float by = side == 0 ? 64.f : 232.f;
    ui::TitledWindow(dl, 454.f, by, 634.f, by + 162.f, side == 0 ? std::string(Tr("Your team")) : std::string(Tr("Your friend's team")));
    const std::vector<uint16_t>& team = D.Team(player);
    for (int k = 0; k < cfg.teamSize; k++) {
      const float tx = 476.f + (k % 3) * 46.f, ty = by + 42.f + (k / 3) * 58.f;
      dl->AddRectFilled(ImVec2(X(tx), Y(ty)), ImVec2(X(tx + 42), Y(ty + 42)), IM_COL32(0, 0, 0, 255), 4.f * s);
      dl->AddRect(ImVec2(X(tx), Y(ty)), ImVec2(X(tx + 42), Y(ty + 42)), IM_COL32(0x70, 0x70, 0x74, 255), 4.f * s, 0, 1.f);
      if (static_cast<size_t>(k) < team.size()) {
        DrawFace(dl, team[static_cast<size_t>(k)], X(tx), Y(ty), 42.f * s, s);
        ui::Text(dl, tx, ty + 42.f, NameOf(team[static_cast<size_t>(k)]), ui::kSoft, 0.4f, 44.f);
      }
    }
  }
  ui::Window(dl, 8.f, 402.f, 634.f, 440.f);
  if (cur != 0) {
    ui::Text(dl, 32.f, 410.f, "#" + std::to_string(cur) + "  " + NameOf(cur), ui::kText, 0.9f, 360.f, 1);
    const dr::Mark m = D.MarkOf(cur);
    std::string st = m == dr::Mark::Banned ? Tr("banned") : m == dr::Mark::Free ? Tr("available") : ((m == dr::Mark::PickedByHost) == (me == 0) ? Tr("picked by you") : Tr("picked by your friend"));
    ui::Text(dl, 608.f - std::min(ui::Width(st, 0.75f), 220.f), 412.f, st, m == dr::Mark::Banned ? ui::kBad : m == dr::Mark::Free ? ui::kGood : ui::kGold, 0.75f, 220.f);
  }
  ui::HintBar(dl, Tr("D-pad: move    A: choose    B: cancel"));
  return true;
}

void PageOpen(void*) { sPage->open = true; }
void PageClose(void*) { sPage->open = false; }

std::string RandomNonce() {
  static std::random_device rd;
  char b[40];
  std::snprintf(b, sizeof b, "%08x%08x%08x%08x", rd(), rd(), rd(), rd());
  return b;
}

}

bool RunDraft(bool host, const RoomRules& rules, BattleTransport* link, std::vector<xd::save::Pokemon>& mine, std::vector<uint16_t>& friendSpecies, std::string& why) {
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  auto now = [t0] { return std::chrono::duration<double>(clock::now() - t0).count(); };
  Page page;
  page.table = BuildTable();
  if (page.table.empty()) { why = Tr("The game's Pokemon tables are not available for the draft."); return false; }
  dr::Settings cfg;
  if (!dr::ParseSettings(rules.team, rules.draft, cfg, &why)) { why = Fmt(Tr("The room's draft options are not valid: %s"), why.c_str()); return false; }
  if (!dr::CheckForRoom(cfg, rules.ruleSet, page.table, &why)) { why = Fmt(Tr("The room's draft cannot be made: %s"), why.c_str()); return false; }
  size_t valid = 0;
  for (const dr::Species& sp : page.table) valid += sp.valid;
  std::fprintf(stderr, "[online draft] %s; %zu species in the tables (%zu usable), %zu pass the filters\n", dr::SettingsDescribe(cfg).c_str(), page.table.size(), valid,
               dr::Candidates(cfg, page.table).size());
  page.sess.reset(new dr::Session(host, cfg, rules.timerSeconds, page.table, link, RandomNonce, now));
  if (const char* a = std::getenv("XD_ONLINE_DRAFT_AUTO")) page.autoCount = std::max(1, std::atoi(a));
  if (host) {
    uint64_t seed = (static_cast<uint64_t>(std::random_device()()) << 32) ^ std::random_device()();
    if (const char* sd = std::getenv("XD_ONLINE_DRAFT_SEED")) seed = std::strtoull(sd, nullptr, 16);
    page.sess->Start(seed);
  }
  for (int i = 0; i < 60 * 180 && !page.sess->Ready() && page.sess->state() != dr::Session::State::Failed; i++) {
    page.sess->Poll();
    fightMainWaitFrame(1);
  }
  if (!page.sess->Ready()) { why = page.sess->error().empty() ? std::string(Tr("Your friend's pool never came.")) : page.sess->error(); return false; }
  sPage = &page;
  for (int rounds = 0; rounds < 400; rounds++) {
    for (uint16_t id : ShownNow(*page.sess)) LoadFace(id);
    page.needFaces = false;
    PortMenuPage pg{"draft", PageOpen, PageFrame, PageClose, nullptr};
    int guard = 0;
    while (!PortMenu_Open(&pg) && guard++ < 600) fightMainWaitFrame(1);
    while (!page.open && !page.finished.load() && guard++ < 1200) fightMainWaitFrame(1);
    while (PortMenu_Active() || PortMenu_Pending()) fightMainWaitFrame(1);
    if (page.finished.load()) break;
    if (!page.needFaces) {
      if (page.sess->state() == dr::Session::State::Done || page.sess->state() == dr::Session::State::Failed) break;
    }
  }
  sPage = nullptr;
  if (page.sess->state() != dr::Session::State::Done) {
    why = page.sess->error().empty() ? std::string(Tr("The draft did not finish.")) : page.sess->error();
    return false;
  }
  const int level = LevelOf(rules);
  mine.clear();
  for (uint16_t id : page.sess->Team(page.sess->me())) {
    xd::save::Pokemon rec;
    if (!BuildRecord(id, level, rec)) { why = Fmt(Tr("A drafted Pokemon (%s) could not be made."), NameOf(id).c_str()); return false; }
    mine.push_back(rec);
  }
  friendSpecies = page.sess->Team(1 - page.sess->me());
  std::fprintf(stderr, "[online draft] done: my team");
  for (uint16_t id : page.sess->Team(page.sess->me())) std::fprintf(stderr, " %u", id);
  std::fprintf(stderr, "; the friend's");
  for (uint16_t id : friendSpecies) std::fprintf(stderr, " %u", id);
  std::fprintf(stderr, "\n");
  return true;
}

extern "C" {
void heroInit(void* hero);
uint8_t heroAddPokemon(void* hero, void* pokemon);
void heroBiosSetNamePtr(void* hero, const uint16_t* name);
void heroBiosSetSexDataId(void* hero, uint8_t value);
int32_t heroGetLivingPokemonCount(void* hero);
}
bool MakePlaceholderHero(void* hero, int count) {
  if (hero == nullptr || count < 1 || count > 6) return false;
  void* attest = reinterpret_cast<void*>(gamedataGetStatus(nullptr, 1));
  if (attest == nullptr) return false;
  heroInit(hero);
  static uint16_t name[8] = {'P', 'l', 'a', 'y', 'e', 'r', 0, 0};
  heroBiosSetNamePtr(hero, name);
  heroBiosSetSexDataId(hero, 0);
  for (int i = 0; i < count; i++) {
    alignas(16) uint8_t buf[0x100];
    std::memset(buf, 0, sizeof buf);
    SeedKeep keep;
    pokemonCreate(buf, 1, 5, attest);
    buf[0x04] = buf[0x90];
    buf[0x05] = buf[0x91];
    buf[0x16] = 0;
    heroAddPokemon(hero, buf);
  }
  std::fprintf(stderr, "[online lobby] placeholder hero: %d Pokemon asked, %d living in it\n", count, heroGetLivingPokemonCount(hero));
  return true;
}

bool BuildTeamFromSpecies(const RoomRules& rules, const std::vector<uint16_t>& species, std::vector<xd::save::Pokemon>& out, std::string& why) {
  out.clear();
  const int level = LevelOf(rules);
  for (uint16_t id : species) {
    xd::save::Pokemon rec;
    if (!BuildRecord(id, level, rec)) { why = Fmt(Tr("A Pokemon you picked (%s) could not be made."), NameOf(id).c_str()); return false; }
    out.push_back(rec);
  }
  return true;
}

}
