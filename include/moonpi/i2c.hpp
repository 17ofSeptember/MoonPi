#pragma once
#include "hardware.hpp"
#include <array>
namespace moonpi {
class SimulatedI2cController final : public II2cController {
  std::map<std::string, std::array<std::uint8_t, 256>> devices_;
  int fail_after_ = -1;

public:
  void configure(const std::vector<I2cLease> &) override;
  void release_all() noexcept override {
    devices_.clear();
  }
  std::vector<std::uint8_t> transfer(const std::string &, std::span<const std::uint8_t>,
                                     size_t) override;
  void inject_failure_after(int operations) {
    fail_after_ = operations;
  }
};
void validate_i2c_leases(const std::vector<I2cLease> &);
std::unique_ptr<II2cController> make_linux_i2c_controller();
} // namespace moonpi
