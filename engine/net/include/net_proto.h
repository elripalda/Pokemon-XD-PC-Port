#pragma once
#include <cstdint>
#include <vector>

#include "net_types.h"

namespace xdnet {

std::vector<uint8_t> stunBuildRequest(const uint8_t transactionId[12]);
bool stunParseResponse(const uint8_t* d, size_t n, const uint8_t transactionId[12], Endpoint& mapped);
bool looksLikeStun(const uint8_t* d, size_t n);

std::vector<uint8_t> trackerBuildConnect(uint32_t tx);
bool trackerParseConnect(const uint8_t* d, size_t n, uint32_t tx, uint64_t& connectionId);
enum TrackerEvent : uint32_t { kEvNone = 0, kEvCompleted = 1, kEvStarted = 2, kEvStopped = 3 };
std::vector<uint8_t> trackerBuildAnnounce(uint64_t connectionId, uint32_t tx, const uint8_t infohash[20],
                                          const uint8_t peerId[20], TrackerEvent ev, uint32_t key, uint16_t port);
bool trackerParseAnnounce(const uint8_t* d, size_t n, uint32_t tx, uint32_t& intervalSec, std::vector<Endpoint>& peers);
int trackerPeekAction(const uint8_t* d, size_t n, uint32_t& tx);

}
