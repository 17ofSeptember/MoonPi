#pragma once
#include "graph.hpp"
#include <span>
namespace moonpi {
struct GpioState {
  std::string owner;
  bool value = false;
  bool input = false;
};
using GpioSnapshot = std::map<int, GpioState>;
class IGpioController {
public:
  virtual ~IGpioController() = default;
  virtual void configure(int bcm, const std::string &owner, bool safe) = 0;
  virtual void configure_input(int, const std::string &, bool) {
    throw Error("hardware.input", "Input mode unavailable");
  }
  virtual void inject_input(int, bool) {
    throw Error("hardware.simulation", "Input injection requires simulation");
  }
  virtual void write(int bcm, bool value) = 0;
  virtual GpioSnapshot snapshot() const = 0;
  virtual void release_all() noexcept = 0;
  virtual std::string mode() const {
    return "simulation";
  }
};
// Bus services will be injected into drivers, never arbitrary device paths.
class II2cController {
public:
  virtual ~II2cController() = default;
  virtual void configure(const std::vector<I2cLease> &) = 0;
  virtual void release_all() noexcept = 0;
  virtual std::vector<std::uint8_t> transfer(const std::string &lease,
                                             std::span<const std::uint8_t> tx, size_t receive) = 0;
};
class ISpiController {
public:
  virtual ~ISpiController() = default;
  virtual std::vector<std::uint8_t> transfer(const std::string &lease,
                                             std::span<const std::uint8_t> tx) = 0;
};
class IUartController {
public:
  virtual ~IUartController() = default;
  virtual void send(const std::string &lease, std::span<const std::uint8_t> bytes) = 0;
};
class IPwmController {
public:
  virtual ~IPwmController() = default;
  virtual void set(const std::string &lease, double frequency, double duty) = 0;
};
class IOneWireController {
public:
  virtual ~IOneWireController() = default;
  virtual Json read(const std::string &lease) = 0;
};
class ISystemInfoProvider {
public:
  virtual ~ISystemInfoProvider() = default;
  virtual Json status() const = 0;
};
class SimulatedHardwareBackend final : public IGpioController {
  GpioSnapshot lines_;
  int fail_after_ = -1;

public:
  void configure(int, const std::string &, bool) override;
  void configure_input(int, const std::string &, bool) override;
  void inject_input(int, bool) override;
  void write(int, bool) override;
  GpioSnapshot snapshot() const override {
    return lines_;
  }
  void release_all() noexcept override {
    lines_.clear();
  }
  void inject_failure_after(int operations) {
    fail_after_ = operations;
  }

private:
  void maybe_fail();
};
class HardwareAuthority {
  std::unique_ptr<IGpioController> gpio_;
  std::vector<Lease> leases_;
  std::map<int, bool> desired_;

public:
  explicit HardwareAuthority(std::unique_ptr<IGpioController> gpio) : gpio_(std::move(gpio)) {}
  std::string mode() const {
    return gpio_->mode();
  }
  ~HardwareAuthority() {
    safe_stop();
  }
  void apply(const std::vector<Lease> &leases);
  void set(const std::string &node, bool value);
  void inject_input(const std::string &node, bool value);
  bool value(const std::string &node) const;
  void safe_stop() noexcept;
  Json state() const;
};
} // namespace moonpi
