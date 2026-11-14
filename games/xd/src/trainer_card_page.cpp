#include "trainer_card_game.h"
#include "xd_ui.h"

#include <i18n.h>

#include <SDL3/SDL_surface.h>

#include <aurora/imgui.h>
#include <dolphin/pad.h>
#include <imgui.h>
#include <port_menu.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace xd::card {
namespace {

using engine::i18n::Tr;

enum class Mode { Main, Titles, Models, Keyboard };

struct Page {
  Card card;
  int row = 0;
  Mode mode = Mode::Main;
  int titleCursor = 0, modelCursor = 0, keyCursor = 0;
  std::vector<ModelEntry> models;
  std::map<std::string, ImTextureID> textures;
  std::map<std::string, bool> triedTexture;
  bool saved = false;
} P;

std::string Utf8(const std::u16string& s) {
  std::string out;
  for (char16_t c : s) {
    if (c == 0x201C || c == 0x201D) out += '"';
    else if (c == 0x2018) out += '\'';
    else if (c == 0x2030) out += "..";
    else if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    else { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
  }
  return out;
}

ImTextureID TextureFor(const ModelEntry& m) {
  const std::string key = (m.model.source == Source::Xd ? "x" : "c") + m.model.id;
  auto it = P.textures.find(key);
  if (it != P.textures.end()) return it->second;
  if (P.triedTexture[key]) return ImTextureID{};
  P.triedTexture[key] = true;
  SDL_Surface* loaded = SDL_LoadPNG(m.icon.string().c_str());
  SDL_Surface* rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
  ImTextureID id{};
  if (rgba && rgba->pitch == rgba->w * 4) {
    id = aurora_imgui_add_texture(static_cast<uint32_t>(rgba->w), static_cast<uint32_t>(rgba->h), rgba->pixels);
    P.textures[key] = id;
  }
  if (rgba) SDL_DestroySurface(rgba);
  if (loaded) SDL_DestroySurface(loaded);
  return id;
}

int ModelIndexOf(const Model& m) {
  for (size_t i = 0; i < P.models.size(); i++) if (P.models[i].model == m) return static_cast<int>(i);
  return -1;
}

void OpenPage(void*) {
  P = Page();
  P.card = MyCard();
  P.models = Models();
  P.row = 0;
  const std::vector<TitleEntry>& titles = Titles();
  for (size_t i = 0; i < titles.size(); i++) if (titles[i].kind == P.card.title) P.titleCursor = static_cast<int>(i) + 1;
  const int mi = ModelIndexOf(P.card.model);
  P.modelCursor = mi < 0 ? 0 : mi + 1;
}

void ClosePage(void*) {
  if (!SaveMyCard(P.card)) std::fprintf(stderr, "[trainer card] the card could not be saved\n");
  else std::fprintf(stderr, "[trainer card] saved: %s\n", Encode(P.card).c_str());
}

std::string TitleText() {
  if (P.card.title < 0) return Tr("(the game's own)");
  for (const TitleEntry& t : Titles()) if (t.kind == P.card.title) return Utf8(t.text);
  return Tr("(unknown)");
}
std::string ModelLabel() {
  if (P.card.model.id == "0" && P.card.model.source == Source::Xd) return Tr("(the game's own)");
  const int i = ModelIndexOf(P.card.model);
  return i < 0 ? std::string(Tr("(unknown)")) : P.models[static_cast<size_t>(i)].label;
}

struct View { float x, y, s; };
View V;
ImVec2 At(float x, float y) { return ImVec2(V.x + x * V.s, V.y + y * V.s); }
void DrawSign(ImDrawList* dl, float x, float y, float size, ImU32 col, bool male) {
  const float r = 0.2f * size, th = 1.6f;
  const float cx = x + 0.38f * size, cy = y + (male ? 0.62f : 0.45f) * size;
  dl->AddCircle(At(cx, cy), r * V.s, col, 16, th * V.s);
  if (male) {
    const float ex = cx + r * 0.71f, ey = cy - r * 0.71f, tx = x + 0.78f * size, ty = y + 0.18f * size;
    dl->AddLine(At(ex, ey), At(tx, ty), col, th * V.s);
    dl->AddLine(At(tx, ty), At(tx - 0.2f * size, ty), col, th * V.s);
    dl->AddLine(At(tx, ty), At(tx, ty + 0.2f * size), col, th * V.s);
  } else {
    const float by = cy + r, ey = y + 0.95f * size;
    dl->AddLine(At(cx, by), At(cx, ey), col, th * V.s);
    dl->AddLine(At(cx - 0.12f * size, by + 0.17f * size), At(cx + 0.12f * size, by + 0.17f * size), col, th * V.s);
  }
}
float TextWidth(float size, const std::string& t) { return xd::ui::Width(t, size / 22.f); }
void Text(ImDrawList* dl, float x, float y, float size, ImU32 col, const std::string& t) {
  std::string seg;
  float cx = x;
  const uint8_t rgba[4] = {static_cast<uint8_t>(col & 0xFF), static_cast<uint8_t>((col >> 8) & 0xFF), static_cast<uint8_t>((col >> 16) & 0xFF),
                           static_cast<uint8_t>((col >> 24) & 0xFF)};
  auto flush = [&] {
    if (seg.empty()) return;
    xd::ui::Text(dl, cx, y, seg, rgba, size / 22.f);
    cx += TextWidth(size, seg);
    seg.clear();
  };
  for (size_t i = 0; i < t.size(); i++) {
    if (i + 2 < t.size() && static_cast<unsigned char>(t[i]) == 0xE2 && static_cast<unsigned char>(t[i + 1]) == 0x99 &&
        (static_cast<unsigned char>(t[i + 2]) == 0x82 || static_cast<unsigned char>(t[i + 2]) == 0x80)) {
      flush();
      DrawSign(dl, cx, y, size, col, static_cast<unsigned char>(t[i + 2]) == 0x82);
      cx += 0.9f * size;
      i += 2;
    } else {
      seg += t[i];
    }
  }
  flush();
}

const char* const kKeysExtra[] = {"DEL", "OK"};

bool Frame(void*, const PortMenuInput* in) {
  PortMenu_PictureRect(&V.x, &V.y, &V.s);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImU32 white = IM_COL32(240, 240, 240, 255), dim = IM_COL32(192, 192, 200, 255), gold = IM_COL32(255, 216, 104, 255);
  namespace ui = xd::ui;
  ui::Backdrop(dl);
  ui::Header(dl, Tr("TRAINER CARD"));
  const std::vector<TitleEntry>& titles = Titles();
  const bool up = in->repeat & PAD_BUTTON_UP, down = in->repeat & PAD_BUTTON_DOWN, left = in->repeat & PAD_BUTTON_LEFT, right = in->repeat & PAD_BUTTON_RIGHT;
  const bool a = in->pressed & PAD_BUTTON_A, b = in->pressed & PAD_BUTTON_B;

  const int mi = ModelIndexOf(P.card.model);
  ImTextureID tex = mi >= 0 ? TextureFor(P.models[static_cast<size_t>(mi)]) : ImTextureID{};
  ui::Window(dl, 372.f, 72.f, 608.f, 380.f);
  dl->AddRectFilled(At(396, 82), At(584, 270), IM_COL32(0, 0, 0, 255), 4 * V.s);
  if (tex) dl->AddImage(tex, At(396, 82), At(584, 270));
  else ui::Para(dl, 404.f, 160.f, 172.f, mi < 0 ? Tr("the game's own model") : Tr("no picture made yet"), ui::kSoft, 0.64f, 16.f, 3);
  const std::string who = (P.card.title >= 0 ? TitleText() + " " : std::string()) + (P.card.name.empty() ? std::string(Tr("(name)")) : Utf8(P.card.name));
  Text(dl, 398, 290, 16, white, who);
  Text(dl, 398, 316, 14, dim, Tr("would like to battle!"));
  ui::Window(dl, 36.f, 72.f, 368.f, 436.f);

  if (P.mode == Mode::Main) {
    const char* names[4] = {Tr("TITLE"), Tr("NAME"), Tr("CHARACTER"), Tr("DONE")};
    const std::string values[4] = {TitleText(), Utf8(P.card.name) + "_", ModelLabel(), ""};
    for (int i = 0; i < 4; i++) {
      const float y = 90 + i * 52;
      if (i == 3) { ui::Button(dl, 92.f, y + 4.f, 220.f, names[i], i == P.row); continue; }
      if (i == P.row) ui::RowSel(dl, 42.f, 362.f, y - 6, 46.f);
      Text(dl, 70, y - 1, 15, i == P.row ? gold : dim, names[i]);
      Text(dl, 70, y + 18, 17, white, values[i]);
    }
    ui::HintBar(dl, Tr("Up/Down: row   A: change   B: done"));
    if (up) P.row = (P.row + 3) % 4;
    if (down) P.row = (P.row + 1) % 4;
    if (a) {
      if (P.row == 0) P.mode = Mode::Titles;
      else if (P.row == 1) { P.mode = Mode::Keyboard; P.keyCursor = 0; }
      else if (P.row == 2) P.mode = Mode::Models;
      else return false;
    }
    if (b) return false;
    return true;
  }

  if (P.mode == Mode::Titles) {
    const int n = static_cast<int>(titles.size()) + 1;
    if (up) P.titleCursor = (P.titleCursor + n - 1) % n;
    if (down) P.titleCursor = (P.titleCursor + 1) % n;
    if (left) P.titleCursor = std::max(0, P.titleCursor - 8);
    if (right) P.titleCursor = std::min(n - 1, P.titleCursor + 8);
    const int rows = 12, first = std::max(0, std::min(P.titleCursor - rows / 2, n - rows));
    for (int r = 0; r < rows && first + r < n; r++) {
      const int i = first + r;
      const float y = 84 + r * 29;
      if (i == P.titleCursor) ui::RowSel(dl, 42.f, 362.f, y - 3, 27.f);
      Text(dl, 66, y, 16, i == P.titleCursor ? white : dim, i == 0 ? std::string("(the game's own)") : Utf8(titles[static_cast<size_t>(i) - 1].text));
    }
    ui::HintBar(dl, Tr("A: choose   B: back"));
    P.card.title = P.titleCursor == 0 ? kTitleDefault : titles[static_cast<size_t>(P.titleCursor) - 1].kind;
    if (a || b) P.mode = Mode::Main;
    return true;
  }
  if (P.mode == Mode::Models) {
    const int n = static_cast<int>(P.models.size()) + 1, cols = 4;
    if (left) P.modelCursor = (P.modelCursor + n - 1) % n;
    if (right) P.modelCursor = (P.modelCursor + 1) % n;
    if (up) P.modelCursor = std::max(0, P.modelCursor - cols);
    if (down) P.modelCursor = std::min(n - 1, P.modelCursor + cols);
    const int vrows = 4, crow = P.modelCursor / cols, firstRow = std::max(0, crow - vrows + 1);
    for (int r = 0; r < vrows; r++) {
      for (int c = 0; c < cols; c++) {
        const int i = (firstRow + r) * cols + c;
        if (i >= n) break;
        const float x = 62 + c * 72, y = 86 + r * 76;
        if (i == P.modelCursor) dl->AddRect(At(x - 4, y - 4), At(x + 68, y + 68), IM_COL32(0xFF, 0xD8, 0x68, ui::Pulse()), 5 * V.s, 0, 3 * V.s);
        dl->AddRectFilled(At(x, y), At(x + 64, y + 64), IM_COL32(0, 0, 0, 255), 3 * V.s);
        if (i == 0) { Text(dl, x + 6, y + 26, 11, white, Tr("game's own")); continue; }
        const ModelEntry& m = P.models[static_cast<size_t>(i) - 1];
        if (ImTextureID t = TextureFor(m)) dl->AddImage(t, At(x, y), At(x + 64, y + 64));
        else Text(dl, x + 4, y + 28, 10, dim, m.model.source == Source::Xd ? "XD " + m.model.id : m.model.id);
      }
    }
    if (P.modelCursor > 0) Text(dl, 62, 400, 14, white, P.models[static_cast<size_t>(P.modelCursor) - 1].label);
    ui::HintBar(dl, Tr("A: choose   B: back"));
    P.card.model = P.modelCursor == 0 ? Model() : P.models[static_cast<size_t>(P.modelCursor) - 1].model;
    if (a || b) P.mode = Mode::Main;
    return true;
  }
  const std::u16string& chars = NameChars();
  const int total = static_cast<int>(chars.size()) + 2, cols = 10;
  if (left) P.keyCursor = (P.keyCursor + total - 1) % total;
  if (right) P.keyCursor = (P.keyCursor + 1) % total;
  if (up) P.keyCursor = P.keyCursor - cols >= 0 ? P.keyCursor - cols : P.keyCursor;
  if (down) P.keyCursor = P.keyCursor + cols < total ? P.keyCursor + cols : P.keyCursor;
  for (int i = 0; i < total; i++) {
    const float x = 64 + (i % cols) * 29, y = 88 + (i / cols) * 34;
    if (i == P.keyCursor) ui::RowSel(dl, x - 5, x + 25, y - 4, 30.f, false);
    std::string label;
    if (i < static_cast<int>(chars.size())) label = Utf8(std::u16string(1, chars[static_cast<size_t>(i)]));
    else label = Tr(kKeysExtra[i - static_cast<int>(chars.size())]);
    if (label == " ") label = "_";
    Text(dl, x, y, label.size() > 1 ? 11.f : 18.f, i == P.keyCursor ? white : dim, label);
  }
  ui::HintBar(dl, Tr("A: type   B: delete   choose OK when done"));
  if (b && !P.card.name.empty()) P.card.name.pop_back();
  if (a) {
    if (P.keyCursor < static_cast<int>(chars.size())) {
      if (P.card.name.size() < kMaxName) P.card.name += chars[static_cast<size_t>(P.keyCursor)];
    } else if (P.keyCursor == static_cast<int>(chars.size())) {
      if (!P.card.name.empty()) P.card.name.pop_back();
    } else {
      P.mode = Mode::Main;
    }
  }
  return true;
}

const PortMenuPage kPage = {"trainer card", OpenPage, Frame, ClosePage, nullptr};

}
}

extern "C" {
const PortMenuPage* PortXD_TrainerCardPage(void) { return &xd::card::kPage; }
bool PortXD_OpenTrainerCardPage(void) { return PortMenu_Open(&xd::card::kPage); }
void PortXD_TrainerCardScriptPage(void) {
  const char* v = std::getenv("XD_TRAINER_CARD_PAGE");
  if (v && v[0] == '1') PortMenu_SetScriptPage(&xd::card::kPage);
}
}
