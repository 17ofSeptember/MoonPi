#pragma once
#include "hardware.hpp"
#include "schedule.hpp"
#include "script.hpp"
#include "sensors.hpp"
#include <condition_variable>
#include <queue>
#include <set>
#include <thread>
namespace moonpi {
enum class DriverState { Ready, Active, Stopped, Error };
class IDeviceDriver {
public:
  virtual ~IDeviceDriver() = default;
  virtual void event(const std::string &port, const Json &value) = 0;
  virtual DriverState state() const = 0;
};
class GpioOutputDriver final : public IDeviceDriver {
  HardwareAuthority &hardware_;
  std::string node_;
  DriverState state_ = DriverState::Ready;

public:
  GpioOutputDriver(HardwareAuthority &h, std::string node) : hardware_(h), node_(std::move(node)) {}
  void event(const std::string &, const Json &) override;
  DriverState state() const override {
    return state_;
  }
};
class Runtime {
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  HardwareAuthority hardware_;
  SensorWorker sensors_;
  CalendarScheduler calendar_;
  CompiledGraph graph_;
  std::map<std::string, std::unique_ptr<IDeviceDriver>> drivers_;
  std::map<std::string, std::unique_ptr<PinScript>> scripts_;
  Json values_ = Json::object(), inputs_ = Json::object();
  std::map<std::string, Clock::time_point> sensor_retry_;
  std::set<std::string> sensor_pending_;
  struct Input {
    bool raw = false, stable = false, active_low = false;
    int debounce_ms = 0;
    Clock::time_point changed;
  };
  std::map<std::string, Input> gpio_inputs_;
  struct Event {
    std::string node, port;
    Json value;
  };
  std::deque<Event> queue_;
  struct Timer {
    Clock::time_point due;
    std::chrono::milliseconds period;
    std::string node;
    std::string port = "tick";
    bool repeat = true;
  };
  struct Later {
    bool operator()(const Timer &a, const Timer &b) const {
      return a.due > b.due || (a.due == b.due && a.node > b.node);
    }
  };
  std::priority_queue<Timer, std::vector<Timer>, Later> timers_;
  bool running_ = false, emergency_ = false;
  std::uint64_t events_ = 0, sequence_ = 0;
  Json error_ = nullptr;
  std::jthread worker_;
  void loop(std::stop_token);
  void stop_locked();
  void emit_locked(const std::string &, const std::string &, const Json &);
  void process_locked();
  void sequence_step_locked(const CompiledNode &);
  void cancel_sequence_timer_locked(const std::string &);

public:
  explicit Runtime(std::unique_ptr<IGpioController>, std::unique_ptr<II2cController> = nullptr,
                   fs::path scheduler_state = {});
  ~Runtime();
  void apply(const CompiledGraph &);
  void start();
  void stop();
  void emergency_stop();
  void clear_emergency();
  void inject_input(const std::string &, bool);
  void trigger(const std::string &, const std::string &);
  Json snapshot();
};
} // namespace moonpi
