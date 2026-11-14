#include <game/winwork.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "i18n.h"

extern "C" {
void menuTitleCursorAnime(void* w, void* s);
void winSpriteSetDisp(void* sprite, bool on);
int32_t winSpriteGetLayerID(void* window, void* sprite);
void msgctrlSetValue(int32_t id, uintptr_t value);
void GSmsgPrint2(int32_t layer, int32_t x, int32_t y, uint32_t color, uint32_t msg);
void* PortXD_RealMenuItemPtr(int32_t menuID, uint32_t index);
bool PortXD_VsRowEnabled(void);
}

namespace {

struct Item {
  uint8_t flags;
  uint8_t flags2;
  uint8_t unk2, unk3;
  uint16_t imageId;
  int16_t x, y, w, h;
  int16_t spriteId;
  uint32_t msgId;
  void* hook;
  int16_t next;
  int16_t unk1A;
};
static_assert(sizeof(Item) == 0x28, "the item table's entry");
using Hook = void (*)(void*, void*);

constexpr int32_t kMenu = 0x118;
constexpr int kBackFrame = 114, kBackBase = 109, kBackText = 115, kDesc = 116, kLastPanel = 113;
constexpr int kFrameId = 30100, kBaseId = 30101, kTextId = 30102;
constexpr int kRowX = 48, kRowY = 297;
const char16_t* Wide(const char* utf8, std::u16string& keep) {
  keep.clear();
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8); *p != 0;) {
    const unsigned c = *p;
    const int extra = c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    unsigned cp = extra == 0 ? c : extra == 1 ? (c & 0x1Fu) : (c & 0x0Fu);
    p++;
    for (int i = 0; i < extra && (*p & 0xC0) == 0x80; i++, p++) cp = cp << 6 | (*p & 0x3Fu);
    keep.push_back(static_cast<char16_t>(cp));
  }
  return keep.c_str();
}
const char16_t* Label() {
  static std::u16string s;
  return Wide(engine::i18n::Tr("ONLINE BATTLE"), s);
}
const char16_t* Info() {
  static std::u16string s;
  return Wide(engine::i18n::Tr("Battle a friend over the internet."), s);
}

Item sExt[3];
bool sLaidOut = false;
Hook sRealFrameHook = nullptr, sRealDescHook = nullptr;

Item* Real(int idx) { return static_cast<Item*>(PortXD_RealMenuItemPtr(kMenu, static_cast<uint32_t>(idx))); }

void Say(void* w, void* sprite, const char16_t* text, int y) {
  msgctrlSetValue(0x37, reinterpret_cast<uintptr_t>(text));
  const uint8_t alpha = reinterpret_cast<const uint8_t*>(&static_cast<WinWorkLayout*>(w)->pad_90)[3];
  GSmsgPrint2(winSpriteGetLayerID(w, sprite), 0, y, alpha | 0xFFFFFF00u, 0x4277);
}

void CursorFrame(void* w, void* s, int position) {
  if (static_cast<WinWorkLayout*>(w)->cursor == position) {
    menuTitleCursorAnime(w, s);
    winSpriteSetDisp(s, true);
  } else {
    winSpriteSetDisp(s, false);
  }
}

void FrameHook(void* w, void* s) {
  const int id = static_cast<SpriteWorkLayout*>(s)->id;
  if (id == kFrameId) CursorFrame(w, s, 2);
  else if (id == kBackFrame) CursorFrame(w, s, 3);
  else if (sRealFrameHook != nullptr) sRealFrameHook(w, s);
}

void TextHook(void* w, void* s) { Say(w, s, Label(), 0); }

void DescHook(void* w, void* s) {
  auto* win = static_cast<WinWorkLayout*>(w);
  const int pos = win->cursor;
  if (pos == 2) {
    Say(w, s, Info(), -8);
  } else if (pos == 3) {
    win->cursor = 2;
    if (sRealDescHook != nullptr) sRealDescHook(w, s);
    win->cursor = 3;
  } else if (sRealDescHook != nullptr) {
    sRealDescHook(w, s);
  }
}

void LayOut() {
  if (sLaidOut) return;
  Item* frame = Real(kBackFrame);
  Item* base = Real(kBackBase);
  Item* text = Real(kBackText);
  Item* desc = Real(kDesc);
  Item* panel = Real(kLastPanel);
  sRealFrameHook = reinterpret_cast<Hook>(frame->hook);
  sRealDescHook = reinterpret_cast<Hook>(desc->hook);
  const int16_t backNext = panel->next;
  sExt[0] = *frame;
  sExt[0].x = kRowX, sExt[0].y = kRowY, sExt[0].spriteId = 0;
  sExt[0].hook = reinterpret_cast<void*>(&FrameHook);
  sExt[1] = *base;
  sExt[1].x = static_cast<int16_t>(kRowX + 3), sExt[1].y = static_cast<int16_t>(kRowY + 3);
  sExt[1].flags &= static_cast<uint8_t>(~0x80);
  sExt[2] = *text;
  sExt[2].x = static_cast<int16_t>(kRowX + 7), sExt[2].y = static_cast<int16_t>(kRowY + 10), sExt[2].w = 136;
  sExt[2].msgId = 0;
  sExt[2].flags &= static_cast<uint8_t>(~0x80);
  sExt[2].hook = reinterpret_cast<void*>(&TextHook);
  for (Item& e : sExt) e.flags &= static_cast<uint8_t>(~0x40);
  panel->next = kBaseId;
  sExt[1].next = kTextId;
  sExt[2].next = kFrameId;
  sExt[0].next = backNext;
  frame->hook = reinterpret_cast<void*>(&FrameHook);
  desc->hook = reinterpret_cast<void*>(&DescHook);
  sLaidOut = true;
  std::fprintf(stderr, "[online lobby] VS MODE: third row added (menu 0x118)\n");
}

}

extern "C" void* PortXD_VsRowItem(int32_t menuID, uint32_t index) {
  if (menuID != kMenu || (!sLaidOut && !PortXD_VsRowEnabled())) return nullptr;
  LayOut();
  if (index == kFrameId) return &sExt[0];
  if (index == kBaseId) return &sExt[1];
  if (index == kTextId) return &sExt[2];
  return nullptr;
}
