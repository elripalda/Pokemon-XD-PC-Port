#ifndef XD_SPEEDRUN_H
#define XD_SPEEDRUN_H

#include <playthrough.h>

#include <cstdint>
#include <string>

namespace xd::speedrun {

void Frame();
void NewGame(const engine::playthrough::Speedrun& timer);
void Continue(const engine::playthrough::Setup& setup, const std::string& key, bool hasSetup);
engine::playthrough::Speedrun TimerState();
engine::playthrough::PlayStats StatsState();
void BattleEnded(bool won, unsigned result, unsigned battle);
void SaveWritten();
bool TitleWatchNeeded(bool cardHasTimer);
bool MapStarted();
engine::playthrough::Speedrun NewGameDefault();

bool Active();
bool PlaythroughChosen();
int RowMode();
bool SetRowMode(int mode);
std::string StatsText();
int RowCount(int which);
const char* RowName(int which, int index);
int RowValue(int which);
bool SetRow(int which, int index);
std::string TimerRowInfo(int which = 0);
void NotePad(int stickX, int stickY);
void CreditsStarted();
void EndingCinematic();
void NewGameStartPressed();
void NewGameConfirmed();
void SetEngine(bool enhanced);

}

extern "C" void PortXD_MoviePlay(uint32_t movie, int32_t a, int loop, void* tex);
extern "C" int32_t PortXD_MenuNameEntryOpen(int32_t mode, intptr_t param);

#endif
