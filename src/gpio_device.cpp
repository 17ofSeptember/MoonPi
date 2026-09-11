#include "moonpi/gpio_device.hpp"

namespace moonpi {
bool is_pi3b_plus(std::string_view compatible) {
  while (!compatible.empty()) {
    const auto end = compatible.find('\0');
    if (compatible.substr(0, end) == "raspberrypi,3-model-b-plus")
      return true;
    if (end == std::string_view::npos)
      break;
    compatible.remove_prefix(end + 1);
  }
  return false;
}
GpioCharacterDeviceBackend::GpioCharacterDeviceBackend(std::unique_ptr<IGpioChip> chip)
    : chip_(std::move(chip)) {
  if (!chip_ || chip_->label() != "pinctrl-bcm2835" || chip_->line_count() != 54)
    throw Error("hardware.chip", "Expected the Pi 3 B+ pinctrl-bcm2835 GPIO chip (54 lines)");
}
void GpioCharacterDeviceBackend::configure(int bcm, const std::string &owner, bool safe) {
  if (bcm < 2 || bcm > 27 || safe || owner.empty())
    throw Error("hardware.invalid_plan",
                "Only header GPIO2-27 with a LOW safe state are supported");
  if (lines_.contains(bcm))
    throw Error("hardware.busy", "GPIO is already requested");
  // Allocate the bookkeeping first, so an allocation failure cannot leak a live request.
  auto [entry, inserted] = lines_.emplace(bcm, Line{owner, nullptr});
  (void)inserted;
  try {
    entry->second.request = chip_->request_output(bcm, owner, false);
    if (!entry->second.request || entry->second.request->read())
      throw Error("hardware.verify", "GPIO did not read LOW after requesting output");
  } catch (...) {
    if (entry->second.request) {
      try {
        entry->second.request->write(false);
      } catch (...) {
      }
    }
    lines_.erase(entry);
    throw;
  }
}
void GpioCharacterDeviceBackend::write(int bcm, bool value) {
  const auto line = lines_.find(bcm);
  if (line == lines_.end())
    throw Error("hardware.no_lease", "GPIO not requested");
  if (line->second.input)
    throw Error("hardware.direction", "Cannot write an input");
  line->second.request->write(value);
}
void GpioCharacterDeviceBackend::configure_input(int bcm, const std::string &owner, bool pull_up) {
  if (bcm < 2 || bcm > 27 || owner.empty() || lines_.contains(bcm))
    throw Error("hardware.input", "Invalid or occupied GPIO input");
  auto [entry, inserted] = lines_.emplace(bcm, Line{owner, nullptr, true});
  (void)inserted;
  try {
    entry->second.request = chip_->request_input(bcm, owner, pull_up);
    if (!entry->second.request)
      throw Error("hardware.input", "Input request failed");
    entry->second.request->read();
  } catch (...) {
    lines_.erase(entry);
    throw;
  }
}
GpioSnapshot GpioCharacterDeviceBackend::snapshot() const {
  GpioSnapshot result;
  for (const auto &[bcm, line] : lines_)
    result.emplace(bcm, GpioState{line.owner, line.request->read(), line.input});
  return result;
}
void GpioCharacterDeviceBackend::release_all() noexcept {
  for (const auto &[bcm, line] : lines_) {
    (void)bcm;
    try {
      if (!line.input)
        line.request->write(false);
    } catch (...) {
    }
  }
  lines_.clear();
}
#ifndef __linux__
std::unique_ptr<IGpioController> make_linux_gpio_backend(const fs::path &) {
  throw Error("hardware.unavailable",
              "Hardware mode requires Linux on a Raspberry Pi 3 B+; use --simulation on Windows");
}
#endif
} // namespace moonpi
