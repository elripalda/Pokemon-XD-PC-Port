#ifndef ENGINE_CARD_SOURCES_H
#define ENGINE_CARD_SOURCES_H

#include <gc_memcard.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace engine::memcard::sources {

struct Environment {
  enum class Os { Windows, Linux, Mac };
  Os os;
  std::function<std::string(const char* name)> getenv;
};
Environment RealEnvironment();

std::vector<std::filesystem::path> DolphinUserFolders(const Environment& env);

struct Found {
  std::filesystem::path path;
  bool cardImage = false;
  std::string where;
};
std::vector<Found> FindDolphinCardFiles(const std::vector<std::filesystem::path>& userFolders,
                                        const std::string& region = "USA");

enum class Container { Gci, CardImage, Gcs, DatelSav, Unknown };
const char* ContainerName(Container c);

bool ReadAnyCardFile(const std::vector<uint8_t>& bytes, std::vector<CardFile>& files, Container& kind,
                     std::string& error);

}

#endif
