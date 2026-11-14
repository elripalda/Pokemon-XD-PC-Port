#include "hd_textures.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <system_error>

namespace engine::hdtex {
namespace fs = std::filesystem;

namespace {

std::string Lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

bool ParseU32(std::string_view s, uint32_t& out) {
  if (s.empty() || s.size() > 9) return false;
  uint32_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint32_t>(c - '0');
  }
  out = v;
  return true;
}

bool ParseHex(std::string_view s, uint64_t& out) {
  if (s.empty() || s.size() > 16) return false;
  uint64_t v = 0;
  for (char c : s) {
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = (v << 4) | static_cast<uint64_t>(d);
  }
  out = v;
  return true;
}

std::vector<std::string_view> Split(std::string_view s, char sep) {
  std::vector<std::string_view> parts;
  size_t at = 0;
  while (true) {
    const size_t next = s.find(sep, at);
    if (next == std::string_view::npos) {
      parts.push_back(s.substr(at));
      return parts;
    }
    parts.push_back(s.substr(at, next - at));
    at = next + 1;
  }
}

std::string Utf8(const fs::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

bool IsPicture(const fs::path& p) {
  const std::string e = Lower(Utf8(p.extension()));
  return e == ".png" || e == ".dds";
}

}

std::string FormatName(const Name& n) {
  char buf[160];
  int len = std::snprintf(buf, sizeof buf, "tex1_%ux%u%s_", n.width, n.height, n.mipmapped ? "_m" : "");
  std::string out(buf, static_cast<size_t>(len));
  if (n.wildcardTex) {
    out += "$";
  } else {
    len = std::snprintf(buf, sizeof buf, "%016" PRIx64, n.texHash);
    out.append(buf, static_cast<size_t>(len));
  }
  if (n.hasTlut) {
    out += "_";
    if (n.wildcardTlut) {
      out += "$";
    } else {
      len = std::snprintf(buf, sizeof buf, "%016" PRIx64, n.tlutHash);
      out.append(buf, static_cast<size_t>(len));
    }
  }
  out += "_" + std::to_string(n.format);
  return out;
}

std::optional<Name> ParseName(std::string_view stem) {
  if (stem.substr(0, 5) != "tex1_") return std::nullopt;
  const size_t arb = stem.rfind("_arb");
  if (arb != std::string_view::npos) {
    std::string tmp(stem.substr(0, arb));
    tmp.append(stem.substr(arb + 4));
    auto inner = ParseName(tmp);
    if (!inner) return std::nullopt;
    inner->arbitraryMipmaps = true;
    return inner;
  }
  const size_t mip = stem.rfind("_mip");
  if (mip != std::string_view::npos) {
    uint32_t level = 0;
    if (!ParseU32(stem.substr(mip + 4), level) || level == 0) return std::nullopt;
    auto inner = ParseName(stem.substr(0, mip));
    if (!inner) return std::nullopt;
    inner->mipLevel = static_cast<int>(level);
    return inner;
  }
  Name n;
  const auto parts = Split(stem, '_');
  size_t i = 1;
  if (parts.size() < 4) return std::nullopt;
  const size_t x = parts[i].find('x');
  if (x == std::string_view::npos || !ParseU32(parts[i].substr(0, x), n.width) ||
      !ParseU32(parts[i].substr(x + 1), n.height) || n.width == 0 || n.height == 0) {
    return std::nullopt;
  }
  i++;
  if (parts[i] == "m") {
    n.mipmapped = true;
    i++;
  }
  const size_t left = parts.size() - i;
  if (left != 2 && left != 3) return std::nullopt;
  if (parts[i] == "$") n.wildcardTex = true;
  else if (!ParseHex(parts[i], n.texHash)) return std::nullopt;
  i++;
  if (left == 3) {
    n.hasTlut = true;
    if (parts[i] == "$") n.wildcardTlut = true;
    else if (!ParseHex(parts[i], n.tlutHash)) return std::nullopt;
    i++;
  }
  if (!ParseU32(parts[i], n.format)) return std::nullopt;
  return n;
}

std::vector<fs::path> FindPackFolders(const fs::path& root, std::string_view gameId) {
  std::vector<fs::path> result;
  std::error_code ec;
  if (root.empty() || !fs::is_directory(root, ec)) return result;
  const std::string id(gameId);
  const std::string id3 = id.substr(0, 3);
  auto add = [&](const fs::path& p) {
    for (const fs::path& q : result) {
      if (q == p) return;
    }
    result.push_back(p);
  };
  if (fs::is_directory(root / id, ec)) add(root / id);
  else if (id3 != id && fs::is_directory(root / id3, ec)) add(root / id3);
  std::set<fs::path> tops;
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2)) continue;
    if (Lower(Utf8(it->path().extension())) != ".txt") continue;
    const std::string base = Utf8(it->path().stem());
    if (base != id && base != id3 && base != "all") continue;
    const fs::path rel = it->path().lexically_relative(root);
    if (rel.empty() || rel.begin() == rel.end()) continue;
    const fs::path top = root / *rel.begin();
    if (fs::is_directory(top, e2)) tops.insert(top);
  }
  for (const fs::path& t : tops) add(t);
  return result;
}

PackInfo ScanPack(const fs::path& folder) {
  PackInfo info;
  info.folder = folder;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2)) continue;
    const fs::path& p = it->path();
    if (!IsPicture(p)) {
      info.otherFiles++;
      continue;
    }
    const auto name = ParseName(Utf8(p.stem()));
    if (!name) {
      info.otherFiles++;
      continue;
    }
    const uint64_t size = fs::file_size(p, e2);
    if (!e2) info.bytes += size;
    if (name->mipLevel > 0) {
      info.mipFiles++;
      continue;
    }
    info.textures++;
    if (name->wildcardTex || name->wildcardTlut) info.wildcards++;
    if (name->arbitraryMipmaps) info.arb++;
    if (info.firstName.empty()) info.firstName = Utf8(p.filename());
  }
  return info;
}

namespace {
const std::array<uint32_t, 256>& CrcTable() {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; n++) {
      uint32_t c = n;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  return table;
}
uint32_t Crc(const uint8_t* d, size_t n) {
  const auto& t = CrcTable();
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) crc = t[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}
void Put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x >> 24));
  v.push_back(static_cast<uint8_t>(x >> 16));
  v.push_back(static_cast<uint8_t>(x >> 8));
  v.push_back(static_cast<uint8_t>(x));
}
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  Put32(out, static_cast<uint32_t>(data.size()));
  std::vector<uint8_t> body(type, type + 4);
  body.insert(body.end(), data.begin(), data.end());
  out.insert(out.end(), body.begin(), body.end());
  Put32(out, Crc(body.data(), body.size()));
}
}

std::vector<uint8_t> EncodePngRgba(uint32_t width, uint32_t height, const uint8_t* rgba) {
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  Put32(ihdr, width);
  Put32(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  Chunk(out, "IHDR", ihdr);
  const size_t stride = static_cast<size_t>(width) * 4;
  std::vector<uint8_t> raw;
  raw.reserve((stride + 1) * height);
  for (uint32_t y = 0; y < height; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba + y * stride, rgba + (y + 1) * stride);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  size_t at = 0;
  do {
    const size_t n = std::min<size_t>(65535, raw.size() - at);
    const bool last = at + n >= raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(static_cast<uint8_t>(n & 0xFF));
    z.push_back(static_cast<uint8_t>(n >> 8));
    z.push_back(static_cast<uint8_t>(~n & 0xFF));
    z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
    z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
    at += n;
  } while (at < raw.size());
  Put32(z, (b << 16) | a);
  Chunk(out, "IDAT", z);
  Chunk(out, "IEND", {});
  return out;
}

bool WritePngRgba(const fs::path& file, uint32_t width, uint32_t height, const uint8_t* rgba) {
  if (width == 0 || height == 0 || rgba == nullptr) return false;
  const std::vector<uint8_t> bytes = EncodePngRgba(width, height, rgba);
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f) return false;
  }
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

}
