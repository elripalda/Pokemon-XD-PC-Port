#ifndef XD_SHINY_H
#define XD_SHINY_H

#include <string>
#include <vector>

namespace xd::shiny {

struct OddsChoice {
  int divisor;
  const char* name;
};
inline constexpr OddsChoice kOdds[] = {
    {8192, "Original (1 in 8192)"}, {4096, "Modern (1 in 4096)"}, {1024, "1 in 1024"}, {512, "1 in 512"}, {128, "1 in 128"}};
inline constexpr int kOddsCount = 5;

std::vector<std::string> OptionsOf(int oddsIndex, int unlocks);
inline constexpr int kUnlockShadow = 1, kUnlockGifts = 2, kUnlockSpots = 4;
int OddsIndexOf(const std::vector<std::string>& options);
int UnlocksOf(const std::vector<std::string>& options);

std::vector<std::string> TestSwitch();

void SetActive(const std::vector<std::string>& options, const char* why);
const std::vector<std::string>& Active();

}

#endif
