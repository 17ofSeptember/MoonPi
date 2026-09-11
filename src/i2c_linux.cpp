#include "moonpi/i2c.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace moonpi {
namespace {
class LinuxI2c final : public II2cController {
  int fd_ = -1;
  std::map<std::string, int> addresses_;
  [[noreturn]] static void fail(const char *op) {
    throw Error("i2c.io", std::string(op) + ": " + std::strerror(errno));
  }

public:
  ~LinuxI2c() override {
    release_all();
  }
  void release_all() noexcept override {
    addresses_.clear();
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }
  void configure(const std::vector<I2cLease> &leases) override {
    validate_i2c_leases(leases);
    release_all();
    if (leases.empty())
      return;
    try {
      fd_ = ::open("/dev/i2c-1", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
      if (fd_ < 0)
        fail("Open /dev/i2c-1 (enable I2C and check permissions)");
      struct stat s{};
      if (::fstat(fd_, &s) < 0 || !S_ISCHR(s.st_mode))
        throw Error("i2c.device", "I2C adapter is not a character device");
      if (::flock(fd_, LOCK_EX | LOCK_NB) < 0)
        fail("Reserve I2C adapter");
      unsigned long funcs = 0;
      if (::ioctl(fd_, I2C_FUNCS, &funcs) < 0 || !(funcs & I2C_FUNC_I2C))
        throw Error("i2c.capability", "Adapter does not support combined I2C transfers");
      // Kernel adapter timeout is shared: documented explicitly in the hardware guide.
      if (::ioctl(fd_, I2C_TIMEOUT, 10ul) < 0 || ::ioctl(fd_, I2C_RETRIES, 0ul) < 0)
        fail("Configure bounded I2C timeout");
      for (const auto &l : leases) {
        if (::ioctl(fd_, I2C_SLAVE, static_cast<unsigned long>(l.address)) < 0)
          fail("Reserve I2C address");
        addresses_.emplace(l.node, l.address);
      }
    } catch (...) {
      release_all();
      throw;
    }
  }
  std::vector<uint8_t> transfer(const std::string &node, std::span<const uint8_t> tx,
                                size_t receive) override {
    if (!addresses_.contains(node) || fd_ < 0)
      throw Error("i2c.no_lease", "No I2C lease for node");
    if (tx.empty() || tx.size() > 256 || receive > 256)
      throw Error("i2c.request", "I2C transaction exceeds bounds");
    const auto address = static_cast<__u16>(addresses_.at(node));
    if (::ioctl(fd_, I2C_SLAVE, static_cast<unsigned long>(address)) < 0)
      fail("Check I2C address ownership");
    std::vector<uint8_t> result(receive);
    i2c_msg messages[2]{};
    messages[0] = {address, 0, static_cast<__u16>(tx.size()), const_cast<uint8_t *>(tx.data())};
    messages[1] = {address, I2C_M_RD, static_cast<__u16>(receive), result.data()};
    i2c_rdwr_ioctl_data request{messages, receive ? 2u : 1u};
    const int count = ::ioctl(fd_, I2C_RDWR, &request);
    if (count < 0)
      fail("I2C transfer");
    if (static_cast<unsigned>(count) != request.nmsgs)
      throw Error("i2c.short", "Incomplete I2C transfer");
    return result;
  }
};
} // namespace
std::unique_ptr<II2cController> make_linux_i2c_controller() {
  return std::make_unique<LinuxI2c>();
}
} // namespace moonpi
