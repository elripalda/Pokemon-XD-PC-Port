#ifndef XD_ONLINE_DRAFT_GAME_H
#define XD_ONLINE_DRAFT_GAME_H

#include "online_battle.h"
#include "online_room.h"

#include <xd_save.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xd::online {

bool RunDraft(bool host, const RoomRules& rules, BattleTransport* link, std::vector<xd::save::Pokemon>& mine, std::vector<uint16_t>& friendSpecies,
              std::string& why);

bool MakePlaceholderHero(void* hero, int count);
bool BuildTeamFromSpecies(const RoomRules& rules, const std::vector<uint16_t>& species, std::vector<xd::save::Pokemon>& out, std::string& why);

}

#endif
