#include <xd_colosseum.h>

#include <xd_save_write.h>

#include <cstring>

namespace xd::colosseum {

namespace {

using engine::colo::PokemonCore;
using xd::save::ByteOrder;
using xd::save::Read16;
using xd::save::Read32;
using xd::save::Write16;
using xd::save::Write32;

constexpr size_t kSpecies = 0x00, kItem = 0x02, kHp = 0x04, kFriend = 0x06, kFloor = 0x08, kCatchLevel = 0x0E, kBall = 0x0F,
                 kOtSex = 0x10, kLevel = 0x11, kFur = 0x12, kPokerus = 0x13, kMark = 0x14, kMail = 0x15, kFlags = 0x1D,
                 kExp = 0x20, kOtId = 0x24, kPid = 0x28, kEventGet = 0x30, kAttest = 0x34, kOtName = 0x38, kNickname = 0x4E,
                 kNicknameOrg = 0x64, kRibbon = 0x7C, kMoves = 0x80, kStats = 0x90, kEvs = 0x9C, kIvs = 0xA8, kContest = 0xAE,
                 kMedals = 0xB3, kShadowId = 0xBA;
constexpr uint8_t kFlagEgg = 0x80, kFlagSecondAbility = 0x40, kFlagInvalid = 0x20;
constexpr size_t kBoxStride = 0x14 + xd::save::kBoxSlots * xd::save::kPokemonSize;

void StoreName(uint8_t* rec, size_t at, const std::u16string& s, ByteOrder order) {
  for (size_t i = 0; i < 11; i++) Write16(rec + at + i * 2, i < s.size() && i < 10 ? static_cast<uint16_t>(s[i]) : 0, order);
}

}

engine::colo::PokemonCore CoreFromXd(const uint8_t* r, ByteOrder order) {
  PokemonCore c;
  c.species = Read16(r + kSpecies, order);
  c.heldItem = Read16(r + kItem, order);
  c.hp = Read16(r + kHp, order);
  c.friendship = Read16(r + kFriend, order);
  c.metLocation = Read16(r + kFloor, order);
  c.metLevel = r[kCatchLevel];
  c.ball = r[kBall];
  c.otGender = r[kOtSex];
  c.level = r[kLevel];
  c.sheen = r[kFur];
  c.pokerus = r[kPokerus];
  c.mark = r[kMark];
  c.egg = (r[kFlags] & kFlagEgg) != 0;
  c.secondAbility = (r[kFlags] & kFlagSecondAbility) != 0;
  c.invalid = (r[kFlags] & kFlagInvalid) != 0;
  c.exp = Read32(r + kExp, order);
  c.otId = Read32(r + kOtId, order);
  c.pid = Read32(r + kPid, order);
  std::memcpy(c.origin.data(), r + kAttest, 4);
  c.eventFlag = Read32(r + kEventGet, order);
  c.otName = xd::save::ReadString(r + kOtName, 11, order);
  c.nickname = xd::save::ReadString(r + kNickname, 11, order);
  const uint32_t xr = Read32(r + kRibbon, order);
  for (int i = 0; i < 12; i++) c.ribbons[static_cast<size_t>(i)] = static_cast<uint8_t>(xr >> (31 - i) & 1u);
  for (int i = 0; i < 4; i++) {
    const uint8_t* w = r + kMoves + i * 4;
    c.moves[static_cast<size_t>(i)] = {Read16(w, order), w[2], w[3]};
  }
  for (int i = 0; i < 6; i++) {
    c.stats[static_cast<size_t>(i)] = Read16(r + kStats + i * 2, order);
    c.evs[static_cast<size_t>(i)] = Read16(r + kEvs + i * 2, order);
    c.ivs[static_cast<size_t>(i)] = r[kIvs + i];
  }
  for (int i = 0; i < 5; i++) {
    c.contest[static_cast<size_t>(i)] = r[kContest + i];
    c.contestRanks[static_cast<size_t>(i)] = r[kMedals + i];
  }
  c.shadowId = Read16(r + kShadowId, order);
  return c;
}

void XdFromCore(const engine::colo::PokemonCore& c, uint8_t* r, ByteOrder order) {
  std::memset(r, 0, xd::save::kPokemonSize);
  r[kOtSex] = 2;
  r[kMail] = 0xFF;
  Write16(r + kSpecies, c.species, order);
  Write16(r + kItem, c.heldItem, order);
  Write16(r + kHp, c.hp, order);
  Write16(r + kFriend, c.friendship, order);
  Write16(r + kFloor, c.metLocation, order);
  r[kCatchLevel] = c.metLevel;
  r[kBall] = c.ball;
  r[kOtSex] = c.otGender;
  r[kLevel] = c.level;
  r[kFur] = c.sheen;
  r[kPokerus] = c.pokerus;
  r[kMark] = c.mark;
  r[kFlags] = static_cast<uint8_t>((c.egg ? kFlagEgg : 0) | (c.secondAbility ? kFlagSecondAbility : 0) | (c.invalid ? kFlagInvalid : 0));
  Write32(r + kExp, c.exp, order);
  Write32(r + kOtId, c.otId, order);
  Write32(r + kPid, c.pid, order);
  std::memcpy(r + kAttest, c.origin.data(), 4);
  Write32(r + kEventGet, c.eventFlag, order);
  StoreName(r, kOtName, c.otName, order);
  StoreName(r, kNickname, c.nickname, order);
  StoreName(r, kNicknameOrg, c.nickname, order);
  uint32_t xr = 0;
  for (int i = 0; i < 12; i++) xr |= static_cast<uint32_t>(c.ribbons[static_cast<size_t>(i)] != 0) << (31 - i);
  Write32(r + kRibbon, xr, order);
  for (int i = 0; i < 4; i++) {
    uint8_t* w = r + kMoves + i * 4;
    Write16(w, c.moves[static_cast<size_t>(i)].id, order);
    w[2] = c.moves[static_cast<size_t>(i)].pp;
    w[3] = c.moves[static_cast<size_t>(i)].ppUps;
  }
  for (int i = 0; i < 6; i++) {
    Write16(r + kStats + i * 2, c.stats[static_cast<size_t>(i)], order);
    Write16(r + kEvs + i * 2, c.evs[static_cast<size_t>(i)], order);
    r[kIvs + i] = c.ivs[static_cast<size_t>(i)];
  }
  for (int i = 0; i < 5; i++) {
    r[kContest + i] = c.contest[static_cast<size_t>(i)];
    r[kMedals + i] = c.contestRanks[static_cast<size_t>(i)];
  }
  Write16(r + kShadowId, c.shadowId, order);
}

std::vector<Candidate> Candidates(const engine::colo::Save& save, const xd::legality::Rules& rules, ByteOrder order) {
  std::vector<Candidate> out;
  auto one = [&](const PokemonCore& core, const std::string& where) {
    if (core.IsEmpty()) return;
    Candidate cand;
    cand.where = where;
    cand.core = core;
    PokemonCore xdCore = core;
    if (core.heldItem != 0 && (core.heldItem >= kFirstColosseumOnlyItem || core.heldItem >= rules.items.size() ||
                               !rules.items[core.heldItem].exists)) {
      cand.lost.push_back("its held item (item #" + std::to_string(core.heldItem) + "): Pokemon XD doesn't have it");
      xdCore.heldItem = 0;
    }
    if (core.metLocation != 0) {
      cand.lost.push_back("the place it was met (a Colosseum place; XD's places are other ones)");
    }
    xdCore.metLocation = 0;
    if (core.WasShadow()) {
      cand.lost.push_back("its Shadow Pokemon record (it was purified: it arrives as an ordinary Pokemon)");
    }
    xdCore.shadowId = 0;
    xdCore.heartGauge = -1;
    if (core.egg) {
      cand.problem = "An egg: Pokemon XD has no eggs.";
    } else if (core.invalid) {
      cand.problem = "The Colosseum save marks it as invalid (a \"bad egg\").";
    } else if (core.IsOpenShadow()) {
      cand.problem = "A Shadow Pokemon that is not purified yet: it stays in the Colosseum save (its Shadow record can't come along). Purify it first.";
    } else if (core.origin[0] == 0 || core.origin[1] == 0 || core.origin[2] == 0 || core.origin[3] == 0) {
      cand.problem = "Its origin mark is incomplete: Pokemon XD would treat it as an empty place.";
    }
    XdFromCore(xdCore, cand.xd.data(), order);
    if (cand.problem.empty()) {
      xd::legality::Context ctx;
      ctx.movesNotJudged = true;
      const auto res = xd::legality::Check(xd::save::ParsePokemon(cand.xd.data(), order), rules, xd::legality::Level::Legal, ctx);
      if (!res.Ok()) cand.problem = "Not possible in Pokemon XD: " + res.Text(" ");
    }
    if (!cand.problem.empty()) cand.lost.clear();
    out.push_back(std::move(cand));
  };
  for (int i = 0; i < engine::colo::kPartySize; i++) one(save.party[static_cast<size_t>(i)], "Party " + std::to_string(i + 1));
  for (int b = 0; b < engine::colo::kBoxCount; b++) {
    for (int i = 0; i < engine::colo::kBoxSlots; i++) {
      one(save.boxes[static_cast<size_t>(b)][static_cast<size_t>(i)],
          "Box " + std::to_string(b + 1) + ", place " + std::to_string(i + 1));
    }
  }
  return out;
}

int FreeBoxPlaces(const xd::save::Save& save) {
  int n = 0;
  for (const auto& box : save.boxes) {
    for (const auto& p : box.slots) n += p.IsEmpty() ? 1 : 0;
  }
  return n;
}

bool PlaceInBoxes(xd::save::Save& save, const std::vector<std::array<uint8_t, xd::save::kPokemonSize>>& records, std::string& error) {
  const size_t base = xd::save::SectionOffsetInSlot(save, 3);
  if (base == 0 || save.slotBytes.size() != xd::save::kSlotSize) {
    error = "The save has no PC boxes section.";
    return false;
  }
  if (static_cast<int>(records.size()) > FreeBoxPlaces(save)) {
    error = "The PC boxes have room for " + std::to_string(FreeBoxPlaces(save)) + " more Pokemon, and " +
            std::to_string(records.size()) + " were chosen. Make room in the XD game first.";
    return false;
  }
  std::vector<uint8_t> work = save.slotBytes;
  size_t next = 0;
  for (int b = 0; b < xd::save::kBoxCount && next < records.size(); b++) {
    for (int j = 0; j < xd::save::kBoxSlots && next < records.size(); j++) {
      if (!save.boxes[static_cast<size_t>(b)].slots[static_cast<size_t>(j)].IsEmpty()) continue;
      uint8_t* at = work.data() + base + static_cast<size_t>(b) * kBoxStride + 0x14 + static_cast<size_t>(j) * xd::save::kPokemonSize;
      std::memcpy(at, records[next].data(), xd::save::kPokemonSize);
      save.boxes[static_cast<size_t>(b)].slots[static_cast<size_t>(j)] = xd::save::ParsePokemon(at, save.order);
      next++;
    }
  }
  save.slotBytes = std::move(work);
  return true;
}

}
