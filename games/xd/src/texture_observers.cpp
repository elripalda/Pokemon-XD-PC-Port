#include "texture_observers.h"

#include <mutex>

namespace xd::texobs {
namespace {

std::mutex sMutex;
Watcher sSlots[kSlotCount];

thread_local uint32_t tWanted = 0;

bool Wants(void*, const aurora::texture::TextureSourceKey& key) {
  Watcher slots[kSlotCount];
  {
    std::lock_guard lk(sMutex);
    for (int i = 0; i < kSlotCount; i++) slots[i] = sSlots[i];
  }
  uint32_t wanted = 0;
  for (int i = 0; i < kSlotCount; i++) {
    if (slots[i].wants != nullptr && slots[i].wants(slots[i].user, key)) wanted |= 1u << i;
  }
  tWanted = wanted;
  return wanted != 0;
}

void Decoded(void*, const aurora::texture::TextureSourceKey& key, const uint8_t* rgba, uint32_t w, uint32_t h,
             bool mipmapped) {
  Watcher slots[kSlotCount];
  {
    std::lock_guard lk(sMutex);
    for (int i = 0; i < kSlotCount; i++) slots[i] = sSlots[i];
  }
  const uint32_t wanted = tWanted;
  tWanted = 0;
  for (int i = 0; i < kSlotCount; i++) {
    if ((wanted & (1u << i)) != 0 && slots[i].decoded != nullptr) slots[i].decoded(slots[i].user, key, rgba, w, h, mipmapped);
  }
}

}

void Set(Slot slot, Watcher watcher) {
  bool any = false;
  {
    std::lock_guard lk(sMutex);
    sSlots[slot] = watcher.wants != nullptr ? watcher : Watcher{};
    for (const Watcher& w : sSlots) any = any || w.wants != nullptr;
  }
  aurora::texture::SourceObserver observer;
  if (any) {
    observer.wants = Wants;
    observer.decodedInfo = Decoded;
  }
  aurora::texture::set_source_observer(observer);
}

}
