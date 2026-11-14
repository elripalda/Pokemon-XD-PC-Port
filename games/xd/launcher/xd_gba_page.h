#ifndef XD_GBA_PAGE_H
#define XD_GBA_PAGE_H

#include <launcher.h>

#include <filesystem>
#include <string>

namespace xd::gbapage {

bool Open(const std::filesystem::path& cardRoot, const std::filesystem::path& gbaFile, const std::string& discPath,
          launcher::GameInfo::Saves::TradeView& out, std::string& error);

launcher::GameInfo::Saves::TradeResult Trade(const std::filesystem::path& cardRoot, const std::filesystem::path& gbaFile,
                                      const std::string& discPath, int xdSlot, int gbaSlot);

}

#endif
