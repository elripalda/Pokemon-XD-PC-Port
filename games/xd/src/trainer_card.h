#ifndef XD_TRAINER_CARD_H
#define XD_TRAINER_CARD_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xd::card {

constexpr size_t kMaxName = 10;
constexpr int kTitleDefault = -1;

enum class Source : uint8_t { Xd = 0, Colosseum = 1 };

struct Model {
  Source source = Source::Xd;
  std::string id = "0";
  bool operator==(const Model& o) const { return source == o.source && id == o.id; }
};

struct Card {
  int title = kTitleDefault;
  std::u16string name;
  Model model;
  bool operator==(const Card& o) const { return title == o.title && name == o.name && model == o.model; }
};

bool NameCharAllowed(char16_t c);
const std::u16string& NameChars();
std::u16string SanitizeName(const std::u16string& in);
std::string NameToUtf8(const std::u16string& name);

std::string Encode(const Card& c);
bool Decode(const std::string& line, Card& out, std::string* why);

bool LoadFile(const std::string& path, Card& out);
bool SaveFile(const std::string& path, const Card& c);

}

#endif
