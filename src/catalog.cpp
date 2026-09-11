#include "moonpi/catalog.hpp"
#include "moonpi/logic_contracts.hpp"
#include <algorithm>
#include <set>
namespace moonpi {
Schemas::Schemas(const fs::path &root) {
  for (const auto &name : {"component", "board", "project", "command", "config", "event"}) {
    auto validator = std::make_unique<nlohmann::json_schema::json_validator>();
    validator->set_root_schema(read_json(root / (std::string(name) + ".schema.json")));
    validators_.emplace(name, std::move(validator));
  }
}
void Schemas::validate(const std::string &kind, const Json &value) const {
  try {
    validators_.at(kind)->validate(value);
  } catch (const std::exception &e) {
    throw Error("schema." + kind, e.what());
  }
}
bool ComponentRegistry::driver_available(const std::string &id) {
  static const std::set<std::string> names = {"gpio_output",
                                              "gpio_input",
                                              "interval",
                                              "bme280",
                                              "constant_boolean",
                                              "constant_number",
                                              "compare",
                                              "math",
                                              "boolean",
                                              "counter",
                                              "to_float",
                                              "display",
                                              "delay",
                                              "latch",
                                              "condition",
                                              "cron",
                                              "once",
                                              "sequence",
                                              "pin_script"};
  return id.starts_with("moonpi.driver.") && names.contains(id.substr(14));
}
void ComponentRegistry::load(const fs::path &root, bool user) {
  if (!fs::exists(root))
    return;
  std::vector<fs::path> paths;
  if (fs::is_regular_file(root))
    paths.push_back(root);
  else
    for (const auto &entry : fs::recursive_directory_iterator(root))
      if (entry.is_regular_file() && entry.path().extension() == ".json")
        paths.push_back(entry.path());
  std::sort(paths.begin(), paths.end());
  for (const auto &path : paths)
    try {
      auto c = read_json(path);
      schemas_.validate("component", c);
      auto id = c.at("id").get<std::string>();
      if (user && !id.starts_with("user."))
        throw Error("component.namespace", "User components must use the user. namespace");
      if (components_.contains(id))
        throw Error("component.duplicate", "Duplicate component ID: " + id);
      std::set<std::string> ports;
      for (const auto &list : {"connections", "ports"})
        for (const auto &p : c.at(list))
          if (!ports.insert(p.at("id").get<std::string>()).second)
            throw Error("component.port_duplicate", "Duplicate port ID");
      Json defaults = Json::object(), properties = Json::object();
      for (const auto &[key, p] : c.at("properties").items()) {
        auto s = p;
        s.erase("title");
        defaults[key] = p.at("default");
        properties[key] = s;
      }
      nlohmann::json_schema::json_validator validator;
      validator.set_root_schema(
          {{"type", "object"}, {"properties", properties}, {"additionalProperties", false}});
      validator.validate(defaults);
      const auto driver = c.at("driver").at("id").get<std::string>();
      if (logic_contracts().contains(driver)) {
        const auto &contract = logic_contracts().at(driver);
        if (!c.at("connections").empty() || c.at("ports") != contract.at("ports") ||
            c.at("properties").size() != contract.at("properties").size())
          throw Error("driver.contract",
                      "Virtual driver ports/properties do not match its contract");
        for (const auto &[key, expected] : contract.at("properties").items()) {
          const auto &actual = c.at("properties").at(key);
          if (actual.at("type") != expected.at("type"))
            throw Error("driver.contract", "Incorrect driver property type");
          if (expected.contains("minimum") &&
              (!actual.contains("minimum") || actual.at("minimum") < expected.at("minimum")))
            throw Error("driver.contract", "Driver property minimum exceeds safe range");
          if (expected.contains("maximum") &&
              (!actual.contains("maximum") || actual.at("maximum") > expected.at("maximum")))
            throw Error("driver.contract", "Driver property maximum exceeds safe range");
          if (expected.contains("enum")) {
            if (!actual.contains("enum"))
              throw Error("driver.contract", "Driver property requires an enumeration");
            for (const auto &value : actual.at("enum"))
              if (std::find(expected.at("enum").begin(), expected.at("enum").end(), value) ==
                  expected.at("enum").end())
                throw Error("driver.contract", "Unsupported driver operation");
          }
        }
      }
      if (driver == "moonpi.driver.bme280") {
        std::set<std::string> roles;
        for (const auto &conn : c.at("connections")) {
          if (!conn.at("required").get<bool>())
            throw Error("driver.contract", "BME280 wiring must be required");
          if (conn.at("voltage") != (conn.at("role") == "ground" ? 0.0 : 3.3))
            throw Error("driver.contract", "BME280 connections require 3.3V logic and supply");
          roles.insert(conn.at("role"));
        }
        if (roles != std::set<std::string>{"ground", "power", "i2c_sda", "i2c_scl"} ||
            c.at("connections").size() != 4 || !defaults.contains("address") ||
            !defaults.at("address").is_number_integer())
          throw Error("driver.contract", "BME280 requires SDA, SCL, 3.3V, ground and an address");
        const Json sensor_ports =
            Json::array({{{"id", "read"}, {"type", "Trigger"}, {"direction", "input"}},
                         {{"id", "temperature"}, {"type", "Float"}, {"direction", "output"}},
                         {{"id", "pressure"}, {"type", "Float"}, {"direction", "output"}},
                         {{"id", "humidity"}, {"type", "Float"}, {"direction", "output"}},
                         {{"id", "reading"}, {"type", "JSON"}, {"direction", "output"}}});
        const auto &address = c.at("properties").at("address");
        if (c.at("ports") != sensor_ports || address.at("type") != "integer" ||
            !address.contains("enum") || address.at("enum").empty())
          throw Error("driver.contract",
                      "BME280 needs its typed sensor ports and explicit address choices");
        for (const auto &value : address.at("enum"))
          if (value != 118 && value != 119)
            throw Error("driver.contract", "BME280 address must be 0x76 or 0x77");
      }
      if (driver == "moonpi.driver.gpio_input") {
        int signals = 0;
        for (const auto &conn : c.at("connections"))
          if (conn.at("role") == "gpio_input" && conn.at("required") == true)
            ++signals;
        const Json input_ports =
            Json::array({{{"id", "value"}, {"type", "Boolean"}, {"direction", "output"}},
                         {{"id", "pressed"}, {"type", "Trigger"}, {"direction", "output"}},
                         {{"id", "released"}, {"type", "Trigger"}, {"direction", "output"}}});
        if (signals != 1 || c.at("ports") != input_ports || !defaults.contains("pull_up") ||
            !defaults.at("pull_up").is_boolean() || !defaults.contains("active_low") ||
            !defaults.at("active_low").is_boolean() || !defaults.contains("debounce_ms"))
          throw Error("driver.contract", "GPIO input contract requires signal, "
                                         "value/pressed/released ports and input settings");
        const auto &debounce = c.at("properties").at("debounce_ms");
        if (c.at("properties").at("pull_up").at("type") != "boolean" ||
            c.at("properties").at("active_low").at("type") != "boolean")
          throw Error("driver.contract", "Input pull-up and active-low properties must be Boolean");
        if (debounce.at("type") != "integer" || debounce.value("minimum", -1) < 0 ||
            debounce.value("maximum", 10001) > 10000)
          throw Error("driver.contract", "Input debounce must be bounded 0-10000ms");
      }
      if (driver == "moonpi.driver.interval") {
        if (!c.at("connections").empty() ||
            c.at("ports") !=
                Json::array({{{"id", "tick"}, {"direction", "output"}, {"type", "Trigger"}}}) ||
            !c.at("properties").contains("interval_ms"))
          throw Error("driver.contract",
                      "Interval driver requires tick Trigger output and interval_ms");
        const auto &period = c.at("properties").at("interval_ms");
        if (period.at("type") != "integer" || period.value("minimum", 0) < 50 ||
            period.value("maximum", 86400001) > 86400000)
          throw Error("driver.timing", "Interval must be bounded between 50 and 86400000 ms");
      }
      if (driver == "moonpi.driver.pin_script") {
        const Json expected =
            Json::array({{{"id", "run"}, {"type", "Trigger"}, {"direction", "input"}},
                         {{"id", "input"}, {"type", "Boolean"}, {"direction", "input"}},
                         {{"id", "value"}, {"type", "Boolean"}, {"direction", "output"}},
                         {{"id", "done"}, {"type", "Trigger"}, {"direction", "output"}}});
        int signals = 0;
        for (const auto &conn : c.at("connections")) {
          if (conn.at("role") == "gpio_output" && conn.at("required") == true)
            ++signals;
          else if (conn.at("role") != "ground")
            throw Error("driver.contract", "Pin script supports one output and ground only");
        }
        if (signals != 1 || c.at("ports") != expected || defaults.size() != 1 ||
            c.at("properties").at("script").at("type") != "string" ||
            c.at("properties").at("script").value("maxLength", 16385) > 16384)
          throw Error("driver.contract",
                      "Pin script requires its fixed ports and bounded script text");
      }
      if (driver == "moonpi.driver.gpio_output") {
        int signals = 0;
        for (const auto &connection : c.at("connections"))
          if (connection.at("role") == "gpio_output" && connection.at("required") == true)
            ++signals;
        if (signals != 1)
          throw Error("driver.contract",
                      "GPIO output driver requires exactly one required GPIO output");
        for (const auto &p : c.at("ports"))
          if (p.at("direction") != "input" ||
              !((p.at("id") == "toggle" && p.at("type") == "Trigger") ||
                (p.at("id") == "set" && p.at("type") == "Boolean")))
            throw Error("driver.contract",
                        "GPIO output accepts toggle/Trigger and set/Boolean only");
      }
      c["driver_available"] = driver_available(driver);
      c["provenance"] = user ? "user-supplied" : "built-in";
      components_.emplace(id, std::move(c));
    } catch (const std::exception &e) {
      diagnostics_.push_back(diagnostic("component.invalid", e.what(), path.string()));
    }
}
const Json *ComponentRegistry::find(const std::string &id) const {
  auto it = components_.find(id);
  return it == components_.end() ? nullptr : &it->second;
}
Json ComponentRegistry::catalogue(const std::string &query) const {
  auto lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
  };
  Json result = Json::array();
  for (const auto &[id, c] : components_)
    if (query.empty() || lower(c.dump()).find(lower(query)) != std::string::npos)
      result.push_back(c);
  return result;
}
BoardRegistry::BoardRegistry(const fs::path &root, const Schemas &schemas) {
  for (const auto &entry : fs::directory_iterator(root))
    if (entry.path().extension() == ".json") {
      auto board = read_json(entry.path());
      schemas.validate("board", board);
      std::set<int> physical, bcm;
      std::set<std::string> ids;
      for (const auto &p : board.at("pins")) {
        if (!physical.insert(p.at("physical").get<int>()).second ||
            !ids.insert(p.at("id").get<std::string>()).second)
          throw Error("board.duplicate", "Duplicate board pin");
        if (!p.at("bcm").is_null() && !bcm.insert(p.at("bcm").get<int>()).second)
          throw Error("board.bcm_duplicate", "Duplicate BCM GPIO");
        if (p.at("kind") != "gpio" && !p.at("capabilities").empty())
          throw Error("board.rail_capability",
                      "Power/reserved pins cannot advertise controllable capabilities");
        if (p.at("kind") == "gpio" && p.at("bcm").is_null())
          throw Error("board.missing_bcm", "GPIO requires a line number");
      }
      if (!boards_.emplace(board.at("id"), board).second)
        throw Error("board.duplicate", "Duplicate board definition");
    }
}
const Json &BoardRegistry::get(const std::string &id) const {
  auto it = boards_.find(id);
  if (it == boards_.end())
    throw Error("board.missing", "Board definition unavailable: " + id);
  return it->second;
}
Json BoardRegistry::catalogue() const {
  Json a = Json::array();
  for (const auto &[id, b] : boards_)
    a.push_back(b);
  return a;
}
} // namespace moonpi
