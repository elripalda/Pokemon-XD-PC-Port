#include "trainer_card.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace xd::card {

const std::u16string& NameChars() {
  static const std::u16string chars = [] {
    std::u16string s;
    for (char16_t c = u'A'; c <= u'Z'; c++) s += c;
    for (char16_t c = u'a'; c <= u'z'; c++) s += c;
    for (char16_t c = u'0'; c <= u'9'; c++) s += c;
    s += u' ';
    s += u"!?";
    s += char16_t(0x2642);
    s += char16_t(0x2640);
    s += char16_t(0x201C);
    s += char16_t(0x201D);
    s += char16_t(0x2018);
    s += u"'/-.,";
    s += char16_t(0x2030);
    return s;
  }();
  return chars;
}

bool NameCharAllowed(char16_t c) { return NameChars().find(c) != std::u16string::npos; }

std::u16string SanitizeName(const std::u16string& in) {
  std::u16string out;
  for (char16_t c : in) {
    if (out.size() >= kMaxName) break;
    out += NameCharAllowed(c) ? c : u'?';
  }
  return out;
}

std::string NameToUtf8(const std::u16string& name) {
  std::string out;
  for (char16_t c : name) {
    if (c >= 0x20 && c < 0x7F) out += static_cast<char>(c);
    else { char b[8]; std::snprintf(b, sizeof b, "\\u%04X", static_cast<unsigned>(c)); out += b; }
  }
  return out;
}

namespace {
bool ModelIdOk(const std::string& id) {
  if (id.empty() || id.size() > 32) return false;
  for (char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}
}

std::string Encode(const Card& c) {
  std::string hex;
  for (char16_t ch : c.name) { char b[8]; std::snprintf(b, sizeof b, "%04x", static_cast<unsigned>(ch)); hex += b; }
  if (hex.empty()) hex = "-";
  return "K " + std::to_string(c.title) + " " + std::to_string(static_cast<int>(c.model.source)) + " " + c.model.id + " " + hex;
}

bool Decode(const std::string& line, Card& out, std::string* why) {
  std::istringstream in(line);
  std::string tag, title, src, model, hex;
  if (!(in >> tag >> title >> src >> model >> hex) || tag != "K") { if (why) *why = "not a card message"; return false; }
  Card c;
  char* end = nullptr;
  const long t = std::strtol(title.c_str(), &end, 10);
  if (*end || t < -1 || t > 4095) { if (why) *why = "title out of range"; return false; }
  c.title = static_cast<int>(t);
  if (src != "0" && src != "1") { if (why) *why = "unknown model source"; return false; }
  c.model.source = src == "1" ? Source::Colosseum : Source::Xd;
  if (!ModelIdOk(model)) { if (why) *why = "model id not allowed"; return false; }
  c.model.id = model;
  if (hex != "-") {
    if (hex.size() % 4 != 0 || hex.size() > 4 * 64) { if (why) *why = "name length"; return false; }
    std::u16string raw;
    for (size_t i = 0; i < hex.size(); i += 4) {
      const std::string part = hex.substr(i, 4);
      char* e2 = nullptr;
      const unsigned long v = std::strtoul(part.c_str(), &e2, 16);
      if (*e2) { if (why) *why = "name digits"; return false; }
      raw += static_cast<char16_t>(v);
    }
    c.name = SanitizeName(raw);
  }
  out = c;
  return true;
}

bool LoadFile(const std::string& path, Card& out) {
  std::ifstream f(path);
  std::string line;
  return f && std::getline(f, line) && Decode(line, out, nullptr);
}

bool SaveFile(const std::string& path, const Card& c) {
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  f << Encode(c) << "\n";
  return static_cast<bool>(f);
}

}
