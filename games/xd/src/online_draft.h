#ifndef XD_ONLINE_DRAFT_H
#define XD_ONLINE_DRAFT_H

#include "online_battle.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace xd::online::draft {

struct Species {
  uint16_t id = 0;
  bool valid = false;
  int gen = 3;
  bool fullyEvolved = false;
  bool hasPreEvo = false;
  bool legend = false;
};

struct Settings {
  std::string mode = "snake";
  int teamSize = 6;
  int poolSize = 24;
  int bans = 0;
  std::string first = "host";
  bool gen4 = false;
  bool fullyEvolved = false;
  bool finalStage = false;
  bool noLegend = false;
  int offerCount = 3;
};

bool ParseSettings(const std::string& mode, const std::string& text, Settings& out, std::string* error);
std::string SettingsText(const Settings& s);
std::string SettingsDescribe(const Settings& s);
bool CheckForRoom(const Settings& s, const std::string& ruleSet, const std::vector<Species>& table, std::string* error);

std::vector<uint16_t> Candidates(const Settings& s, const std::vector<Species>& table);
std::vector<uint16_t> MakePool(const Settings& s, const std::vector<Species>& table, uint64_t seed);
int FirstPlayer(const Settings& s, uint64_t seed);
int SnakeTurn(int pickIndex, int first);
std::vector<uint16_t> Offer(const Settings& s, const std::vector<Species>& table, uint64_t seed, int player, int slot, const std::vector<uint16_t>& taken);

enum class Mark { Free, Banned, PickedByHost, PickedByGuest };

class Draft {
public:
  Draft(const Settings& s, const std::vector<Species>& table, std::vector<uint16_t> pool, uint64_t seed);

  enum class Phase { Banning, Picking, Done };
  Phase phase() const { return phase_; }
  const Settings& settings() const { return s_; }
  const std::vector<uint16_t>& pool() const { return pool_; }
  int first() const { return first_; }
  Mark MarkOf(uint16_t id) const;
  const std::vector<uint16_t>& Team(int player) const { return team_[player]; }
  int Turn() const;
  bool CanPick(int player) const;
  std::vector<uint16_t> CurrentOffer(int player) const;
  int PickNumber() const { return picks_; }

  bool BansSubmitted(int player) const { return bansIn_[player]; }
  bool SubmitBans(int player, const std::vector<uint16_t>& ids, std::string* why);
  bool Pick(int player, uint16_t id, std::string* why);
  void UndoLastPick(int player);
  uint16_t AutoPick(int player) const;
  std::vector<uint16_t> AutoBans(int player) const;
  bool Done() const { return phase_ == Phase::Done; }
  uint64_t TeamsHash() const;

private:
  bool InPool(uint16_t id) const;
  Settings s_;
  std::vector<Species> table_;
  std::vector<uint16_t> pool_;
  uint64_t seed_;
  int first_;
  Phase phase_;
  std::vector<uint16_t> banned_;
  std::vector<uint16_t> bansOf_[2];
  bool bansIn_[2] = {false, false};
  std::vector<uint16_t> team_[2];
  int picks_ = 0;
};

class Session {
public:
  enum class State { Waiting, Banning, Picking, Finishing, Done, Failed };

  Session(bool host, const Settings& s, int timerSeconds, const std::vector<Species>& table, BattleTransport* link, std::function<std::string()> nonce,
          std::function<double()> now);

  void Start(uint64_t hostSeed);
  void Poll();
  State state() const { return state_; }
  const std::string& error() const { return error_; }
  bool Ready() const { return draft_ != nullptr; }
  const Draft& draft() const { return *draft_; }
  int me() const { return host_ ? 0 : 1; }
  bool MyTurn() const;
  bool Choose(uint16_t id, std::string* why = nullptr);
  bool ChooseBans(const std::vector<uint16_t>& ids, std::string* why = nullptr);
  double TimeLeft() const;
  bool Finished() const { return state_ == State::Done; }
  std::string lastRefusal;
  const std::vector<uint16_t>& Team(int player) const { return draft_->Team(player); }

private:
  void Send(const std::string& line);
  void Fail(const std::string& why, bool tell);
  void Handle(const std::string& line);
  void Advance();
  void StartTimer();
  bool host_;
  Settings s_;
  int timer_;
  std::vector<Species> table_;
  BattleTransport* link_;
  std::function<std::string()> nonce_;
  std::function<double()> now_;
  State state_ = State::Waiting;
  std::string error_;
  std::unique_ptr<Draft> draft_;
  uint64_t seed_ = 0;
  std::string myNonce_, myBanRecord_, myCommit_, peerCommit_;
  bool sentBc_ = false, sentBv_ = false;
  std::string peerBv_;
  double deadline_ = -1;
  int timerFor_ = -1;
  bool sentEnd_ = false, gotEnd_ = false;
  uint64_t peerEnd_ = 0;
  bool bansWanted_ = false;
  int refusalsSent_ = 0, refusalsGot_ = 0;
};

}

#endif
