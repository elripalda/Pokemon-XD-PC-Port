
#include "../src/online_team.h"

#include <cstdio>
#include <cstring>

using namespace xd::online;

static int sFailed = 0, sChecks = 0;
#define CHECK(c)                                          \
  do {                                                    \
    sChecks++;                                            \
    if (!(c)) {                                           \
      sFailed++;                                          \
      std::printf("FAILED line %d: %s\n", __LINE__, #c);  \
    }                                                     \
  } while (0)

static void Put16(uint8_t* raw, size_t at, uint16_t v) {
  raw[at] = static_cast<uint8_t>(v & 255);
  raw[at + 1] = static_cast<uint8_t>(v >> 8);
}
static void Put32(uint8_t* raw, size_t at, uint32_t v) {
  for (int i = 0; i < 4; i++) raw[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}


static xd::save::Pokemon Make(uint16_t species, uint16_t shadow = 0) {
  uint8_t raw[xd::save::kPokemonSize];
  std::memset(raw, 0, sizeof raw);
  Put16(raw, 0x00, species);
  raw[0x11] = 20;
  Put32(raw, 0x20, 8000);
  Put32(raw, 0x28, 12345);
  Put16(raw, 0x04, 100);
  for (int i = 0; i < 6; i++) Put16(raw, 0x90 + static_cast<size_t>(i) * 2, 100);
  Put16(raw, 0x80, 1);
  Put16(raw, 0x84, 2);
  for (int i = 0; i < 6; i++) raw[0xA8 + i] = 15;
  Put16(raw, 0xBA, shadow);
  return xd::save::ParsePokemon(raw, xd::save::ByteOrder::PC);
}

int main() {

  xd::legality::Rules rules;
  rules.vanillaSpecies = 3;
  rules.vanillaMoves = 5;
  rules.vanillaItems = 4;
  rules.species.resize(3);
  rules.moves.resize(5);
  rules.items.resize(4);
  rules.expCurves.resize(1);
  for (int l = 0; l <= 100; l++) rules.expCurves[0][static_cast<size_t>(l)] = static_cast<uint32_t>(l * l * l);
  for (size_t i = 1; i < rules.moves.size(); i++) rules.moves[i].exists = true;
  for (size_t i = 1; i < rules.items.size(); i++) rules.items[i].exists = true;
  for (size_t s = 1; s <= 2; s++) {
    rules.species[s].exists = true;
    rules.species[s].abilities[0] = 5;
    for (int i = 0; i < 6; i++) rules.species[s].baseStats[i] = 50;
    rules.species[s].levelUp = {{1, 1}, {10, 2}};
  }
  RoomRules room;

  std::vector<xd::save::Pokemon> team = {Make(1), Make(2)};
  const std::string line = EncodeTeam(team);
  std::vector<xd::save::Pokemon> back;
  std::string why;
  CHECK(DecodeTeam(line, back, &why));
  CHECK(back.size() == 2 && back[0].species == 1 && back[1].species == 2 && back[0].level == 20 && back[0].exp == 8000);
  CHECK(back[0].raw == team[0].raw);
  CHECK(EncodeTeam(back) == line);


  CHECK(!DecodeTeam("T 2 abcd", back, &why));
  CHECK(!DecodeTeam("T 1 " + std::string(xd::save::kPokemonSize * 2, 'z'), back, &why));
  CHECK(!DecodeTeam("T 7 ", back, &why));
  CHECK(!DecodeTeam("X 1 00", back, &why));
  CHECK(DecodeTeam("T 0 ", back, &why) && back.empty());


  const std::vector<std::string> ok = CheckTeam(team, rules, room);
  for (const std::string& f : ok) std::printf("  (unexpected finding: %s)\n", f.c_str());
  CHECK(ok.empty());


  std::vector<xd::save::Pokemon> odd = {Make(1), Make(77)};
  std::vector<std::string> f = CheckTeam(odd, rules, room);
  CHECK(!f.empty() && f[0].find("Pokemon 2") != std::string::npos);


  CHECK(!CheckTeam({}, rules, room).empty());
  std::vector<xd::save::Pokemon> shadow = {Make(1, 3)};
  CHECK(!CheckTeam(shadow, rules, room).empty());
  room.shadowAllowed = true;
  CHECK(CheckTeam(shadow, rules, room).empty());

  std::printf("online_team_test: %d checks, %d failed\n", sChecks, sFailed);
  return sFailed ? 1 : 0;
}
