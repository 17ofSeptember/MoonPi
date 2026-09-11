#include "moonpi/app.hpp"
#include "moonpi/doctor.hpp"
#include <functional>
#include <iostream>
using namespace moonpi;
int main(int argc, char **argv) {
  if (argc < 2)
    return 1;
  fs::path root = argv[1];
  int failed = 0, passed = 0;
  auto test = [&](const char *name, const std::function<void()> &fn) {
    try {
      fn();
      ++passed;
      std::cout << "PASS " << name << "\n";
    } catch (const std::exception &e) {
      ++failed;
      std::cerr << "FAIL " << name << ": " << e.what() << "\n";
    }
  };
  auto require = [](bool ok) {
    if (!ok)
      throw std::runtime_error("assertion failed");
  };
  Schemas schemas(root / "schemas");
  ComponentRegistry registry(schemas);
  registry.load(root / "resources/components");
  BoardRegistry boards(root / "resources/boards", schemas);
  GraphCompiler compiler(schemas, registry, boards);
  auto project = read_json(root / "examples/led-blink.moonpi.json");
  test("all built-in definitions", [&] {
    require(registry.diagnostics().empty());
    require(registry.catalogue().size() >= 15);
  });
  test("accurate header", [&] {
    auto b = boards.get("moonpi.board.rpi3b-plus");
    require(b["pins"].size() == 40);
    require(b["pins"][10]["bcm"] == 17);
    require(b["pins"][26]["reserved"] == true);
    require(b["pins"][1]["kind"] == "power");
  });
  test("compile blink", [&] {
    auto v = compiler.compile(project, 1);
    if (!v.valid())
      throw std::runtime_error(v.json().dump());
    require(v.graph.leases.size() == 1);
    require(v.graph.leases[0].bcm == 17);
  });
  test("power rail cannot be GPIO", [&] {
    auto p = project;
    p["edges"][0]["source_port"] = "header.2";
    require(!compiler.compile(p, 1).valid());
  });
  test("missing ground", [&] {
    auto p = project;
    p["edges"].erase(1);
    require(!compiler.compile(p, 1).valid());
  });
  test("resistor current limit", [&] {
    auto p = project;
    p["nodes"][0]["properties"]["resistor_ohms"] = 1;
    require(!compiler.compile(p, 1).valid());
  });
  test("typed port mismatch", [&] {
    auto p = project;
    p["edges"][2]["target_port"] = "set";
    require(!compiler.compile(p, 1).valid());
  });
  test("duplicate UUID", [&] {
    auto p = project;
    p["nodes"].push_back(p["nodes"][0]);
    require(!compiler.compile(p, 1).valid());
  });
  test("missing component retained", [&] {
    auto p = project;
    p["nodes"][0]["component"] = "user.missing";
    require(!compiler.compile(p, 1).valid());
    schemas.validate("project", p);
    require(p["nodes"].size() == 2);
  });
  test("exclusive reservation conflict", [&] {
    auto p = project;
    auto node = p["nodes"][0];
    node["id"] = uuid();
    p["nodes"].push_back(node);
    for (int i = 0; i < 2; ++i) {
      auto edge = p["edges"][i];
      edge["id"] = uuid();
      edge["target"] = node["id"];
      p["edges"].push_back(edge);
    }
    require(!compiler.compile(p, 1).valid());
  });
  test("deterministic candidate scoring", [&] {
    auto a = compiler.compatible_resources("moonpi.board.rpi3b-plus", "gpio_output");
    require(a == compiler.compatible_resources("moonpi.board.rpi3b-plus", "gpio_output"));
    require(a[0]["id"] == "header.11");
  });
  test("transaction failure releases prepared lines", [&] {
    auto gpio = std::make_unique<SimulatedHardwareBackend>();
    auto *raw = gpio.get();
    HardwareAuthority h(std::move(gpio));
    raw->inject_failure_after(1);
    bool threw = false;
    try {
      h.apply({{"header.11", "a", 17, false}, {"header.13", "b", 27, false}});
    } catch (...) {
      threw = true;
    }
    require(threw);
    require(raw->snapshot().empty());
    require(h.state().empty());
  });
  test("native interval and emergency stop", [&] {
    Runtime runtime(std::make_unique<SimulatedHardwareBackend>());
    runtime.apply(compiler.compile(project, 1).graph);
    runtime.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    require(runtime.snapshot()["event_count"].get<int>() >= 1);
    require(runtime.snapshot()["hardware"][0]["actual"] == true);
    runtime.emergency_stop();
    require(runtime.snapshot()["hardware"].empty());
    bool threw = false;
    try {
      runtime.start();
    } catch (...) {
      threw = true;
    }
    require(threw);
    runtime.clear_emergency();
    runtime.start();
    runtime.stop();
  });
  test("repeated runtime lifecycle", [&] {
    Runtime runtime(std::make_unique<SimulatedHardwareBackend>());
    for (int i = 0; i < 100; ++i) {
      runtime.apply(compiler.compile(project, 1).graph);
      runtime.start();
      runtime.stop();
    }
    require(runtime.snapshot()["active_timers"] == 0);
    require(runtime.snapshot()["hardware"].empty());
  });
  auto temp = root / "build/test-data" / uuid();
  fs::create_directories(temp);
  test("atomic save and backup", [&] {
    atomic_write(temp / "project.json", project.dump());
    auto p = project;
    p["extensions"] = {{"future", 42}};
    atomic_write(temp / "project.json", p.dump());
    require(read_json(temp / "project.json") == p);
    require(read_json(temp / "project.json.bak") == project);
  });
  test("app revisions, persistence and safe reload", [&] {
    {
      App app(schemas, registry, boards, temp / "app");
      auto s = app.command({{"version", 1},
                            {"command", "SetDesign"},
                            {"expected_revision", 1},
                            {"project", project}});
      require(s["design_revision"] == 2);
      app.command({{"version", 1}, {"command", "Save"}, {"expected_revision", 2}});
      bool threw = false;
      try {
        app.command({{"version", 1}, {"command", "Apply"}, {"expected_revision", 1}});
      } catch (...) {
        threw = true;
      }
      require(threw);
    }
    App app(schemas, registry, boards, temp / "app");
    require(app.snapshot()["project"] == project);
    require(app.snapshot()["runtime"]["running"] == false);
    require(app.snapshot()["runtime"]["hardware"].empty());
  });
  test("unavailable AI fails closed", [&] {
    UnavailableAiEngine ai;
    require(ai.status()["available"] == false);
    bool threw = false;
    try {
      ai.propose({});
    } catch (...) {
      threw = true;
    }
    require(threw);
  });
  test("malformed graph corpus", [&] {
    for (auto p : {Json(), Json::array(), Json::object(), Json("bad"), Json{{"version", 999}}})
      require(!compiler.compile(p, 1).valid());
  });
  test("all physical BCM mappings", [&] {
    std::vector<int> expected = {-1, -1, 2,  -1, 3,  -1, 4,  14, -1, 15, 17, 18, 27, -1,
                                 22, 23, -1, 24, 10, -1, 9,  25, 11, 8,  -1, 7,  0,  1,
                                 5,  -1, 6,  12, 13, -1, 19, 16, 26, 20, -1, 21};
    const auto &pins = boards.get("moonpi.board.rpi3b-plus").at("pins");
    for (size_t i = 0; i < 40; ++i) {
      require(pins[i]["physical"] == i + 1);
      require((pins[i]["bcm"].is_null() ? -1 : pins[i]["bcm"].get<int>()) == expected[i]);
    }
  });
  test("unavailable driver remains visible", [&] {
    auto dir = temp / "missing-driver";
    auto c = read_json(root / "resources/components/basic/led.json");
    c["id"] = "user.unknown";
    c["driver"]["id"] = "user.driver.unknown";
    atomic_write(dir / "component.json", c.dump());
    ComponentRegistry r(schemas);
    r.load(dir, true);
    require(r.find("user.unknown") != nullptr);
    require(r.find("user.unknown")->at("driver_available") == false);
  });
  test("malformed definitions skipped with filenames", [&] {
    auto dir = temp / "malformed";
    atomic_write(dir / "broken.json", "{bad");
    ComponentRegistry r(schemas);
    r.load(dir);
    require(r.catalogue().empty());
    require(r.diagnostics().size() == 1);
    require(r.diagnostics()[0]["subject"].get<std::string>().find("broken.json") !=
            std::string::npos);
  });
  test("duplicate catalogue IDs rejected", [&] {
    ComponentRegistry r(schemas);
    r.load(root / "resources/components");
    r.load(root / "resources/components");
    require(r.catalogue().size() == registry.catalogue().size());
    require(r.diagnostics().size() == registry.catalogue().size());
  });
  test("future project version protected", [&] {
    auto p = project;
    p["version"] = 999;
    atomic_write(temp / "future/project.moonpi.json", p.dump());
    App app(schemas, registry, boards, temp / "future");
    require(app.snapshot()["load_failed"] == true);
    bool rejected = false;
    try {
      app.command({{"version", 1}, {"command", "Save"}, {"expected_revision", 1}});
    } catch (...) {
      rejected = true;
    }
    require(rejected);
    require(read_json(temp / "future/project.moonpi.json") == p);
  });
  test("design edits do not mutate running snapshot", [&] {
    App app(schemas, registry, boards, temp / "edits");
    app.command(
        {{"version", 1}, {"command", "SetDesign"}, {"expected_revision", 1}, {"project", project}});
    app.command({{"version", 1}, {"command", "Apply"}, {"expected_revision", 2}});
    app.command({{"version", 1}, {"command", "Start"}, {"expected_revision", 2}});
    auto invalid = project;
    invalid["edges"][0]["source_port"] = "header.2";
    auto s = app.command(
        {{"version", 1}, {"command", "SetDesign"}, {"expected_revision", 2}, {"project", invalid}});
    require(s["validation"]["valid"] == false);
    require(s["runtime"]["running_revision"] == 2);
    require(s["design_revision"] == 3);
    require(s["runtime"]["hardware"][0]["bcm"] == 17);
  });
  test("emergency stop survives unavailable audit disk", [&] {
    App app(schemas, registry, boards, temp / "disk");
    app.command(
        {{"version", 1}, {"command", "SetDesign"}, {"expected_revision", 1}, {"project", project}});
    app.command({{"version", 1}, {"command", "Apply"}, {"expected_revision", 2}});
    app.command({{"version", 1}, {"command", "Start"}, {"expected_revision", 2}});
    fs::rename(temp / "disk/logs/audit.jsonl", temp / "disk/logs/saved.jsonl");
    fs::create_directory(temp / "disk/logs/audit.jsonl");
    auto s = app.command({{"version", 1}, {"command", "EmergencyStop"}, {"expected_revision", 0}});
    require(s["runtime"]["emergency"] == true);
    require(s["runtime"]["hardware"].empty());
  });
  test("single process data ownership", [&] {
    ProcessLock one(temp / "locked/process.lock");
    bool rejected = false;
    try {
      ProcessLock two(temp / "locked/process.lock");
    } catch (...) {
      rejected = true;
    }
    require(rejected);
  });
  test("bounded JSON depth", [&] {
    bool rejected = false;
    try {
      parse_json(std::string(100, '[') + "0" + std::string(100, ']'));
    } catch (...) {
      rejected = true;
    }
    require(rejected);
  });
  test("saved state tracks disk independently from validation and apply", [&] {
    App app(schemas, registry, boards, temp / "saved-state");
    auto send = [&](const std::string &name) {
      return app.command({{"version", 1},
                          {"command", name},
                          {"expected_revision", app.snapshot()["design_revision"]}});
    };
    require(app.snapshot()["project_saved"] == false);
    send("Save");
    require(app.snapshot()["project_saved"] == true);
    auto changed = app.snapshot()["project"];
    changed["name"] = "Unsaved edit";
    app.command({{"version", 1},
                 {"command", "SetDesign"},
                 {"expected_revision", app.snapshot()["design_revision"]},
                 {"project", changed}});
    send("Validate");
    send("Apply");
    require(app.snapshot()["project_saved"] == false);
    send("Load");
    require(app.snapshot()["project_saved"] == true);
    require(app.snapshot()["project"]["name"] != "Unsaved edit");
  });
  test("every gallery project passes native compilation", [&] {
    for (const auto &name :
         {"led-blink", "button-led", "led-sequence", "bme280-alarm", "scripted-pin"}) {
      const auto v =
          compiler.compile(read_json(root / "examples" / (std::string(name) + ".moonpi.json")), 1);
      if (!v.valid())
        throw std::runtime_error(v.json().dump());
    }
  });
  test("installation check never creates project data and detects missing web", [&] {
    const auto data = temp / "doctor-uncreated";
    const auto web = temp / "doctor-web";
    fs::create_directories(web / "assets");
    atomic_write(web / "index.html", "<html></html>", false);
    Json config = {{"version", 1},
                   {"resources", (root / "resources").string()},
                   {"schemas", (root / "schemas").string()},
                   {"web", web.string()},
                   {"data", data.string()},
                   {"mode", "simulation"},
                   {"gpio_chip", "/dev/gpiochip0"}};
    auto report = installation_check(config);
    require(report["ready"] == true);
    require(report["read_only"] == true);
    require(!fs::exists(data));
    config["web"] = (temp / "missing-web").string();
    require(installation_check(config)["ready"] == false);
    require(!fs::exists(data));
    config["schemas"] = (temp / "missing-schemas").string();
    require(installation_check(config)["ready"] == false);
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
