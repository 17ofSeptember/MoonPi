#pragma once
#include "common.hpp"
namespace moonpi {
std::string session_cookie(std::string_view cookies);
bool secret_equal(std::string_view a, std::string_view b);
class Sessions {
  mutable std::mutex mutex_;
  std::map<std::string, Clock::time_point> sessions_;

public:
  std::string create(const std::string &existing = {}, Clock::time_point now = Clock::now());
  bool valid(const std::string &, Clock::time_point now = Clock::now()) const;
  void revoke(const std::string &);
};
} // namespace moonpi
