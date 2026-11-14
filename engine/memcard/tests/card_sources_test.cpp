#include <card_sources.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace engine::memcard;
using namespace engine::memcard::sources;
namespace fs = std::filesystem;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
void put32(uint8_t* p, uint32_t v) { put16(p, uint16_t(v >> 16)); put16(p + 2, uint16_t(v)); }

void makeEntry(uint8_t* e, const char* game, const char* name, uint16_t first, uint16_t blocks) {
  std::memset(e, 0, kDirEntrySize);
  std::memcpy(e, game, 4);
  std::memcpy(e + 4, "01", 2);
  e[6] = 0xFF;
  std::memcpy(e + 8, name, std::strlen(name));
  put32(e + 0x2C, 0x40);
  put16(e + 0x36, first);
  put16(e + 0x38, blocks);
  put16(e + 0x3A, 0xFFFF);
}

std::vector<uint8_t> makeGci(const char* game, const char* name, uint16_t blocks) {
  std::vector<uint8_t> v(kDirEntrySize + size_t(blocks) * kBlockSize, 0x5A);
  makeEntry(v.data(), game, name, 5, blocks);
  return v;
}

void sealDir(uint8_t* dir, int16_t code) {
  put16(dir + 0x1FFA, uint16_t(code));
  uint16_t s, si;
  CardChecksum(dir, kBlockSize - 4, &s, &si);
  put16(dir + 0x1FFC, s);
  put16(dir + 0x1FFE, si);
}
void sealFat(uint8_t* fat, int16_t code) {
  put16(fat + 4, uint16_t(code));
  uint16_t s, si;
  CardChecksum(fat + 4, kBlockSize - 4, &s, &si);
  put16(fat, s);
  put16(fat + 2, si);
}

std::vector<uint8_t> makeCard() {
  std::vector<uint8_t> card(64 * kBlockSize, 0);
  put16(card.data() + 0x22, 4);
  for (int d = 1; d <= 2; d++) std::memset(card.data() + d * kBlockSize, 0xFF, kBlockSize);
  for (int d = 1; d <= 2; d++) {
    uint8_t* dir = card.data() + d * kBlockSize;
    makeEntry(dir, "GXXE", "PokemonXD", 5, 2);
    sealDir(dir, int16_t(d));
  }
  for (int f = 3; f <= 4; f++) {
    uint8_t* fat = card.data() + f * kBlockSize;
    put16(fat + 5 * 2, 6);
    put16(fat + 6 * 2, 0xFFFF);
    sealFat(fat, int16_t(f));
  }
  return card;
}

void write(const fs::path& p, const std::vector<uint8_t>& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}
void writeText(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary);
  out << text;
}

Environment envOf(Environment::Os os, std::map<std::string, std::string> vars) {
  return {os, [vars](const char* name) {
            const auto it = vars.find(name);
            return it == vars.end() ? std::string() : it->second;
          }};
}

}

int main() {
  const fs::path root = fs::temp_directory_path() / "xd_card_sources_test";
  fs::remove_all(root);
  fs::create_directories(root);
  const std::string R = root.generic_string();

  write(root / "win" / "AppData" / "Roaming" / "Dolphin Emulator" / "GC" / "USA" / "Card A" / "01-GXXE-PokemonXD.gci",
        makeGci("GXXE", "PokemonXD", 2));
  write(root / "win" / "AppData" / "Roaming" / "Dolphin Emulator" / "GC" / "USA" / "Card A" / "note.txt", {1, 2});
  write(root / "win" / "AppData" / "Roaming" / "Dolphin Emulator" / "GC" / "EUR" / "Card A" / "01-GXXP-PokemonXD.gci",
        makeGci("GXXP", "PokemonXD", 2));
  write(root / "win" / "Documents" / "Dolphin Emulator" / "GC" / "MemoryCardA.USA.raw", makeCard());
  write(root / "win" / "Documents" / "Dolphin Emulator" / "GC" / "MemoryCardA.EUR.raw", makeCard());
  {
    const Environment env = envOf(Environment::Os::Windows,
                                  {{"APPDATA", R + "/win/AppData/Roaming"}, {"USERPROFILE", R + "/win"}});
    const auto users = DolphinUserFolders(env);
    check(users.size() == 2, "Windows: both Dolphin folders found (AppData and Documents)");
    const auto found = FindDolphinCardFiles(users);
    int gci = 0, raw = 0;
    for (const Found& f : found) (f.cardImage ? raw : gci)++;
    check(gci == 1 && raw == 1, "Windows: one .gci (USA only; .txt and EUR left out) and one USA card image");
  }
  write(root / "lin" / "home" / ".local" / "share" / "dolphin-emu" / "GC" / "USA" / "Card A" / "a.gci",
        makeGci("GXXE", "PokemonXD", 2));
  write(root / "lin" / "elsewhere" / "mycard.raw", makeCard());
  write(root / "lin" / "gcifolder" / "b.gci", makeGci("GXXE", "PokemonXD", 2));
  writeText(root / "lin" / "home" / ".local" / "share" / "dolphin-emu" / "Config" / "Dolphin.ini",
            "[Core]\nMemcardAPath = " + R + "/lin/elsewhere/mycard.raw\nGCIFolderBPath = " + R + "/lin/gcifolder\n");
  {
    const Environment env = envOf(Environment::Os::Linux, {{"HOME", R + "/lin/home"}});
    const auto users = DolphinUserFolders(env);
    check(users.size() == 1, "Linux: ~/.local/share/dolphin-emu found");
    const auto found = FindDolphinCardFiles(users);
    check(found.size() == 3, "Linux: the GCI folder file, the ini's card image and the ini's GCI folder file (" +
                                 std::to_string(found.size()) + ")");
  }
  write(root / "mac" / "Library" / "Application Support" / "Dolphin" / "GC" / "USA" / "Card A" / "c.gci",
        makeGci("GXXE", "PokemonXD", 2));
  {
    const Environment env = envOf(Environment::Os::Mac, {{"HOME", R + "/mac"}});
    const auto users = DolphinUserFolders(env);
    check(users.size() == 1 && FindDolphinCardFiles(users).size() == 1, "Mac: Application Support/Dolphin found");
  }
  {
    const Environment env = envOf(Environment::Os::Linux, {{"HOME", R + "/nobody"}});
    check(DolphinUserFolders(env).empty() && FindDolphinCardFiles({}).empty(), "no Dolphin: nothing found, no error");
  }
  {
    const Environment env = envOf(Environment::Os::Windows, {{"XD_DOLPHIN_USER_DIR", R + "/mac/Library/Application Support/Dolphin"}});
    check(DolphinUserFolders(env).size() == 1, "XD_DOLPHIN_USER_DIR is looked at");
  }

  std::vector<CardFile> files;
  Container kind;
  std::string error;
  check(ReadAnyCardFile(makeGci("GXXE", "PokemonXD", 3), files, kind, error) && kind == Container::Gci &&
            files.size() == 1 && files[0].data.size() == 3 * kBlockSize,
        ".gci read");
  check(ReadAnyCardFile(makeCard(), files, kind, error) && kind == Container::CardImage && files.size() == 1 &&
            files[0].entry.fileName == "PokemonXD",
        "card image read, the file found by name");
  {
    std::vector<uint8_t> v(0x80, 0);
    std::memcpy(v.data(), "DATELGC_SAVE", 12);
    const auto g = makeGci("GXXE", "PokemonXD", 2);
    v.insert(v.end(), g.begin(), g.end());
    check(ReadAnyCardFile(v, files, kind, error) && kind == Container::DatelSav && files.size() == 1, "Datel .sav wrapper read");
  }
  {
    std::vector<uint8_t> v(0x110, 0);
    std::memcpy(v.data(), "GCSAVE", 6);
    const auto g = makeGci("GXXE", "PokemonXD", 2);
    v.insert(v.end(), g.begin(), g.end());
    check(ReadAnyCardFile(v, files, kind, error) && kind == Container::Gcs, "GameShark .gcs wrapper read");
  }
  check(!ReadAnyCardFile(std::vector<uint8_t>(0x20000, 0xFF), files, kind, error) && error.find("Game Boy Advance") != std::string::npos,
        "a 128 KB file is named as a GBA save");
  check(!ReadAnyCardFile(std::vector<uint8_t>(1000, 1), files, kind, error) && error.find("not a GameCube") != std::string::npos,
        "garbage: plain refusal (" + error.substr(0, 40) + "...)");
  check(!ReadAnyCardFile({}, files, kind, error), "empty file refused");
  {
    std::vector<uint8_t> v(0x80, 0);
    std::memcpy(v.data(), "DATELGC_SAVE", 12);
    check(!ReadAnyCardFile(v, files, kind, error) && error.find(".gci") != std::string::npos, "a broken wrapper says to export .gci");
  }

  fs::remove_all(root);
  std::printf(sOk ? "card_sources_test: all passed\n" : "card_sources_test: FAILED\n");
  return sOk ? 0 : 1;
}
