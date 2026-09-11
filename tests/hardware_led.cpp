#include "moonpi/gpio_device.hpp"
#include <cstdlib>
#include <iostream>
#include <thread>
int main() {
  const auto *enabled = std::getenv("MOONPI_TEST_GPIO17");
  if (!enabled || std::string_view(enabled) != "1") {
    std::cout
        << "SKIP: requires MOONPI_TEST_GPIO17=1 and the GPIO17 LED circuit in docs/LINUX_GPIO.md\n";
    return 77;
  }
  try {
    const auto *chip = std::getenv("MOONPI_GPIO_CHIP");
    moonpi::HardwareAuthority hardware(
        moonpi::make_linux_gpio_backend(chip ? chip : "/dev/gpiochip0"));
    hardware.apply({{"header.11", "hardware-led-test", 17, false}});
    for (int i = 0; i < 3; ++i) {
      hardware.set("hardware-led-test", true);
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      hardware.set("hardware-led-test", false);
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    hardware.safe_stop();
    if (!hardware.state().empty())
      throw std::runtime_error("GPIO lease was not released");
    std::cout << "PASS GPIO17 request, three pulses, readback and release\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
