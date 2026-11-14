#include <xd_legality.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace xd::legality;
using xd::save::Pokemon;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

bool has(const Result& r, const char* code) {
  for (const Finding& f : r.findings) {
    if (r.findings.empty()) return false;
    if (f.code == code) return true;
  }
  return false;
}

Rules makeRules(bool gen4) {
  Rules r;
  r.vanillaSpecies = 4;
  r.vanillaMoves = 7;
  r.vanillaItems = 4;
  r.gen4Species = 1;
  r.gen4Moves = 2;
  r.gen4Items = 1;
  r.species.resize(gen4 ? 5 : 4);
  r.moves.resize(gen4 ? 9 : 7);
  r.items.resize(gen4 ? 5 : 4);
  r.expCurves.resize(1);
  for (int l = 0; l <= 100; l++) r.expCurves[0][static_cast<size_t>(l)] = static_cast<uint32_t>(l * l * l);
  for (size_t i = 1; i < r.moves.size(); i++) r.moves[i].exists = true;
  r.moves[6].shadow = true;
  for (size_t i = 1; i < r.items.size(); i++) r.items[i].exists = true;
  auto& s1 = r.species[1];
  s1.exists = true;
  s1.growth = 0;
  s1.abilities[0] = 5;
  s1.abilities[1] = 6;
  for (int i = 0; i < 6; i++) s1.baseStats[i] = 50;
  s1.levelUp = {{1, 1}, {10, 2}, {30, 3}};
  s1.machine[0] = 1;
  s1.tutor[1] = 1;
  auto& s2 = r.species[2];
  s2 = s1;
  s2.evolvesFrom = 1;
  s2.levelUp = {{1, 1}, {36, 4}};
  s2.abilities[1] = 0;
  s2.machine[0] = 0;
  auto& s3 = r.species[3];
  s3.exists = true;
  for (int i = 0; i < 6; i++) s3.baseStats[i] = 1;
  s3.baseStats[0] = 1;
  s3.levelUp = {{1, 5}};
  if (gen4) {
    r.species[4] = s1;
    r.species[4].levelUp = {{1, 7}};
    r.moves[7].exists = r.moves[8].exists = true;
    r.items[4].exists = true;
  }
  r.machineMove[0] = 4;
  r.tutorMove[1] = 2;
  return r;
}

Pokemon good(const Rules& r) {
  Pokemon p;
  p.species = 1;
  p.level = 20;
  p.exp = 8000;
  p.pid = 12345;
  p.moves[0].id = 1;
  p.moves[1].id = 2;
  p.metLevel = 5;
  p.hp = 1;
  std::memset(p.raw.data(), 0, p.raw.size());
  for (int i = 0; i < 6; i++) {
    p.ivs[static_cast<size_t>(i)] = 15;
    p.evs[static_cast<size_t>(i)] = 0;
  }
  std::array<uint16_t, 6> st{};
  ExpectedStats(r, p, st);
  p.stats = st;
  p.hp = st[0];
  return p;
}

}

int main() {
  const Rules r = makeRules(false);
  const Rules r4 = makeRules(true);
  check(!r.Gen4On() && r4.Gen4On(), "the made-up rules: Gen 4 off / on");

  Pokemon p = good(r);
  check(Check(p, r, Level::Legal).Ok(), "a good Pokemon passes both levels");
  check(Check(p, r, Level::Safe).Ok(), "... and the Safe one");
  check(Check(Pokemon{}, r, Level::Legal).Ok(), "an empty slot is fine");

  {
    Pokemon q = good(r);
    q.species = 99;
    const Result res = Check(q, r, Level::Safe);
    check(has(res, "species.range"), "a species past the table: refused (" + res.Text() + ")");
  }
  {
    Pokemon q = good(r);
    q.species = 4;
    const Result res = Check(q, r, Level::Safe);
    check(has(res, "species.gen4") && res.Text().find("Gen 4 option") != std::string::npos,
          "a Gen 4 species in a game without Gen 4: its own reason (" + res.Text() + ")");
    Pokemon g = good(r4);
    g.species = 4;
    g.moves[0].id = 7;
    g.moves[1].id = 0;
    std::array<uint16_t, 6> st{};
    ExpectedStats(r4, g, st);
    g.stats = st;
    g.hp = st[0];
    check(Check(g, r4, Level::Legal).Ok(), "the same species is fine where Gen 4 is on");
  }
  {
    Pokemon q = good(r);
    q.moves[2].id = 8;
    check(has(Check(q, r, Level::Safe), "moves.gen4"), "a Gen 4 move without Gen 4: refused");
    q.moves[2].id = 50;
    check(has(Check(q, r, Level::Safe), "moves.range"), "a move that doesn't exist: refused");
  }
  {
    Pokemon q = good(r);
    q.heldItem = 4;
    check(has(Check(q, r, Level::Safe), "item.gen4"), "a Gen 4 held item without Gen 4: refused");
    q.heldItem = 77;
    check(has(Check(q, r, Level::Safe), "item.range"), "a held item that doesn't exist: refused");
  }
  {
    Pokemon q = good(r);
    q.level = 0;
    check(has(Check(q, r, Level::Safe), "level.range"), "level 0: refused");
    q = good(r);
    q.ivs[3] = 40;
    check(has(Check(q, r, Level::Safe), "iv.range"), "IV 40: refused");
    q = good(r);
    q.evs[2] = 300;
    check(has(Check(q, r, Level::Safe), "ev.range"), "EV 300: refused");
    q = good(r);
    q.hp = q.stats[0] + 5;
    check(has(Check(q, r, Level::Safe), "hp.range"), "HP above max: refused");
    q = good(r);
    q.exp = 2000000;
    check(has(Check(q, r, Level::Safe), "exp.range"), "experience beyond level 100: refused");
    q = good(r);
    q.moves[0].ppUps = 5;
    check(has(Check(q, r, Level::Safe), "moves.ppups"), "5 PP Ups: refused");
    q = good(r);
    q.flags |= 0x20;
    check(has(Check(q, r, Level::Safe), "flags.invalid"), "marked invalid: refused");
    q = good(r);
    for (int i = 0; i < 11; i++) q.raw[0x4E + static_cast<size_t>(i) * 2 + 1] = 'A';
    check(has(Check(q, r, Level::Safe), "name.length"), "a name with no end: refused");
  }
  {
    Pokemon q = good(r);
    q.shadowId = 3;
    check(has(Check(q, r, Level::Safe), "shadow.open"), "an open Shadow Pokemon: refused");
    xd::save::ShadowEntry e;
    e.status = 4;
    Context ctx;
    ctx.shadow = &e;
    check(!has(Check(q, r, Level::Safe, ctx), "shadow.open"), "a purified one passes the Safe level");
  }
  {
    Pokemon q = good(r);
    q.exp = 100;
    q.moves[0].id = 3;
    check(Check(q, r, Level::Safe).Ok(), "randomized-looking Pokemon (odd exp, odd move) pass the Safe level");
    const Result res = Check(q, r, Level::Legal);
    check(has(res, "exp.level") && has(res, "moves.unlearnable"), "... and fail the Legal one: " + res.Text(" | "));
    Context foreign;
    foreign.movesNotJudged = true;
    const Result f = Check(q, r, Level::Legal, foreign);
    check(!has(f, "moves.unlearnable") && has(f, "exp.level"), "a Pokemon from another game: moves not judged, the rest is");
  }

  {
    Pokemon q = good(r);
    q.exp = 7999;
    check(has(Check(q, r, Level::Legal), "exp.level"), "experience one short of the level: refused");
    q.exp = 9260;
    check(!has(Check(q, r, Level::Legal), "exp.level"), "experience at the top of the level: fine");
    q.exp = 9261;
    check(has(Check(q, r, Level::Legal), "exp.level"), "experience of the next level: refused");
  }
  {
    Pokemon q = good(r);
    q.level = 100;
    q.exp = 1000000;
    std::array<uint16_t, 6> st{};
    ExpectedStats(r, q, st);
    q.stats = st;
    q.hp = st[0];
    check(Check(q, r, Level::Legal).Ok(), "level 100 at exactly its experience: fine");
  }
  {
    Pokemon q = good(r);
    q.moves[0].id = 4;
    check(Check(q, r, Level::Legal).Ok(), "a TM move the species can learn: fine");
    q.species = 2;
    q.moves[1].id = 2;
    std::array<uint16_t, 6> st{};
    ExpectedStats(r, q, st);
    q.stats = st;
    q.hp = st[0];
    check(Check(q, r, Level::Legal).Ok(), "a move of the pre-evolution: fine (" + Check(q, r, Level::Legal).Text() + ")");
    q.moves[2].id = 5;
    check(has(Check(q, r, Level::Legal), "moves.unlearnable"), "a move of another species: refused");
    q.moves[2].id = 3;
    check(has(Check(q, r, Level::Legal), "moves.unlearnable"), "a level-up move of a higher level: refused");
    Rules gift = r;
    gift.giftMoves.push_back({1, 3});
    check(!has(Check(q, gift, Level::Legal), "moves.unlearnable"), "a gift Pokemon's own move: fine, also after evolving");
    q.moves[2].id = 6;
    check(has(Check(q, r, Level::Legal), "moves.shadow"), "a Shadow move on a Pokemon that was never a Shadow one: refused");
  }
  {
    Pokemon q = good(r);
    q.moves[2].id = 2;
    check(has(Check(q, r, Level::Legal), "moves.twice"), "the same move twice: refused");
    q = good(r);
    q.moves[1].id = 0;
    q.moves[2].id = 1;
    q.moves[2].id = 2;
    check(has(Check(q, r, Level::Legal), "moves.gap"), "a gap before a move: refused");
  }
  {
    Pokemon q = good(r);
    q.flags |= 0x40;
    check(!has(Check(q, r, Level::Legal), "ability.slot"), "second ability on a species with two: fine");
    q.species = 2;
    check(has(Check(q, r, Level::Legal), "ability.slot"), "second ability on a species with one: refused");
    q = good(r);
    q.flags |= 0x80;
    check(has(Check(q, r, Level::Legal), "egg"), "an egg: refused (XD has no eggs)");
    q = good(r);
    q.metLevel = 50;
    check(has(Check(q, r, Level::Legal), "met.level"), "met at a higher level than now: refused");
    q = good(r);
    q.evs[0] = 255;
    q.evs[1] = 255;
    q.evs[2] = 100;
    std::array<uint16_t, 6> st{};
    ExpectedStats(r, q, st);
    q.stats = st;
    q.hp = st[0];
    check(has(Check(q, r, Level::Legal), "ev.total"), "610 EVs in all: refused");
  }
  {
    Pokemon q = good(r);
    q.stats[2] += 3;
    check(has(Check(q, r, Level::Legal), "stats.formula"), "a stat that the formula doesn't give: refused");
    Pokemon z = good(r);
    z.species = 3;
    z.moves[0].id = 5;
    z.moves[1].id = 0;
    std::array<uint16_t, 6> st{};
    ExpectedStats(r, z, st);
    check(st[0] == 1, "a 1-HP species has 1 HP");
  }
  {
    Pokemon q = good(r);
    q.shadowId = 3;
    q.moves[0].id = 6;
    q.moves[1].id = 3;
    q.otId = 0;
    q.pid = 0x00000003;
    std::array<uint16_t, 6> st{};
    ExpectedStats(r, q, st);
    q.stats = st;
    q.hp = st[0];
    xd::save::ShadowEntry e;
    e.status = 4;
    Context ctx;
    ctx.shadow = &e;
    const Result res = Check(q, r, Level::Legal, ctx);
    check(q.IsShiny() && has(res, "shiny.shadow") && !has(res, "moves.unlearnable"),
          "a shiny Shadow Pokemon: refused; its own moves are not judged (" + res.Text(" | ") + ")");
  }
  {
    Pokemon q = good(r);
    q.species = 50;
    const Result res = Check(q, r, Level::Legal);
    check(res.findings.size() == 1 && res.Text().find("not in this game's list") != std::string::npos,
          "plain words: \"" + res.Text() + "\"");
  }
  std::printf(sOk ? "xd_legality_test: all passed\n" : "xd_legality_test: FAILED\n");
  return sOk ? 0 : 1;
}
