#include "moonpi/hardware.hpp"
#include <set>
namespace moonpi {
void SimulatedHardwareBackend::maybe_fail() {
  if (fail_after_ == 0) {
    fail_after_ = -1;
    throw Error("hardware.injected_failure", "Simulated GPIO operation failed");
  }
  if (fail_after_ > 0)
    --fail_after_;
}
void SimulatedHardwareBackend::configure(int bcm, const std::string &owner, bool safe) {
  maybe_fail();
  if (lines_.contains(bcm))
    throw Error("hardware.busy", "GPIO is already requested");
  lines_.emplace(bcm, GpioState{owner, safe});
}
void SimulatedHardwareBackend::write(int bcm, bool value) {
  maybe_fail();
  auto it = lines_.find(bcm);
  if (it == lines_.end())
    throw Error("hardware.no_lease", "GPIO not requested");
  if (it->second.input)
    throw Error("hardware.direction", "Cannot write an input");
  it->second.value = value;
}
void SimulatedHardwareBackend::configure_input(int bcm, const std::string &owner, bool pull_up) {
  configure(bcm, owner, pull_up);
  lines_.at(bcm).input = true;
}
void SimulatedHardwareBackend::inject_input(int bcm, bool value) {
  const auto it = lines_.find(bcm);
  if (it == lines_.end() || !it->second.input)
    throw Error("hardware.input", "No input lease");
  it->second.value = value;
}
void HardwareAuthority::apply(const std::vector<Lease> &leases) {
  std::set<int> pins;
  std::set<std::string> owners;
  for (const auto &l : leases)
    if (l.safe || !pins.insert(l.bcm).second || !owners.insert(l.node).second)
      throw Error("hardware.invalid_plan", "Duplicate lease or unsupported safe state");
  safe_stop();
  try {
    for (const auto &l : leases)
      if (l.input)
        gpio_->configure_input(l.bcm, l.node, l.pull_up);
      else
        gpio_->configure(l.bcm, l.node, l.safe);
    const auto actual = gpio_->snapshot();
    for (const auto &l : leases)
      if (!actual.contains(l.bcm) || actual.at(l.bcm).owner != l.node ||
          (!l.input && actual.at(l.bcm).value != l.safe))
        throw Error("hardware.verify", "Prepared GPIO state did not match desired state");
    leases_ = leases;
    for (const auto &l : leases)
      desired_[l.bcm] = l.safe;
  } catch (...) {
    safe_stop();
    throw;
  }
}
void HardwareAuthority::set(const std::string &node, bool value) {
  for (const auto &l : leases_)
    if (l.node == node) {
      if (l.input)
        throw Error("hardware.direction", "Input cannot be driven as an output");
      desired_[l.bcm] = value;
      gpio_->write(l.bcm, value);
      if (gpio_->snapshot().at(l.bcm).value != value)
        throw Error("hardware.verify", "GPIO write verification failed");
      return;
    }
  throw Error("hardware.no_lease", "Node has no output lease: " + node);
}
void HardwareAuthority::inject_input(const std::string &node, bool value) {
  if (gpio_->mode() != "simulation")
    throw Error("hardware.simulation", "Input injection requires simulation");
  for (const auto &l : leases_)
    if (l.node == node && l.input) {
      gpio_->inject_input(l.bcm, value);
      return;
    }
  throw Error("hardware.input", "Node has no input lease");
}
bool HardwareAuthority::value(const std::string &node) const {
  auto states = gpio_->snapshot();
  for (const auto &[pin, state] : states)
    if (state.owner == node)
      return state.value;
  return false;
}
void HardwareAuthority::safe_stop() noexcept {
  for (const auto &l : leases_)
    try {
      if (!l.input)
        gpio_->write(l.bcm, l.safe);
    } catch (...) {
    }
  gpio_->release_all();
  leases_.clear();
  desired_.clear();
}
Json HardwareAuthority::state() const {
  Json result = Json::array();
  const auto actual = gpio_->snapshot();
  for (const auto &l : leases_) {
    auto it = actual.find(l.bcm);
    result.push_back({{"resource", l.resource},
                      {"bcm", l.bcm},
                      {"node", l.node},
                      {"desired", l.input ? Json() : Json(desired_.at(l.bcm))},
                      {"direction", l.input ? "input" : "output"},
                      {"actual", it == actual.end() ? Json() : Json(it->second.value)},
                      {"status", it == actual.end() ? "ERROR" : "READY"}});
  }
  return result;
}
} // namespace moonpi
