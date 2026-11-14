#ifndef XD_ONLINE_TEAM_H
#define XD_ONLINE_TEAM_H

#include "online_room.h"

#include <xd_legality.h>
#include <xd_save.h>

#include <string>
#include <vector>

namespace xd::online {

constexpr size_t kMaxTeam = 6;

std::string EncodeTeam(const std::vector<xd::save::Pokemon>& team);
bool DecodeTeam(const std::string& line, std::vector<xd::save::Pokemon>& out, std::string* why);

std::vector<std::string> CheckTeam(const std::vector<xd::save::Pokemon>& team, const xd::legality::Rules& rules, const RoomRules& room);

}

#endif
