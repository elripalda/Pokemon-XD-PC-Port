#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "pbr_batch.h"

namespace colo {

constexpr const char* CONVERTER_VERSION = "1.0.0";
constexpr const char* MOD_ID = "colosseum-characters";
constexpr const char* COLO_ID = "GC6E01";
constexpr const char* XD_ID = "GXXE01";

bool convert_trainer_pkx(const std::vector<uint8_t>& colosseum, std::vector<uint8_t>& xd, std::string& why);

int guess_head_joint(const std::vector<uint8_t>& xd_pkx);

struct Character {
  std::string id;
  std::string name;
  std::string group;
};
Character describe(const std::string& entry_name);
bool is_trainer_name(const std::string& entry_name);

struct RunArgs {
  std::string disc, out;
  std::string xd_disc;
};
void run_models(const RunArgs& a, const pbr::DiscOpener& open_disc, const std::function<void(const std::string&)>& say);
int colo_models_main(const std::vector<std::string>& args, const pbr::DiscOpener& open_disc);

}
