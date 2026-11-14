#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include "colo_models.h"

namespace fs = std::filesystem;

namespace colo {

namespace {

std::string lower(std::string s) {
  for (auto& c : s) c = (char)tolower((unsigned char)c);
  return s;
}

void write_file(const fs::path& p, const void* data, size_t n) {
  std::ofstream f(p, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write " + p.string());
  f.write((const char*)data, (std::streamsize)n);
  if (!f) throw std::runtime_error("cannot write " + p.string());
}

std::string collapse(const std::string& s) {
  std::string out;
  bool sp = false;
  for (unsigned char c : s) {
    if (isspace(c)) {
      sp = true;
      continue;
    }
    if (sp && !out.empty()) out += ' ';
    sp = false;
    out += (char)c;
  }
  return out;
}

struct Found {
  std::string id, file;
  Character who;
  long long size = 0;
  std::vector<uint8_t> data;
  std::string state, note;
};

}

void run_models(const RunArgs& a, const pbr::DiscOpener& open_disc, const std::function<void(const std::string&)>& say) {
  std::error_code ec;
  if (!fs::is_regular_file(fs::u8path(a.disc), ec)) throw pbr::Fail("Colosseum disc image not found: " + a.disc, 2);
  std::unique_ptr<pbr::DiscSource> disc = open_disc(a.disc, "Colosseum");
  if (disc->game_id() != COLO_ID)
    throw pbr::Fail("This disc image is " + disc->game_id() + ", but this needs " + COLO_ID + " (Pokemon Colosseum, USA).", 2);
  fs::path out = fs::absolute(fs::u8path(a.out));
  fs::path tmp = out;
  tmp += ".tmp";

  std::vector<std::pair<std::string, std::string>> todo;
  for (const auto& f : disc->files()) {
    const std::string base = f.first.substr(f.first.find_last_of('/') == std::string::npos ? 0 : f.first.find_last_of('/') + 1);
    const std::string lb = lower(base);
    if (lb.size() > 9 && lb.compare(0, 4, "pkx_") == 0 && lb.compare(lb.size() - 5, 5, ".fsys") == 0) {
      const std::string name = lb.substr(4, lb.size() - 9);
      if (is_trainer_name(name)) todo.emplace_back(f.first, name);
    }
  }
  std::sort(todo.begin(), todo.end(), [](const auto& x, const auto& y) { return x.second < y.second; });
  if (todo.empty()) throw pbr::Fail("No trainer model files (pkx_*_a1.fsys ...) on this disc.");

  if (fs::is_directory(tmp, ec)) fs::remove_all(tmp, ec);
  fs::create_directories(tmp / "models");
  auto t0 = std::chrono::steady_clock::now();
  std::vector<Found> found;
  long long nbytes = 0;
  int converted = 0, done = 0;
  const int total = (int)todo.size();
  for (const auto& t : todo) {
    Found r;
    r.id = t.second;
    r.file = "models/" + t.second + ".pkx";
    r.who = describe(t.second);
    try {
      const std::vector<uint8_t> archive = disc->read(t.first);
      const std::vector<pbr::FsysEntry> entries = pbr::fsys_entries(archive);
      const pbr::FsysEntry* pick = nullptr;
      for (const auto& e : entries)
        if (e.type == 15 && lower(e.name) == t.second) pick = &e;
      if (!pick) throw std::runtime_error("no model entry named " + t.second + " in " + t.first);
      const std::vector<uint8_t> pkx = pbr::fsys_extract(archive, *pick);
      std::vector<uint8_t> xd;
      std::string why;
      if (!convert_trainer_pkx(pkx, xd, why)) {
        r.state = "skipped";
        r.note = why;
      } else {
        r.data = std::move(xd);
        r.state = "converted";
        r.size = (long long)r.data.size();
        nbytes += r.size;
        converted++;
      }
    } catch (const std::exception& ex) {
      r.state = "failed";
      r.note = ex.what();
    }
    found.push_back(std::move(r));
    say("progress " + std::to_string(++done) + " " + std::to_string(total) + " " + t.second);
  }
  disc.reset();
  if (!a.xd_disc.empty() && converted > 0) {
    try {
      std::unique_ptr<pbr::DiscSource> xd = open_disc(a.xd_disc, "XD");
      if (xd->game_id() == XD_ID) {
        std::map<std::string, std::string> where;
        for (const auto& f : xd->files()) {
          const size_t slash = f.first.find_last_of('/');
          const std::string lb = lower(f.first.substr(slash == std::string::npos ? 0 : slash + 1));
          if (lb.size() > 9 && lb.compare(0, 4, "pkx_") == 0) where[lb.substr(4, lb.size() - 9)] = f.first;
        }
        for (auto& r : found) {
          auto w = where.find(r.id);
          if (r.state != "converted" || w == where.end()) continue;
          try {
            const std::vector<uint8_t> archive = xd->read(w->second);
            for (const auto& e : pbr::fsys_entries(archive)) {
              if (e.type != 15 || lower(e.name) != r.id) continue;
              const std::vector<uint8_t> theirs = pbr::fsys_extract(archive, e);
              if (theirs.size() == r.data.size() && theirs.size() > 0x84 && memcmp(&theirs[0x84], &r.data[0x84], theirs.size() - 0x84) == 0) {
                r.data[0x6E] = theirs[0x6E];
                r.data[0x6F] = theirs[0x6F];
                r.note = "head joint " + std::to_string(theirs[0x6F]) + " from XD's own copy";
              }
            }
          } catch (const std::exception&) {}
        }
      }
    } catch (const std::exception&) {}
  }
  for (auto& r : found) {
    if (r.state != "converted" || !r.note.empty()) continue;
    const int j = guess_head_joint(r.data);
    if (j > 0 && j < 0x10000) {
      r.data[0x6E] = (uint8_t)(j >> 8);
      r.data[0x6F] = (uint8_t)j;
      r.note = "head joint " + std::to_string(j) + " from the skeleton";
    }
  }
  for (const auto& r : found)
    if (r.state == "converted") write_file(tmp / "models" / (r.id + ".pkx"), r.data.data(), r.data.size());
  if (converted == 0) throw pbr::Fail("None of the trainer models could be converted (see the log: " + (tmp / "conversion_log.txt").string() + ").");

  std::map<std::string, int> same;
  auto base_of = [](const std::string& n) {
    size_t i = n.size();
    while (i > 0 && isdigit((unsigned char)n[i - 1])) i--;
    return (i < n.size() && i > 0 && n[i - 1] == ' ') ? n.substr(0, i - 1) : n;
  };
  for (const auto& r : found)
    if (r.state == "converted") same[base_of(r.who.name)]++;
  for (auto& r : found)
    if (r.who.name.size() > 2 && r.who.name.compare(r.who.name.size() - 2, 2, " 1") == 0 && same[base_of(r.who.name)] == 1)
      r.who.name.resize(r.who.name.size() - 2);

  auto rank = [](const Found& f) {
    static const char* const kGroups[] = {"Hero", "Cipher", "Gym", "Trainer", "Townsperson"};
    int g = 5;
    for (int i = 0; i < 5; i++)
      if (f.who.group == kGroups[i]) g = i;
    return std::make_pair(g, f.id == "ken_a1" ? 0 : 1);
  };
  std::stable_sort(found.begin(), found.end(), [&](const Found& x, const Found& y) { return rank(x) < rank(y); });

  std::string list = "{\n \"id\": \"colosseum-characters\",\n \"version\": 1,\n \"characters\": [\n";
  bool first = true;
  for (const auto& r : found) {
    if (r.state != "converted") continue;
    list += std::string(first ? "" : ",\n") + "  {\"id\": \"" + r.id + "\", \"name\": \"" + r.who.name + "\", \"group\": \"" + r.who.group +
            "\", \"file\": \"" + r.file + "\", \"source\": \"pkx_" + r.id + ".fsys\"}";
    first = false;
  }
  list += "\n ]\n}\n";
  write_file(tmp / "characters.json", list.data(), list.size());
  {
    std::string j = "{\n  \"id\": \"" + std::string(MOD_ID) + "\",\n  \"name\": \"Colosseum characters\",\n  \"version\": \"" + CONVERTER_VERSION +
                    "\",\n  \"author\": \"made on your PC from your own disc\",\n  \"description\": \"Pokemon Colosseum's trainer models "
                    "(Wes, Rui and the others) in Pokemon XD's model format, converted from your own Colosseum disc.\",\n  \"game\": \"" +
                    XD_ID + "\",\n  \"generated_from\": \"" + COLO_ID + "\",\n  \"applies\": \"restart\",\n  \"format\": 1\n}";
    write_file(tmp / "mod.json", j.data(), j.size());
  }
  {
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    char head[256];
    snprintf(head, sizeof head, "colo_models %s: %d trainer models, %d converted, %lld bytes, %.1f s\n", CONVERTER_VERSION, total, converted, nbytes, secs);
    std::string log = head;
    for (const auto& r : found) {
      log += "\n== " + r.id + ": " + r.state + " (" + r.who.name + ", " + r.who.group + "), " + std::to_string(r.size) + " bytes\n";
      if (!r.note.empty()) log += r.note + "\n";
    }
    write_file(tmp / "conversion_log.txt", log.data(), log.size());
  }
  fs::path old = out;
  old += ".old";
  if (fs::is_directory(old, ec)) fs::remove_all(old, ec);
  if (fs::is_directory(out, ec)) fs::rename(out, old);
  fs::rename(tmp, out);
  if (fs::is_directory(old, ec)) fs::remove_all(old, ec);
  say("done " + std::to_string(converted) + " " + std::to_string(total - converted) + " " + std::to_string(nbytes));
}

int colo_models_main(const std::vector<std::string>& args, const pbr::DiscOpener& open_disc) {
  auto say = [](const std::string& s) {
    fputs(s.c_str(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
  };
  if (args.size() == 1 && args[0] == "--version") {
    say(std::string("version ") + CONVERTER_VERSION);
    return 0;
  }
  RunArgs a;
  bool have_disc = false, have_out = false, bad = false;
  for (size_t i = 0; i < args.size(); i++) {
    std::string k = args[i], v;
    bool has_v = false;
    size_t eq = k.find('=');
    if (k.rfind("--", 0) == 0 && eq != std::string::npos) {
      v = k.substr(eq + 1);
      k = k.substr(0, eq);
      has_v = true;
    }
    auto take = [&](std::string& dst, bool& flag) {
      if (!has_v) {
        if (i + 1 >= args.size()) {
          bad = true;
          return;
        }
        v = args[++i];
      }
      dst = v;
      flag = true;
    };
    if (k == "--disc" || k == "--colosseum-disc") take(a.disc, have_disc);
    else if (k == "--out") take(a.out, have_out);
    else if (k == "--xd-disc") {
      if (!has_v && (i + 1 >= args.size() || args[i + 1].rfind("--", 0) == 0)) continue;
      bool f = false;
      take(a.xd_disc, f);
    }
    else bad = true;
  }
  if (bad || !have_disc || !have_out) {
    say("error Wrong command line: use --disc <Colosseum image> and --out <folder>.");
    return 1;
  }
  try {
    run_models(a, open_disc, say);
    return 0;
  } catch (const pbr::Fail& ex) {
    say("error " + collapse(ex.what()));
    return ex.code;
  } catch (const std::exception& ex) {
    say("error " + collapse(std::string("Error: ") + ex.what()));
    return 1;
  }
}

}
