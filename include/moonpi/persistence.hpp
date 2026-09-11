#pragma once
#include "common.hpp"
namespace moonpi {
void atomic_write(const fs::path &path, const std::string &data, bool backup = true);
class ProcessLock {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  explicit ProcessLock(const fs::path &);
  ~ProcessLock();
};
class AuditLog {
  fs::path path_;
  mutable std::mutex mutex_;
  std::string error_;
  size_t rotation_bytes_;

public:
  explicit AuditLog(fs::path path, size_t rotation_bytes = 1024 * 1024)
      : path_(std::move(path)), rotation_bytes_(rotation_bytes) {}
  void record(const std::string &event, const Json &fields = Json::object());
  Json status() const;
};
} // namespace moonpi
