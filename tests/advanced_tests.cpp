#include "moonpi/app.hpp"
#include "moonpi/security.hpp"
#include "moonpi/sensors.hpp"
#include <cmath>
#include <functional>
#include <iostream>
using namespace moonpi;
namespace {
struct Gate {
  std::mutex mutex;
  std::condition_variable wake;
  bool entered = false, release = false;
};
class BlockingI2c final : public II2cController {
  SimulatedI2cController inner_;
  std::shared_ptr<Gate> gate_;

public:
  explicit BlockingI2c(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}
  void configure(const std::vector<I2cLease> &l) override {
    inner_.configure(l);
  }
  void release_all() noexcept override {
    inner_.release_all();
  }
  std::vector<uint8_t> transfer(const std::string &node, std::span<const uint8_t> tx,
                                size_t count) override {
    {
      std::unique_lock lock(gate_->mutex);
      gate_->entered = true;
      gate_->wake.notify_all();
      gate_->wake.wait(lock, [&] { return gate_->release; });
    }
    return inner_.transfer(node, tx, count);
  }
};
void require(bool value) {
  if (!value)
    throw std::runtime_error("assertion failed");
}
void rejects(const std::function<void()> &fn) {
  bool failed = false;
  try {
    fn();
  } catch (const Error &) {
    failed = true;
  }
  require(failed);
}
Json wait(Runtime &r, const std::function<bool(const Json &)> &ready) {
  for (int i = 0; i < 200; ++i) {
    auto s = r.snapshot();
    if (ready(s))
      return s;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  throw std::runtime_error("runtime condition timed out: " + r.snapshot().dump());
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  const fs::path root = argv[1];
  int passed = 0, failed = 0;
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
  Schemas schemas(root / "schemas");
  ComponentRegistry registry(schemas);
  registry.load(root / "resources/components");
  BoardRegistry boards(root / "resources/boards", schemas);
  GraphCompiler compiler(schemas, registry, boards);
  const auto project = read_json(root / "examples/bme280-alarm.moonpi.json");
  const auto sensor = project["nodes"][0]["id"].get<std::string>();
  test("expanded catalogue contracts", [&] {
    require(registry.diagnostics().empty());
    require(registry.find("moonpi.sensor.bme280")->at("driver_available") == true);
  });
  test("I2C sensor graph compiles", [&] {
    auto v = compiler.compile(project, 1);
    if (!v.valid())
      throw std::runtime_error(v.json().dump());
    require(v.graph.i2c.size() == 1);
    require(v.graph.leases.size() == 1);
  });
  test("sensor definitions cannot misdeclare runtime port types or addresses", [&] {
    for (int mutation = 0; mutation < 2; ++mutation) {
      auto definition = read_json(root / "resources/components/sensors/bme280.json");
      if (mutation == 0)
        definition["ports"][1]["type"] = "Boolean";
      else
        definition["properties"]["address"]["enum"].push_back(120);
      const auto file = root / "build/test-data" / uuid() / "component.json";
      atomic_write(file, definition.dump());
      ComponentRegistry invalid(schemas);
      invalid.load(file);
      require(!invalid.diagnostics().empty());
      require(invalid.find("moonpi.sensor.bme280") == nullptr);
    }
  });
  test("same-bus distinct addresses share SDA/SCL", [&] {
    auto p = project;
    auto second = p["nodes"][0];
    second["id"] = uuid();
    second["properties"]["address"] = 119;
    p["nodes"].push_back(second);
    for (int i = 0; i < 4; ++i) {
      auto e = p["edges"][i];
      e["id"] = uuid();
      e["target"] = second["id"];
      p["edges"].push_back(e);
    }
    require(compiler.compile(p, 1).valid());
    p["nodes"].back()["properties"]["address"] = 118;
    require(!compiler.compile(p, 1).valid());
  });
  test("I2C and GPIO pin conflicts reject both edge orders", [&] {
    auto p = project;
    p["edges"][4]["source_port"] = "header.3";
    require(!compiler.compile(p, 1).valid());
    std::reverse(p["edges"].begin(), p["edges"].end());
    require(!compiler.compile(p, 1).valid());
  });
  test("I2C wrong pin and supply rejected", [&] {
    auto p = project;
    p["edges"][0]["source_port"] = "header.11";
    require(!compiler.compile(p, 1).valid());
    p = project;
    p["edges"][2]["source_port"] = "header.2";
    require(!compiler.compile(p, 1).valid());
  });
  test("BME280 compensation uses real register fixture", [&] {
    SimulatedI2cController bus;
    bus.configure({{sensor, 1, 118, "moonpi.driver.bme280"}});
    Bme280Sensor bme(bus, sensor, [] { return false; });
    const auto r = bme.sample();
    require(std::abs(r.at("temperature").get<double>() - 25.08) < .02);
    require(std::abs(r.at("pressure").get<double>() - 1006.53) < .1);
    require(r.at("humidity").get<double>() >= 0 && r.at("humidity").get<double>() <= 100);
  });
  test("I2C leases bound every transfer", [&] {
    SimulatedI2cController bus;
    bus.configure({{sensor, 1, 118, "moonpi.driver.bme280"}});
    const uint8_t reg = 0xd0;
    rejects([&] { bus.transfer("unowned", std::span(&reg, 1), 1); });
    rejects([&] { bus.transfer(sensor, std::span(&reg, 1), 1000); });
    rejects([&] {
      bus.configure({{sensor, 1, 118, "moonpi.driver.bme280"},
                     {"duplicate", 1, 118, "moonpi.driver.bme280"}});
    });
  });
  test("sensor compare drives GPIO through typed runtime", [&] {
    Runtime r(std::make_unique<SimulatedHardwareBackend>());
    auto graph = compiler.compile(project, 1).graph;
    r.apply(graph);
    r.start();
    auto s = wait(r, [](const Json &s) {
      return !s["hardware"].empty() && s["hardware"][0]["actual"] == true;
    });
    require(s["nodes"][sensor]["temperature"].is_number());
    r.emergency_stop();
    require(r.snapshot()["hardware"].empty());
  });
  test("sensor failure is visible and retry recovers", [&] {
    auto bus = std::make_unique<SimulatedI2cController>();
    bus->inject_failure_after(0);
    Runtime r(std::make_unique<SimulatedHardwareBackend>(), std::move(bus));
    r.apply(compiler.compile(project, 1).graph);
    r.start();
    wait(r, [&](const Json &s) { return s["nodes"][sensor]["state"] == "DEGRADED"; });
    wait(r, [&](const Json &s) { return s["nodes"][sensor]["state"] == "ACTIVE"; });
    require(r.snapshot()["running"] == true);
  });
  test("Boolean constant drives output on Run", [&] {
    auto p = read_json(root / "examples/led-blink.moonpi.json");
    auto &n = p["nodes"][1];
    n["component"] = "moonpi.logic.constant_boolean";
    n["properties"] = {{"value", true}};
    p["edges"][2]["source_port"] = "value";
    p["edges"][2]["target_port"] = "set";
    auto v = compiler.compile(p, 1);
    require(v.valid());
    Runtime r(std::make_unique<SimulatedHardwareBackend>());
    r.apply(v.graph);
    r.start();
    wait(r, [](const Json &s) { return s["hardware"][0]["actual"] == true; });
  });
  test("delay counter conversion math pipeline", [&] {
    CompiledGraph g;
    g.revision = 1;
    g.nodes = {{"timer", "moonpi.driver.interval", {{"interval_ms", 50}}},
               {"delay", "moonpi.driver.delay", {{"delay_ms", 30}}},
               {"counter", "moonpi.driver.counter", {}},
               {"float", "moonpi.driver.to_float", {}},
               {"math", "moonpi.driver.math", {{"operation", "multiply"}, {"operand", 2}}},
               {"display", "moonpi.driver.display", {}}};
    g.routes = {{"timer", "tick", "delay", "trigger"},
                {"delay", "done", "counter", "increment"},
                {"counter", "value", "float", "input"},
                {"float", "value", "math", "input"},
                {"math", "value", "display", "input"}};
    Runtime r(std::make_unique<SimulatedHardwareBackend>());
    r.apply(g);
    r.start();
    wait(r, [](const Json &s) {
      return s["nodes"]["display"]["value"].is_number() &&
             s["nodes"]["display"]["value"].get<double>() >= 4;
    });
    r.stop();
    require(r.snapshot()["active_timers"] == 0);
  });
  test("math failure stops and reports", [&] {
    CompiledGraph g;
    g.revision = 1;
    g.nodes = {{"number", "moonpi.driver.constant_number", {{"value", 1.0}}},
               {"math", "moonpi.driver.math", {{"operation", "divide"}, {"operand", 0}}}};
    g.routes = {{"number", "value", "math", "input"}};
    Runtime r(std::make_unique<SimulatedHardwareBackend>());
    r.apply(g);
    r.start();
    auto s = wait(r, [](const Json &s) { return !s["error"].is_null(); });
    require(s["running"] == false);
  });
  test("blocked sensor cannot block emergency stop", [&] {
    auto gate = std::make_shared<Gate>();
    Runtime r(std::make_unique<SimulatedHardwareBackend>(), std::make_unique<BlockingI2c>(gate));
    r.apply(compiler.compile(project, 1).graph);
    r.start();
    bool entered;
    {
      std::unique_lock lock(gate->mutex);
      entered = gate->wake.wait_for(lock, std::chrono::seconds(2), [&] { return gate->entered; });
    }
    const auto before = Clock::now();
    r.emergency_stop();
    const auto elapsed = Clock::now() - before;
    {
      std::lock_guard lock(gate->mutex);
      gate->release = true;
      gate->wake.notify_all();
    }
    require(entered);
    require(elapsed < std::chrono::milliseconds(100));
    require(r.snapshot()["hardware"].empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    require(r.snapshot()["nodes"][sensor]["state"] == "STOPPED");
  });
  test("debounced button drives output; bounce is ignored", [&] {
    auto p = read_json(root / "examples/button-led.moonpi.json");
    p["nodes"][0]["properties"]["debounce_ms"] = 150;
    const auto button = p["nodes"][0]["id"].get<std::string>();
    const auto led = p["nodes"][1]["id"].get<std::string>();
    auto v = compiler.compile(p, 1);
    require(v.valid());
    Runtime r(std::make_unique<SimulatedHardwareBackend>());
    r.apply(v.graph);
    r.start();
    rejects([&] { r.inject_input(led, false); });
    r.inject_input(button, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    r.inject_input(button, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    require(r.snapshot()["nodes"][led]["value"] == false);
    r.inject_input(button, false);
    wait(r, [&](const Json &s) { return s["nodes"][led]["value"] == true; });
    r.inject_input(button, true);
    wait(r, [&](const Json &s) { return s["nodes"][button]["value"] == false; });
    r.inject_input(button, false);
    wait(r, [&](const Json &s) { return s["nodes"][led]["value"] == false; });
    r.stop();
    rejects([&] { r.inject_input(button, true); });
  });
  test("UTC cron ranges, steps, weekdays and syntax", [&] {
    Cron c("*/15 9-17 * * 1-5");
    require(c.matches(parse_utc("2026-09-09T12:00:00Z")));
    require(!c.matches(parse_utc("2026-09-13T12:00:00Z")));
    require(!c.matches(parse_utc("2026-09-09T12:01:00Z")));
    require(Cron("0 12 1 * 3").matches(parse_utc("2026-09-09T12:00:00Z")));
    rejects([] { Cron c("60 * * * *"); });
    rejects([] { Cron c("*/0 * * * *"); });
    rejects([] { Cron c("* * * *"); });
    rejects([] { parse_utc("2026-02-30T12:00:00Z"); });
  });
  test("one-time checkpoints prevent restart replay", [&] {
    const auto path = root / "build/test-data" / uuid() / "scheduler.json";
    const auto when = parse_utc("2026-09-09T12:00:00Z");
    std::vector<CompiledNode> nodes = {
        {"once",
         "moonpi.driver.once",
         {{"at_utc", "2026-09-09T12:00:00Z"}, {"missed", "RUN_ONCE_AFTER_START"}}}};
    {
      CalendarScheduler s(path);
      s.configure(nodes, when - 1);
      require(s.due(when - 1).empty());
      require(s.due(when) == std::vector<std::string>{"once"});
      require(s.due(when + 1).empty());
    }
    CalendarScheduler reloaded(path);
    reloaded.configure(nodes, when + 60);
    require(reloaded.due(when + 60).empty());
  });
  test("runtime routes a catch-up schedule once across process-object restart", [&] {
    const auto path = root / "build/test-data" / uuid() / "scheduler.json";
    CompiledGraph graph;
    graph.revision = 1;
    graph.nodes = {{"once",
                    "moonpi.driver.once",
                    {{"at_utc", "2000-01-01T00:00:00Z"}, {"missed", "RUN_ONCE_AFTER_START"}}},
                   {"counter", "moonpi.driver.counter", {}}};
    graph.routes = {{"once", "tick", "counter", "increment"}};
    {
      Runtime r(std::make_unique<SimulatedHardwareBackend>(), nullptr, path);
      r.apply(graph);
      r.start();
      wait(r, [](const Json &s) { return s["nodes"]["counter"]["value"] == 1; });
      r.stop();
    }
    Runtime reloaded(std::make_unique<SimulatedHardwareBackend>(), nullptr, path);
    reloaded.apply(graph);
    reloaded.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(reloaded.snapshot()["event_count"] == 0);
    require(reloaded.snapshot()["running"] == true);
  });
  test("missed once skips by default; catch-up is explicit", [&] {
    const auto when = parse_utc("2026-09-09T12:00:00Z");
    std::vector<CompiledNode> n = {
        {"one",
         "moonpi.driver.once",
         {{"at_utc", "2026-09-09T12:00:00Z"}, {"missed", "SKIP_MISSED"}}}};
    CalendarScheduler s;
    s.configure(n, when + 60);
    require(s.due(when + 60).empty());
    n[0].properties["missed"] = "RUN_ONCE_AFTER_START";
    s.configure(n, when + 60);
    require(s.due(when + 60).size() == 1);
  });
  test("cron clock jumps skip backlog and prevent duplicates", [&] {
    const auto now = parse_utc("2026-09-09T12:00:00Z");
    CalendarScheduler s;
    s.configure({{"cron", "moonpi.driver.cron", {{"expression", "* * * * *"}}}}, now);
    require(s.due(now).empty());
    require(s.due(now + 60).size() == 1);
    require(s.due(now + 60).empty());
    require(s.due(now - 60).empty());
    require(s.due(now + 600).size() == 1);
  });
  test("corrupt scheduler state protects schedules but permits manual graphs", [&] {
    const auto path = root / "build/test-data" / uuid() / "scheduler.json";
    atomic_write(path, "{bad");
    CalendarScheduler s(path);
    s.configure({}, 0);
    rejects(
        [&] { s.configure({{"cron", "moonpi.driver.cron", {{"expression", "* * * * *"}}}}, 0); });
  });
  test("scheduler rejects overflowing and negative checkpoint slots", [&] {
    for (const auto &slot : {Json(-1), Json(UINT64_MAX)}) {
      const auto path = root / "build/test-data" / uuid() / "scheduler.json";
      atomic_write(path, Json{{"version", 1},
                              {"records", {{"cron", {{"fingerprint", "test"}, {"slot", slot}}}}}}
                             .dump());
      CalendarScheduler s(path);
      rejects(
          [&] { s.configure({{"cron", "moonpi.driver.cron", {{"expression", "* * * * *"}}}}, 0); });
    }
  });
  test("checkpoint failure emits nothing and can retry after storage recovery", [&] {
    const auto directory = root / "build/test-data" / uuid();
    fs::create_directories(directory);
    const auto blocker = directory / "blocked";
    CalendarScheduler s(blocker / "scheduler.json");
    atomic_write(blocker, "file prevents directory creation");
    const auto now = parse_utc("2026-09-09T12:00:00Z");
    s.configure({{"once",
                  "moonpi.driver.once",
                  {{"at_utc", "2026-09-09T12:00:00Z"}, {"missed", "SKIP_MISSED"}}}},
                now);
    bool failed_write = false;
    try {
      s.due(now);
    } catch (const std::exception &) {
      failed_write = true;
    }
    require(failed_write);
    fs::remove(blocker);
    require(s.due(now) == std::vector<std::string>{"once"});
    require(s.due(now).empty());
  });
  const auto sequence_project = read_json(root / "examples/led-sequence.moonpi.json");
  const auto sequence_graph = [&] {
    auto p = sequence_project;
    for (auto &n : p["nodes"])
      if (n["component"] == "moonpi.logic.sequence")
        for (int i = 1; i <= 4; ++i)
          n["properties"]["step_" + std::to_string(i) + "_ms"] = 80;
    const auto v = compiler.compile(p, 1);
    if (!v.valid())
      throw std::runtime_error(v.json().dump());
    return v.graph;
  };
  const auto sequence_id = [&] {
    for (const auto &n : sequence_project["nodes"])
      if (n["component"] == "moonpi.logic.sequence")
        return n["id"].get<std::string>();
    throw std::runtime_error("Missing sequence fixture");
  }();
  test("sequence waits for trigger and executes four ordered steps", [&] {
    Runtime r(std::make_unique<SimulatedHardwareBackend>());
    r.apply(sequence_graph());
    r.start();
    require(r.snapshot()["nodes"][sequence_id]["active"] == false);
    require(r.snapshot()["active_timers"] == 0);
    rejects([&] { r.trigger(sequence_id, "step_1"); });
    rejects([&] { r.trigger("unknown", "start"); });
    r.trigger(sequence_id, "start");
    for (int step = 1; step <= 4; ++step) {
      auto s = wait(r, [&](const Json &s) { return s["nodes"][sequence_id]["step"] == step; });
      require(s["hardware"][0]["actual"] == (step % 2 == 1));
      require(s["active_timers"] == 1);
    }
    auto s =
        wait(r, [&](const Json &s) { return s["nodes"][sequence_id]["state"] == "COMPLETED"; });
    require(s["nodes"][sequence_id]["active"] == false);
    require(s["active_timers"] == 0);
    require(s["hardware"][0]["actual"] == false);
    r.stop();
    rejects([&] { r.trigger(sequence_id, "start"); });
  });
  test("sequence cancellation removes pending work and can start again", [&] {
    Runtime r(std::make_unique<SimulatedHardwareBackend>());
    r.apply(sequence_graph());
    r.start();
    r.trigger(sequence_id, "start");
    wait(r, [&](const Json &s) { return s["nodes"][sequence_id]["step"] == 1; });
    r.trigger(sequence_id, "cancel");
    auto s =
        wait(r, [&](const Json &s) { return s["nodes"][sequence_id]["state"] == "CANCELLED"; });
    require(s["active_timers"] == 0);
    require(s["hardware"][0]["actual"] == false);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    require(r.snapshot()["nodes"][sequence_id]["step"] == 0);
    r.trigger(sequence_id, "start");
    wait(r, [&](const Json &s) { return s["nodes"][sequence_id]["active"] == true; });
    r.emergency_stop();
    require(r.snapshot()["active_timers"] == 0);
    require(r.snapshot()["nodes"][sequence_id]["active"] == false);
    require(r.snapshot()["hardware"].empty());
  });
  test("sequence restart replaces its timer; ignore does not rewind", [&] {
    for (const auto &policy : {"IGNORE", "RESTART"}) {
      auto graph = sequence_graph();
      for (auto &n : graph.nodes)
        if (n.id == sequence_id)
          n.properties["retrigger"] = policy;
      Runtime r(std::make_unique<SimulatedHardwareBackend>());
      r.apply(graph);
      r.start();
      r.trigger(sequence_id, "start");
      wait(r, [&](const Json &s) { return s["nodes"][sequence_id]["step"] == 2; });
      r.trigger(sequence_id, "start");
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      auto s = r.snapshot();
      require(s["nodes"][sequence_id]["step"] == (std::string(policy) == "RESTART" ? 1 : 2));
      require(s["active_timers"] == 1);
    }
  });
  test("sequence compiler rejects unbounded durations and step counts", [&] {
    for (const auto &property : {"steps", "step_1_ms"}) {
      auto p = sequence_project;
      for (auto &n : p["nodes"])
        if (n["id"] == sequence_id)
          n["properties"][property] = 0;
      require(!compiler.compile(p, 1).valid());
    }
  });
  test("Trigger fan-in allows distinct events but rejects duplicate wires", [&] {
    require(compiler.compile(sequence_project, 1).valid());
    auto p = sequence_project;
    auto duplicate = p["edges"].back();
    duplicate["id"] = uuid();
    p["edges"].push_back(duplicate);
    require(!compiler.compile(p, 1).valid());
    p = sequence_project;
    auto second_latch = p["nodes"].back();
    second_latch["id"] = uuid();
    p["nodes"].push_back(second_latch);
    auto writer = p["edges"].back();
    writer["id"] = uuid();
    writer["source"] = second_latch["id"];
    p["edges"].push_back(writer);
    require(!compiler.compile(p, 1).valid());
  });
  test("manual Trigger API rejects missing fields and unapplied design revisions", [&] {
    App app(schemas, registry, boards, root / "build/test-data" / uuid());
    app.command({{"version", 1},
                 {"command", "SetDesign"},
                 {"expected_revision", 1},
                 {"project", sequence_project}});
    app.command({{"version", 1}, {"command", "Apply"}, {"expected_revision", 2}});
    app.command({{"version", 1}, {"command", "Start"}, {"expected_revision", 2}});
    rejects(
        [&] { app.command({{"version", 1}, {"command", "Trigger"}, {"expected_revision", 2}}); });
    app.command({{"version", 1},
                 {"command", "Trigger"},
                 {"expected_revision", 2},
                 {"node", sequence_id},
                 {"port", "start"}});
    auto changed = sequence_project;
    changed["name"] = "Edited while running";
    app.command(
        {{"version", 1}, {"command", "SetDesign"}, {"expected_revision", 2}, {"project", changed}});
    rejects([&] {
      app.command({{"version", 1},
                   {"command", "Trigger"},
                   {"expected_revision", 3},
                   {"node", sequence_id},
                   {"port", "start"}});
    });
    app.command({{"version", 1}, {"command", "EmergencyStop"}, {"expected_revision", 0}});
    require(app.snapshot()["runtime"]["hardware"].empty());
  });
  test("session cookies allow unrelated cookies and reject ambiguity", [] {
    require(session_cookie("other=a; moonpi_session=abc; theme=dark") == "abc");
    require(session_cookie("moonpi_session=abc; moonpi_session=abc").empty());
    require(session_cookie("other_moonpi_session=abc").empty());
    require(session_cookie("\tmoonpi_session=abc \t") == "abc");
    require(secret_equal("same", "same"));
    require(!secret_equal("same", "some"));
    require(!secret_equal("same", "same-long"));
  });
  test("OS-generated sessions expire without reconnect extension and can be revoked", [] {
    Sessions sessions;
    const auto now = Clock::now();
    const auto id = sessions.create({}, now);
    require(id.size() == 64);
    require(sessions.valid(id, now));
    require(sessions.create(id, now + std::chrono::hours(7)) == id);
    require(!sessions.valid(id, now + std::chrono::hours(8)));
    const auto fresh = sessions.create({}, now);
    require(fresh != id);
    sessions.revoke(fresh);
    require(!sessions.valid(fresh, now));
  });
  test("session allocation is bounded and expired slots are reclaimed", [] {
    Sessions sessions;
    const auto now = Clock::now();
    for (int i = 0; i < 16; ++i)
      sessions.create({}, now);
    rejects([&] { sessions.create({}, now); });
    const auto next = sessions.create({}, now + std::chrono::hours(8));
    require(sessions.valid(next, now + std::chrono::hours(8)));
  });
  test("audit health records a disk failure and recovers on successful write", [&] {
    const auto directory = root / "build/test-data" / uuid();
    fs::create_directories(directory / "audit.jsonl");
    AuditLog log(directory / "audit.jsonl");
    try {
      log.record("test.failed");
    } catch (const std::exception &) {
    }
    require(log.status()["status"] == "degraded");
    fs::remove(directory / "audit.jsonl");
    log.record("test.recovered");
    require(log.status()["status"] == "healthy");
  });
  test("audit rotation retains three archives and valid JSON records", [&] {
    const auto directory = root / "build/test-data" / uuid();
    const auto file = directory / "audit.jsonl";
    AuditLog log(file, 1);
    for (int i = 0; i < 6; ++i)
      log.record("rotation", {{"index", i}});
    require(log.status()["status"] == "healthy");
    for (int archive = 0; archive <= 3; ++archive) {
      const auto saved =
          read_json(archive == 0 ? file : fs::path(file.string() + "." + std::to_string(archive)));
      require(saved["fields"]["index"] == 5 - archive);
    }
    require(!fs::exists(file.string() + ".4"));
  });
  test("pin script event values and persistent isolated state", [&] {
    PinScript script(
        "count = (count or 0) + 1; pin.write(event == 'input' and value or count % 2 == 1)");
    require(script.execute("run", false, false));
    require(!script.execute("run", false, true));
    require(script.execute("input", true, false));
    PinScript second("pin.write(count == nil)");
    require(second.execute("start", false, false));
  });
  test("pin script rejects syntax binary and oversized source", [&] {
    rejects([&] { PinScript s("if then"); });
    rejects([&] { PinScript s(std::string(16385, ' ')); });
    rejects([&] { PinScript s(std::string("\x1bLua", 4)); });
  });
  test("pin script sandbox has no external or hook bypass APIs", [&] {
    PinScript s(
        "assert(os == nil and io == nil and package == nil and require == nil and load == nil and "
        "dofile == nil and loadfile == nil and debug == nil and pcall == nil and xpcall == nil and "
        "coroutine == nil and setmetatable == nil and collectgarbage == nil); pin.write(true)");
    require(s.execute("run", false, false));
    PinScript typed("pin.write(1)");
    rejects([&] { typed.execute("run", false, false); });
  });
  test("pin script infinite loop and memory exhaustion are bounded", [&] {
    for (const auto *source :
         {"while true do end", "local s='xxxxxxxx'; for i=1,30 do s=s..s end"}) {
      PinScript s(source);
      const auto began = Clock::now();
      rejects([&] { s.execute("run", false, false); });
      require(Clock::now() - began < std::chrono::milliseconds(100));
    }
  });
  const auto scripted = read_json(root / "examples/scripted-pin.moonpi.json");
  test("script compiler enforces syntax and exclusive pin wiring", [&] {
    require(compiler.compile(scripted, 1).valid());
    auto p = scripted;
    p["nodes"][0]["properties"]["script"] = "if then";
    require(!compiler.compile(p, 1).valid());
    p = scripted;
    auto n = p["nodes"][0];
    n["id"] = uuid();
    p["nodes"].push_back(n);
    for (int i = 0; i < 2; ++i) {
      auto edge = p["edges"][i];
      edge["id"] = uuid();
      edge["target"] = n["id"];
      p["edges"].push_back(edge);
    }
    require(!compiler.compile(p, 1).valid());
  });
  test("script runtime start manual run emergency stop and restart", [&] {
    auto p = scripted;
    p["nodes"].erase(1);
    p["edges"].erase(2);
    auto v = compiler.compile(p, 1);
    require(v.valid());
    const auto id = p["nodes"][0]["id"].get<std::string>();
    Runtime r(std::make_unique<SimulatedHardwareBackend>());
    r.apply(v.graph);
    r.start();
    wait(r, [&](const Json &s) { return s["event_count"].get<int>() >= 1; });
    require(r.snapshot()["nodes"][id]["value"] == false);
    r.trigger(id, "run");
    wait(r, [&](const Json &s) { return s["nodes"][id]["value"] == true; });
    r.emergency_stop();
    require(r.snapshot()["nodes"][id]["value"] == false);
    rejects([&] { r.trigger(id, "run"); });
    r.clear_emergency();
    r.start();
    wait(r, [&](const Json &s) { return s["nodes"][id]["value"] == false; });
  });
  test("script fault deenergizes graph and retains node diagnostic", [&] {
    auto p = scripted;
    p["nodes"][0]["properties"]["script"] = "pin.write(true); while true do end";
    const auto id = p["nodes"][0]["id"].get<std::string>();
    auto v = compiler.compile(p, 1);
    require(v.valid());
    Runtime r(std::make_unique<SimulatedHardwareBackend>());
    r.apply(v.graph);
    r.start();
    auto s = wait(r, [](const Json &s) { return !s["running"].get<bool>(); });
    require(s["nodes"][id]["value"] == false);
    require(s["nodes"][id]["state"] == "ERROR");
    require(s["nodes"][id]["error"].get<std::string>().find("instruction limit") !=
            std::string::npos);
    require(s["hardware"].empty());
  });
  test("new digital component profiles compile with documented wiring", [&] {
    for (const auto &c : registry.catalogue()) {
      const std::string driver = c["driver"]["id"];
      if (driver != "moonpi.driver.gpio_input" && driver != "moonpi.driver.gpio_output")
        continue;
      auto p = scripted;
      p["nodes"].erase(1);
      p["edges"].erase(2);
      p["nodes"][0]["component"] = c["id"];
      p["nodes"][0]["properties"] = Json::object();
      const auto v = compiler.compile(p, 1);
      if (!v.valid())
        throw std::runtime_error(v.json().dump());
      require(v.graph.leases.size() == 1);
    }
  });
  test("idle worker shutdown survives repeated scheduling races", [&] {
    for (int i = 0; i < 500; ++i) {
      Runtime runtime(std::make_unique<SimulatedHardwareBackend>());
      if (i % 2)
        std::this_thread::yield();
      if (i % 5 == 0)
        runtime.stop();
    }
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
