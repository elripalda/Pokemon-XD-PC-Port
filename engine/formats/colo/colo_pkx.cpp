#include <cctype>
#include <cstring>
#include <map>

#include "colo_models.h"
#include "pbr_hsd_read.h"
#include <functional>

namespace colo {

namespace {

uint32_t rd32(const std::vector<uint8_t>& b, size_t o) {
  return ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) | b[o + 3];
}
void wr32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  b[o] = (uint8_t)(v >> 24);
  b[o + 1] = (uint8_t)(v >> 16);
  b[o + 2] = (uint8_t)(v >> 8);
  b[o + 3] = (uint8_t)v;
}
void wr32le(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  b[o] = (uint8_t)v;
  b[o + 1] = (uint8_t)(v >> 8);
  b[o + 2] = (uint8_t)(v >> 16);
  b[o + 3] = (uint8_t)(v >> 24);
}
size_t align32(size_t v) { return (v + 31) & ~(size_t)31; }

const size_t kColoHeader = 0x40, kXdHeader = 0x84, kSection = 0xD0;

}

bool convert_trainer_pkx(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string& why) {
  out.clear();
  if (in.size() < kColoHeader + 0x20) {
    why = "too short for a model file";
    return false;
  }
  const uint32_t size = rd32(in, 0), count = rd32(in, 8);
  if (size == 0 || size > in.size() || kColoHeader + (size_t)size > in.size()) {
    why = "the model archive does not fit in the file";
    return false;
  }
  if (rd32(in, kColoHeader) != size) {
    why = "no model archive at 0x40 (its size word is not the header's)";
    return false;
  }
  if (count != 0x11) {
    why = "not a trainer model (" + std::to_string(count) + " animation sections, not 17)";
    return false;
  }
  const size_t secs = align32(kColoHeader + (size_t)size);
  if (secs + (size_t)count * kSection > in.size()) {
    why = "the animation sections do not fit in the file";
    return false;
  }
  std::vector<uint8_t> o(kXdHeader + (size_t)count * kSection, 0);
  wr32(o, 0x00, size);
  wr32(o, 0x10, count);
  o[0x1A] = 0x00;
  o[0x1B] = 0x0C;
  for (int k = 0; k < 4; k++) {
    const size_t r = 0x1C + 0x13 * (size_t)k;
    memset(&o[r + 2], 0xFF, 16);
  }
  const uint32_t third = rd32(in, 0x20);
  if (third != 0xFFFFFFFFu) {
    const size_t r = 0x1C + 0x13 * 2;
    o[r] = 1;
    o[r + 0x12] = (uint8_t)third;
  }
  for (uint32_t k = 0; k < 4; k++) wr32le(o, 0x70 + 4 * k, k);
  memset(&o[0x80], 0x7F, 4);
  static const float kInv60 = (float)(1.0 / 60.0);
  for (uint32_t k = 0; k < count; k++) {
    const uint8_t* cs = &in[secs + (size_t)k * kSection];
    uint8_t* xs = &o[kXdHeader + (size_t)k * kSection];
    memcpy(xs, cs, 0x8C);
    for (int w : {0x10, 0x14, 0x18, 0x1C}) {
      const uint32_t v = ((uint32_t)cs[w] << 24) | ((uint32_t)cs[w + 1] << 16) | ((uint32_t)cs[w + 2] << 8) | cs[w + 3];
      if (v == 0) continue;
      const float f = (float)((double)(float)v * (double)kInv60);
      uint32_t bits;
      memcpy(&bits, &f, 4);
      xs[w] = (uint8_t)(bits >> 24);
      xs[w + 1] = (uint8_t)(bits >> 16);
      xs[w + 2] = (uint8_t)(bits >> 8);
      xs[w + 3] = (uint8_t)bits;
    }
    const uint32_t used = ((uint32_t)cs[4] << 24) | ((uint32_t)cs[5] << 16) | ((uint32_t)cs[6] << 8) | cs[7];
    for (uint32_t i = 0; i < 8; i++) {
      const uint32_t idx = ((uint32_t)cs[0x90 + 8 * i] << 24) | ((uint32_t)cs[0x91 + 8 * i] << 16) | ((uint32_t)cs[0x92 + 8 * i] << 8) |
                           cs[0x93 + 8 * i];
      uint32_t type = 0;
      if (k == 0 && i == 0) type = 2;
      else if (k != 0 && idx != 0 && i < used) type = 1;
      wr32(o, kXdHeader + (size_t)k * kSection + 0x8C + 8 * i, type);
      wr32(o, kXdHeader + (size_t)k * kSection + 0x90 + 8 * i, idx);
    }
    wr32(o, kXdHeader + (size_t)k * kSection + 0xCC, 3);
  }
  o.resize(align32(o.size()), 0);
  o.insert(o.end(), in.begin() + kColoHeader, in.begin() + kColoHeader + size);
  o.resize(align32(o.size()), 0);
  out = std::move(o);
  return true;
}

int guess_head_joint(const std::vector<uint8_t>& xd_pkx) {
  try {
    pbr::ReadResult r = pbr::hsd_read_pkx(xd_pkx, false);
    const auto& J = r.model.joints;
    const int n = (int)J.size();
    if (n < 3) return 0;
    std::vector<pbr::Mat> W(n);
    std::vector<double> ys(n);
    std::vector<std::vector<int>> kids(n);
    std::vector<int> roots;
    for (int i = 0; i < n; i++) {
      const pbr::Mat loc = pbr::mat_srt(J[i].scale, J[i].rot, J[i].pos);
      const int p = J[i].parent;
      if (p >= i) return 0;
      W[i] = p < 0 ? loc : pbr::mat_mul(W[p], loc);
      ys[i] = pbr::mat_point(W[i], {0.0, 0.0, 0.0})[1];
      if (p < 0) roots.push_back(i);
      else kids[p].push_back(i);
    }
    if (roots.empty()) return 0;
    double ytop = ys[0];
    for (double y : ys) ytop = std::max(ytop, y);
    std::vector<double> sub(n);
    for (int i = n - 1; i >= 0; i--) {
      sub[i] = ys[i];
      for (int c : kids[i]) sub[i] = std::max(sub[i], sub[c]);
    }
    auto best_kid = [&](int k) {
      int b = kids[k][0];
      for (int c : kids[k]) if (sub[c] > sub[b]) b = c;
      return b;
    };
    int best = -1, cur = roots[0];
    for (;;) {
      if (kids[cur].size() >= 3 && ys[cur] <= 0.8 * ytop) best = cur;
      if (kids[cur].empty()) break;
      cur = best_kid(cur);
    }
    return best < 0 ? 0 : best_kid(best);
  } catch (const std::exception&) {
    return 0;
  }
}

bool is_trainer_name(const std::string& n) {
  size_t i = n.size();
  while (i > 0 && n[i - 1] >= '0' && n[i - 1] <= '9') i--;
  return i < n.size() && i >= 2 && n[i - 1] == 'a' && n[i - 2] == '_';
}

namespace {
struct Role {
  const char* stem;
  const char* name;
  const char* group;
};
const Role kRoles[] = {
    {"ken", "Wes", "Hero"},
    {"hizuki", "Rui", "Hero"},
    {"agent", "Cipher Peon", "Cipher"},
    {"agb", "GBA Trainer", "Trainer"},
    {"snach_zako", "Snagem Grunt", "Cipher"},
    {"boss555", "Boss 555", "Cipher"},
    {"boss777", "Boss 777", "Cipher"},
    {"boss999", "Boss 999", "Cipher"},
    {"boss_s", "Boss S", "Cipher"},
    {"syotyou", "Chief", "Cipher"},
    {"akami", "Akami", "Cipher"},
    {"gym_a", "Gym Trainer A", "Gym"},
    {"gym_b", "Gym Trainer B", "Gym"},
    {"gym_c", "Gym Trainer C", "Gym"},
    {"hunter", "Hunter", "Trainer"},
    {"rider", "Rider", "Trainer"},
    {"sport", "Sports Fan", "Trainer"},
    {"thief", "Thief", "Trainer"},
    {"niku", "Butcher", "Trainer"},
    {"mania", "Maniac", "Trainer"},
    {"worker", "Worker", "Trainer"},
    {"traner", "Trainer", "Trainer"},
    {"warugaki", "Brat", "Trainer"},
    {"obo", "Obo", "Trainer"},
    {"ojo", "Young Lady", "Trainer"},
    {"battleyama", "Battleyama", "Trainer"},
    {"ippan", "Citizen", "Townsperson"},
    {"kenkyuin", "Researcher", "Townsperson"},
    {"caster_a", "Newscaster", "Townsperson"},
    {"geinin", "Entertainer", "Townsperson"},
    {"grandpa", "Grandpa", "Townsperson"},
    {"jiji_a", "Old Man", "Townsperson"},
    {"baba_a", "Old Woman", "Townsperson"},
    {"ojisan", "Uncle", "Townsperson"},
    {"sensei", "Teacher", "Townsperson"},
    {"sityo", "Mayor", "Townsperson"},
};
}

Character describe(const std::string& entry) {
  Character c;
  c.id = entry;
  std::string s = entry;
  size_t i = s.size();
  while (i > 0 && s[i - 1] >= '0' && s[i - 1] <= '9') i--;
  const std::string number = s.substr(i);
  if (i >= 2 && s[i - 1] == 'a' && s[i - 2] == '_') s.resize(i - 2);
  std::string sex;
  if (s.size() > 2 && s[s.size() - 2] == '_' && (s.back() == 'm' || s.back() == 'f')) {
    sex = s.back() == 'm' ? " (man)" : " (woman)";
    s.resize(s.size() - 2);
  }
  const Role* role = nullptr;
  for (const Role& r : kRoles)
    if (s == r.stem) role = &r;
  if (!role && !s.empty())
    for (const Role& r : kRoles)
      if (s + "_a" == r.stem) role = &r;
  if (role) {
    c.name = role->name;
    c.group = role->group;
  } else {
    c.name = s;
    if (!c.name.empty()) c.name[0] = (char)toupper((unsigned char)c.name[0]);
    c.group = "Other";
  }
  const bool hero = c.group == "Hero";
  if (!hero) {
    c.name += sex;
    if (!number.empty()) c.name += " " + number;
  }
  return c;
}

}
