#include "online_team.h"

#include <cstdio>
#include <cstdlib>

namespace xd::online {

std::string EncodeTeam(const std::vector<xd::save::Pokemon>& team) {
  static const char* hex = "0123456789abcdef";
  std::string out = "T " + std::to_string(team.size()) + " ";
  for (const xd::save::Pokemon& p : team) {
    for (uint8_t b : p.raw) {
      out += hex[b >> 4];
      out += hex[b & 15];
    }
  }
  return out;
}

bool DecodeTeam(const std::string& line, std::vector<xd::save::Pokemon>& out, std::string* why) {
  auto fail = [&](const char* m) {
    if (why) *why = m;
    return false;
  };
  if (line.size() < 4 || line[0] != 'T' || line[1] != ' ') return fail("not a team message");
  const size_t sp = line.find(' ', 2);
  if (sp == std::string::npos) return fail("not a team message");
  const int count = std::atoi(line.substr(2, sp - 2).c_str());
  if (count < 0 || static_cast<size_t>(count) > kMaxTeam) return fail("a team has 0 to 6 Pokemon");
  const std::string hex = line.substr(sp + 1);
  if (hex.size() != static_cast<size_t>(count) * xd::save::kPokemonSize * 2) return fail("the team message has the wrong length");
  std::vector<xd::save::Pokemon> team;
  for (int i = 0; i < count; i++) {
    uint8_t raw[xd::save::kPokemonSize];
    for (size_t k = 0; k < xd::save::kPokemonSize; k++) {
      const char a = hex[(static_cast<size_t>(i) * xd::save::kPokemonSize + k) * 2], b = hex[(static_cast<size_t>(i) * xd::save::kPokemonSize + k) * 2 + 1];
      auto val = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
      if (val(a) < 0 || val(b) < 0) return fail("the team message is not hexadecimal");
      raw[k] = static_cast<uint8_t>(val(a) * 16 + val(b));
    }
    team.push_back(xd::save::ParsePokemon(raw, xd::save::ByteOrder::PC));
  }
  out = team;
  return true;
}

std::vector<std::string> CheckTeam(const std::vector<xd::save::Pokemon>& team, const xd::legality::Rules& rules, const RoomRules& room) {
  std::vector<std::string> out;
  size_t members = 0;
  for (const xd::save::Pokemon& p : team) {
    if (!p.IsEmpty()) members++;
  }
  if (members == 0) out.push_back("The team has no Pokemon.");
  if (team.size() > kMaxTeam) out.push_back("A team has at most 6 Pokemon.");
  int slot = 0;
  for (const xd::save::Pokemon& p : team) {
    slot++;
    if (p.IsEmpty()) continue;
    xd::legality::Context context;
    context.shadowStaysInItsSave = true;
    const xd::legality::Result r = xd::legality::Check(p, rules, xd::legality::Level::Safe, context);
    if (!r.Ok()) out.push_back("Pokemon " + std::to_string(slot) + ": " + r.Text());
    if (p.shadowId != 0 && !room.shadowAllowed) out.push_back("Pokemon " + std::to_string(slot) + " is a Shadow Pokemon; this room does not allow them.");
  }
  return out;
}

}
