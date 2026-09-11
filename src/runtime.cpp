#include "moonpi/runtime.hpp"
#include "moonpi/logic_contracts.hpp"
#include <cmath>
namespace moonpi {
void GpioOutputDriver::event(const std::string &port, const Json &value) {
  try {
    if (port == "toggle")
      hardware_.set(node_, !hardware_.value(node_));
    else if (port == "set" && value.is_boolean())
      hardware_.set(node_, value.get<bool>());
    else
      throw Error("driver.event", "Unsupported output event");
    state_ = DriverState::Active;
  } catch (...) {
    state_ = DriverState::Error;
    throw;
  }
}
Runtime::Runtime(std::unique_ptr<IGpioController> gpio, std::unique_ptr<II2cController> i2c,
                 fs::path scheduler_state)
    : hardware_(std::move(gpio)),
      sensors_(i2c                              ? std::move(i2c)
               : hardware_.mode() == "hardware" ? make_linux_i2c_controller()
                                                : std::make_unique<SimulatedI2cController>()),
      calendar_(std::move(scheduler_state)), worker_([this](std::stop_token s) { loop(s); }) {}
Runtime::~Runtime() {
  {
    // Synchronize predicate changes with wait() so shutdown cannot lose its wakeup.
    std::lock_guard lock(mutex_);
    worker_.request_stop();
  }
  wake_.notify_all();
  worker_.join();
  stop();
}
void Runtime::stop_locked() {
  running_ = false;
  timers_ = {};
  drivers_.clear();
  scripts_.clear();
  hardware_.safe_stop();
  sensors_.cancel();
  sensor_pending_.clear();
  queue_.clear();
  gpio_inputs_.clear();
  calendar_.stop();
  for (const auto &node : graph_.nodes)
    if (node.driver == "moonpi.driver.sequence")
      values_[node.id] = {{"value", 0}, {"step", 0}, {"active", false}, {"state", "STOPPED"}};
  ++sequence_;
  wake_.notify_all();
}
void Runtime::stop() {
  std::lock_guard lock(mutex_);
  stop_locked();
}
void Runtime::emergency_stop() {
  std::lock_guard lock(mutex_);
  emergency_ = true;
  stop_locked();
}
void Runtime::clear_emergency() {
  std::lock_guard lock(mutex_);
  emergency_ = false;
  ++sequence_;
}
void Runtime::inject_input(const std::string &node, bool value) {
  std::lock_guard lock(mutex_);
  if (!running_ || emergency_ || !gpio_inputs_.contains(node))
    throw Error("runtime.input", "Run an applied input node before simulating it");
  hardware_.inject_input(node, value);
  wake_.notify_all();
}
void Runtime::apply(const CompiledGraph &graph) {
  std::lock_guard lock(mutex_);
  if (emergency_)
    throw Error("runtime.emergency", "Clear emergency stop before applying");
  if (running_)
    throw Error("runtime.running", "Stop the graph before applying changes");
  try {
    sensors_.configure(graph.i2c);
    hardware_.apply(graph.leases);
    graph_ = graph;
    error_ = nullptr;
    ++sequence_;
  } catch (const std::exception &e) {
    sensors_.cancel();
    hardware_.safe_stop();
    graph_ = {};
    error_ = diagnostic("hardware.transaction", e.what());
    ++sequence_;
    throw;
  }
}
void Runtime::trigger(const std::string &id, const std::string &port) {
  std::lock_guard lock(mutex_);
  if (!running_ || emergency_)
    throw Error("runtime.trigger", "Run an applied graph before triggering a node");
  const auto node = std::find_if(graph_.nodes.begin(), graph_.nodes.end(),
                                 [&](const auto &n) { return n.id == id; });
  if (node == graph_.nodes.end())
    throw Error("runtime.trigger", "Node is not in the applied graph");
  bool valid = (node->driver == "moonpi.driver.gpio_output" && port == "toggle") ||
               (node->driver == "moonpi.driver.bme280" && port == "read") ||
               (node->driver == "moonpi.driver.pin_script" && port == "run");
  if (logic_contracts().contains(node->driver))
    for (const auto &p : logic_contracts().at(node->driver).at("ports"))
      if (p.at("direction") == "input" && p.at("type") == "Trigger" && p.at("id") == port)
        valid = true;
  if (!valid)
    throw Error("runtime.trigger", "Port is not a supported Trigger input");
  if (queue_.size() >= 4096)
    throw Error("runtime.queue", "Graph event queue limit exceeded");
  queue_.push_back({id, port, true});
  wake_.notify_all();
}
void Runtime::cancel_sequence_timer_locked(const std::string &id) {
  decltype(timers_) retained;
  while (!timers_.empty()) {
    auto timer = timers_.top();
    timers_.pop();
    if (timer.node != id || timer.port != "__sequence")
      retained.push(std::move(timer));
  }
  timers_ = std::move(retained);
}
void Runtime::sequence_step_locked(const CompiledNode &node) {
  auto &value = values_[node.id];
  const int step = value.value("step", 0) + 1;
  if (step > node.properties.at("steps").get<int>()) {
    value["active"] = false;
    value["state"] = "COMPLETED";
    emit_locked(node.id, "active", false);
    emit_locked(node.id, "done", true);
    return;
  }
  value = {{"value", step}, {"step", step}, {"active", true}, {"state", "ACTIVE"}};
  emit_locked(node.id, "step", step);
  emit_locked(node.id, "step_" + std::to_string(step), true);
  if (timers_.size() >= 4096)
    throw Error("runtime.timers", "Pending timer limit reached");
  timers_.push(
      {Clock::now() + std::chrono::milliseconds(
                          node.properties.at("step_" + std::to_string(step) + "_ms").get<int>()),
       std::chrono::milliseconds(0), node.id, "__sequence", false});
}
void Runtime::start() {
  std::lock_guard lock(mutex_);
  if (emergency_)
    throw Error("runtime.emergency", "Emergency stop is latched");
  if (running_)
    return;
  if (graph_.revision == 0)
    throw Error("runtime.unapplied", "Apply a valid graph before Run");
  try {
    sensors_.configure(graph_.i2c);
    calendar_.configure(graph_.nodes, timestamp_ms() / 1000);
    hardware_.apply(graph_.leases);
    drivers_.clear();
    scripts_.clear();
    timers_ = {};
    values_ = Json::object();
    inputs_ = Json::object();
    queue_.clear();
    sensor_retry_.clear();
    sensor_pending_.clear();
    const auto now = Clock::now();
    for (const auto &n : graph_.nodes) {
      if (n.driver == "moonpi.driver.pin_script") {
        scripts_[n.id] = std::make_unique<PinScript>(n.properties.at("script"));
        queue_.push_back({n.id, "start", false});
      }
      if (n.driver == "moonpi.driver.sequence") {
        values_[n.id] = {{"value", 0}, {"step", 0}, {"active", false}, {"state", "READY"}};
        emit_locked(n.id, "active", false);
      }
      if (n.driver == "moonpi.driver.gpio_output")
        drivers_[n.id] = std::make_unique<GpioOutputDriver>(hardware_, n.id);
      else if (n.driver == "moonpi.driver.gpio_input") {
        const auto raw = hardware_.value(n.id);
        const bool active_low = n.properties.at("active_low");
        gpio_inputs_[n.id] = {raw, raw, active_low, n.properties.at("debounce_ms"), now};
        values_[n.id] = {{"value", raw != active_low}, {"state", "ACTIVE"}};
        emit_locked(n.id, "value", raw != active_low);
      } else if (n.driver == "moonpi.driver.interval") {
        auto period = std::chrono::milliseconds(n.properties.at("interval_ms").get<int>());
        timers_.push({now + period, period, n.id});
      }
      if (n.driver == "moonpi.driver.constant_boolean" ||
          n.driver == "moonpi.driver.constant_number") {
        values_[n.id] = {{"value", n.properties.at("value")}, {"state", "ACTIVE"}};
        emit_locked(n.id, "value", n.properties.at("value"));
      }
    }
    running_ = true;
    error_ = nullptr;
    ++sequence_;
    wake_.notify_all();
  } catch (const std::exception &e) {
    error_ = diagnostic("runtime.start", e.what());
    stop_locked();
    throw;
  }
}
void Runtime::emit_locked(const std::string &source, const std::string &port, const Json &value) {
  for (const auto &r : graph_.routes)
    if (r.source == source && r.source_port == port) {
      if (queue_.size() >= 4096)
        throw Error("runtime.queue", "Graph event queue limit exceeded");
      queue_.push_back({r.target, r.target_port, value});
    }
}
void Runtime::process_locked() {
  size_t processed = 0;
  const auto slice_end = Clock::now() + std::chrono::milliseconds(20);
  while (!queue_.empty()) {
    if (processed && Clock::now() >= slice_end)
      return; // Let control commands acquire the mutex between bounded slices.
    if (++processed > 4096)
      throw Error("runtime.budget", "Graph event budget exceeded");
    auto event = std::move(queue_.front());
    queue_.pop_front();
    ++events_;
    auto node = std::find_if(graph_.nodes.begin(), graph_.nodes.end(),
                             [&](const auto &n) { return n.id == event.node; });
    if (node == graph_.nodes.end())
      throw Error("runtime.node", "Unknown event target");
    const auto &driver = node->driver;
    const auto &props = node->properties;
    inputs_[node->id][event.port] = event.value;
    auto output = [&](const Json &value, const std::string &port = "value") {
      values_[node->id] = {{"value", value}, {"state", "ACTIVE"}};
      emit_locked(node->id, port, value);
    };
    if (driver == "moonpi.driver.pin_script") {
      try {
        const bool next = scripts_.at(node->id)->execute(event.port, event.value.get<bool>(),
                                                         hardware_.value(node->id));
        hardware_.set(node->id, next);
        output(next);
        emit_locked(node->id, "done", true);
      } catch (const std::exception &e) {
        values_[node->id] = {{"value", false}, {"state", "ERROR"}, {"error", e.what()}};
        throw Error("script.execution", node->id + ": " + e.what());
      }
    } else if (driver == "moonpi.driver.gpio_output") {
      drivers_.at(node->id)->event(event.port, event.value);
    } else if (driver == "moonpi.driver.bme280") {
      if (!sensor_pending_.contains(node->id) && Clock::now() >= sensor_retry_[node->id] &&
          sensors_.request(node->id))
        sensor_pending_.insert(node->id);
    } else if (driver == "moonpi.driver.constant_boolean" ||
               driver == "moonpi.driver.constant_number")
      output(props.at("value"));
    else if (driver == "moonpi.driver.compare") {
      const double a = event.value.get<double>(), b = props.at("threshold").get<double>();
      const std::string op = props.at("operation");
      output(op == "gt"    ? a > b
             : op == "gte" ? a >= b
             : op == "lt"  ? a < b
             : op == "lte" ? a <= b
                           : a == b);
    } else if (driver == "moonpi.driver.math") {
      const double a = event.value.get<double>(), b = props.at("operand").get<double>();
      const std::string op = props.at("operation");
      if (op == "divide" && b == 0)
        throw Error("runtime.divide_zero", "Division by zero");
      const double value = op == "add"        ? a + b
                           : op == "subtract" ? a - b
                           : op == "multiply" ? a * b
                                              : a / b;
      if (!std::isfinite(value))
        throw Error("runtime.nonfinite", "Math result is not finite");
      output(value);
    } else if (driver == "moonpi.driver.boolean") {
      const auto &in = inputs_[node->id];
      const bool a = in.value("a", false), b = in.value("b", false);
      const std::string op = props.at("operation");
      output(op == "and" ? a && b : op == "or" ? a || b : op == "xor" ? a != b : !a);
    } else if (driver == "moonpi.driver.counter") {
      const auto before =
          values_.contains(node->id) ? values_[node->id].value("value", int64_t{0}) : 0;
      if (before >= 1000000000 && event.port != "reset")
        throw Error("runtime.counter", "Counter limit reached");
      output(event.port == "reset" ? 0 : before + 1);
    } else if (driver == "moonpi.driver.to_float")
      output(event.value.get<double>());
    else if (driver == "moonpi.driver.display")
      output(event.value);
    else if (driver == "moonpi.driver.delay") {
      if (timers_.size() >= 4096)
        throw Error("runtime.timers", "Pending timer limit reached");
      timers_.push({Clock::now() + std::chrono::milliseconds(props.at("delay_ms").get<int>()),
                    std::chrono::milliseconds(0), node->id, "done", false});
    } else if (driver == "moonpi.driver.sequence") {
      auto &value = values_[node->id];
      const bool active = value.value("active", false);
      if (event.port == "cancel") {
        cancel_sequence_timer_locked(node->id);
        value = {{"value", 0}, {"step", 0}, {"active", false}, {"state", "CANCELLED"}};
        if (active) {
          emit_locked(node->id, "active", false);
          emit_locked(node->id, "cancelled", true);
        }
      } else if (event.port == "start") {
        if (active && props.at("retrigger") == "IGNORE")
          continue;
        cancel_sequence_timer_locked(node->id);
        value = {{"step", 0}, {"active", true}};
        emit_locked(node->id, "active", true);
        sequence_step_locked(*node);
      } else
        throw Error("runtime.sequence", "Unsupported sequence event");
    } else if (driver == "moonpi.driver.latch")
      output(event.port == "set");
    else if (driver == "moonpi.driver.condition") {
      if (event.port == "trigger" && inputs_[node->id].value("condition", false))
        emit_locked(node->id, "then", true);
    } else
      throw Error("runtime.driver", "Unsupported runtime node");
  }
}
void Runtime::loop(std::stop_token stop) {
  std::unique_lock lock(mutex_);
  while (!stop.stop_requested()) {
    if (!running_) {
      wake_.wait(lock, [&] { return stop.stop_requested() || running_; });
      continue;
    }
    const auto poll = Clock::now() + std::chrono::milliseconds(10);
    const auto due = timers_.empty() ? poll : std::min(poll, timers_.top().due);
    if (wake_.wait_until(lock,
                         queue_.empty() ? due : Clock::now() + std::chrono::milliseconds(1)) !=
        std::cv_status::timeout)
      continue;
    if (stop.stop_requested() || !running_)
      continue;
    try {
      for (const auto &node : calendar_.due(timestamp_ms() / 1000))
        emit_locked(node, "tick", true);
      for (auto &[node, input] : gpio_inputs_) {
        const bool raw = hardware_.value(node);
        const auto now = Clock::now();
        if (raw != input.raw) {
          input.raw = raw;
          input.changed = now;
        }
        if (raw != input.stable &&
            now - input.changed >= std::chrono::milliseconds(input.debounce_ms)) {
          input.stable = raw;
          const bool active = raw != input.active_low;
          values_[node] = {{"value", active}, {"state", "ACTIVE"}};
          emit_locked(node, "value", active);
          emit_locked(node, active ? "pressed" : "released", true);
        }
      }
      for (auto &[node, result] : sensors_.drain()) {
        sensor_pending_.erase(node);
        values_[node] = result;
        if (result.at("state") == "ACTIVE") {
          for (const auto &port : {"temperature", "pressure", "humidity"})
            emit_locked(node, port, result.at(port));
          emit_locked(node, "reading", result);
        } else
          sensor_retry_[node] = Clock::now() + std::chrono::seconds(1);
      }
      while (!timers_.empty() && timers_.top().due <= Clock::now()) {
        auto timer = timers_.top();
        timers_.pop();
        if (timer.port == "__sequence") {
          const auto node = std::find_if(graph_.nodes.begin(), graph_.nodes.end(),
                                         [&](const auto &n) { return n.id == timer.node; });
          if (node != graph_.nodes.end() && values_[timer.node].value("active", false))
            sequence_step_locked(*node);
        } else
          emit_locked(timer.node, timer.port, true);
        if (timer.repeat) {
          timer.due += timer.period;
          if (timer.due <= Clock::now())
            timer.due = Clock::now() + timer.period;
          timers_.push(timer);
        }
      }
      process_locked();
      ++sequence_;
    } catch (const std::exception &e) {
      error_ = diagnostic("runtime.execution", e.what());
      stop_locked();
    }
  }
}
Json Runtime::snapshot() {
  std::lock_guard lock(mutex_);
  Json hardware;
  try {
    hardware = hardware_.state();
  } catch (const std::exception &e) {
    error_ = diagnostic("hardware.read", e.what());
    stop_locked();
    hardware = Json::array();
  }
  Json nodes = Json::object();
  for (const auto &n : graph_.nodes) {
    const bool output_pin =
        n.driver == "moonpi.driver.gpio_output" || n.driver == "moonpi.driver.pin_script";
    Json value = output_pin ? Json(false) : Json();
    for (const auto &line : hardware)
      if (line.at("node") == n.id)
        value = line.at("actual");
    nodes[n.id] = values_.contains(n.id)
                      ? values_.at(n.id)
                      : Json{{"value", value}, {"state", running_ ? "ACTIVE" : "STOPPED"}};
    if (output_pin)
      nodes[n.id]["value"] = value;
    if (!running_ && !nodes[n.id].contains("error"))
      nodes[n.id]["state"] = "STOPPED";
  }
  return {{"mode", hardware_.mode()},
          {"running", running_},
          {"emergency", emergency_},
          {"applied_revision", graph_.revision},
          {"running_revision", running_ ? graph_.revision : 0},
          {"sequence", sequence_},
          {"event_count", events_},
          {"active_timers", timers_.size()},
          {"queue_depth", queue_.size()},
          {"hardware", hardware},
          {"nodes", nodes},
          {"error", error_}};
}
} // namespace moonpi
