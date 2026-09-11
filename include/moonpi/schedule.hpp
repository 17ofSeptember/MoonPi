#pragma once
#include "graph.hpp"
#include "persistence.hpp"
#include <array>
#include <bitset>
#include <optional>
namespace moonpi {
class Cron {
  std::array<std::bitset<64>, 5> fields_;
  bool any_day_ = false, any_weekday_ = false;

public:
  explicit Cron(const std::string &);
  bool matches(int64_t utc_seconds) const;
};
int64_t parse_utc(const std::string &);
class CalendarScheduler {
  fs::path path_;
  Json records_ = Json::object();
  std::string load_error_;
  struct Entry {
    std::string node, fingerprint;
    std::optional<Cron> cron;
    int64_t at = 0;
    bool catch_up = false, checked = false;
  };
  std::vector<Entry> entries_;
  int64_t start_ = 0, last_minute_ = 0;

public:
  explicit CalendarScheduler(fs::path = {});
  void configure(const std::vector<CompiledNode> &, int64_t now);
  std::vector<std::string> due(int64_t now);
  void stop() {
    entries_.clear();
  }
};
} // namespace moonpi
