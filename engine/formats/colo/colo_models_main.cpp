#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "colo_models.h"
#include "port_disc.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

class PortSource : public pbr::DiscSource {
 public:
  explicit PortSource(std::string path) : path_(std::move(path)) {}
  ~PortSource() override { PortDisc_Close(); }
  void open_first() {
    char err[512];
    if (!PortDisc_Open(path_.c_str(), err, sizeof err)) {
      std::string e = err;
      while (!e.empty() && isspace((unsigned char)e.back())) e.pop_back();
      throw pbr::Fail(path_ + " cannot be read as a disc image (" + e + ").", 2);
    }
    id_ = PortDisc_GameId();
    PortDisc_ListFiles([](const char* p, uint32_t size, void* user) { static_cast<PortSource*>(user)->list_.emplace_back(p, size); }, this);
  }
  std::string game_id() const override { return id_; }
  std::vector<std::pair<std::string, uint32_t>> files() const override { return list_; }
  std::vector<uint8_t> read(const std::string& path) const override {
    size_t n = 0;
    uint8_t* d = PortDisc_ReadOriginalFile(path.c_str(), &n);
    if (!d) throw std::runtime_error("read " + path);
    std::vector<uint8_t> v(d, d + n);
    PortDisc_Free(d);
    return v;
  }

 private:
  std::string path_, id_;
  std::vector<std::pair<std::string, uint32_t>> list_;
};

int run_main(const std::vector<std::string>& args) {
  pbr::DiscOpener opener = [](const std::string& path, const char*) -> std::unique_ptr<pbr::DiscSource> {
    auto s = std::make_unique<PortSource>(path);
    s->open_first();
    return s;
  };
  return colo::colo_models_main(args, opener);
}

}

#ifdef _WIN32
static std::string utf8(const wchar_t* w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s((size_t)(n > 0 ? n - 1 : 0), '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) args.push_back(utf8(argv[i]));
  return run_main(args);
}
#else
int main(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) args.push_back(argv[i]);
  return run_main(args);
}
#endif
