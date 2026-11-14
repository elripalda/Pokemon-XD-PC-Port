#include <gs_formats.h>
#include <port_disc.h>

#include "xd_languages.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion = "1.0.0";

uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
void PutBe32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((uint8_t)(x >> 24));
  v.push_back((uint8_t)(x >> 16));
  v.push_back((uint8_t)(x >> 8));
  v.push_back((uint8_t)x);
}

[[noreturn]] void Fail(int code, const std::string& message) {
  std::printf("error %s\n", message.c_str());
  std::fflush(stdout);
  std::exit(code);
}

std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool EndsWith(const std::string& s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string BaseName(const std::string& name) {
  for (const char* suf : {"_fr.fsys", "_ge.fsys", "_it.fsys", "_sp.fsys"}) {
    if (EndsWith(name, suf)) return name.substr(0, name.size() - 8) + ".fsys";
  }
  return {};
}

struct DiscFiles {
  std::vector<std::string> fsys;
  std::set<std::string> all;
};
void Collect(const char* path, uint32_t, void* user) {
  auto* d = static_cast<DiscFiles*>(user);
  d->all.insert(path);
  if (EndsWith(path, ".fsys")) d->fsys.emplace_back(path);
}

struct UsInfo {
  std::map<std::string, std::map<uint16_t, std::vector<uint8_t>>> rels;
  std::map<std::string, int> messages;
  std::vector<uint8_t> toc;
  std::set<std::string> files;
};

bool WriteFile(const fs::path& path, const void* data, size_t size) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(static_cast<const char*>(data), (std::streamsize)size);
  return (bool)out;
}

struct Group {
  uint32_t id;
  std::string name;
};
std::vector<Group> ReadToc(const uint8_t* d, size_t size, std::vector<uint8_t>* header) {
  std::vector<Group> out;
  if (size < 0x20) return out;
  const uint32_t count = Be32(d + 8), groups = Be32(d + 0x10);
  if (header != nullptr) header->assign(d, d + 0x20);
  for (uint32_t i = 0; i < count; i++) {
    const size_t at = groups + (size_t)i * 16;
    if (at + 16 > size) break;
    const uint32_t id = Be32(d + at), nameAt = Be32(d + at + 4);
    if (nameAt >= size) continue;
    out.push_back({id, std::string(reinterpret_cast<const char*>(d + nameAt))});
  }
  return out;
}
std::vector<uint8_t> WriteToc(const std::vector<uint8_t>& header, const std::vector<Group>& groups) {
  std::vector<uint8_t> names, out = header;
  std::vector<uint32_t> nameAt;
  for (const Group& g : groups) {
    nameAt.push_back((uint32_t)(0x20 + names.size()));
    names.insert(names.end(), g.name.begin(), g.name.end());
    names.push_back(0);
  }
  while ((0x20 + names.size()) % 16 != 0) names.push_back(0);
  const uint32_t groupOffset = (uint32_t)(0x20 + names.size());
  out.resize(0x20);
  out[8] = (uint8_t)(groups.size() >> 24);
  out[9] = (uint8_t)(groups.size() >> 16);
  out[10] = (uint8_t)(groups.size() >> 8);
  out[11] = (uint8_t)groups.size();
  out[0x10] = (uint8_t)(groupOffset >> 24);
  out[0x11] = (uint8_t)(groupOffset >> 16);
  out[0x12] = (uint8_t)(groupOffset >> 8);
  out[0x13] = (uint8_t)groupOffset;
  out.insert(out.end(), names.begin(), names.end());
  for (size_t i = 0; i < groups.size(); i++) {
    PutBe32(out, groups[i].id);
    PutBe32(out, nameAt[i]);
    PutBe32(out, 0);
    PutBe32(out, 0);
  }
  return out;
}

bool DolRead(const uint8_t* dol, size_t size, uint32_t address, uint32_t n, std::vector<uint8_t>& out) {
  out.assign(n, 0);
  if (PortDol_Read(dol, size, address, out.data(), n)) return true;
  for (uint32_t take = n; take >= 0x40; take -= std::min<uint32_t>(take, 0x100)) {
    out.assign(take, 0);
    if (PortDol_Read(dol, size, address, out.data(), take)) return true;
  }
  out.clear();
  return false;
}

struct BankKind {
  const char* name;
  std::vector<uint8_t> head;
  std::vector<uint8_t> tail;
};
struct FoundBank {
  uint32_t address;
  std::string tag;
};
std::vector<FoundBank> FindBanks(const uint8_t* dol, size_t size, const BankKind& kind) {
  std::vector<FoundBank> out;
  for (uint32_t i = 0; i < 18; i++) {
    const uint32_t address = Be32(dol + 0x48 + 4 * i), length = Be32(dol + 0x90 + 4 * i), fileOff = Be32(dol + 4 * i);
    if (length == 0 || (uint64_t)fileOff + length > size) continue;
    const uint8_t* s = dol + fileOff;
    for (uint32_t k = 0; k + 24 <= length; k += 4) {
      if (std::memcmp(s + k, kind.head.data(), kind.head.size()) != 0) continue;
      if (std::memcmp(s + k + 8, "\0\0\0\0\0\0\0\0", 8) != 0) continue;
      if (std::memcmp(s + k + 16, kind.tail.data(), kind.tail.size()) != 0) continue;
      out.push_back({address + k, std::string(reinterpret_cast<const char*>(s + k + 6), 2)});
    }
  }
  std::sort(out.begin(), out.end(), [](const FoundBank& a, const FoundBank& b) { return a.address < b.address; });
  return out;
}

uint32_t BankTextEnd(const std::vector<uint8_t>& b) {
  const uint32_t count = (uint32_t)b[4] << 8 | b[5];
  uint32_t most = 0;
  for (uint32_t i = 0; i < count && 0x10 + 8 * i + 8 <= b.size(); i++) most = std::max(most, Be32(b.data() + 0x10 + 8 * i + 4));
  uint32_t at = most;
  while (at + 2 <= b.size() && (b[at] | b[at + 1]) != 0) at += 2;
  return at + 2;
}

}

int main(int argc, char** argv) {
  std::string palDisc, xdDisc, out;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--version") {
      std::printf("xd_languages %s\n", kVersion);
      return 0;
    }
    const auto value = [&](std::string& dst) {
      if (i + 1 < argc) dst = argv[++i];
    };
    if (a == "--pal-disc") value(palDisc);
    else if (a == "--xd-disc") value(xdDisc);
    else if (a == "--out") value(out);
  }
  if (palDisc.empty() || xdDisc.empty() || out.empty()) {
    std::fprintf(stderr, "Usage: xd_languages --pal-disc <GXXP01 image> --xd-disc <GXXE01 image> --out <folder>\n");
    return 1;
  }
  const fs::path outFinal = fs::path(out), outTmp = fs::path(out + ".tmp");
  std::error_code ec;
  fs::remove_all(outTmp, ec);

  UsInfo us;
  char error[512];
  if (!PortDisc_Open(xdDisc.c_str(), error, sizeof error)) Fail(1, std::string("can't read the USA disc: ") + error);
  if (std::strcmp(PortDisc_GameId(), "GXXE01") != 0) {
    Fail(3, std::string("the main disc is ") + PortDisc_GameId() + ", not the USA copy (GXXE01)");
  }
  {
    DiscFiles list;
    PortDisc_ListFiles(Collect, &list);
    us.files = list.all;
    size_t tocSize = 0;
    if (uint8_t* toc = PortDisc_ReadOriginalFile("GSfsys.toc", &tocSize)) {
      us.toc.assign(toc, toc + tocSize);
      PortDisc_Free(toc);
    }
    int n = 0;
    for (const std::string& name : list.fsys) {
      std::printf("progress %d %zu reading the USA disc\n", n++, list.fsys.size() * 2 + 8);
      std::fflush(stdout);
      size_t size = 0;
      uint8_t* data = PortDisc_ReadOriginalFile(name.c_str(), &size);
      FsysArchive fsys{};
      if (data == nullptr || !Fsys_Open(data, size, &fsys)) {
        PortDisc_Free(data);
        continue;
      }
      for (uint32_t i = 0; i < fsys.count; i++) {
        FsysEntry e{};
        if (!Fsys_Entry(&fsys, i, &e)) continue;
        if (e.type == 5) us.messages[Lower(name)]++;
        if (e.type == 14 || e.type == 6) {
          uint32_t relSize = 0;
          if (uint8_t* rel = Fsys_Extract(&fsys, i, &relSize)) {
            us.rels[Lower(name)][e.id].assign(rel, rel + relSize);
            std::free(rel);
          }
        }
      }
      PortDisc_Free(data);
    }
  }
  if (us.toc.empty()) Fail(1, "the USA disc has no archive list (GSfsys.toc)");
  PortDisc_Close();

  if (!PortDisc_Open(palDisc.c_str(), error, sizeof error)) Fail(1, std::string("can't read the European disc: ") + error);
  if (std::strcmp(PortDisc_GameId(), xd::languages::kPalGameId) != 0) {
    Fail(2, std::string("this disc is ") + PortDisc_GameId() + ", not the European copy (GXXP01)");
  }
  DiscFiles pal;
  PortDisc_ListFiles(Collect, &pal);
  size_t palTocSize = 0;
  uint8_t* palToc = PortDisc_ReadOriginalFile("GSfsys.toc", &palTocSize);
  if (palToc == nullptr) Fail(1, "the European disc has no archive list (GSfsys.toc)");
  std::vector<uint8_t> usHeader;
  std::vector<Group> usGroups = ReadToc(us.toc.data(), us.toc.size(), &usHeader);
  const std::vector<Group> palGroups = ReadToc(palToc, palTocSize, nullptr);
  PortDisc_Free(palToc);
  std::set<uint32_t> usIds;
  for (const Group& g : usGroups) usIds.insert(g.id);

  std::vector<std::string> wanted;
  for (const std::string& name : pal.fsys) {
    const std::string lower = Lower(name);
    const bool languageNamed = !BaseName(name).empty();
    const bool palOnly = us.files.count(name) == 0;
    bool moreText = false;
    if (!languageNamed && !palOnly) {
      moreText = true;
    }
    (void)lower;
    if (languageNamed || palOnly || moreText) wanted.push_back(name);
  }

  fs::create_directories(outTmp / "files", ec);
  fs::create_directories(outTmp / "tables", ec);
  uint64_t bytes = 0;
  int made = 0, kept = 0;
  std::vector<std::string> madeNames;
  std::map<std::string, uint32_t> madeEntries;
  const size_t total = wanted.size() + 8;
  size_t done = pal.fsys.size() + 0;
  (void)done;
  size_t step = 0;
  for (const std::string& name : wanted) {
    step++;
    std::printf("progress %zu %zu %s\n", step, total, name.c_str());
    std::fflush(stdout);
    size_t size = 0;
    uint8_t* data = PortDisc_ReadOriginalFile(name.c_str(), &size);
    FsysArchive fsys{};
    if (data == nullptr || !Fsys_Open(data, size, &fsys)) {
      PortDisc_Free(data);
      continue;
    }
    int messages = 0;
    std::vector<FsysReplacement> replacements;
    std::vector<std::vector<uint8_t>> keep;
    bool missingRel = false;
    keep.reserve(fsys.count);
    for (uint32_t i = 0; i < fsys.count; i++) {
      FsysEntry e{};
      if (!Fsys_Entry(&fsys, i, &e)) continue;
      if (e.type == 5) messages++;
      if (e.type != 14 && e.type != 6) continue;
      const std::vector<uint8_t>* rel = nullptr;
      for (const std::string& usName : {BaseName(name), name}) {
        if (rel != nullptr || usName.empty() || us.files.count(usName) == 0) continue;
        auto archive = us.rels.find(Lower(usName));
        if (archive == us.rels.end()) continue;
        auto it = archive->second.find(e.id);
        if (it != archive->second.end()) rel = &it->second;
      }
      if (rel == nullptr) {
        if (e.type == 6) continue;
        missingRel = true;
        break;
      }
      keep.push_back(*rel);
      replacements.push_back({i, keep.back().data(), (uint32_t)keep.back().size()});
    }
    const bool languageNamed = !BaseName(name).empty(), palOnly = us.files.count(name) == 0;
    const bool mapWithText = !languageNamed && !palOnly && messages > us.messages[Lower(name)];
    if (!languageNamed && !palOnly && !mapWithText) {
      PortDisc_Free(data);
      kept++;
      continue;
    }
    if (missingRel) {
      std::fprintf(stderr, "skipped %s: it holds code and the USA disc has no counterpart\n", name.c_str());
      PortDisc_Free(data);
      continue;
    }
    size_t newSize = 0;
    uint8_t* rebuilt = replacements.empty() ? nullptr : Fsys_Rebuild(data, size, replacements.data(), replacements.size(), &newSize);
    const uint8_t* write = rebuilt != nullptr ? rebuilt : data;
    const size_t writeSize = rebuilt != nullptr ? newSize : size;
    if (!replacements.empty() && rebuilt == nullptr) {
      PortDisc_Free(data);
      Fail(1, "could not rebuild " + name);
    }
    if (!WriteFile(outTmp / "files" / name, write, writeSize)) {
      std::free(rebuilt);
      PortDisc_Free(data);
      Fail(1, "could not write " + name + " (is there room in the data folder?)");
    }
    bytes += writeSize;
    made++;
    madeNames.push_back(name);
    madeEntries[name] = fsys.count;
    std::free(rebuilt);
    PortDisc_Free(data);
  }

  std::vector<std::string> languages;
  {
    std::printf("progress %zu %zu text and fonts\n", total - 1, total);
    std::fflush(stdout);
    size_t dolSize = 0;
    const uint8_t* dol = PortDisc_Dol(&dolSize);
    if (dol == nullptr) Fail(1, "the European disc's main program could not be read");
    const BankKind common{"common_tbl", {0, 0, 0, 1, 0x03, 0x5B}, {0, 0, 0, 1}};
    const BankKind system{"system_error_tbl", {0, 0, 0, 0, 0x00, 0x05}, {0, 0, 0, 0x65, 0, 0, 0, 0x38}};
    for (const BankKind* kind : {&common, &system}) {
      const std::vector<FoundBank> found = FindBanks(dol, dolSize, *kind);
      for (size_t i = 0; i < found.size(); i++) {
        const xd::languages::Language* lang = nullptr;
        for (const auto& l : xd::languages::All()) {
          if (found[i].tag == l.tag) lang = &l;
        }
        const bool uk = found[i].tag == "UK";
        if (lang == nullptr && !uk) continue;
        if (uk) continue;
        std::vector<uint8_t> bank;
        const uint32_t room = i + 1 < found.size() ? found[i + 1].address - found[i].address : 0x20000;
        if (!DolRead(dol, dolSize, found[i].address, std::min<uint32_t>(room, 0x20000), bank)) Fail(1, "a text table could not be read");
        const uint32_t end = std::min<uint32_t>(BankTextEnd(bank) + 0x20, (uint32_t)bank.size());
        bank.resize(end);
        const std::string file = std::string(kind->name) + "_" + lang->id;
        if (!WriteFile(outTmp / "tables" / (file + ".bin"), bank.data(), bank.size())) Fail(1, "could not write " + file);
        bytes += bank.size();
        if (kind == &common) languages.push_back(lang->id);
      }
    }
    struct Font {
      const char* name;
      uint32_t address, size;
    };
    for (const Font& f : {Font{"resident_fnt", 0x80330768, 0x45060}, Font{"common_fnt", 0x803757C8, 0x310},
                          Font{"_menu_sprite", 0x803018D8, 0x11E98}}) {
      std::vector<uint8_t> data;
      const bool font = f.name[0] != '_';
      if (!DolRead(dol, dolSize, f.address, f.size, data) || (font && (data[2] != 0x16 || data[3] != 0x16))) {
        Fail(1, std::string("the font ") + f.name + " is not where it was expected in the European disc's main program");
      }
      if (!WriteFile(outTmp / "tables" / (std::string(f.name) + ".bin"), data.data(), data.size())) Fail(1, "could not write a font");
      bytes += data.size();
    }
  }
  {
    struct LayoutRow {
      int table, row;
    };
    static const LayoutRow kRows[] = {
#include "xd_languages_layout_rows.inc"
    };
    static const uint32_t kPalTable[4] = {0x804277B8, 0x80313D20, 0x804212D0, 0x8042CB28};
    static const uint32_t kPalSize[4] = {0x4F50, 0xFA28, 0x32C0, 0x1228};
    size_t dolSize = 0;
    const uint8_t* dol = PortDisc_Dol(&dolSize);
    std::vector<uint8_t> out;
    for (const LayoutRow& r : kRows) {
      std::vector<uint8_t> row;
      if (!DolRead(dol, dolSize, kPalTable[r.table] + (uint32_t)r.row * 28, 28, row) || (uint32_t)r.row * 28 + 28 > kPalSize[r.table]) {
        Fail(1, "a menu layout row is not where it was expected in the European disc's main program");
      }
      out.push_back((uint8_t)r.table);
      out.push_back(0);
      out.push_back((uint8_t)(r.row >> 8));
      out.push_back((uint8_t)r.row);
      out.push_back(row[1]);
      out.push_back(0);
      out.insert(out.end(), row.begin() + 6, row.begin() + 0x0E);
      out.insert(out.end(), row.begin() + 0x10, row.begin() + 0x14);
      out.insert(out.end(), row.begin() + 0x18, row.begin() + 0x1A);
    }
    if (!WriteFile(outTmp / "tables" / "menu_layout.bin", out.data(), out.size())) Fail(1, "could not write the menu layout");
    bytes += out.size();
    std::vector<uint8_t> images;
    for (int t = 0; t < 4; t++) {
      for (uint32_t row = 0; row * 28 + 28 <= kPalSize[t]; row++) {
        std::vector<uint8_t> r;
        if (!DolRead(dol, dolSize, kPalTable[t] + row * 28, 28, r)) Fail(1, "a menu item row is not in the European main program");
        images.insert(images.end(), r.begin() + 4, r.begin() + 6);
        images.insert(images.end(), r.begin() + 0x0E, r.begin() + 0x10);
      }
    }
    if (!WriteFile(outTmp / "tables" / "menu_images.bin", images.data(), images.size())) Fail(1, "could not write the menu pictures");
    bytes += images.size();
  }
  {
    size_t size = 0;
    uint8_t* fsysData = PortDisc_ReadOriginalFile("common.fsys", &size);
    FsysArchive fsys{};
    if (fsysData == nullptr || !Fsys_Open(fsysData, size, &fsys)) Fail(1, "the European disc's common.fsys could not be read");
    int relIndex = -1;
    for (uint32_t i = 0; i < fsys.count; i++) {
      FsysEntry e{};
      if (Fsys_Entry(&fsys, i, &e) && e.type == 14 && std::strcmp(e.name, "common_rel") == 0) relIndex = (int)i;
    }
    uint32_t relSize = 0;
    uint8_t* rel = relIndex >= 0 ? Fsys_Extract(&fsys, (uint32_t)relIndex, &relSize) : nullptr;
    if (rel == nullptr) Fail(1, "the European common_rel could not be read");
    std::vector<std::pair<uint32_t, std::string>> found;
    for (uint32_t at = 0; at + 24 <= relSize; at += 4) {
      if (rel[at] == 0 && rel[at + 1] == 0 && rel[at + 2] == 0 && rel[at + 3] == 1 && rel[at + 4] == 0x07 && rel[at + 5] == 0x8B &&
          std::memcmp(rel + at + 8, "\0\0\0\0\0\0\0\0", 8) == 0) {
        found.push_back({at, std::string(reinterpret_cast<const char*>(rel + at + 6), 2)});
      }
    }
    int taken = 0;
    for (size_t i = 0; i < found.size(); i++) {
      const xd::languages::Language* lang = nullptr;
      for (const auto& l : xd::languages::All()) {
        if (found[i].second == l.tag && std::strcmp(l.id, "us") != 0) lang = &l;
      }
      if (lang == nullptr) continue;
      const uint32_t end = i + 1 < found.size() ? found[i + 1].first : relSize;
      std::vector<uint8_t> bank(rel + found[i].first, rel + end);
      if (i + 1 == found.size()) bank.resize(std::min<size_t>(bank.size(), BankTextEnd(bank) + 0x20));
      if (!WriteFile(outTmp / "tables" / (std::string("table_res_") + lang->id + ".bin"), bank.data(), bank.size())) Fail(1, "could not write a text bank");
      bytes += bank.size();
      taken++;
    }
    std::free(rel);
    PortDisc_Free(fsysData);
    if (taken < 4) Fail(1, "the European common_rel's text banks were not found");
  }
  if (languages.empty()) Fail(1, "no text tables of the other languages were found in the European disc's main program");
  std::sort(languages.begin(), languages.end());
  languages.erase(std::unique(languages.begin(), languages.end()), languages.end());
  std::vector<std::string> ordered;
  for (const auto& l : xd::languages::All()) {
    if (std::find(languages.begin(), languages.end(), l.id) != languages.end()) ordered.push_back(l.id);
  }
  {
    std::set<std::string> madeSet(madeNames.begin(), madeNames.end());
    std::vector<Group> merged = usGroups;
    for (const Group& g : palGroups) {
      if (usIds.count(g.id) == 0 && madeSet.count(g.name + ".fsys")) merged.push_back(g);
    }
    for (const std::string& id : ordered) {
      std::vector<Group> list = merged;
      int renamed = 0;
      for (Group& g : list) {
        const std::string target = g.name + "_" + id;
        const auto it = madeEntries.find(target + ".fsys");
        if (BaseName(g.name + ".fsys").empty() && it != madeEntries.end() && it->second > 0) {
          g.name = target;
          renamed++;
        }
      }
      const std::vector<uint8_t> toc = WriteToc(usHeader, list);
      if (!WriteFile(outTmp / ("lang_" + id) / "files" / "GSfsys.toc", toc.data(), toc.size())) Fail(1, "could not write the archive list");
      bytes += toc.size();
      std::fprintf(stderr, "language %s: %d archive names changed\n", id.c_str(), renamed);
    }
  }
  std::string names;
  for (const std::string& id : ordered) names += (names.empty() ? "" : ", ") + std::string(xd::languages::Find(id)->name);
  const std::string json = xd::languages::PackJson(ordered, made, xd::languages::kPalGameId);
  WriteFile(outTmp / xd::languages::kPackFile, json.data(), json.size());
  const std::string mod = std::string("{\n  \"id\": \"") + xd::languages::kManifestId +
                          "\",\n  \"name\": \"Languages\",\n  \"version\": \"" + kVersion +
                          "\",\n  \"description\": \"The other languages of the European disc.\",\n  \"game\": [\"GXXE01\"],\n"
                          "  \"count\": " + std::to_string(ordered.size()) + ",\n  \"unit\": \"language\",\n  \"units\": \"languages\",\n"
                          "  \"detail\": \"" + names + "\"\n}\n";
  WriteFile(outTmp / "mod.json", mod.data(), mod.size());

  fs::remove_all(outFinal, ec);
  fs::rename(outTmp, outFinal, ec);
  if (ec) Fail(1, "could not put the finished pack in place: " + ec.message());
  std::printf("done %d %d %llu\n", made, kept, (unsigned long long)bytes);
  std::fflush(stdout);
  return 0;
}
