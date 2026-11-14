#pragma once

#include <SDL3/SDL.h>
#include <controls.h>
#include <imgui.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace launcher::pad {

enum class Btn : uint8_t {
  Up, Down, Left, Right,
  Confirm,
  Cancel,
  X,
  Y,
  LB, RB,
  LT, RT,
  Start,
  Back,
  Count
};
constexpr size_t kBtnCount = static_cast<size_t>(Btn::Count);

struct PadInfo {
  SDL_Gamepad* pad = nullptr;
  engine::controls::Device device = engine::controls::Device::Generic;
};

class Input {
 public:
  void Update(const std::vector<PadInfo>& pads, bool live);
  bool Down(Btn b) const { return cur_[idx(b)]; }
  bool Pressed(Btn b) const { return cur_[idx(b)] && !prev_[idx(b)]; }
  bool Released(Btn b) const { return !cur_[idx(b)] && prev_[idx(b)]; }
  bool PressedRepeat(Btn b) const { return repeat_[idx(b)]; }
  bool AnyDown() const;
  bool Activity() const { return activity_; }
  engine::controls::Device Device() const { return device_; }
  void SetDevice(engine::controls::Device d) { device_ = d; deviceFixed_ = true; }
  void Virtual(Btn b, int passes = 3) { virt_[idx(b)] = passes; virtualUsed_ = true; }
  void Feed(ImGuiIO& io, bool consume, bool tweaking);
  bool Present() const { return present_ || virtualUsed_; }

 private:
  static size_t idx(Btn b) { return static_cast<size_t>(b); }
  std::array<bool, kBtnCount> cur_{}, prev_{}, repeat_{};
  std::array<uint64_t, kBtnCount> since_{}, last_{};
  std::array<int, kBtnCount> virt_{};
  std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> rawPrev_{};
  bool stickPrev_ = false;
  bool activity_ = false, present_ = false, virtualUsed_ = false, deviceFixed_ = false;
  engine::controls::Device device_ = engine::controls::Device::Xbox;
};

struct Prompt {
  Btn button;
  std::string text;
};
float DrawPrompts(ImDrawList* dl, ImVec2 pos, float width, const std::vector<Prompt>& prompts, engine::controls::Device device,
                  ImFont* font, float scale, ImU32 ink, ImU32 dim);
float DrawButton(ImDrawList* dl, float left, float y, float size, Btn button, engine::controls::Device device,
                 ImFont* font);
std::string ButtonName(Btn button, engine::controls::Device device);

class Keyboard {
 public:
  void Update(Input& in, ImGuiIO& io, bool wanted, uint64_t now);
  bool Open() const { return open_; }
  void Draw(float scale, ImFont* font, ImFont* small, engine::controls::Device device);
  void Type(const std::string& text) { scriptText_ += text; }
  void SetSystemKeyboard(std::function<bool()> ask) { systemKeyboard_ = std::move(ask); }
  float Height() const { return height_; }

 private:
  struct Key {
    std::string normal, shifted, symbol;
    enum class Kind : uint8_t { Char, Shift, Symbols, Space, Left, Right, Back, Done, System } kind = Kind::Char;
    float width = 1.0f;
  };
  void Build();
  void PressKey(const Key& k, ImGuiIO& io);
  std::vector<std::vector<Key>> rows_;
  bool open_ = false;
  bool shift_ = false, symbols_ = false;
  int row_ = 1, col_ = 0;
  float height_ = 0;
  struct Pending { ImGuiKey key; bool down; };
  std::vector<Pending> release_;
  std::string scriptText_;
  std::function<bool()> systemKeyboard_;
  std::string preview_;
  int caret_ = 0;
  bool waitRelease_ = false;
  uint64_t openedAt_ = 0;
};

struct FileFilter {
  std::string name;
  std::vector<std::string> exts;
};

class FileBrowser {
 public:
  enum class Mode { OpenFile, SaveFile, OpenFolder };
  struct Request {
    Mode mode = Mode::OpenFile;
    std::string title;
    std::vector<FileFilter> filters;
    std::filesystem::path start;
  };
  explicit FileBrowser(std::filesystem::path recentFile = {}) : recentFile_(std::move(recentFile)) {}
  void Open(const Request& request, std::function<void(const std::string&)> done);
  bool IsOpen() const { return open_; }
  void Draw(float scale, ImFont* heading, ImFont* small, const Input& in, bool controllerPrompts);
  std::vector<Prompt> Prompts() const;
  std::string Where() const;
  struct Place {
    std::string label;
    std::filesystem::path path;
  };
  static std::vector<Place> DefaultPlaces();
  void Poll();

 private:
  struct Entry {
    std::string name;
    bool folder = false;
    uint64_t size = 0;
  };
  void Go(const std::filesystem::path& folder, const std::string& select = {});
  void Look();
  bool Matches(const std::string& name) const;
  void Finish(const std::string& result);
  void LoadRecent();
  void AddRecent(const std::filesystem::path& folder);
  Request req_;
  std::function<void(const std::string&)> done_;
  std::filesystem::path recentFile_, folder_;
  std::vector<std::filesystem::path> recent_;
  std::vector<Entry> entries_;
  std::string error_;
  std::string name_;
  std::string select_;
  size_t filter_ = 0;
  bool open_ = false, opening_ = false, rootList_ = false, goUp_ = false, focusList_ = false;
  bool showHidden_ = false;
  int framesOpen_ = 0;
  bool recentLoaded_ = false;
  std::vector<Place> places_;
  std::string result_;
  bool hasResult_ = false;
};

void FocusLastItem();
void EnterCurrentWindow();
void WarnAboutChildWindows();
std::string FocusInfo();

bool InputText(const char* label, std::string* text, ImGuiInputTextFlags flags = 0);

}
