#ifndef XD_COLOSSEUM_H
#define XD_COLOSSEUM_H

#include <colo_save.h>
#include <xd_legality.h>
#include <xd_save.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xd::colosseum {

engine::colo::PokemonCore CoreFromXd(const uint8_t* record, xd::save::ByteOrder order);
void XdFromCore(const engine::colo::PokemonCore& core, uint8_t* record, xd::save::ByteOrder order);

constexpr uint16_t kFirstColosseumOnlyItem = 500;

struct Candidate {
  std::string where;
  engine::colo::PokemonCore core;
  std::array<uint8_t, xd::save::kPokemonSize> xd{};
  std::vector<std::string> lost;
  std::string problem;
};

std::vector<Candidate> Candidates(const engine::colo::Save& save, const xd::legality::Rules& rules, xd::save::ByteOrder order);

bool PlaceInBoxes(xd::save::Save& save, const std::vector<std::array<uint8_t, xd::save::kPokemonSize>>& records, std::string& error);
int FreeBoxPlaces(const xd::save::Save& save);

}

#endif
