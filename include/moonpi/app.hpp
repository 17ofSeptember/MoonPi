#pragma once
#include "ai.hpp"
#include "persistence.hpp"
#include "runtime.hpp"
namespace moonpi {
class App {
  mutable std::mutex mutex_;
  const Schemas &schemas_;
  const ComponentRegistry &components_;
  const BoardRegistry &boards_;
  GraphCompiler compiler_;
  ProcessLock process_lock_;
  mutable Runtime runtime_;
  UnavailableAiEngine ai_;
  AuditLog log_;
  fs::path data_;
  Json design_ = blank_project();
  Json saved_design_ = nullptr;
  Validation validation_;
  std::uint64_t revision_ = 1;
  bool recovery_ = false, load_failed_ = false;
  Json snapshot_locked() const;

public:
  App(const Schemas &, const ComponentRegistry &, const BoardRegistry &, fs::path,
      std::unique_ptr<IGpioController> = std::make_unique<SimulatedHardwareBackend>());
  ~App();
  Json snapshot() const;
  Json catalogue() const;
  Json command(const Json &);
  Json resources(const std::string &role) const;
};
} // namespace moonpi
