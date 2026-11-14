#ifndef XD_TRAINER_CARD_GAME_H
#define XD_TRAINER_CARD_GAME_H

#include "trainer_card.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xd::card {

Card MyCard();
const Card* HostCard();
void SetRoom(bool iAmHost, const Card& mine, const Card& friendCard);
void ClearRoom();
bool RoomActive();
const Card* GuestCard();

struct TitleEntry { int kind; uint32_t msg; std::u16string text; };
const std::vector<TitleEntry>& Titles();

struct ModelEntry {
  Model model;
  std::string label;
  bool available;
  std::filesystem::path icon;
};
std::vector<ModelEntry> Models();

std::filesystem::path CardFolder();
std::string SavedCardPath();
bool SaveMyCard(const Card& c);
std::filesystem::path ColosseumPack();
std::filesystem::path IconPath(const Model& m);

std::string StandIn(const Model& m);

}

#endif
