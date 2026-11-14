#ifndef XD_PC_TRADE_H
#define XD_PC_TRADE_H

#include <xd_trade.h>

#include <memory>
#include <string>

std::unique_ptr<xd::trade::Trader> PortXD_MakeLiveTrader(const xd::trade::Config& cfg);

std::string PortXD_LiveMessage(uint32_t key);

bool PortXD_PcTradeSwallowInput();

bool PortXD_PcTradeAvailable();
bool PortXD_OpenOnlineTradeLive();
bool PortXD_ShowRuleMessage(const std::string& text);

#endif
