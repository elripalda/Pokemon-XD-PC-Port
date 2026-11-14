#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::hdtex {

struct Name {
  uint32_t width = 0;
  uint32_t height = 0;
  bool mipmapped = false;
  bool wildcardTex = false;
  uint64_t texHash = 0;
  bool hasTlut = false;
  bool wildcardTlut = false;
  uint64_t tlutHash = 0;
  uint32_t format = 0;
  bool arbitraryMipmaps = false;
  int mipLevel = 0;
};

std::string FormatName(const Name& name);

std::optional<Name> ParseName(std::string_view stem);

std::vector<std::filesystem::path> FindPackFolders(const std::filesystem::path& root, std::string_view gameId);

struct PackInfo {
  std::filesystem::path folder;
  uint64_t textures = 0;
  uint64_t mipFiles = 0;
  uint64_t wildcards = 0;
  uint64_t arb = 0;
  uint64_t bytes = 0;
  uint64_t otherFiles = 0;
  std::string firstName;
};
PackInfo ScanPack(const std::filesystem::path& folder);

bool WritePngRgba(const std::filesystem::path& file, uint32_t width, uint32_t height, const uint8_t* rgba);

std::vector<uint8_t> EncodePngRgba(uint32_t width, uint32_t height, const uint8_t* rgba);

}
