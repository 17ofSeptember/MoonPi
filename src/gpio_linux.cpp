#include "moonpi/gpio_device.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/gpio.h>
#include <regex>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace moonpi {
namespace {
class Fd {
  int fd_;

public:
  explicit Fd(int fd) : fd_(fd) {}
  ~Fd() {
    if (fd_ >= 0)
      ::close(fd_);
  }
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
  int get() const {
    return fd_;
  }
  int take() {
    return std::exchange(fd_, -1);
  }
};
[[noreturn]] void device_error(const std::string &operation) {
  const int error = errno;
  const auto code = error == EBUSY                      ? "hardware.busy"
                    : error == EACCES || error == EPERM ? "hardware.permission"
                                                        : "hardware.io";
  throw Error(code, operation + ": " + std::strerror(error));
}
void checked_ioctl(int fd, unsigned long command, void *argument, const char *operation) {
  // No unbounded retry loop; interruption or device failure is reported to the runtime.
  if (::ioctl(fd, command, argument) < 0)
    device_error(operation);
}
class LinuxLine final : public IGpioLine {
  Fd fd_;

public:
  explicit LinuxLine(int fd) : fd_(fd) {}
  void write(bool value) override {
    gpio_v2_line_values values{};
    values.mask = 1;
    values.bits = value ? 1 : 0;
    checked_ioctl(fd_.get(), GPIO_V2_LINE_SET_VALUES_IOCTL, &values, "Write GPIO");
  }
  bool read() const override {
    gpio_v2_line_values values{};
    values.mask = 1;
    checked_ioctl(fd_.get(), GPIO_V2_LINE_GET_VALUES_IOCTL, &values, "Read GPIO");
    return (values.bits & 1) != 0;
  }
};
class LinuxChip final : public IGpioChip {
  Fd fd_;
  gpiochip_info info_{};

public:
  explicit LinuxChip(const fs::path &path)
      : fd_(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)) {
    if (fd_.get() < 0)
      device_error("Open GPIO chip " + path.string());
    struct stat type{};
    if (::fstat(fd_.get(), &type) < 0)
      device_error("Inspect GPIO chip");
    if (!S_ISCHR(type.st_mode))
      throw Error("hardware.chip", "GPIO path is not a character device");
    checked_ioctl(fd_.get(), GPIO_GET_CHIPINFO_IOCTL, &info_, "Inspect GPIO chip");
  }
  std::string label() const override {
    return std::string(info_.label, strnlen(info_.label, sizeof(info_.label)));
  }
  unsigned line_count() const override {
    return info_.lines;
  }
  std::unique_ptr<IGpioLine> request_input(int offset, const std::string &owner,
                                           bool pull_up) override {
    gpio_v2_line_request request{};
    request.offsets[0] = static_cast<unsigned>(offset);
    request.num_lines = 1;
    const auto consumer = "Moon Pi " + owner;
    std::memcpy(request.consumer, consumer.data(),
                std::min(consumer.size(), sizeof(request.consumer) - 1));
    request.config.flags = GPIO_V2_LINE_FLAG_INPUT | (pull_up ? GPIO_V2_LINE_FLAG_BIAS_PULL_UP
                                                              : GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN);
    checked_ioctl(fd_.get(), GPIO_V2_GET_LINE_IOCTL, &request, "Request exclusive GPIO input");
    Fd guard(request.fd);
    auto line = std::make_unique<LinuxLine>(guard.get());
    guard.take();
    return line;
  }
  std::unique_ptr<IGpioLine> request_output(int offset, const std::string &owner,
                                            bool initial) override {
    gpio_v2_line_request request{};
    request.offsets[0] = static_cast<unsigned>(offset);
    request.num_lines = 1;
    const auto consumer = "Moon Pi " + owner;
    std::memcpy(request.consumer, consumer.data(),
                std::min(consumer.size(), sizeof(request.consumer) - 1));
    request.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    request.config.num_attrs = 1;
    request.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    request.config.attrs[0].attr.values = initial ? 1 : 0;
    request.config.attrs[0].mask = 1;
    checked_ioctl(fd_.get(), GPIO_V2_GET_LINE_IOCTL, &request, "Request exclusive GPIO output");
    Fd guard(request.fd);
    // Keep ownership in the guard until allocation has succeeded.
    auto line = std::make_unique<LinuxLine>(guard.get());
    guard.take();
    return line;
  }
};
} // namespace
std::unique_ptr<IGpioController> make_linux_gpio_backend(const fs::path &chip) {
  if (!std::regex_match(chip.string(), std::regex(R"(/dev/gpiochip[0-9]+)")))
    throw Error("hardware.path", "GPIO chip must be an explicit /dev/gpiochipN device");
  std::ifstream input("/sys/firmware/devicetree/base/compatible", std::ios::binary);
  if (!input)
    throw Error("hardware.board", "Cannot verify Raspberry Pi device-tree identity");
  char bytes[4096];
  input.read(bytes, sizeof(bytes));
  if (input.bad() || input.gcount() == sizeof(bytes) ||
      !is_pi3b_plus(std::string_view(bytes, static_cast<size_t>(input.gcount()))))
    throw Error("hardware.board", "Hardware mode currently supports only Raspberry Pi 3 B+");
  return std::make_unique<GpioCharacterDeviceBackend>(std::make_unique<LinuxChip>(chip));
}
} // namespace moonpi
