#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "colo_models.h"

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  std::vector<std::pair<std::string, std::string>> pairs;
  if (argc == 3 && std::string(argv[1]) == "--list") {
    std::ifstream l(argv[2]);
    std::string a, b;
    while (l >> a >> b) pairs.emplace_back(a, b);
  } else {
    for (int i = 1; i + 1 < argc; i += 2) pairs.emplace_back(argv[i], argv[i + 1]);
  }
  int ok = 0, bad = 0;
  if (argc >= 3 && std::string(argv[1]) == "--heads") {
    std::ifstream l(argv[2]);
    std::string a, b;
    int hit = 0, all = 0;
    while (l >> a >> b) {
      std::vector<uint8_t> in = slurp(a), want = slurp(b), got;
      std::string why;
      if (!colo::convert_trainer_pkx(in, got, why)) continue;
      const int g = colo::guess_head_joint(got), w = (want[0x6E] << 8) | want[0x6F];
      all++;
      if (g == w) hit++;
      else printf("head %s: guessed %d, XD says %d\n", a.c_str(), g, w);
    }
    printf("head joint rule: %d of %d equal to XD's own value\n", hit, all);
    return 0;
  }
  for (auto& p : pairs) {
    std::vector<uint8_t> in = slurp(p.first), want = slurp(p.second), got;
    std::string why;
    if (!colo::convert_trainer_pkx(in, got, why)) {
      printf("FAIL %s: %s\n", p.first.c_str(), why.c_str());
      bad++;
      continue;
    }
    size_t diffs = 0, first = 0;
    if (got.size() != want.size()) {
      diffs = (size_t)-1;
    } else {
      for (size_t i = 0; i < got.size(); i++)
        if (got[i] != want[i] && i != 0x6F) {
          if (!diffs) first = i;
          diffs++;
        }
    }
    if (diffs == 0) {
      ok++;
    } else {
      bad++;
      printf("DIFF %s: sizes %zu / %zu, %zu bytes differ (first at 0x%zx)\n", p.first.c_str(), got.size(), want.size(), diffs, first);
    }
  }
  printf("%d equal (apart from the head joint), %d different\n", ok, bad);
  return bad ? 1 : 0;
}
