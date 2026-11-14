#include <port_speedrun.h>

#include <settings.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>

namespace {

namespace es = engine::settings;

std::mutex sLock;
PortSpeedrunView sView{};

int IntSetting(const char* id, int fallback) {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find(id);
  return i == es::kNoSetting ? fallback : reg.ValueOf(i).i;
}

}

extern "C" void PortSpeedrun_SetView(const PortSpeedrunView* view) {
  std::lock_guard<std::mutex> lock(sLock);
  if (view == nullptr) {
    sView.visible = 0;
  } else {
    sView = *view;
  }
}

extern "C" void PortSpeedrun_Draw(void) {
  PortSpeedrunView v;
  {
    std::lock_guard<std::mutex> lock(sLock);
    v = sView;
  }
  if (!v.visible || ImGui::GetCurrentContext() == nullptr) return;
  const es::Registry& reg = es::Global();
  const int where = IntSetting("video.speedrun_timer_position", 0);
  const bool right = where == 0 || where == 2, bottom = where >= 2;
  const bool showSplits = IntSetting("video.speedrun_timer_splits", 1) != 0 && v.splitCount > 0;
  (void)reg;
  const float scale = static_cast<float>(std::clamp(IntSetting("video.speedrun_timer_size", 100), 50, 300)) / 100.f;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImFont* font = ImGui::GetFont();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  float unit = display.y / 30.f;
  if (unit < 12.f) unit = 12.f;
  unit *= scale;
  const float big = unit * 1.9f, pad = unit * 0.4f;

  std::vector<int> rows;
  if (showSplits) {
    constexpr int kLines = 7;
    int first = std::max(0, std::min(v.current >= 0 ? v.current - 2 : v.splitCount - kLines, v.splitCount - kLines));
    if (first < 0) first = 0;
    for (int i = first; i < std::min(v.splitCount, first + kLines); i++) rows.push_back(i);
  }
  const float nameW = unit * 9.f, timeW = unit * 5.2f, deltaW = unit * 4.6f;
  const ImVec2 bigSize = font->CalcTextSizeA(big, 100000.f, 0.f, v.time);
  const ImVec2 labelSize = font->CalcTextSizeA(unit * 0.8f, 100000.f, 0.f, v.label);
  const ImVec2 infoSize = font->CalcTextSizeA(unit * 0.8f, 100000.f, 0.f, v.info);
  const ImVec2 info2Size = font->CalcTextSizeA(unit * 0.8f, 100000.f, 0.f, v.info2);
  float boxW = std::max({bigSize.x + labelSize.x + pad * 3.f, rows.empty() ? 0.f : nameW + timeW + deltaW + pad * 2.f,
                         infoSize.x + pad * 2.f, info2Size.x + pad * 2.f});
  const float lineH = unit * 1.25f;
  const float infoH = (v.info[0] != 0 ? infoSize.y + pad * 0.3f : 0.f) + (v.info2[0] != 0 ? info2Size.y + pad * 0.3f : 0.f);
  const float boxH = pad + bigSize.y + infoH + (rows.empty() ? 0.f : pad * 0.5f + lineH * static_cast<float>(rows.size())) + pad;
  const float margin = unit * 0.6f;
  const float x0 = right ? display.x - margin - boxW : margin;
  const float y0 = bottom ? display.y - margin - boxH : margin;
  dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + boxW, y0 + boxH), IM_COL32(0, 0, 0, 165), pad);
  if (v.checkCount > 0 && IntSetting("video.speedrun_checklist", 1) != 0) {
    float cw = 0.f;
    for (int i = 0; i < v.checkCount; i++) cw = std::max(cw, font->CalcTextSizeA(unit * 0.85f, 100000.f, 0.f, v.checks[i].text).x);
    cw += pad * 3.f + unit;
    const float ch = pad * 2.f + unit * 1.15f * static_cast<float>(v.checkCount);
    const float cx = right ? display.x - margin - cw : margin;
    const float cy = bottom ? y0 - pad - ch : y0 + boxH + pad;
    dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cw, cy + ch), IM_COL32(0, 0, 0, 165), pad);
    float ly = cy + pad;
    for (int i = 0; i < v.checkCount; i++) {
      const int st = v.checks[i].state;
      const ImU32 col = st == 1 ? IM_COL32(120, 225, 130, 255) : st == 2 ? IM_COL32(160, 160, 160, 255) : IM_COL32(244, 244, 244, 255);
      dl->AddText(font, unit * 0.85f, ImVec2(cx + pad, ly), col, st == 1 ? "[x]" : st == 2 ? "[?]" : "[ ]");
      dl->AddText(font, unit * 0.85f, ImVec2(cx + pad + unit * 1.9f, ly), col, v.checks[i].text);
      ly += unit * 1.15f;
    }
  }
  float y = y0 + pad * 0.7f;
  const ImU32 white = IM_COL32(244, 244, 244, 255), grey = IM_COL32(170, 170, 170, 255);
  dl->AddText(font, big, ImVec2(x0 + boxW - pad - bigSize.x, y), v.warn ? IM_COL32(255, 120, 90, 255) : white, v.time);
  dl->AddText(font, unit * 0.8f, ImVec2(x0 + pad, y + bigSize.y - labelSize.y - unit * 0.1f), grey, v.label);
  y += bigSize.y;
  if (v.info[0] != 0) {
    dl->AddText(font, unit * 0.8f, ImVec2(x0 + boxW - pad - infoSize.x, y + pad * 0.15f), v.warn ? IM_COL32(255, 120, 90, 255) : grey, v.info);
    y += infoSize.y + pad * 0.3f;
  }
  if (v.info2[0] != 0) {
    dl->AddText(font, unit * 0.8f, ImVec2(x0 + boxW - pad - info2Size.x, y + pad * 0.15f), IM_COL32(255, 120, 90, 255), v.info2);
    y += info2Size.y + pad * 0.3f;
  }
  y += pad * 0.5f;
  for (int i : rows) {
    const PortSpeedrunSplit& s = v.splits[i];
    const bool cur = i == v.current;
    if (cur) dl->AddRectFilled(ImVec2(x0 + pad * 0.4f, y), ImVec2(x0 + boxW - pad * 0.4f, y + lineH), IM_COL32(60, 90, 160, 150), 3.f);
    const ImU32 nameCol = s.time[0] != 0 || cur ? white : grey;
    dl->AddText(font, unit, ImVec2(x0 + pad, y + unit * 0.1f), nameCol, s.name);
    const ImVec2 ts = font->CalcTextSizeA(unit, 100000.f, 0.f, s.time);
    dl->AddText(font, unit, ImVec2(x0 + boxW - pad - ts.x, y + unit * 0.1f), white, s.time);
    if (s.delta[0] != 0) {
      const ImVec2 ds = font->CalcTextSizeA(unit, 100000.f, 0.f, s.delta);
      const ImU32 col = s.ahead < 0 ? IM_COL32(110, 220, 120, 255) : s.ahead > 0 ? IM_COL32(235, 110, 100, 255) : grey;
      dl->AddText(font, unit, ImVec2(x0 + boxW - pad - ts.x - unit * 0.6f - ds.x, y + unit * 0.1f), col, s.delta);
    }
    y += lineH;
  }
}
