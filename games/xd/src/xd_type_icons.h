#ifndef XD_TYPE_ICONS_H
#define XD_TYPE_ICONS_H

#include <dolphin/gx.h>
#include <aurora/texture.hpp>

#include <cstdint>

namespace xd::type_icons {

void SetActive(bool on);
bool Active();
bool Wants(const aurora::texture::TextureSourceKey& key);
void Decoded(const aurora::texture::TextureSourceKey& key, const uint8_t* rgba, uint32_t w, uint32_t h);

}

#endif
