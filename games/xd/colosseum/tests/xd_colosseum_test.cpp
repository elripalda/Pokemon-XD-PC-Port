#include <xd_colosseum.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace xd::colosseum;
using engine::colo::PokemonCore;
using xd::legality::Rules;
using xd::save::ByteOrder;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

Rules MakeRules() {
  Rules r;
  r.vanillaSpecies = r.vanillaMoves = r.vanillaItems = 1000;
  r.species.resize(300);
  r.moves.resize(400);
  r.items.resize(400);
  r.expCurves.resize(1);
  for (int l = 0; l <= 100; l++) r.expCurves[0][static_cast<size_t>(l)] = static_cast<uint32_t>(l * l * l);
  for (size_t i = 1; i < r.moves.size(); i++) r.moves[i].exists = true;
  for (size_t i = 1; i < r.items.size(); i++) r.items[i].exists = true;
  r.items[400 - 1].exists = true;
  auto& s = r.species[133];
  s.exists = true;
  for (int i = 0; i < 6; i++) s.baseStats[i] = 50;
  s.abilities[0] = 1;
  s.abilities[1] = 2;
  s.levelUp = {{1, 33}, {1, 45}};
  r.species[25] = s;
  r.species[196] = s;
  return r;
}

PokemonCore Good(const Rules& rules, uint16_t species) {
  PokemonCore c;
  c.species = species;
  c.pid = 0x00001000u + species;
  c.origin = {7, 3, 1, 2};
  c.otGender = 1;
  c.otId = 0x00AB1234;
  c.otName = u"WES";
  c.nickname = u"Spot";
  c.level = 20;
  c.exp = 8000;
  c.friendship = 70;
  c.moves = {{{33, 35, 1}, {45, 40, 0}, {0, 0, 0}, {0, 0, 0}}};
  c.metLevel = 5;
  c.ball = 4;
  c.ribbons = {1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1};
  c.contest = {1, 2, 3, 4, 5};
  c.contestRanks = {0, 1, 2, 3, 0};
  c.ivs = {10, 11, 12, 13, 14, 15};
  c.evs = {4, 8, 12, 0, 0, 0};
  c.sheen = 9;
  xd::save::Pokemon probe;
  probe.species = species;
  probe.level = c.level;
  probe.pid = c.pid;
  probe.ivs = c.ivs;
  probe.evs = c.evs;
  std::array<uint16_t, 6> st{};
  xd::legality::ExpectedStats(rules, probe, st);
  c.stats = st;
  c.hp = st[0];
  return c;
}

}

int main() {
  const Rules rules = MakeRules();

  for (ByteOrder order : {ByteOrder::GameCube, ByteOrder::PC}) {
    const PokemonCore c = Good(rules, 133);
    uint8_t rec[xd::save::kPokemonSize];
    XdFromCore(c, rec, order);
    const PokemonCore back = CoreFromXd(rec, order);
    const xd::save::Pokemon p = xd::save::ParsePokemon(rec, order);
    check(back.species == 133 && back.pid == c.pid && back.exp == 8000 && back.level == 20 && back.otName == u"WES" &&
              back.nickname == u"Spot" && back.stats == c.stats && back.evs == c.evs && back.ivs == c.ivs &&
              back.ribbons == c.ribbons && back.contest == c.contest && back.contestRanks == c.contestRanks &&
              back.moves[0].id == 33 && back.moves[0].ppUps == 1 && back.origin == c.origin && back.sheen == 9,
          std::string("an XD record written and read back (") + xd::save::ByteOrderName(order) + ")");
    check(p.species == 133 && p.otGender == 1 && p.raw[0x15] == 0xFF, "the XD reader agrees; mail is \"none\" as pokemonInit makes it");
  }

  engine::colo::Save save;
  save.party[0] = Good(rules, 133);
  save.party[1] = Good(rules, 25);
  save.party[1].heldItem = 520;
  save.party[1].metLocation = 0x12;
  save.party[2] = Good(rules, 196);
  save.party[2].shadowId = 9;
  save.party[2].heartGauge = 4000;
  save.boxes[0][3] = Good(rules, 133);
  save.boxes[0][3].shadowId = 9;
  save.boxes[0][3].heartGauge = -1;
  save.boxes[1][0] = Good(rules, 133);
  save.boxes[1][0].egg = true;
  save.boxes[2][29] = Good(rules, 133);
  save.boxes[2][29].moves[0].id = 399;
  save.boxes[2][29].level = 30;
  save.party[3] = Good(rules, 133);
  save.party[3].origin = {0, 3, 1, 2};
  save.party[4] = Good(rules, 250);
  const auto cands = Candidates(save, rules, ByteOrder::GameCube);
  auto find = [&](const std::string& where) -> const Candidate* {
    for (const Candidate& c : cands) {
      if (c.where == where) return &c;
    }
    return nullptr;
  };
  check(cands.size() == 8, "eight Pokemon found (" + std::to_string(cands.size()) + ")");
  const Candidate* a = find("Party 1");
  check(a && a->problem.empty() && a->lost.empty(), "a good Pokemon: comes as it is, nothing lost");
  const Candidate* b = find("Party 2");
  check(b && b->problem.empty() && b->lost.size() == 2 && b->lost[0].find("held item") != std::string::npos &&
            b->lost[1].find("place it was met") != std::string::npos,
        "a Colosseum key item and the met place: dropped, both listed");
  if (b) {
    const xd::save::Pokemon p = xd::save::ParsePokemon(b->xd.data(), ByteOrder::GameCube);
    check(p.heldItem == 0 && p.metLocation == 0, "... and gone from the XD record");
  }
  const Candidate* d = find("Party 3");
  check(d && !d->problem.empty() && d->problem.find("Shadow") != std::string::npos && d->problem.find("stays") != std::string::npos &&
            d->lost.empty(),
        "an unpurified Shadow Pokemon stays behind, with the reason");
  const Candidate* e = find("Box 1, place 4");
  check(e && e->problem.empty() && e->lost.size() == 1 && e->lost[0].find("Shadow") != std::string::npos &&
            xd::save::ParsePokemon(e->xd.data(), ByteOrder::GameCube).shadowId == 0,
        "a purified one comes as an ordinary Pokemon, the lost Shadow record listed");
  const Candidate* f = find("Box 2, place 1");
  check(f && f->problem.find("egg") != std::string::npos, "an egg stays behind");
  const Candidate* g = find("Box 3, place 30");
  check(g && g->problem.find("Not possible") != std::string::npos, "the legality check refuses an impossible one: " + (g ? g->problem : ""));
  const Candidate* h = find("Party 4");
  check(h && h->problem.find("origin") != std::string::npos, "an incomplete origin mark stays behind");
  const Candidate* i = find("Party 5");
  check(i && i->problem.find("not in this game") != std::string::npos, "a species XD doesn't have stays behind");

  {
    xd::save::Save xd;
    xd.order = ByteOrder::GameCube;
    xd.slotBytes.assign(xd::save::kSlotSize, 0);
    xd::save::Section sec;
    sec.id = 3;
    sec.offset = 0x100;
    xd.sections.push_back(sec);
    xd.boxes[0].slots[0].species = 5;
    std::vector<std::array<uint8_t, xd::save::kPokemonSize>> two = {a->xd, b->xd};
    check(FreeBoxPlaces(xd) == 8 * 30 - 1, "free places counted");
    std::string error;
    check(PlaceInBoxes(xd, two, error), "two Pokemon placed: " + error);
    const size_t base = xd::save::kSlotHeaderSize + 0x100;
    check(xd.slotBytes[base + 0x14 + 0xC4 + 1] == 133 && !xd.boxes[0].slots[1].IsEmpty() && !xd.boxes[0].slots[2].IsEmpty() &&
              xd.boxes[0].slots[0].species == 5,
          "they went into the first free places (1 was taken), as the game's own records");
    std::vector<std::array<uint8_t, xd::save::kPokemonSize>> many(8 * 30, a->xd);
    const auto before = xd.slotBytes;
    check(!PlaceInBoxes(xd, many, error) && error.find("room for") != std::string::npos && xd.slotBytes == before,
          "too many: refused in words, nothing changed");
  }
  std::printf(sOk ? "xd_colosseum_test: all passed\n" : "xd_colosseum_test: FAILED\n");
  return sOk ? 0 : 1;
}
