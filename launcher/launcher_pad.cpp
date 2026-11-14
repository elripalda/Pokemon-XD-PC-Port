#include "launcher_pad.h"

#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace launcher::pad {

namespace ec = engine::controls;
namespace fs = std::filesystem;

namespace {

ImVec4 Hex(uint32_t rgb, float a = 1.0f) {
  return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}
ImU32 Col(uint32_t rgb, float a = 1.0f) { return ImGui::GetColorU32(Hex(rgb, a)); }

constexpr uint32_t kText = 0xEFEAFA, kDim = 0xACA4C4, kFaint = 0x7B7296, kAccent = 0x9B6BFF, kAccentHover = 0xB089FF;
constexpr uint32_t kPanel = 0x1A1329, kPanelHigh = 0x231A37, kLine = 0x302549;

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
fs::path PathOf(const std::string& utf8) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}
std::string Lower(std::string s) {
  for (char& c : s) {
    c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }
  return s;
}

bool IsSwitchLike(ec::Device d) { return d == ec::Device::Switch; }

}

void Input::Update(const std::vector<PadInfo>& pads, bool live) {
  prev_ = cur_;
  const uint64_t now = SDL_GetTicks();
  std::array<bool, kBtnCount> raw{};
  std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> now_{};
  activity_ = false;
  present_ = !pads.empty();
  bool stick = false;
  if (live) {
    for (const PadInfo& p : pads) {
      const auto B = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(p.pad, b); };
      const auto A = [&](SDL_GamepadAxis a) { return static_cast<float>(SDL_GetGamepadAxis(p.pad, a)) / 32767.0f; };
      const auto face = [&](ec::FaceLabel l) {
        return static_cast<SDL_GamepadButton>(ec::LabelPosition(l, p.device));
      };
      const float lx = A(SDL_GAMEPAD_AXIS_LEFTX), ly = A(SDL_GAMEPAD_AXIS_LEFTY);
      raw[static_cast<size_t>(Btn::Up)] |= B(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -0.55f;
      raw[static_cast<size_t>(Btn::Down)] |= B(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 0.55f;
      raw[static_cast<size_t>(Btn::Left)] |= B(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -0.55f;
      raw[static_cast<size_t>(Btn::Right)] |= B(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 0.55f;
      raw[static_cast<size_t>(Btn::Confirm)] |= B(face(ec::FaceLabel::A));
      raw[static_cast<size_t>(Btn::Cancel)] |= B(face(ec::FaceLabel::B));
      raw[static_cast<size_t>(Btn::X)] |= B(face(ec::FaceLabel::X));
      raw[static_cast<size_t>(Btn::Y)] |= B(face(ec::FaceLabel::Y));
      raw[static_cast<size_t>(Btn::LB)] |= B(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
      raw[static_cast<size_t>(Btn::RB)] |= B(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
      raw[static_cast<size_t>(Btn::LT)] |= A(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.5f;
      raw[static_cast<size_t>(Btn::RT)] |= A(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.5f;
      raw[static_cast<size_t>(Btn::Start)] |= B(SDL_GAMEPAD_BUTTON_START);
      raw[static_cast<size_t>(Btn::Back)] |= B(SDL_GAMEPAD_BUTTON_BACK);
      stick |= std::fabs(lx) > 0.6f || std::fabs(ly) > 0.6f;
      for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
        const bool down = B(static_cast<SDL_GamepadButton>(b));
        now_[static_cast<size_t>(b)] = now_[static_cast<size_t>(b)] || down;
        if (down && !rawPrev_[static_cast<size_t>(b)] && !deviceFixed_) {
          device_ = p.device;
        }
      }
    }
    for (size_t b = 0; b < now_.size(); b++) {
      activity_ |= now_[b] && !rawPrev_[b];
    }
    activity_ |= stick && !stickPrev_;
  }
  rawPrev_ = now_;
  stickPrev_ = stick;
  for (size_t b = 0; b < kBtnCount; b++) {
    if (virt_[b] > 0) {
      raw[b] = true;
      virt_[b]--;
    }
    cur_[b] = raw[b];
    repeat_[b] = false;
    if (cur_[b] && !prev_[b]) {
      since_[b] = last_[b] = now;
      repeat_[b] = true;
    } else if (cur_[b] && now - since_[b] > 420 && now - last_[b] >= 75) {
      last_[b] = now;
      repeat_[b] = true;
    }
  }
}

bool Input::AnyDown() const {
  for (bool b : cur_) {
    if (b) {
      return true;
    }
  }
  return false;
}

void Input::Feed(ImGuiIO& io, bool consume, bool tweaking) {
  if (Present()) {
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  }
  const auto key = [&](ImGuiKey k, bool down) { io.AddKeyEvent(k, down && !consume); };
  key(ImGuiKey_GamepadDpadUp, Down(Btn::Up));
  key(ImGuiKey_GamepadDpadDown, Down(Btn::Down));
  key(ImGuiKey_GamepadDpadLeft, Down(Btn::Left));
  key(ImGuiKey_GamepadDpadRight, Down(Btn::Right));
  key(ImGuiKey_GamepadFaceDown, Down(Btn::Confirm));
  key(ImGuiKey_GamepadFaceRight, Down(Btn::Cancel));
  key(ImGuiKey_GamepadL1, tweaking && Down(Btn::LB));
  key(ImGuiKey_GamepadR1, tweaking && Down(Btn::RB));
}

std::string ButtonName(Btn b, ec::Device d) {
  switch (b) {
  case Btn::Confirm: return ec::InputLabel(ec::Input::Label(ec::FaceLabel::A), d).text;
  case Btn::Cancel: return ec::InputLabel(ec::Input::Label(ec::FaceLabel::B), d).text;
  case Btn::X: return ec::InputLabel(ec::Input::Label(ec::FaceLabel::X), d).text;
  case Btn::Y: return ec::InputLabel(ec::Input::Label(ec::FaceLabel::Y), d).text;
  case Btn::LB: return ec::InputLabel(ec::Input::Button(ec::PadButton::LeftShoulder), d).text;
  case Btn::RB: return ec::InputLabel(ec::Input::Button(ec::PadButton::RightShoulder), d).text;
  case Btn::LT: return ec::InputLabel(ec::Input::Axis(ec::PadAxis::LeftTrigger, 1), d).text;
  case Btn::RT: return ec::InputLabel(ec::Input::Axis(ec::PadAxis::RightTrigger, 1), d).text;
  case Btn::Start: return ec::InputLabel(ec::Input::Button(ec::PadButton::Start), d).text;
  case Btn::Back: return ec::InputLabel(ec::Input::Button(ec::PadButton::Back), d).text;
  default: return "D-pad";
  }
}

float DrawButton(ImDrawList* dl, float left, float y, float size, Btn b, ec::Device device, ImFont* font) {
  const bool face = b == Btn::Confirm || b == Btn::Cancel || b == Btn::X || b == Btn::Y;
  const bool ps = device == ec::Device::PlayStation;
  const float r = size * 0.5f;
  const float fs = font->FontSize;
  if (face) {
    const ImVec2 c(left + r, y);
    if (dl == nullptr) {
      return size;
    }
    if (ps) {
      static const uint32_t colour[] = {0x6C9BE8, 0xE8707A, 0xD890C0, 0x5CC8A0};
      const int which = b == Btn::Confirm ? 0 : b == Btn::Cancel ? 1 : b == Btn::X ? 2 : 3;
      const ImU32 k = Col(colour[which]);
      dl->AddCircleFilled(c, r, Col(0x2B2640), 24);
      dl->AddCircle(c, r, Col(0x4A4266), 24, 1.2f);
      const float s = r * 0.48f, t = std::max(1.5f, size * 0.07f);
      switch (which) {
      case 0:
        dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), k, t);
        dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), k, t);
        break;
      case 1: dl->AddCircle(c, s * 1.05f, k, 20, t); break;
      case 2: dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), k, 0, 0, t); break;
      default:
        dl->AddTriangle(ImVec2(c.x, c.y - s * 1.1f), ImVec2(c.x + s * 1.1f, c.y + s * 0.8f),
                        ImVec2(c.x - s * 1.1f, c.y + s * 0.8f), k, t);
        break;
      }
      return size;
    }
    const bool xbox = device == ec::Device::Xbox || device == ec::Device::Generic;
    static const uint32_t xc[] = {0x62C46A, 0xE5605A, 0x5B8DEF, 0xF2C14E};
    const int which = b == Btn::Confirm ? 0 : b == Btn::Cancel ? 1 : b == Btn::X ? 2 : 3;
    const std::string name = ButtonName(b, device);
    dl->AddCircleFilled(c, r, xbox ? Col(0x241E33) : Col(0x3A3550), 24);
    dl->AddCircle(c, r, xbox ? Col(xc[which], 0.9f) : Col(0x6A6288), 24, 1.6f);
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, name.c_str());
    dl->AddText(font, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), xbox ? Col(xc[which]) : Col(kText),
                name.c_str());
    return size;
  }
  if (b == Btn::Up || b == Btn::Down || b == Btn::Left || b == Btn::Right) {
    if (dl != nullptr) {
      const ImVec2 c(left + r, y);
      const float a = r * 0.86f, w = r * 0.62f;
      const ImU32 k = Col(0xD9D2EC);
      dl->AddRectFilled(ImVec2(c.x - a, c.y - w * 0.5f), ImVec2(c.x + a, c.y + w * 0.5f), k, 2.0f);
      dl->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - a), ImVec2(c.x + w * 0.5f, c.y + a), k, 2.0f);
    }
    return size;
  }
  const std::string name = ButtonName(b, device);
  const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, name.c_str());
  const float w = std::max(size, ts.x + size * 0.55f);
  if (dl != nullptr) {
    const ImVec2 a(left, y - r), e(left + w, y + r);
    dl->AddRectFilled(a, e, Col(0x2B2640), r * 0.55f);
    dl->AddRect(a, e, Col(0x6A6288), r * 0.55f, 0, 1.4f);
    dl->AddText(font, fs, ImVec2(left + (w - ts.x) * 0.5f, y - ts.y * 0.5f), Col(kText), name.c_str());
  }
  return w;
}

float DrawPrompts(ImDrawList* dl, ImVec2 pos, float width, const std::vector<Prompt>& prompts, ec::Device device,
                  ImFont* font, float scale, ImU32 ink, ImU32 dim) {
  (void)dim;
  (void)width;
  const float size = font->FontSize * 1.45f;
  const float cy = pos.y + size * 0.5f;
  float x = pos.x;
  for (const Prompt& p : prompts) {
    x += DrawButton(dl, x, cy, size, p.button, device, font) + 7 * scale;
    const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, p.text.c_str());
    dl->AddText(font, font->FontSize, ImVec2(x, cy - ts.y * 0.5f), ink, p.text.c_str());
    x += ts.x + 26 * scale;
  }
  return size;
}

void FocusLastItem() {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  ImGuiWindow* w = g.CurrentWindow;
  if (w == nullptr || g.LastItemData.ID == 0) {
    return;
  }
  ImGui::SetNavWindow(w);
  ImGui::SetFocusID(g.LastItemData.ID, w);
  ImGui::SetNavCursorVisible(true);
}

void WarnAboutChildWindows() {
  static std::vector<std::string> said;
  ImGuiContext& g = *ImGui::GetCurrentContext();
  for (ImGuiWindow* w : g.Windows) {
    if (w->Active && (w->Flags & ImGuiWindowFlags_ChildWindow) && !(w->Flags & ImGuiWindowFlags_Popup) &&
        !(w->ChildFlags & ImGuiChildFlags_NavFlattened) && (w->DC.NavLayersActiveMask & 1) != 0 &&
        std::find(said.begin(), said.end(), w->Name) == said.end()) {
      said.push_back(w->Name);
      std::fprintf(stderr, "[launcher] pad: child window \"%s\" holds controls but is not NavFlattened: a controller can't reach them "
                   "(BeginChild needs ImGuiChildFlags_NavFlattened)\n", w->Name);
    }
  }
}

void EnterCurrentWindow() {
  ImGuiWindow* w = ImGui::GetCurrentContext()->CurrentWindow;
  if (w == nullptr) {
    return;
  }
  ImGui::SetNavWindow(w);
  ImGui::NavInitWindow(w, true);
  ImGui::SetNavCursorVisible(true);
}

std::string FocusInfo() {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  char buf[320];
  std::snprintf(buf, sizeof buf, "window \"%s\", item 0x%08X%s", g.NavWindow != nullptr ? g.NavWindow->Name : "(none)", g.NavId,
                g.NavCursorVisible ? "" : " (no highlight)");
  return buf;
}

bool InputText(const char* label, std::string* text, ImGuiInputTextFlags flags) {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  const ImGuiID id = ImGui::GetID(label);
  if (g.NavId == id && g.ActiveId != id && !(flags & ImGuiInputTextFlags_ReadOnly) &&
      ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) {
    ImGui::SetKeyboardFocusHere();
  }
  return ImGui::InputText(label, text, flags);
}

void Keyboard::Build() {
  rows_.clear();
  const auto C = [](const char* n, const char* s, const char* y) {
    Key k;
    k.normal = n;
    k.shifted = s;
    k.symbol = y;
    return k;
  };
  const auto Plain = [&](const char* letters, const char* upper, const char* sym) {
    std::vector<Key> row;
    for (size_t i = 0; letters[i] != 0; i++) {
      row.push_back(C(std::string(1, letters[i]).c_str(), std::string(1, upper[i]).c_str(), std::string(1, sym[i]).c_str()));
    }
    return row;
  };
  rows_.push_back(Plain("1234567890", "!@#$%^&*()", "1234567890"));
  rows_.push_back(Plain("qwertyuiop", "QWERTYUIOP", "!@#$%^&*()"));
  rows_.push_back(Plain("asdfghjkl'", "ASDFGHJKL\"", "-_=+[]{};:"));
  rows_.push_back(Plain("zxcvbnm,.?", "ZXCVBNM,.?", "'\",./\\|<>~"));
  std::vector<Key> last;
  const auto S = [](const char* label, Key::Kind kind, float w) {
    Key k;
    k.normal = k.shifted = k.symbol = label;
    k.kind = kind;
    k.width = w;
    return k;
  };
  last.push_back(S("Shift", Key::Kind::Shift, 1.5f));
  last.push_back(S("123 #", Key::Kind::Symbols, 1.5f));
  last.push_back(S("Space", Key::Kind::Space, 2.6f));
  last.push_back(S("<", Key::Kind::Left, 0.8f));
  last.push_back(S(">", Key::Kind::Right, 0.8f));
  last.push_back(S("Delete", Key::Kind::Back, 1.4f));
  last.push_back(S("Done", Key::Kind::Done, 1.4f));
  rows_.push_back(last);
}

void Keyboard::PressKey(const Key& k, ImGuiIO& io) {
  const auto tap = [&](ImGuiKey key) {
    io.AddKeyEvent(key, true);
    release_.push_back({key, false});
  };
  switch (k.kind) {
  case Key::Kind::Char: {
    const std::string& s = symbols_ ? k.symbol : shift_ ? k.shifted : k.normal;
    io.AddInputCharactersUTF8(s.c_str());
    if (shift_ && !symbols_) {
      shift_ = false;
    }
    break;
  }
  case Key::Kind::Shift: shift_ = !shift_; break;
  case Key::Kind::Symbols: symbols_ = !symbols_; break;
  case Key::Kind::Space: io.AddInputCharactersUTF8(" "); break;
  case Key::Kind::Left: tap(ImGuiKey_LeftArrow); break;
  case Key::Kind::Right: tap(ImGuiKey_RightArrow); break;
  case Key::Kind::Back: tap(ImGuiKey_Backspace); break;
  case Key::Kind::Done: tap(ImGuiKey_Enter); break;
  case Key::Kind::System:
    if (systemKeyboard_) {
      systemKeyboard_();
    }
    break;
  }
}

void Keyboard::Update(Input& in, ImGuiIO& io, bool wanted, uint64_t now) {
  if (rows_.empty()) {
    Build();
  }
  for (const Pending& p : release_) {
    io.AddKeyEvent(p.key, p.down);
  }
  release_.clear();
  if (wanted && !open_) {
    open_ = true;
    openedAt_ = now;
    waitRelease_ = true;
    shift_ = false;
    symbols_ = false;
    row_ = 1;
    col_ = 0;
  } else if (!wanted && open_) {
    open_ = false;
    scriptText_.clear();
  }
  if (!open_) {
    return;
  }
  if (waitRelease_) {
    waitRelease_ = in.AnyDown();
    return;
  }
  if (!scriptText_.empty()) {
    const char ch = scriptText_[0];
    scriptText_.erase(0, 1);
    if (ch == '\n') {
      io.AddKeyEvent(ImGuiKey_Enter, true);
      release_.push_back({ImGuiKey_Enter, false});
    } else {
      const std::string s(1, ch);
      io.AddInputCharactersUTF8(s.c_str());
    }
    return;
  }
  const auto centre = [&](int row, int col) {
    float x = 0;
    for (int i = 0; i < col; i++) {
      x += rows_[static_cast<size_t>(row)][static_cast<size_t>(i)].width;
    }
    return x + rows_[static_cast<size_t>(row)][static_cast<size_t>(col)].width * 0.5f;
  };
  const auto nearest = [&](int row, float cx) {
    int best = 0;
    float bd = FLT_MAX;
    for (int i = 0; i < static_cast<int>(rows_[static_cast<size_t>(row)].size()); i++) {
      const float d = std::fabs(centre(row, i) - cx);
      if (d < bd) {
        bd = d;
        best = i;
      }
    }
    return best;
  };
  const int nrows = static_cast<int>(rows_.size());
  if (in.PressedRepeat(Btn::Left)) {
    const int n = static_cast<int>(rows_[static_cast<size_t>(row_)].size());
    col_ = (col_ + n - 1) % n;
  }
  if (in.PressedRepeat(Btn::Right)) {
    const int n = static_cast<int>(rows_[static_cast<size_t>(row_)].size());
    col_ = (col_ + 1) % n;
  }
  if (in.PressedRepeat(Btn::Up)) {
    const float cx = centre(row_, col_);
    row_ = (row_ + nrows - 1) % nrows;
    col_ = nearest(row_, cx);
  }
  if (in.PressedRepeat(Btn::Down)) {
    const float cx = centre(row_, col_);
    row_ = (row_ + 1) % nrows;
    col_ = nearest(row_, cx);
  }
  col_ = std::min(col_, static_cast<int>(rows_[static_cast<size_t>(row_)].size()) - 1);
  if (in.Pressed(Btn::Confirm)) {
    PressKey(rows_[static_cast<size_t>(row_)][static_cast<size_t>(col_)], io);
  }
  if (in.PressedRepeat(Btn::X)) {
    PressKey(rows_[4][5], io);
  }
  if (in.Pressed(Btn::Y)) {
    PressKey(rows_[4][2], io);
  }
  if (in.PressedRepeat(Btn::LB)) {
    PressKey(rows_[4][3], io);
  }
  if (in.PressedRepeat(Btn::RB)) {
    PressKey(rows_[4][4], io);
  }
  if (in.Pressed(Btn::LT)) {
    shift_ = !shift_;
  }
  if (in.Pressed(Btn::RT)) {
    symbols_ = !symbols_;
  }
  if (in.Pressed(Btn::Start)) {
    PressKey(rows_[4][6], io);
  }
  if (in.Pressed(Btn::Cancel)) {
    io.AddKeyEvent(ImGuiKey_Escape, true);
    release_.push_back({ImGuiKey_Escape, false});
  }
}

void Keyboard::Draw(float scale, ImFont* font, ImFont* small, ec::Device device) {
  if (!open_ || rows_.empty()) {
    return;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float unit = 56 * scale, keyH = 46 * scale, gap = 6 * scale, pad = 14 * scale;
  float widest = 0;
  for (const auto& row : rows_) {
    float w = 0;
    for (const Key& k : row) {
      w += k.width * unit;
    }
    widest = std::max(widest, w);
  }
  const float rowW = 10 * unit;
  (void)widest;
  const float panelW = rowW + 2 * pad;
  const float previewH = 44 * scale, hintH = 34 * scale;
  const float panelH = pad + previewH + static_cast<float>(rows_.size()) * (keyH + gap) + hintH + pad * 0.5f;
  height_ = panelH + 18 * scale;
  const float x0 = vp->WorkPos.x + (vp->WorkSize.x - panelW) * 0.5f;
  const float y0 = vp->WorkPos.y + vp->WorkSize.y - panelH - 14 * scale;
  dl->AddRectFilled(ImVec2(vp->WorkPos.x, y0 - 12 * scale), ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y),
                    Col(0x06030C, 0.45f));
  dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + panelW, y0 + panelH), Col(0x1E1631), 14 * scale);
  dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + panelW, y0 + panelH), Col(kAccent, 0.7f), 14 * scale, 0, 1.5f * scale);
  std::string text;
  int caret = 0;
  {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (ImGuiInputTextState* st = ImGui::GetInputTextState(g.ActiveId)) {
      if (st->TextA.Size > 0) {
        text.assign(st->TextA.Data, static_cast<size_t>(std::max(0, std::min(st->TextLen, st->TextA.Size - 1))));
      }
      caret = std::max(0, std::min(st->GetCursorPos(), static_cast<int>(text.size())));
      if (st->Flags & ImGuiInputTextFlags_Password) {
        text.assign(text.size(), '*');
      }
    }
  }
  const ImVec2 pa(x0 + pad, y0 + pad * 0.6f), pe(x0 + panelW - pad, y0 + pad * 0.6f + previewH - 6 * scale);
  dl->AddRectFilled(pa, pe, Col(0x110B1C), 8 * scale);
  const std::string shown = text.substr(0, static_cast<size_t>(caret));
  const std::string after = text.substr(static_cast<size_t>(caret));
  const float tx = pa.x + 12 * scale;
  const float ty = pa.y + ((pe.y - pa.y) - font->FontSize) * 0.5f;
  dl->PushClipRect(pa, pe, true);
  const float wBefore = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, shown.c_str()).x;
  const float wAll = wBefore + font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, after.c_str()).x;
  const float room = pe.x - pa.x - 24 * scale;
  const float shift = wBefore > room ? wBefore - room : 0.0f;
  (void)wAll;
  dl->AddText(font, font->FontSize, ImVec2(tx - shift, ty), Col(kText), shown.c_str());
  dl->AddText(font, font->FontSize, ImVec2(tx - shift + wBefore, ty), Col(kText), after.c_str());
  if (static_cast<int>(ImGui::GetTime() * 2.0) % 2 == 0) {
    dl->AddRectFilled(ImVec2(tx - shift + wBefore, ty), ImVec2(tx - shift + wBefore + 2 * scale, ty + font->FontSize), Col(kAccentHover));
  }
  dl->PopClipRect();

  float y = y0 + pad + previewH;
  for (size_t r = 0; r < rows_.size(); r++) {
    float total = 0;
    for (const Key& k : rows_[r]) {
      total += k.width * unit;
    }
    float x = x0 + pad + (rowW - total) * 0.5f;
    for (size_t c = 0; c < rows_[r].size(); c++) {
      const Key& k = rows_[r][c];
      const float w = k.width * unit - gap;
      const bool sel = static_cast<int>(r) == row_ && static_cast<int>(c) == col_;
      const bool lit = (k.kind == Key::Kind::Shift && shift_) || (k.kind == Key::Kind::Symbols && symbols_);
      const bool done = k.kind == Key::Kind::Done;
      ImU32 fill = sel ? Col(kAccent) : done ? Col(0x3D2C70) : lit ? Col(0x4A3A75) : Col(0x2F2549);
      dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + keyH), fill, 8 * scale);
      if (sel) {
        dl->AddRect(ImVec2(x - 2 * scale, y - 2 * scale), ImVec2(x + w + 2 * scale, y + keyH + 2 * scale), Col(0xFFFFFF, 0.9f), 9 * scale, 0, 2 * scale);
      }
      const std::string& label = k.kind == Key::Kind::Char ? (symbols_ ? k.symbol : shift_ ? k.shifted : k.normal)
                                 : k.kind == Key::Kind::Symbols && symbols_ ? std::string("abc") : k.normal;
      const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, label.c_str());
      dl->AddText(font, font->FontSize, ImVec2(x + (w - ts.x) * 0.5f, y + (keyH - ts.y) * 0.5f), Col(kText), label.c_str());
      x += k.width * unit;
    }
    y += keyH + gap;
  }
  const std::vector<Prompt> hint = {{Btn::Confirm, "Type"}, {Btn::X, "Delete"}, {Btn::Y, "Space"}, {Btn::LT, "Shift"},
                                    {Btn::RT, "123"},       {Btn::Start, "Done"}, {Btn::Cancel, "Cancel"}};
  const float hs = small->FontSize * 1.35f;
  float total = 0;
  for (const Prompt& p : hint) {
    total += DrawButton(nullptr, 0, 0, hs, p.button, device, small) + 6 * scale +
             small->CalcTextSizeA(small->FontSize, FLT_MAX, 0, p.text.c_str()).x + 14 * scale;
  }
  float hx = x0 + (panelW - total) * 0.5f;
  const float hy = y + hintH * 0.5f - 2 * scale;
  for (const Prompt& p : hint) {
    hx += DrawButton(dl, hx, hy, hs, p.button, device, small) + 6 * scale;
    const ImVec2 ts = small->CalcTextSizeA(small->FontSize, FLT_MAX, 0, p.text.c_str());
    dl->AddText(small, small->FontSize, ImVec2(hx, hy - ts.y * 0.5f), Col(kDim), p.text.c_str());
    hx += ts.x + 14 * scale;
  }
}

namespace {

std::vector<fs::path> Children(const fs::path& dir) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
       it.increment(ec)) {
    out.push_back(it->path());
  }
  return out;
}

fs::path Home() {
#ifdef _WIN32
  const char* h = std::getenv("USERPROFILE");
#else
  const char* h = std::getenv("HOME");
#endif
  return h != nullptr ? PathOf(h) : fs::path();
}

bool IsDir(const fs::path& p) {
  std::error_code ec;
  return fs::is_directory(p, ec);
}

}

std::vector<FileBrowser::Place> FileBrowser::DefaultPlaces() {
  std::vector<Place> out;
  const fs::path home = Home();
  if (!home.empty() && IsDir(home)) {
    out.push_back({"Home", home});
    for (const char* n : {"Desktop", "Documents", "Downloads"}) {
      if (IsDir(home / n)) {
        out.push_back({n, home / n});
      }
    }
  }
#ifdef _WIN32
  for (char letter = 'C'; letter <= 'Z'; letter++) {
    const fs::path root = fs::path(std::string(1, letter) + ":\\");
    if (IsDir(root)) {
      out.push_back({std::string(1, letter) + ":", root});
    }
  }
#else
#ifdef __APPLE__
  for (const fs::path& v : Children("/Volumes")) {
    out.push_back({Utf8(v.filename()), v});
  }
#else
  for (const char* base : {"/run/media", "/media"}) {
    for (const fs::path& u : Children(base)) {
      if (!IsDir(u)) {
        continue;
      }
      const std::vector<fs::path> inner = Children(u);
      bool any = false;
      for (const fs::path& m : inner) {
        if (IsDir(m) && std::string(base) == "/run/media") {
          out.push_back({Utf8(m.filename()), m});
          any = true;
        }
      }
      if (!any) {
        out.push_back({Utf8(u.filename()), u});
      }
    }
  }
  if (IsDir("/mnt")) {
    for (const fs::path& m : Children("/mnt")) {
      if (IsDir(m)) {
        out.push_back({Utf8(m.filename()), m});
      }
    }
  }
#endif
  out.push_back({"Computer", fs::path("/")});
#endif
  return out;
}

void FileBrowser::LoadRecent() {
  if (recentLoaded_) {
    return;
  }
  recentLoaded_ = true;
  recent_.clear();
  if (recentFile_.empty()) {
    return;
  }
#ifdef _WIN32
  std::FILE* f = _wfopen(recentFile_.c_str(), L"rb");
#else
  std::FILE* f = std::fopen(recentFile_.c_str(), "rb");
#endif
  if (f == nullptr) {
    return;
  }
  std::string text;
  char buf[512];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
    text.append(buf, n);
  }
  std::fclose(f);
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line = text.substr(start, end - start);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (!line.empty() && IsDir(PathOf(line))) {
      recent_.push_back(PathOf(line));
    }
    start = end + 1;
  }
}

void FileBrowser::AddRecent(const fs::path& folder) {
  if (folder.empty()) {
    return;
  }
  LoadRecent();
  recent_.erase(std::remove(recent_.begin(), recent_.end(), folder), recent_.end());
  recent_.insert(recent_.begin(), folder);
  if (recent_.size() > 8) {
    recent_.resize(8);
  }
  if (recentFile_.empty()) {
    return;
  }
  std::error_code ec;
  fs::create_directories(recentFile_.parent_path(), ec);
  std::string text;
  for (const fs::path& p : recent_) {
    text += Utf8(p) + "\n";
  }
#ifdef _WIN32
  std::FILE* f = _wfopen(recentFile_.c_str(), L"wb");
#else
  std::FILE* f = std::fopen(recentFile_.c_str(), "wb");
#endif
  if (f != nullptr) {
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
  }
}

bool FileBrowser::Matches(const std::string& name) const {
  if (req_.filters.empty() || filter_ >= req_.filters.size() || req_.filters[filter_].exts.empty()) {
    return true;
  }
  const size_t dot = name.rfind('.');
  if (dot == std::string::npos) {
    return false;
  }
  const std::string ext = Lower(name.substr(dot + 1));
  for (const std::string& e : req_.filters[filter_].exts) {
    if (e == ext) {
      return true;
    }
  }
  return false;
}

void FileBrowser::Look() {
  entries_.clear();
  error_.clear();
  rootList_ = folder_.empty();
  if (rootList_) {
    for (const Place& p : places_) {
#ifdef _WIN32
      if (p.label.size() == 2 && p.label[1] == ':') {
        entries_.push_back({Utf8(p.path), true, 0});
      }
#else
      (void)p;
#endif
    }
    return;
  }
  std::error_code ec;
  if (!fs::is_directory(folder_, ec)) {
    error_ = "That folder can't be opened.";
    return;
  }
  for (const fs::path& p : Children(folder_)) {
    const std::string name = Utf8(p.filename());
    if (name.empty() || name[0] == '.' || name[0] == '$' || name == "System Volume Information") {
      continue;
    }
    std::error_code e2;
    const bool dir = fs::is_directory(p, e2);
    if (dir) {
      entries_.push_back({name, true, 0});
    } else if (Matches(name)) {
      const uintmax_t size = fs::file_size(p, e2);
      entries_.push_back({name, false, e2 ? 0 : static_cast<uint64_t>(size)});
    }
  }
  std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
    if (a.folder != b.folder) {
      return a.folder;
    }
    return Lower(a.name) < Lower(b.name);
  });
}

void FileBrowser::Go(const fs::path& folder, const std::string& select) {
  folder_ = folder;
  select_ = select;
  focusList_ = true;
  Look();
}

void FileBrowser::Open(const Request& request, std::function<void(const std::string&)> done) {
  req_ = request;
  done_ = std::move(done);
  open_ = opening_ = true;
  hasResult_ = false;
  framesOpen_ = 0;
  filter_ = 0;
  places_ = DefaultPlaces();
  LoadRecent();
  fs::path start = req_.start;
  name_.clear();
  select_.clear();
  std::error_code ec;
  if (!start.empty() && !IsDir(start)) {
    if (req_.mode == Mode::SaveFile) {
      name_ = Utf8(start.filename());
    } else {
      select_ = Utf8(start.filename());
    }
    start = start.parent_path();
  }
  while (!start.empty() && !IsDir(start) && start.has_parent_path() && start.parent_path() != start) {
    start = start.parent_path();
  }
  if (start.empty() || !IsDir(start)) {
    start = !recent_.empty() ? recent_.front() : Home();
  }
  if (start.empty() || !IsDir(start)) {
    start = fs::current_path(ec);
  }
  folder_ = start;
  focusList_ = true;
  Look();
}

void FileBrowser::Finish(const std::string& result) {
  open_ = false;
  hasResult_ = true;
  result_ = result;
  if (!result.empty()) {
    AddRecent(req_.mode == Mode::OpenFolder ? PathOf(result) : PathOf(result).parent_path());
  }
  ImGui::CloseCurrentPopup();
}

void FileBrowser::Poll() {
  if (hasResult_) {
    hasResult_ = false;
    std::function<void(const std::string&)> done = std::move(done_);
    done_ = nullptr;
    if (done) {
      done(result_);
    }
  }
}

std::string FileBrowser::Where() const { return rootList_ ? std::string("(drives)") : Utf8(folder_); }

std::vector<Prompt> FileBrowser::Prompts() const {
  std::vector<Prompt> p = {{Btn::Confirm, "Open"}, {Btn::Cancel, "Up"}, {Btn::X, "Cancel"}};
  if (req_.filters.size() > 1) {
    p.push_back({Btn::Y, "Kind"});
  }
  if (req_.mode == Mode::OpenFolder) {
    p.push_back({Btn::Start, "Use this folder"});
  }
  return p;
}

void FileBrowser::Draw(float scale, ImFont* heading, ImFont* small, const Input& in, bool controllerPrompts) {
  (void)controllerPrompts;
  (void)small;
  if (!open_) {
    return;
  }
  const char* kId = "###filebrowser";
  if (opening_) {
    ImGui::OpenPopup(kId);
    opening_ = false;
    framesOpen_ = 0;
  }
  framesOpen_++;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 size(std::min(vp->WorkSize.x - 30 * scale, 1180 * scale), vp->WorkSize.y - 30 * scale - (controllerPrompts ? 44 * scale : 0));
  ImGui::SetNextWindowSize(size);
  ImGui::SetNextWindowPos(ImVec2(vp->GetCenter().x, vp->WorkPos.y + 15 * scale), 0, ImVec2(0.5f, 0.0f));
  if (!ImGui::BeginPopupModal(kId, nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoSavedSettings)) {
    if (open_) {
      open_ = false;
      hasResult_ = true;
      result_.clear();
    }
    return;
  }
  ImGui::PushFont(heading);
  ImGui::TextUnformatted(req_.title.empty() ? "Choose a file" : req_.title.c_str());
  ImGui::PopFont();
  {
    ImGui::PushStyleColor(ImGuiCol_Text, Hex(kDim));
    const std::string where = rootList_ ? std::string("This computer") : Utf8(folder_);
    ImGui::TextUnformatted(where.c_str());
    ImGui::PopStyleColor();
  }
  const bool saving = req_.mode == Mode::SaveFile;
  const float bottomH = ImGui::GetFrameHeight() * (saving ? 2.0f : 1.0f) + ImGui::GetStyle().ItemSpacing.y * (saving ? 3.0f : 2.0f);
  const float listH = ImGui::GetContentRegionAvail().y - bottomH;
  const bool typing = ImGui::GetActiveID() != 0;
  std::string goTo;
  std::string pick;
  bool goUpNow = false;

  ImGui::BeginChild("##places", ImVec2(250 * scale, listH), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
  const auto Row = [&](const std::string& label, const std::string& sub, bool folderIcon) {
    const float h = ImGui::GetFrameHeight() * 1.05f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool hit = ImGui::Selectable(("##" + label + sub).c_str(), false, 0, ImVec2(0, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ix = p.x + 8 * scale, iy = p.y + h * 0.5f;
    if (folderIcon) {
      dl->AddRectFilled(ImVec2(ix, iy - 7 * scale), ImVec2(ix + 8 * scale, iy - 3 * scale), Col(kAccentHover), 2 * scale);
      dl->AddRectFilled(ImVec2(ix, iy - 5 * scale), ImVec2(ix + 20 * scale, iy + 8 * scale), Col(kAccent), 3 * scale);
    }
    dl->AddText(ImVec2(p.x + 36 * scale, p.y + (h - ImGui::GetFontSize()) * 0.5f), Col(kText), label.c_str());
    return hit;
  };
  if (!recent_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, Hex(kFaint));
    ImGui::TextUnformatted("RECENT");
    ImGui::PopStyleColor();
    for (const fs::path& r : recent_) {
      std::string name = Utf8(r.filename());
      if (name.empty()) {
        name = Utf8(r);
      }
      if (Row(name, Utf8(r), true)) {
        goTo = Utf8(r);
      }
    }
    ImGui::Spacing();
  }
  ImGui::PushStyleColor(ImGuiCol_Text, Hex(kFaint));
  ImGui::TextUnformatted("PLACES");
  ImGui::PopStyleColor();
  for (const Place& p : places_) {
    if (Row(p.label, Utf8(p.path), true)) {
      goTo = Utf8(p.path);
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();

  ImGui::BeginChild("##files", ImVec2(0, listH), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
  bool focusDone = false;
  const auto Item = [&](const std::string& name, bool folder, uint64_t bytes, bool up) {
    const float h = ImGui::GetFrameHeight() * 1.05f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool hit = ImGui::Selectable(("##" + name).c_str(), false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ix = p.x + 8 * scale, iy = p.y + h * 0.5f;
    if (up) {
      dl->AddTriangleFilled(ImVec2(ix + 2 * scale, iy + 5 * scale), ImVec2(ix + 18 * scale, iy + 5 * scale), ImVec2(ix + 10 * scale, iy - 7 * scale), Col(kDim));
    } else if (folder) {
      dl->AddRectFilled(ImVec2(ix, iy - 7 * scale), ImVec2(ix + 8 * scale, iy - 3 * scale), Col(kAccentHover), 2 * scale);
      dl->AddRectFilled(ImVec2(ix, iy - 5 * scale), ImVec2(ix + 20 * scale, iy + 8 * scale), Col(kAccent), 3 * scale);
    } else {
      dl->AddRectFilled(ImVec2(ix + 3 * scale, iy - 8 * scale), ImVec2(ix + 17 * scale, iy + 8 * scale), Col(0x4A4266), 2 * scale);
      dl->AddRect(ImVec2(ix + 3 * scale, iy - 8 * scale), ImVec2(ix + 17 * scale, iy + 8 * scale), Col(kDim, 0.8f), 2 * scale);
    }
    dl->AddText(ImVec2(p.x + 36 * scale, p.y + (h - ImGui::GetFontSize()) * 0.5f), Col(up ? kDim : kText), name.c_str());
    if (!folder && !up) {
      char text[48];
      if (bytes >= (1ull << 30)) {
        std::snprintf(text, sizeof text, "%.1f GB", static_cast<double>(bytes) / static_cast<double>(1ull << 30));
      } else if (bytes >= (1ull << 20)) {
        std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(bytes) / static_cast<double>(1ull << 20));
      } else {
        std::snprintf(text, sizeof text, "%.1f KB", static_cast<double>(bytes) / 1024.0);
      }
      const ImVec2 ts = ImGui::CalcTextSize(text);
      dl->AddText(ImVec2(p.x + ImGui::GetContentRegionAvail().x - ts.x - 10 * scale, p.y + (h - ts.y) * 0.5f), Col(kFaint), text);
    }
    if (focusList_ && framesOpen_ >= 2 && !focusDone && ((!select_.empty() && name == select_) || (select_.empty() && !up && !focusDone))) {
      FocusLastItem();
      ImGui::SetScrollHereY(0.5f);
      focusDone = true;
    }
    return hit;
  };
  const bool canUp = !rootList_ && !folder_.empty();
  if (canUp) {
    if (Item("(up one folder)", true, 0, true)) {
      goUpNow = true;
    }
  }
  for (const auto& e : entries_) {
    if (Item(e.name, e.folder, e.size, false)) {
      if (e.folder) {
        goTo = rootList_ ? e.name : Utf8(folder_ / PathOf(e.name));
      } else if (saving) {
        name_ = e.name;
      } else if (req_.mode == Mode::OpenFile) {
        pick = Utf8(folder_ / PathOf(e.name));
      }
    }
  }
  if (!focusDone && focusList_ && framesOpen_ >= 2) {
    focusDone = true;
  }
  if (entries_.empty() && !error_.empty()) {
    ImGui::TextUnformatted(error_.c_str());
  } else if (entries_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, Hex(kFaint));
    ImGui::TextUnformatted(req_.mode == Mode::OpenFolder ? "No folders here." : "Nothing of this kind here.");
    ImGui::PopStyleColor();
  }
  ImGui::EndChild();
  if (focusDone) {
    focusList_ = false;
    select_.clear();
  }

  if (saving) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("File name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(200.0f * scale, ImGui::GetContentRegionAvail().x - 330 * scale));
    InputText("##savename", &name_);
  }
  if (req_.filters.size() > 1) {
    const std::string label = "Showing: " + (filter_ < req_.filters.size() ? req_.filters[filter_].name : std::string());
    if (ImGui::Button(label.c_str())) {
      filter_ = (filter_ + 1) % req_.filters.size();
      Look();
    }
    ImGui::SameLine();
  }
  if (saving) {
    const bool usable = !name_.empty() && !folder_.empty();
    if (!usable) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save here")) {
      pick = Utf8(folder_ / PathOf(name_));
    }
    if (!usable) {
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
  } else if (req_.mode == Mode::OpenFolder) {
    if (ImGui::Button("Use this folder") && !folder_.empty()) {
      pick = Utf8(folder_);
    }
    ImGui::SameLine();
  }
  if (ImGui::Button("Cancel")) {
    Finish("");
  }

  if (!typing && open_) {
    if (in.Pressed(Btn::X)) {
      Finish("");
    } else if (in.Pressed(Btn::Y) && req_.filters.size() > 1) {
      filter_ = (filter_ + 1) % req_.filters.size();
      Look();
    } else if (in.Pressed(Btn::Start) && req_.mode == Mode::OpenFolder && !folder_.empty()) {
      pick = Utf8(folder_);
    } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
               ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
      goUpNow = true;
      if (rootList_ || folder_.empty() || (folder_.parent_path() == folder_ && folder_.root_path() != folder_)) {
        Finish("");
        goUpNow = false;
      }
    }
  }
  if (open_ && !pick.empty()) {
    Finish(pick);
  } else if (open_ && goUpNow) {
    const fs::path parent = folder_.parent_path();
    const std::string was = Utf8(folder_.filename());
    if (rootList_) {
    } else if (parent == folder_ || parent.empty()) {
#ifdef _WIN32
      Go(fs::path(), Utf8(folder_));
#endif
    } else {
      Go(parent, was);
    }
  } else if (open_ && !goTo.empty()) {
    Go(PathOf(goTo));
  }
  ImGui::EndPopup();
}

}
