#ifndef XD_COLOSSEUM_PAGE_H
#define XD_COLOSSEUM_PAGE_H

#include <launcher.h>

#include <filesystem>
#include <string>
#include <vector>

namespace xd::colosseumpage {

bool Open(const std::filesystem::path& cardRoot, const std::filesystem::path& file, const std::string& discPath,
          launcher::GameInfo::Saves::MonsView& out, std::string& error);

launcher::GameInfo::Saves::TradeResult Import(const std::filesystem::path& cardRoot, const std::filesystem::path& file,
                                              const std::string& discPath, const std::vector<int>& rows);

}

#endif
