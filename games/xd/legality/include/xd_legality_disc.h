#ifndef XD_LEGALITY_DISC_H
#define XD_LEGALITY_DISC_H

#include <xd_legality.h>
#include <xd_save_names.h>

#include <memory>
#include <string>

namespace xd::legality {

struct DiscRules {
  Rules rules;
  std::shared_ptr<xd::save::GameNames> names;
};

bool LoadRulesFromDisc(const std::string& discPath, DiscRules& out, std::string& error);
bool LoadRulesFromRunningGame(DiscRules& out, std::string& error);
bool LoadRulesOriginalInRunningGame(DiscRules& out, std::string& error);

bool BuildRulesFromRunningGame(const void* tables, const uint8_t* machinesBE, const uint8_t* growBE, DiscRules& out, std::string& error);

}

#endif
