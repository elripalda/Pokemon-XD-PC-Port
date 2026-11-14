#ifndef PORT_WINDOW_SHOT_H
#define PORT_WINDOW_SHOT_H

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PortShot_SavePng(const wchar_t* path, const uint8_t* rgba, uint32_t w, uint32_t h);

typedef void (*PortShotRgbaDone)(void* ctx, const uint8_t* rgba, uint32_t w, uint32_t h);
void PortShot_GamePicture(PortShotRgbaDone done, void* ctx);

bool PortShot_SaveWindow(const wchar_t* path);

typedef void (*PortShotLinuxDone)(void* ctx, const uint8_t* rgba, uint32_t w, uint32_t h, bool overlay);
void PortShot_LinuxRequest(bool window, PortShotLinuxDone done, void* ctx);
void PortShot_LinuxFrame(void);
void PortShot_LinuxPoll(void);
bool PortShot_LinuxFrozenDraw(const uint8_t* frame, size_t size);
void PortShot_LinuxNoteTexture(uint64_t id, uint32_t w, uint32_t h, const void* rgba);

#ifdef __cplusplus
}
#endif

#endif
