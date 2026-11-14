#ifndef XD_STARTER_H
#define XD_STARTER_H

#include "xd_randomizer.h"

#include <cstdint>
#include <string>

namespace xd::starter {

void SetPlan(const xd::randomizer::StarterPlan& plan);
const xd::randomizer::StarterPlan& Plan();
bool Randomized();

std::string SpeciesName(uint32_t species);

void Choose(int index);

}

extern "C" void PortXD_NewGameCreate(void* save, uint16_t* name);
bool PortXD_OpenStarterPick(void);

#endif
