#pragma once

#include <dolphin/gx.h>
#include <aurora/texture.hpp>

#include <cstdint>

namespace xd::texobs {

enum Slot { kButtonIcons = 0, kTextureDump = 1, kTypeIcons = 2, kSlotCount };

struct Watcher {
  bool (*wants)(void* user, const aurora::texture::TextureSourceKey& key) = nullptr;
  void (*decoded)(void* user, const aurora::texture::TextureSourceKey& key, const uint8_t* rgba, uint32_t width,
                  uint32_t height, bool mipmapped) = nullptr;
  void* user = nullptr;
};

void Set(Slot slot, Watcher watcher);

}
