#include "xd_modern_types.h"

#include <dolphin/gx.h>
#include <aurora/texture.hpp>

#include <dolphin/gx/GXEnum.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

#include "texture_observers.h"
#include "xd_type_icons.h"

bool PortXD_GameGlyph(char32_t ch, std::vector<uint8_t>& alpha, int& w, int& h);

namespace {

namespace at = aurora::texture;

struct Sheet {
  const char* name;
  uint32_t width, height, format;
  uint64_t textureHash, tlutHash;
  bool small;
};
const Sheet kSheets[] = {
    {"type pictures (big)", 152, 200, 9, 0x57d2fb5e719a7e24ull, 0xabc716ee6c6c88bfull, false},
};

struct Done {
  at::TextureSourceKey key;
  std::vector<uint8_t> painted;
  at::ReplacementRegistration registration;
};

std::mutex sMutex;
bool sActive = false;
std::vector<Done> sDone;

const Sheet* SheetOf(const at::TextureSourceKey& key) {
  for (const Sheet& s : kSheets) {
    if (s.width == key.width && s.height == key.height && s.format == key.format && s.textureHash == key.textureHash &&
        (!key.hasTlut || s.tlutHash == key.tlutHash)) {
      return &s;
    }
  }
  return nullptr;
}

struct Rgb {
  float r, g, b;
};
Rgb Mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

void Over(uint8_t* px, Rgb c, float a) {
  const float da = px[3] / 255.f;
  const float oa = a + da * (1.f - a);
  if (oa <= 0.f) return;
  const float src[3] = {c.r, c.g, c.b};
  for (int k = 0; k < 3; k++) px[k] = static_cast<uint8_t>(std::clamp((src[k] * a + px[k] * da * (1.f - a)) / oa, 0.f, 255.f) + 0.5f);
  px[3] = static_cast<uint8_t>(std::clamp(oa * 255.f, 0.f, 255.f) + 0.5f);
}

void DrawPill(uint8_t* img, int stride, int x, int y, int w, int h, Rgb base, const char* text, float textHeight, float corner = -1.f) {
  for (int j = 0; j < h; j++) std::fill_n(img + static_cast<size_t>(y + j) * stride + static_cast<size_t>(x) * 4, w * 4, uint8_t{0});
  const float r = (corner < 0.f ? h * 0.5f : corner) - 0.5f;
  const Rgb rim = {base.r * 0.55f, base.g * 0.55f, base.b * 0.6f};
  const Rgb light = Mix(base, {255, 255, 255}, 0.55f);
  const Rgb dark = Mix(base, {0, 0, 0}, 0.18f);
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      const float cx = std::clamp(static_cast<float>(i) + 0.5f, r + 0.5f, w - r - 0.5f);
      const float cy = std::clamp(static_cast<float>(j) + 0.5f, r + 0.5f, h - r - 0.5f);
      const float d = std::hypot(i + 0.5f - cx, j + 0.5f - cy) - r;
      const float cover = std::clamp(0.5f - d, 0.f, 1.f);
      if (cover <= 0.f) continue;
      const float t = (j + 0.5f) / h;
      Rgb c = t < 0.45f ? Mix(light, base, t / 0.45f) : Mix(base, dark, (t - 0.45f) / 0.55f);
      const float edge = std::clamp(1.f + d, 0.f, 1.f);
      c = Mix(c, rim, edge);
      Over(img + static_cast<size_t>(y + j) * stride + static_cast<size_t>(x + i) * 4, c, cover);
    }
  }
  std::vector<std::vector<uint8_t>> glyph;
  std::vector<int> gw, gh;
  int total = 0, maxH = 0;
  for (const char* p = text; *p != '\0'; p++) {
    std::vector<uint8_t> a;
    int ww = 0, hh = 0;
    if (!PortXD_GameGlyph(static_cast<char32_t>(*p), a, ww, hh)) {
      ww = 0, hh = 0;
      a.clear();
    }
    glyph.push_back(std::move(a));
    gw.push_back(ww);
    gh.push_back(hh);
    total += ww + 1;
    maxH = std::max(maxH, hh);
  }
  if (maxH == 0 || total == 0) return;
  total -= 1;
  std::vector<float> word(static_cast<size_t>(total) * maxH, 0.f);
  int at0 = 0;
  for (size_t k = 0; k < glyph.size(); k++) {
    for (int j = 0; j < gh[k]; j++) {
      for (int i = 0; i < gw[k]; i++) word[static_cast<size_t>(j + (maxH - gh[k])) * total + at0 + i] = glyph[k][static_cast<size_t>(j) * gw[k] + i] / 255.f;
    }
    at0 += gw[k] + 1;
  }
  const float scale = textHeight / maxH;
  const int dw = std::max(1, static_cast<int>(std::lround(total * scale))), dh = std::max(1, static_cast<int>(std::lround(maxH * scale)));
  const int ox = x + (w - dw) / 2, oy = y + (h - dh) / 2;
  const Rgb ink = {16, 14, 18};
  for (int j = 0; j < dh; j++) {
    for (int i = 0; i < dw; i++) {
      const float sx0 = i / scale, sx1 = (i + 1) / scale, sy0 = j / scale, sy1 = (j + 1) / scale;
      float sum = 0.f, n = 0.f;
      for (int sy = static_cast<int>(sy0); sy < std::min(maxH, static_cast<int>(std::ceil(sy1))); sy++) {
        for (int sx = static_cast<int>(sx0); sx < std::min(total, static_cast<int>(std::ceil(sx1))); sx++) {
          sum += word[static_cast<size_t>(sy) * total + sx];
          n += 1.f;
        }
      }
      if (n > 0.f) Over(img + static_cast<size_t>(oy + j) * stride + static_cast<size_t>(ox + i) * 4, ink, std::min(1.f, sum / n * 1.15f));
    }
  }
}

void Paint(const Sheet& s, std::vector<uint8_t>& img) {
  const Rgb fairy = {238, 153, 172};
  (void)s.small;
  DrawPill(img.data(), static_cast<int>(s.width) * 4, xd::modern::kFairyBigX, xd::modern::kFairyBigY, 72, 18, fairy, "FAIRY", 11.f);
  DrawPill(img.data(), static_cast<int>(s.width) * 4, xd::modern::kFairySmallX, xd::modern::kFairySmallY, 21, 18, fairy, "Fa", 11.f, 5.f);
}

}

namespace xd::type_icons {

bool Active() { return sActive; }

namespace {
bool WantsCb(void*, const at::TextureSourceKey& key) { return Wants(key); }
void DecodedCb(void*, const at::TextureSourceKey& key, const uint8_t* rgba, uint32_t w, uint32_t h, bool) {
  Decoded(key, rgba, w, h);
}
}

void SetActive(bool on) {
  {
    std::lock_guard lk(sMutex);
    sActive = on;
  }
  if (on) xd::texobs::Set(xd::texobs::kTypeIcons, {WantsCb, DecodedCb, nullptr});
  else xd::texobs::Set(xd::texobs::kTypeIcons, {});
}

bool Wants(const at::TextureSourceKey& key) {
  if (SheetOf(key) == nullptr) return false;
  std::lock_guard lk(sMutex);
  if (!sActive) return false;
  for (const Done& d : sDone) {
    if (d.key == key) return d.registration.id == 0;
  }
  return true;
}

void Decoded(const at::TextureSourceKey& key, const uint8_t* rgba, uint32_t w, uint32_t h) {
  const Sheet* sheet = SheetOf(key);
  if (sheet == nullptr) return;
  std::lock_guard lk(sMutex);
  if (!sActive) return;
  Done* done = nullptr;
  for (Done& d : sDone) {
    if (d.key == key) done = &d;
  }
  if (done == nullptr) {
    sDone.emplace_back();
    done = &sDone.back();
    done->key = key;
  }
  if (done->registration.id != 0) at::unregister_replacement(done->registration);
  done->painted.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
  Paint(*sheet, done->painted);
  at::RawTextureReplacement raw;
  raw.bytes = done->painted;
  raw.width = w;
  raw.height = h;
  raw.mipCount = 1;
  raw.gxFormat = GX_TF_RGBA8_PC;
  raw.label = "XD type pictures";
  done->registration = at::register_replacement(key, raw);
  std::fprintf(stderr, "[type icons] %s (%ux%u): the Fairy picture painted\n", sheet->name, w, h);
}

}
