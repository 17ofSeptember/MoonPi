#include "moonpi/graph.hpp"
#include "moonpi/schedule.hpp"
#include "moonpi/script.hpp"
#include <queue>
#include <regex>
#include <set>
namespace moonpi {
Json blank_project() {
  return {{"format", "moonpi-project"},
          {"version", 1},
          {"id", uuid()},
          {"name", "Untitled project"},
          {"board",
           {{"definition", "moonpi.board.rpi3b-plus"},
            {"node_id", uuid()},
            {"position", {{"x", 80}, {"y", 60}}}}},
          {"nodes", Json::array()},
          {"edges", Json::array()}};
}
Json Validation::json() const {
  Json leases = Json::array();
  for (const auto &l : graph.leases)
    leases.push_back({{"resource", l.resource},
                      {"node", l.node},
                      {"bcm", l.bcm},
                      {"safe", l.safe},
                      {"direction", l.input ? "input" : "output"}});
  Json buses = Json::array();
  for (const auto &l : graph.i2c)
    buses.push_back({{"node", l.node}, {"bus", l.bus}, {"address", l.address}});
  return {{"valid", valid()},
          {"diagnostics", diagnostics},
          {"leases", leases},
          {"i2c_leases", buses},
          {"revision", graph.revision}};
}
Json GraphCompiler::compatible_resources(const std::string &board, const std::string &role) const {
  std::vector<Json> pins;
  for (const auto &p : boards_.get(board).at("pins"))
    if (!p.at("reserved").get<bool>() &&
        std::find(p.at("capabilities").begin(), p.at("capabilities").end(), role) !=
            p.at("capabilities").end())
      pins.push_back(p);
  // Prefer ordinary GPIO, preserving scarce alternate functions; physical pin is stable tie-break.
  std::sort(pins.begin(), pins.end(), [](const Json &a, const Json &b) {
    return std::make_pair(a.at("capabilities").size(), a.at("physical").get<int>()) <
           std::make_pair(b.at("capabilities").size(), b.at("physical").get<int>());
  });
  return pins;
}
Validation GraphCompiler::compile(const Json &project, std::uint64_t revision) const {
  Validation v;
  v.graph.revision = revision;
  auto fail = [&](std::string code, std::string message, std::string subject = "") {
    v.diagnostics.push_back(diagnostic(code, message, subject));
  };
  try {
    schemas_.validate("project", project);
    const auto &board = boards_.get(project.at("board").at("definition"));
    const std::string board_id = project.at("board").at("node_id");
    std::map<std::string, const Json *> nodes, defs, pins;
    std::set<std::string> ids{board_id, project.at("id").get<std::string>()};
    for (const auto &pin : board.at("pins"))
      pins[pin.at("id")] = &pin;
    for (const auto &node : project.at("nodes")) {
      std::string id = node.at("id");
      if (!ids.insert(id).second) {
        fail("graph.duplicate_id", "Duplicate identity", id);
        continue;
      }
      nodes[id] = &node;
      const auto *def = components_.find(node.at("component"));
      if (!def) {
        fail("component.missing", "Missing Component: " + node.at("component").get<std::string>(),
             id);
        continue;
      }
      defs[id] = def;
      if (!def->at("driver_available").get<bool>())
        fail("driver.unavailable", "Component driver unavailable", id);
      Json props = Json::object(), property_schemas = Json::object();
      for (const auto &[key, p] : def->at("properties").items()) {
        props[key] = p.at("default");
        property_schemas[key] = p;
      }
      props.update(node.at("properties"));
      try {
        nlohmann::json_schema::json_validator validator;
        validator.set_root_schema({{"type", "object"},
                                   {"properties", property_schemas},
                                   {"additionalProperties", false}});
        validator.validate(props);
      } catch (const std::exception &e) {
        fail("component.property", e.what(), id);
      }
      const auto &electrical = def->at("electrical");
      if (electrical.at("requires_level_shifter").get<bool>() ||
          electrical.at("logic_voltage").get<double>() > board.at("logic_voltage").get<double>())
        fail("safety.level_shift", "Unsupported level shifting or excessive logic voltage", id);
      if (electrical.at("safe_state").get<bool>())
        fail("safety.safe_state", "This driver version supports de-energized LOW safe state only",
             id);
      if (electrical.at("max_current_ma").get<double>() >
          board.at("max_gpio_current_ma").get<double>())
        fail("safety.current", "Component exceeds conservative per-GPIO current budget", id);
      if (electrical.at("requires_resistor").get<bool>()) {
        if (!props.contains("resistor_ohms") || !props["resistor_ohms"].is_number() ||
            props["resistor_ohms"].get<double>() <= 0 ||
            3300.0 / props["resistor_ohms"].get<double>() >
                electrical.at("max_current_ma").get<double>())
          fail("safety.resistor",
               "A current-limiting series resistor within the component budget is required", id);
      }
      const std::string driver = def->at("driver").at("id");
      if (driver == "moonpi.driver.pin_script") {
        try {
          PinScript script(props.at("script"));
        } catch (const std::exception &e) {
          fail("script.invalid", e.what(), id);
        }
      }
      try {
        if (driver == "moonpi.driver.cron") {
          Cron schedule(props.at("expression"));
        }
        if (driver == "moonpi.driver.once")
          parse_utc(props.at("at_utc"));
        if (driver == "moonpi.driver.math" && props.at("operation") == "divide" &&
            props.at("operand") == 0)
          fail("math.divide_zero", "Division by zero is not a valid configuration", id);
      } catch (const std::exception &e) {
        fail("schedule.invalid", e.what(), id);
      }
      v.graph.nodes.push_back({id, driver, props});
    }
    std::set<std::pair<std::string, std::string>> wired, logic_inputs;
    std::map<std::string, std::string> exclusive;
    std::map<std::string, int> shared;
    std::map<std::string, std::map<std::string, int>> i2c_wiring;
    std::map<std::string, std::vector<std::string>> adjacency;
    std::map<std::string, int> indegree;
    double total_current = 0;
    for (const auto &[id, node] : nodes)
      indegree[id] = 0;
    for (const auto &edge : project.at("edges")) {
      std::string id = edge.at("id"), source = edge.at("source"), target = edge.at("target"),
                  sp = edge.at("source_port"), tp = edge.at("target_port");
      if (!ids.insert(id).second) {
        fail("graph.duplicate_id", "Duplicate edge identity", id);
        continue;
      }
      if (edge.at("kind") == "hardware") {
        if (source != board_id || !pins.contains(sp) || !defs.contains(target)) {
          fail("wiring.endpoint",
               "Hardware edges must connect a board pin to a component connection", id);
          continue;
        }
        const auto &def = *defs.at(target);
        const auto &pin = *pins.at(sp);
        const Json *connection = nullptr;
        for (const auto &c : def.at("connections"))
          if (c.at("id") == tp)
            connection = &c;
        if (!connection) {
          fail("wiring.port", "Unknown hardware port", id);
          continue;
        }
        if (!wired.emplace(target, tp).second) {
          fail("wiring.duplicate", "Component connection already wired", id);
          continue;
        }
        std::string role = connection->at("role");
        if (role == "ground") {
          if (pin.at("kind") != "ground")
            fail("safety.ground", "Ground must connect to a ground pin", id);
          continue;
        }
        if (role == "power") {
          if (pin.at("kind") != "power" || pin.at("voltage") != connection->at("voltage"))
            fail("safety.power", "Supply rail voltage mismatch", id);
          continue;
        }
        if (pin.at("kind") != "gpio" || pin.at("reserved").get<bool>()) {
          fail("safety.not_gpio", "Power, ground and reserved pins cannot be controlled as GPIO",
               id);
          continue;
        }
        if (connection->at("voltage").get<double>() > board.at("logic_voltage").get<double>()) {
          fail("safety.voltage", "Signal voltage exceeds board logic voltage", id);
          continue;
        }
        if (role == "i2c_sda" || role == "i2c_scl") {
          const std::regex pattern(role == "i2c_sda" ? R"(i2c([0-9]+)\.sda)"
                                                     : R"(i2c([0-9]+)\.scl)");
          int bus = -1;
          for (const auto &cap : pin.at("capabilities")) {
            std::smatch match;
            const auto name = cap.get<std::string>();
            if (std::regex_match(name, match, pattern))
              bus = std::stoi(match[1]);
          }
          if (bus < 0)
            fail("allocator.capability", "Pin lacks the required I2C function", id);
          else if (exclusive.contains(sp))
            fail("allocator.conflict", "I2C pin already reserved as GPIO", id);
          else {
            shared[sp] = bus;
            i2c_wiring[target][role] = bus;
          }
          continue;
        }
        if (role != "gpio_output" && role != "gpio_input") {
          fail("hardware.unsupported", "Interface driver is not available in this build: " + role,
               id);
          continue;
        }
        if (std::find(pin.at("capabilities").begin(), pin.at("capabilities").end(), role) ==
            pin.at("capabilities").end()) {
          fail("allocator.capability", "Pin lacks required capability", id);
          continue;
        }
        if (exclusive.contains(sp) || shared.contains(sp)) {
          fail("allocator.conflict", sp + " already reserved", id);
          continue;
        }
        exclusive[sp] = target;
        if (role == "gpio_output")
          total_current += def.at("electrical").at("max_current_ma").get<double>();
        bool pull_up = false;
        if (role == "gpio_input") {
          auto n = std::find_if(v.graph.nodes.begin(), v.graph.nodes.end(),
                                [&](const auto &x) { return x.id == target; });
          pull_up = n->properties.at("pull_up").get<bool>();
        }
        v.graph.leases.push_back({sp, target, pin.at("bcm"), false, role == "gpio_input", pull_up});
      } else {
        if (!defs.contains(source) || !defs.contains(target)) {
          fail("graph.endpoint", "Logic endpoint missing", id);
          continue;
        }
        const Json *output = nullptr, *input = nullptr;
        for (const auto &p : defs.at(source)->at("ports"))
          if (p.at("id") == sp && p.at("direction") == "output")
            output = &p;
        for (const auto &p : defs.at(target)->at("ports"))
          if (p.at("id") == tp && p.at("direction") == "input")
            input = &p;
        if (!input || !output || input->at("type") != output->at("type")) {
          fail("graph.port_type", "Ports must have compatible type and direction", id);
          continue;
        }
        const bool first_writer = logic_inputs.emplace(target, tp).second;
        if (!first_writer && input->at("type") != "Trigger") {
          fail("graph.multiple_writers", "Input already has a writer", id);
          continue;
        }
        if (std::any_of(v.graph.routes.begin(), v.graph.routes.end(), [&](const auto &r) {
              return r.source == source && r.source_port == sp && r.target == target &&
                     r.target_port == tp;
            })) {
          fail("graph.duplicate_route", "This connection already exists", id);
          continue;
        }
        adjacency[source].push_back(target);
        ++indegree[target];
        v.graph.routes.push_back({source, sp, target, tp});
      }
    }
    for (const auto &[id, def] : defs)
      for (const auto &c : def->at("connections"))
        if (c.at("required").get<bool>() && !wired.contains({id, c.at("id").get<std::string>()}))
          fail("wiring.required", "Connect required port: " + c.at("id").get<std::string>(), id);
    std::set<std::pair<int, int>> addresses;
    for (const auto &node : v.graph.nodes) {
      if (node.driver != "moonpi.driver.bme280")
        continue;
      const auto &wiring = i2c_wiring[node.id];
      if (!wiring.contains("i2c_sda") || !wiring.contains("i2c_scl") ||
          wiring.at("i2c_sda") != wiring.at("i2c_scl")) {
        fail("allocator.i2c_bus", "SDA and SCL must belong to the same I2C bus", node.id);
        continue;
      }
      if (!node.properties.contains("address") ||
          !node.properties.at("address").is_number_integer())
        continue;
      const auto bus = wiring.at("i2c_sda");
      const auto address = node.properties.at("address").get<int>();
      if (address != 0x76 && address != 0x77)
        fail("allocator.i2c_address", "BME280 address must be 0x76 or 0x77", node.id);
      else if (!addresses.emplace(bus, address).second)
        fail("allocator.i2c_conflict", "I2C address is already reserved on this bus", node.id);
      else
        v.graph.i2c.push_back({node.id, bus, address, node.driver});
    }
    if (total_current > board.at("max_total_current_ma").get<double>())
      fail("safety.total_current", "Combined GPIO current exceeds board policy");
    std::queue<std::string> ready;
    for (const auto &[id, count] : indegree)
      if (count == 0)
        ready.push(id);
    size_t visited = 0;
    while (!ready.empty()) {
      auto id = ready.front();
      ready.pop();
      ++visited;
      for (const auto &next : adjacency[id])
        if (--indegree[next] == 0)
          ready.push(next);
    }
    if (visited != nodes.size())
      fail("graph.cycle", "Combinational cycles are forbidden");
    std::sort(v.graph.nodes.begin(), v.graph.nodes.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(v.graph.leases.begin(), v.graph.leases.end(),
              [](const auto &a, const auto &b) { return a.bcm < b.bcm; });
  } catch (const Error &e) {
    fail(e.code, e.what());
  } catch (const std::exception &e) {
    fail("graph.invalid", e.what());
  }
  return v;
}
} // namespace moonpi
