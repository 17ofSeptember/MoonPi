#include "moonpi/i2c.hpp"
#include <set>
namespace moonpi {
void validate_i2c_leases(const std::vector<I2cLease> &leases) {
  std::set<std::pair<int, int>> addresses;
  std::set<std::string> nodes;
  for (const auto &l : leases)
    if (l.bus != 1 || (l.address != 0x76 && l.address != 0x77) ||
        l.driver != "moonpi.driver.bme280" || l.node.empty() || !nodes.insert(l.node).second ||
        !addresses.emplace(l.bus, l.address).second)
      throw Error("i2c.lease", "Invalid, duplicate or unsupported I2C lease");
}
void SimulatedI2cController::configure(const std::vector<I2cLease> &leases) {
  validate_i2c_leases(leases);
  release_all();
  for (const auto &l : leases) {
    auto &r = devices_[l.node];
    auto word = [&](int reg, int value) {
      r[reg] = static_cast<uint8_t>(value);
      r[reg + 1] = static_cast<uint8_t>(value >> 8);
    };
    auto raw20 = [&](int reg, int value) {
      r[reg] = static_cast<uint8_t>(value >> 12);
      r[reg + 1] = static_cast<uint8_t>(value >> 4);
      r[reg + 2] = static_cast<uint8_t>(value << 4);
    };
    // Bosch reference temperature/pressure coefficients and a deterministic humidity fixture.
    const int trim[] = {27504, 26435, -1000, 36477, -10685, 3024,
                        2855,  140,   -7,    15500, -14600, 6000};
    for (int i = 0; i < 12; ++i)
      word(0x88 + 2 * i, trim[i]);
    r[0xd0] = 0x60;
    r[0xa1] = 75;
    word(0xe1, 362);
    r[0xe3] = 0;
    r[0xe4] = 20;
    r[0xe5] = 0x25;
    r[0xe6] = 3;
    r[0xe7] = 30;
    raw20(0xf7, 415148);
    raw20(0xfa, 519888);
    r[0xfd] = 0x75;
    r[0xfe] = 0x30;
  }
}
std::vector<uint8_t> SimulatedI2cController::transfer(const std::string &node,
                                                      std::span<const uint8_t> tx, size_t receive) {
  if (fail_after_ == 0) {
    fail_after_ = -1;
    throw Error("i2c.injected_failure", "Simulated I2C failure");
  }
  if (fail_after_ > 0)
    --fail_after_;
  if (!devices_.contains(node))
    throw Error("i2c.no_lease", "No I2C lease for node");
  if (tx.empty() || tx.size() > 256 || receive > 256 || (receive && tx.size() != 1))
    throw Error("i2c.request", "Invalid bounded register transaction");
  auto &r = devices_.at(node);
  if (receive) {
    if (static_cast<size_t>(tx[0]) + receive > r.size())
      throw Error("i2c.range", "Register range exceeded");
    return {r.begin() + tx[0], r.begin() + tx[0] + receive};
  }
  // SensorAPI emits register/value pairs for multi-register writes.
  if (tx.size() % 2 != 0)
    throw Error("i2c.request", "Expected register/value pairs");
  for (size_t i = 0; i < tx.size(); i += 2) {
    if (tx[i] == 0xe0 && tx[i + 1] == 0xb6) {
      r[0xf2] = 0;
      r[0xf3] = 0;
      r[0xf4] = 0;
      r[0xf5] = 0;
    } else
      r[tx[i]] = tx[i + 1];
  }
  // Forced conversions complete immediately in the register-level simulator.
  r[0xf4] &= 0xfc;
  return {};
}
#ifndef __linux__
std::unique_ptr<II2cController> make_linux_i2c_controller() {
  class Unavailable final : public II2cController {
    void configure(const std::vector<I2cLease> &l) override {
      if (!l.empty())
        throw Error("i2c.unavailable", "Linux I2C is unavailable on Windows");
    }
    void release_all() noexcept override {}
    std::vector<uint8_t> transfer(const std::string &, std::span<const uint8_t>, size_t) override {
      throw Error("i2c.unavailable", "Linux I2C is unavailable on Windows");
    }
  };
  return std::make_unique<Unavailable>();
}
#endif
} // namespace moonpi
