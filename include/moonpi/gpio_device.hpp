#pragma once
#include "hardware.hpp"

namespace moonpi {
// Each line object owns one exclusive kernel request. Destruction releases it.
class IGpioLine {
public:
  virtual ~IGpioLine() = default;
  virtual void write(bool) = 0;
  virtual bool read() const = 0;
};
class IGpioChip {
public:
  virtual ~IGpioChip() = default;
  virtual std::string label() const = 0;
  virtual unsigned line_count() const = 0;
  virtual std::unique_ptr<IGpioLine> request_output(int offset, const std::string &owner,
                                                    bool initial) = 0;
  virtual std::unique_ptr<IGpioLine> request_input(int, const std::string &, bool) {
    throw Error("hardware.input", "Chip does not support input requests");
  }
};

// The device seam permits failure/lifetime tests without GPIO hardware.
class GpioCharacterDeviceBackend final : public IGpioController {
  std::unique_ptr<IGpioChip> chip_;
  struct Line {
    std::string owner;
    std::unique_ptr<IGpioLine> request;
    bool input = false;
  };
  std::map<int, Line> lines_;

public:
  explicit GpioCharacterDeviceBackend(std::unique_ptr<IGpioChip>);
  ~GpioCharacterDeviceBackend() override {
    release_all();
  }
  void configure(int, const std::string &, bool) override;
  void configure_input(int, const std::string &, bool) override;
  void write(int, bool) override;
  GpioSnapshot snapshot() const override;
  void release_all() noexcept override;
  std::string mode() const override {
    return "hardware";
  }
};

bool is_pi3b_plus(std::string_view compatible);
// Linux implementation checks device-tree identity before opening a chip.
// Other platforms reject hardware mode; they never substitute simulation.
std::unique_ptr<IGpioController> make_linux_gpio_backend(const fs::path &chip);
} // namespace moonpi
