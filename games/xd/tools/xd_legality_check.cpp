#include <gc_memcard.h>
#include <xd_legality_disc.h>
#include <xd_save.h>

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "Usage: %s <disc image> <save.gci | card image> [--quiet]\n", argv[0]);
    return 2;
  }
  const bool quiet = argc > 3 && std::strcmp(argv[3], "--quiet") == 0;
  std::vector<uint8_t> bytes;
  std::vector<engine::memcard::CardFile> files;
  engine::memcard::CardImageInfo info;
  std::string error;
  if (!engine::memcard::ReadWholeFile(argv[2], bytes) || !engine::memcard::LoadCardFiles(bytes, files, info, error)) {
    std::fprintf(stderr, "FAILED: %s\n", error.empty() ? "can't read the save" : error.c_str());
    return 2;
  }
  xd::save::Save save;
  xd::save::ReadReport report;
  bool read = false;
  for (const auto& f : files) {
    if (f.entry.fileName == "PokemonXD" && xd::save::ReadSave(f.data, save, report, error)) {
      read = true;
      break;
    }
  }
  if (!read) {
    std::fprintf(stderr, "FAILED: no readable Pokemon XD save (%s)\n", error.c_str());
    return 2;
  }
  xd::legality::DiscRules disc;
  if (!xd::legality::LoadRulesFromDisc(argv[1], disc, error)) {
    std::fprintf(stderr, "FAILED: %s\n", error.c_str());
    return 2;
  }
  int checked = 0, safeBad = 0, legalBad = 0;
  auto one = [&](const xd::save::Pokemon& p, const std::string& where) {
    if (p.IsEmpty()) return;
    checked++;
    xd::legality::Context ctx;
    ctx.shadow = save.ShadowOf(p);
    const auto safe = xd::legality::Check(p, disc.rules, xd::legality::Level::Safe, ctx);
    const auto legal = xd::legality::Check(p, disc.rules, xd::legality::Level::Legal, ctx);
    safeBad += safe.Ok() ? 0 : 1;
    legalBad += legal.Ok() ? 0 : 1;
    if (!legal.Ok() && !quiet) {
      std::printf("%s: %s lvl %d: %s%s\n", where.c_str(), disc.names->Species(p.species).c_str(), p.level,
                  safe.Ok() ? "" : "[SAFE] ", legal.Text(" | ").c_str());
    }
  };
  for (size_t i = 0; i < save.party.size(); i++) one(save.party[i], "party " + std::to_string(i + 1));
  for (size_t b = 0; b < save.boxes.size(); b++) {
    for (size_t s = 0; s < save.boxes[b].slots.size(); s++) {
      one(save.boxes[b].slots[s], "box " + std::to_string(b + 1) + " slot " + std::to_string(s + 1));
    }
  }
  one(save.dayCare, "day care");
  std::printf("legality: %d Pokemon checked, %d fail the Safe level, %d fail the Legal level\n", checked, safeBad, legalBad);
  return legalBad == 0 ? 0 : 1;
}
