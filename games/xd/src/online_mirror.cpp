#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <game/winwork.h>

extern "C" {
int PortXD_MirrorNow(void);
void C_MTXLookAt(float m[3][4], const void* camPos, const void* camUp, const void* target);
void C_MTXTransApply(const float src[3][4], float dst[3][4], float x, float y, float z);
float* HSD_CObjGetViewingMtxPtr(void* cobj);
void HSD_CObjSetupViewingMtx(void* cobj);
uint8_t fightTargetIsHostSide(void* mon, int32_t status);
uint32_t PortVI_GetFlipCount(void);
void* fightFloorGetGcHeroFightTrainerPtr(void* floor);
uintptr_t fightTrainer_GetSequencePtr(void* trainer);
struct PortGridEntry { void* slot; void* pokemon[2]; uint16_t count; uint8_t arg1, arg2; };
extern struct PortGrid { PortGridEntry entries[4]; uint16_t count, pokemonCount; } gBattleGrid;
}

namespace {
bool g_inGsCam = false;
long g_turned = 0;
bool LogOn() {
  static int on = -1;
  if (on < 0) { const char* v = std::getenv("XD_ONLINE_MIRROR_LOG"); on = (v && (v[0] == '1' || v[0] == '2')) ? 1 : 0; }
  return on == 1;
}
bool LogOn3() {
  static int on = -1;
  if (on < 0) { const char* v = std::getenv("XD_ONLINE_MIRROR_LOG"); on = (v && v[0] == '3') ? 1 : 0; }
  return on == 1;
}
bool LogOn2() {
  static int on = -1;
  if (on < 0) { const char* v = std::getenv("XD_ONLINE_MIRROR_LOG"); on = (v && v[0] == '2') ? 1 : 0; }
  return on == 1;
}
void LogSprite(const WinWorkLayout* w, const SpriteWorkLayout* sp) {
  static struct { const void* sp; int id, x, y, cur; } seen[512];
  static int n = 0;
  for (int i = 0; i < n; ++i) if (seen[i].sp == sp && seen[i].id == sp->id && seen[i].x == sp->x && seen[i].y == sp->y && seen[i].cur == w->cursor) return;
  if (n < 512) seen[n++] = {sp, sp->id, sp->x, sp->y, w->cursor};
  std::fprintf(stderr, "[online mirror] win 0x%X off %d,%d cursor %d: sprite id %d kind %d flags %02X x %d y %d w %d h %d\n", w->menuID, w->offX, w->offY,
               w->cursor, sp->id, sp->kind, sp->flags, sp->x, sp->y, sp->w, sp->h);
}
int HostX() {
  static int cached = 0;
  static uint32_t at = 0;
  static int said = 0;
  const uint32_t now = PortVI_GetFlipCount();
  if (cached != 0 && now - at < 20) return cached;
  at = now;
  int r = -1;
  if (gBattleGrid.count != 0) {
    void* hero = fightFloorGetGcHeroFightTrainerPtr(nullptr);
    const uintptr_t seq = hero != nullptr ? fightTrainer_GetSequencePtr(hero) : 0;
    for (int i = 0; seq != 0 && i < 4; ++i) {
      if (reinterpret_cast<uintptr_t>(gBattleGrid.entries[i].slot) == seq) { r = gBattleGrid.entries[i].arg1 != 0 ? -1 : 1; break; }
    }
  }
  cached = r;
  if (LogOn3()) {
    void* hero = fightFloorGetGcHeroFightTrainerPtr(nullptr);
    std::fprintf(stderr, "[mirror grid %u] count %u hero %p seq %p entries:", now, static_cast<unsigned>(gBattleGrid.count), hero, hero ? reinterpret_cast<void*>(fightTrainer_GetSequencePtr(hero)) : nullptr);
    for (int i = 0; i < 4; ++i) std::fprintf(stderr, " [%p arg1 %u arg2 %d n %u]", gBattleGrid.entries[i].slot, gBattleGrid.entries[i].arg1, static_cast<int8_t>(gBattleGrid.entries[i].arg2), gBattleGrid.entries[i].count);
    std::fprintf(stderr, " -> host x %+d\n", r);
  }
  if (LogOn() && said != r) { said = r; std::fprintf(stderr, "[online mirror] the host's side stands at x %s (grid entries %u)\n", r < 0 ? "-" : "+", static_cast<unsigned>(gBattleGrid.count)); }
  return r;
}
constexpr float kHold = 0.25f;
struct Decision { const void* key; uint32_t frame; bool turn; };
Decision g_dec[48];
bool Decide(const void* key, float forwardX) {
  forwardX *= static_cast<float>(-HostX());
  const uint32_t now = PortVI_GetFlipCount();
  Decision* slot = nullptr;
  Decision* old = nullptr;
  for (Decision& d : g_dec) {
    if (d.key == key) { slot = &d; break; }
    if (old == nullptr && (d.key == nullptr || now - d.frame > 30)) old = &d;
  }
  if (slot != nullptr && now - slot->frame > 30) slot = nullptr;
  if (slot == nullptr) {
    if (old == nullptr) old = &g_dec[now % 48];
    *old = Decision{key, now, forwardX > 0.0f};
    return old->turn;
  }
  slot->frame = now;
  if (forwardX > kHold) slot->turn = true;
  else if (forwardX < -kHold) slot->turn = false;
  return slot->turn;
}
bool Everything() {
  static int on = -1;
  if (on < 0) { const char* v = std::getenv("XD_ONLINE_MIRROR"); on = (v && v[0] == 'a') ? 1 : 0; }
  return on == 1;
}
void TurnView(float m[3][4]) {
  for (int i = 0; i < 3; ++i) { m[i][0] = -m[i][0]; m[i][2] = -m[i][2]; }
  if (LogOn() && (++g_turned % 600) == 1) std::fprintf(stderr, "[online mirror] view matrices turned: %ld\n", g_turned);
}
struct Stat { long turned = 0, notFight = 0, notGs = 0; };
Stat g_stat[3];
void LogPose(int k, const float m[3][4]) {
  if (!LogOn3()) return;
  static uint32_t last[3] = {0, 0, 0};
  static int count[3] = {0, 0, 0};
  const uint32_t now = PortVI_GetFlipCount();
  if (last[k] != now) { last[k] = now; count[k] = 0; }
  if (++count[k] > 6) return;
  float eye[3];
  for (int j = 0; j < 3; ++j) eye[j] = -(m[0][j] * m[0][3] + m[1][j] * m[1][3] + m[2][j] * m[2][3]);
  std::fprintf(stderr, "[mirror cam %u] %d%s eye %.1f %.1f %.1f look %.2f %.2f %.2f\n", now, k, g_inGsCam ? "gs" : "--", eye[0], eye[1], eye[2], -m[2][0], -m[2][1], -m[2][2]);
}
void Note(int k, int what) {
  if (LogOn3()) {
    static uint32_t frame = 0;
    static long f[3][3];
    const uint32_t now = PortVI_GetFlipCount();
    if (now != frame) {
      std::fprintf(stderr, "[mirror frame %u] lookat %ld/%ld/%ld rot %ld/%ld/%ld hsd %ld/%ld/%ld\n", frame, f[0][0], f[0][1], f[0][2], f[1][0], f[1][1], f[1][2], f[2][0], f[2][1], f[2][2]);
      std::memset(f, 0, sizeof f);
      frame = now;
    }
    f[k][what == 3 ? 2 : what]++;
    return;
  }
  if (!LogOn()) return;
  Stat& st = g_stat[k];
  if (what == 3) return;
  (what == 0 ? st.turned : what == 1 ? st.notFight : st.notGs)++;
  static long n = 0;
  if (++n % 2000 == 0)
    std::fprintf(stderr, "[online mirror] view builds: lookat turned %ld / not-in-fight %ld; rot turned %ld / not %ld; hsd turned %ld / not-in-fight %ld / outside a GScamera %ld\n",
                 g_stat[0].turned, g_stat[0].notFight, g_stat[1].turned, g_stat[1].notFight, g_stat[2].turned, g_stat[2].notFight, g_stat[2].notGs);
}
struct Known { const float* mtx; uint32_t frame; };
Known g_known[32];
void Remember(const float* mtx) {
  const uint32_t now = PortVI_GetFlipCount();
  int free_ = -1;
  for (int i = 0; i < 32; ++i) {
    if (g_known[i].mtx == mtx) { g_known[i].frame = now; return; }
    if (free_ < 0 && (g_known[i].mtx == nullptr || now - g_known[i].frame > 3)) free_ = i;
  }
  if (free_ >= 0) g_known[free_] = {mtx, now};
}
bool IsKnown(const float* mtx) {
  const uint32_t now = PortVI_GetFlipCount();
  for (int i = 0; i < 32; ++i) {
    if (g_known[i].mtx == mtx) return now - g_known[i].frame <= 3;
  }
  return false;
}
struct InGs {
  InGs() { g_inGsCam = true; }
  ~InGs() { g_inGsCam = false; }
};
}

extern "C" {
void PortXD_MirLookAt(float m[3][4], const void* pos, const void* up, const void* target) {
  C_MTXLookAt(m, pos, up, target);
  LogPose(0, m);
  if (!PortXD_MirrorNow()) Note(0, 1);
  else if (Everything() || Decide(&m[0][0], -m[2][0])) { TurnView(m); Note(0, 0); }
  else Note(0, 3);
}
void PortXD_MirTransApply(const float src[3][4], float dst[3][4], float x, float y, float z) {
  C_MTXTransApply(src, dst, x, y, z);
  if (LogOn3()) {
    static uint32_t last = 0;
    static int count = 0;
    const uint32_t now = PortVI_GetFlipCount();
    if (last != now) { last = now; count = 0; }
    if (++count <= 6) std::fprintf(stderr, "[mirror cam %u] 1rot eye %.1f %.1f %.1f look %.2f %.2f %.2f\n", now, dst[0][3], dst[1][3], dst[2][3], -dst[0][2], -dst[1][2], -dst[2][2]);
  }
  if (!PortXD_MirrorNow()) Note(1, 1);
  else if (Everything() || Decide(&dst[0][0], -dst[0][2])) {
    for (int j = 0; j < 4; ++j) { dst[0][j] = -dst[0][j]; dst[2][j] = -dst[2][j]; }
    Note(1, 0);
  } else Note(1, 3);
}
float* PortXD_MirGsViewMtx(void* cobj) {
  InGs g;
  float* const p = HSD_CObjGetViewingMtxPtr(cobj);
  Remember(p);
  return p;
}
void PortXD_MirGsSetupMtx(void* cobj) {
  InGs g;
  HSD_CObjSetupViewingMtx(cobj);
}
void PortXD_MirCobjLookAt(float m[3][4], const void* pos, const void* up, const void* target) {
  C_MTXLookAt(m, pos, up, target);
  LogPose(2, m);
  if (!g_inGsCam && !IsKnown(&m[0][0])) Note(2, 2);
  else if (!PortXD_MirrorNow()) Note(2, 1);
  else if (Everything() || Decide(&m[0][0], -m[2][0])) { TurnView(m); Note(2, 0); }
  else Note(2, 3);
}
uint8_t PortXD_MirMsgSide(void* mon, int32_t status) {
  const uint8_t r = fightTargetIsHostSide(mon, status);
  return PortXD_MirrorNow() ? static_cast<uint8_t>(r == 1 ? 0 : 1) : r;
}
}

extern "C" void menuDataBiosGetXY(int32_t menuID, int16_t* x, int16_t* y);
extern "C" int OnlineMirror_WindowShift(const void* window, const void* sprite, int* dx, int* dy) {
  const auto* w = static_cast<const WinWorkLayout*>(window);
  const int menuID = w->menuID;
  if (!PortXD_MirrorNow()) return 0;
  if (LogOn2()) LogSprite(w, static_cast<const SpriteWorkLayout*>(sprite));
  int other = -1;
  switch (menuID) {
  case 0x37: other = 0x38; break;
  case 0x38: other = 0x37; break;
  case 0x40: other = 0x41; break;
  case 0x41: other = 0x40; break;
  default: return 0;
  }
  int16_t ax = 0, ay = 0, bx = 0, by = 0;
  menuDataBiosGetXY(menuID, &ax, &ay);
  menuDataBiosGetXY(other, &bx, &by);
  *dx = bx - ax;
  *dy = by - ay;
  if (LogOn()) {
    static int shown[4] = {0, 0, 0, 0};
    int& n = shown[(menuID - 0x37) & 3];
    if (n++ < 3) std::fprintf(stderr, "[online mirror] panel 0x%X at %d,%d (home %d,%d) -> drawn at 0x%X's home %d,%d (shift %d,%d)\n", menuID, w->offX, w->offY, ax, ay, other, bx, by, *dx, *dy);
  }
  return 1;
}

extern "C" void* menuItemBiosGetPtr(int32_t menuID, uint32_t index);
namespace {
struct ItemView {
  uint8_t bits0, bits1, alpha, pad3;
  int16_t spriteID, x, y, w, h;
};
bool g_targetSwapped = false;
void SwapTargetItems(bool on) {
  if (g_targetSwapped == on) return;
  ItemView* it[4] = {nullptr, nullptr, nullptr, nullptr};
  for (uint32_t i = 0; i < 4; ++i) it[i] = static_cast<ItemView*>(menuItemBiosGetPtr(0x5C, 0x138 + i));
  if (!it[0] || !it[1] || !it[2] || !it[3]) {
    static bool said = false;
    if (!said) { said = true; std::fprintf(stderr, "[online mirror] target menu items not found (menu data), left/right stay as the game has them\n"); }
    g_targetSwapped = on;
    return;
  }
  const int16_t t0 = it[0]->x, t1 = it[1]->x;
  it[0]->x = it[2]->x; it[2]->x = t0;
  it[1]->x = it[3]->x; it[3]->x = t1;
  g_targetSwapped = on;
  if (LogOn()) std::fprintf(stderr, "[online mirror] target menu items %s: x now %d %d %d %d\n", on ? "swapped" : "put back", it[0]->x, it[1]->x, it[2]->x, it[3]->x);
}
}
extern "C" void OnlineMirror_Frame(void) { SwapTargetItems(PortXD_MirrorNow() != 0); }
extern "C" int PortXD_MsgIsHeroSide(void* fop, int isHero) {
  (void)fop;
  return PortXD_MirrorNow() ? (isHero ? 0 : 1) : isHero;
}
extern "C" void OnlineMirror_Reset(void) { SwapTargetItems(false); }
