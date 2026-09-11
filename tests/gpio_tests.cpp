#include "moonpi/app.hpp"
#include "moonpi/gpio_device.hpp"
#include <functional>
#include <iostream>
using namespace moonpi;
namespace {
struct Device {
  std::map<int, bool> values;
  std::vector<std::pair<int, bool>> released;
  int busy = -1;
  bool fail_read = false, fail_write = false, ignore_write = false;
  int requests = 0;
};
class TestLine final : public IGpioLine {
  std::shared_ptr<Device> device_;
  int pin_;

public:
  TestLine(std::shared_ptr<Device> d, int pin, bool initial) : device_(d), pin_(pin) {
    device_->values.emplace(pin, initial);
  }
  ~TestLine() override {
    device_->released.emplace_back(pin_, device_->values.at(pin_));
    device_->values.erase(pin_);
  }
  void write(bool value) override {
    if (device_->fail_write)
      throw Error("hardware.io", "Disconnected output");
    if (!device_->ignore_write)
      device_->values.at(pin_) = value;
  }
  bool read() const override {
    if (device_->fail_read)
      throw Error("hardware.io", "Disconnected input");
    return device_->values.at(pin_);
  }
};
class TestChip final : public IGpioChip {
  std::shared_ptr<Device> device_;
  std::string label_;

public:
  TestChip(std::shared_ptr<Device> d, std::string label = "pinctrl-bcm2835")
      : device_(d), label_(std::move(label)) {}
  std::string label() const override {
    return label_;
  }
  unsigned line_count() const override {
    return 54;
  }
  std::unique_ptr<IGpioLine> request_output(int pin, const std::string &, bool initial) override {
    ++device_->requests;
    if (pin == device_->busy || device_->values.contains(pin))
      throw Error("hardware.busy", "Line owned by another consumer");
    return std::make_unique<TestLine>(device_, pin, initial);
  }
  std::unique_ptr<IGpioLine> request_input(int pin, const std::string &consumer,
                                           bool pull_up) override {
    return request_output(pin, consumer, pull_up);
  }
};
auto backend(const std::shared_ptr<Device> &d) {
  return std::make_unique<GpioCharacterDeviceBackend>(std::make_unique<TestChip>(d));
}
void require(bool ok) {
  if (!ok)
    throw std::runtime_error("assertion failed");
}
void rejects(const std::function<void()> &fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const Error &) {
    rejected = true;
  }
  require(rejected);
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  const fs::path root = argv[1];
  int failed = 0, passed = 0;
  auto test = [&](const char *name, const std::function<void()> &fn) {
    try {
      fn();
      ++passed;
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception &e) {
      ++failed;
      std::cerr << "FAIL " << name << ": " << e.what() << '\n';
    }
  };
  test("device-tree identity uses exact NUL-separated entries", [] {
    constexpr char id[] = "brcm,bcm2837\0raspberrypi,3-model-b-plus\0";
    require(is_pi3b_plus(std::string_view(id, sizeof(id))));
    require(!is_pi3b_plus("raspberrypi,3-model-b-plus-unknown"));
    require(!is_pi3b_plus("raspberrypi,4-model-b"));
    require(!is_pi3b_plus(""));
  });
  test("input request reads pull state and is never driven during write or release", [] {
    auto d = std::make_shared<Device>();
    auto b = backend(d);
    b->configure_input(27, "button", true);
    require(b->snapshot().at(27).value);
    require(b->snapshot().at(27).input);
    rejects([&] { b->write(27, false); });
    require(d->values.at(27));
    d->fail_write = true;
    b->release_all();
    require(d->values.empty());
    require(d->released == std::vector<std::pair<int, bool>>{{27, true}});
  });
  test("wrong chip rejected without requesting lines", [] {
    auto d = std::make_shared<Device>();
    rejects([&] { GpioCharacterDeviceBackend b(std::make_unique<TestChip>(d, "gpio-expander")); });
    require(d->requests == 0);
  });
  test("reserved and non-header lines and HIGH initial state rejected", [] {
    auto d = std::make_shared<Device>();
    auto b = backend(d);
    for (int pin : {-1, 0, 1, 28, 53})
      rejects([&] { b->configure(pin, "output", false); });
    rejects([&] { b->configure(17, "output", true); });
    require(d->requests == 0);
  });
  test("exclusive request, fresh readback and LOW before release", [] {
    auto d = std::make_shared<Device>();
    auto b = backend(d);
    b->configure(17, "output", false);
    rejects([&] { b->configure(17, "second", false); });
    b->write(17, true);
    require(b->snapshot().at(17).value);
    d->values.at(17) = false;
    require(!b->snapshot().at(17).value);
    b->write(17, true);
    b.reset();
    require(d->values.empty());
    require(d->released.size() == 1);
    require(!d->released[0].second);
  });
  test("partial apply releases every prepared line", [] {
    auto d = std::make_shared<Device>();
    d->busy = 27;
    HardwareAuthority h(backend(d));
    rejects([&] { h.apply({{"header.11", "one", 17, false}, {"header.13", "two", 27, false}}); });
    require(d->values.empty());
    require(h.state().empty());
  });
  test("initial read failure releases the line", [] {
    auto d = std::make_shared<Device>();
    d->fail_read = true;
    auto b = backend(d);
    rejects([&] { b->configure(17, "one", false); });
    require(d->values.empty());
  });
  test("write verification detects incorrect actual value", [] {
    auto d = std::make_shared<Device>();
    HardwareAuthority h(backend(d));
    h.apply({{"header.11", "one", 17, false}});
    d->ignore_write = true;
    rejects([&] { h.set("one", true); });
    require(h.state()[0]["desired"] == true);
    require(h.state()[0]["actual"] == false);
  });
  test("shutdown releases requests even when writes fail", [] {
    auto d = std::make_shared<Device>();
    auto b = backend(d);
    b->configure(17, "one", false);
    d->fail_write = true;
    b.reset();
    require(d->values.empty());
  });
  Schemas schemas(root / "schemas");
  ComponentRegistry components(schemas);
  components.load(root / "resources/components");
  BoardRegistry boards(root / "resources/boards", schemas);
  GraphCompiler compiler(schemas, components, boards);
  const auto project = read_json(root / "examples/led-blink.moonpi.json");
  const auto graph = compiler.compile(project, 1).graph;
  test("runtime read failure stops safely and remains observable", [&] {
    auto d = std::make_shared<Device>();
    Runtime runtime(backend(d));
    auto idle_graph = graph;
    std::erase_if(idle_graph.nodes,
                  [](const auto &n) { return n.driver == "moonpi.driver.interval"; });
    runtime.apply(idle_graph);
    runtime.start();
    d->fail_read = true;
    const auto s = runtime.snapshot();
    require(s["mode"] == "hardware");
    require(s["running"] == false);
    require(s["error"]["code"] == "hardware.read");
    require(s["hardware"].empty());
    require(d->values.empty());
    runtime.emergency_stop();
    require(runtime.snapshot()["emergency"] == true);
  });
  test("start failure reports error and releases lines", [&] {
    auto d = std::make_shared<Device>();
    Runtime runtime(backend(d));
    runtime.apply(graph);
    d->busy = 17;
    rejects([&] { runtime.start(); });
    require(runtime.snapshot()["running"] == false);
    require(runtime.snapshot()["error"]["code"] == "runtime.start");
    require(d->values.empty());
  });
  test("repeated hardware lifecycle owns no leaked requests", [&] {
    auto d = std::make_shared<Device>();
    Runtime runtime(backend(d));
    for (int i = 0; i < 100; ++i) {
      runtime.apply(graph);
      runtime.start();
      runtime.stop();
    }
    require(d->values.empty());
    require(d->requests == static_cast<int>(d->released.size()));
  });
  test("application reports injected backend mode and starts without outputs", [&] {
    auto d = std::make_shared<Device>();
    App app(schemas, components, boards, root / "build/test-data" / uuid(), backend(d));
    require(app.snapshot()["runtime"]["mode"] == "hardware");
    require(d->requests == 0);
  });
#ifndef __linux__
  test("Windows rejects hardware instead of simulating",
       [] { rejects([] { make_linux_gpio_backend("/dev/gpiochip0"); }); });
#else
  test("Linux rejects arbitrary device paths",
       [] { rejects([] { make_linux_gpio_backend("/tmp/gpiochip0"); }); });
#endif
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
